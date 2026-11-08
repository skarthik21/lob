#include <benchmark/benchmark.h>
#include "OrderBook.h"
#include "OrderBookBenchmarkAccess.h"
#include "NSpsc.h"
#include "Message.h"
#include "LogMessage.h"
#include "Constants.h"
#include <atomic>
#include <thread>
#include "BenchmarkUtils.h"
#include <vector>
#include <list>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Utils.h"

#include "OrderBookBenchmarkUtils.h"

constexpr std::size_t HIST_SIZE = 4097;     // last bucket = 4096+ ticks (170.7 us)

// fresh book (logs recorded, not published) -> prefill (untimed) -> measured part (timed by replayMeasured) -> check
// returns false (after SkipWithError) if the book and the shadow disagree
template <typename ReplayMeasured>
bool runIteration(benchmark::State& state, const Load& load, MessageQueue& msgQ, LogQueue& logQ, ReplayMeasured replayMeasured) {
    auto book = std::make_unique<OrderBook>(msgQ, logQ, Tick::ALPHABET, LOWER_CIRCUIT, TICK_SIZE, RANGE, LIVE_ORDER_CAPACITY_OF_BOOK, LOG_RESERVE, false);

    for (const auto& msg : load.prefill) replay(*book, msg);

    uint64_t measuredTicks = replayMeasured(*book);
    state.SetIterationTime(static_cast<double>(measuredTicks) / Utils::ticksPerSecond());

    if ((book->getBestBuyLevel() != load.finalBestBuy) || (book->getBestAskLevel() != load.finalBestAsk)) {
        state.SkipWithError("book and shadow disagree on the best levels");
        return false;
    }
    return true;
}



// book only (no msgQ, logs not published)
// args: strategy, sampleSize, crossPct, maxLevels (the last two only for AGGRESSIVE)
static void BM_OrderBookThroughput(benchmark::State& state) {
    setBenchmarkThreadPriority();
    auto strategy = static_cast<Strategy>(state.range(0));
    long sampleSize = state.range(1);
    auto msgQ = std::make_unique<MessageQueue>("orderbook_msg_queue", std::size_t{constants::MESSAGE_QUEUE_SIZE});     // the constructor needs them; unused
    auto logQ = std::make_unique<LogQueue>("orderbook_log_queue", std::size_t{constants::LOG_QUEUE_SIZE});
    Load load = generateLoad(strategy, sampleSize, static_cast<int>(state.range(2)), static_cast<int>(state.range(3)));
    if (!verifyLoad(state, load, *msgQ, *logQ)) return;
    uint64_t totalTicks = 0;

    for (auto _ : state) {
        bool ok = runIteration(state, load, *msgQ, *logQ, [&](OrderBook& book) {
            uint64_t start = Utils::ticks();
            for (const auto& msg : load.measured) replay(book, msg);
            uint64_t end = Utils::ticks();
            totalTicks += end - start;
            return end - start;
        });
        if (!ok) break;
    }

    state.SetItemsProcessed(state.iterations() * sampleSize);
    state.counters["ns_per_msg"] = (static_cast<double>(totalTicks) / Utils::ticksPerSecond()) * 1e9 / (static_cast<double>(state.iterations()) * sampleSize);
    reportTagCounts(state, load);
}


// book only (no msgQ, logs not published), closed loop: per-message service time
// args: strategy, sampleSize, crossPct, maxLevels (the last two only for AGGRESSIVE)
// per message: ticks() around the book call, one histogram per tag
static void BM_OrderBookLatency(benchmark::State& state) {
    setBenchmarkThreadPriority();
    auto strategy = static_cast<Strategy>(state.range(0));
    long sampleSize = state.range(1);
    auto msgQ = std::make_unique<MessageQueue>("orderbook_msg_queue", std::size_t{constants::MESSAGE_QUEUE_SIZE});     // the constructor needs them; unused
    auto logQ = std::make_unique<LogQueue>("orderbook_log_queue", std::size_t{constants::LOG_QUEUE_SIZE});
    Load load = generateLoad(strategy, sampleSize, static_cast<int>(state.range(2)), static_cast<int>(state.range(3)));
    if (!verifyLoad(state, load, *msgQ, *logQ)) return;
    std::vector<std::vector<uint64_t>> hists(TAG_COUNT, std::vector<uint64_t>(HIST_SIZE));
    std::array<uint64_t, TAG_COUNT> maxTicks{};

    for (auto _ : state) {
        bool ok = runIteration(state, load, *msgQ, *logQ, [&](OrderBook& book) {
            uint64_t total = 0;
            for (std::size_t i = 0; i < load.measured.size(); i++) {
                uint64_t start = Utils::ticks();
                replay(book, load.measured[i]);
                uint64_t elapsed = Utils::ticks() - start;
                Tag tag = load.measuredTags[i];
                hists[tag][std::min<uint64_t>(elapsed, HIST_SIZE - 1)]++;
                maxTicks[tag] = std::max(maxTicks[tag], elapsed);
                total += elapsed;
            }
            return total;
        });
        if (!ok) break;
    }

    reportTagCounts(state, load);
    for (int tag = 0; tag < TAG_COUNT; tag++) reportHistogram(state, hists[tag], maxTicks[tag], TAG_NAMES[tag]);
}


// ---------- per operation, block-timed ----------
// one long-lived book in a fixed shape. Each iteration: build a block of messages of one kind (untimed),
// replay it under one timer, then bring the book back to the shape (untimed).
// mean cost per operation = timed ticks / operations, so the 41.67 ns tick doesn't limit it

enum class Op {
    ADD_EXISTING_LEVEL,     // one add on every populated level
    ADD_NEW_LEVEL,          // one add on every empty level inside the band (never a new best)
    DELETE_KEEPS_LEVEL,     // the front order of every level (2 per level)
    DELETE_EMPTIES_LEVEL,   // the only order of every level except the best (1 per level)
    DELETE_EMPTIES_BEST,    // the only order at the best, repeatedly: each one searches for the next level
    MODIFY_QTY_DOWN,        // front order of every level, quantity halved, keeps its place
    MODIFY_QTY_UP,          // front order of every level, quantity +5, moves to the back
    MODIFY_PRICE,           // front order of every level, moves MODIFY_PRICE_SHIFT levels (wrapping inside the band)
    CROSS                   // add that fills k whole levels of the other side, nothing rests; sides alternate
};

constexpr int MODIFY_PRICE_SHIFT = 24;

struct OpShape {
    int stride;             // every stride-th level of the band is populated, counted from the inner edge
    int ordersPerLevel;
};

OpShape opShape(Op op) {
    switch (op) {
        case Op::ADD_NEW_LEVEL: return {2, ORDERS_PER_LEVEL};
        case Op::DELETE_EMPTIES_LEVEL:
        case Op::DELETE_EMPTIES_BEST: return {1, 1};
        default: return {1, ORDERS_PER_LEVEL};
    }
}

// band levels of a side, from the inner edge (the best) outwards
std::vector<int> bandLevels(Side side) {
    std::vector<int> res;
    if (side == Side::BUY) for (int level = BUY_BAND_HI; level >= BUY_BAND_LO; level--) res.push_back(level);
    else for (int level = SELL_BAND_LO; level <= SELL_BAND_HI; level++) res.push_back(level);
    return res;
}

// builds the timed blocks and the restore messages, keeping a shadow of the book
class OpBlocks {
public:
    OpBlocks(Op op, int k): op(op), k(k), shape(opShape(op)), bands{bandLevels(Side::BUY), bandLevels(Side::SELL)} {}

    ShadowBook shadow;
    long fills = 0;         // fills caused by the blocks built so far

    // brings the book to the shape: drops orders a block changed and extras, refills missing ones
    // (also the prefill, from an empty book)
    std::vector<Message> restore() {
        std::vector<Message> msgs;
        for (Side side : {Side::BUY, Side::SELL}) {
            auto& band = bands[side == Side::BUY ? 0 : 1];
            for (int i = 0; i < static_cast<int>(band.size()); i++) {
                int level = band[i];
                int target = (i % shape.stride == 0) ? shape.ordersPerLevel : 0;
                std::vector<int> orderIds(shadow.levelOrders(level).begin(), shadow.levelOrders(level).end());
                int kept = 0;
                for (int orderId : orderIds) {
                    const auto& o = shadow.order(orderId);
                    if (kept < target && o.originalQuantity == ADD_QUANTITY && o.remaining == ADD_QUANTITY) {
                        kept++;
                        continue;
                    }
                    msgs.push_back(deleteOrder(orderId));
                }
                for (; kept < target; kept++) msgs.push_back(addOrder(side, level, ADD_QUANTITY));
            }
        }
        return msgs;
    }

    // one block; sides alternate level by level
    std::vector<Message> block() {
        std::vector<Message> msgs;
        int bandSize = static_cast<int>(bands[0].size());

        if (op == Op::CROSS) {
            // stop a side while it has k levels or fewer left, so it never empties
            bool more = true;
            while (more) {
                more = false;
                for (Side side : {Side::BUY, Side::SELL}) {
                    Side other = otherSide(side);
                    auto levels = shadow.bestLevels(other, k + 1);
                    if (static_cast<int>(levels.size()) <= k) continue;
                    int quantity = 0;
                    for (int i = 0; i < k; i++) quantity += shadow.levelQuantity(levels[i]);
                    fills += shadow.fill(other, quantity);
                    msgs.push_back(createAddOrder(shadow.nextOrderId++, quantity, levels[k - 1], side));
                    more = true;
                }
            }
            return msgs;
        }

        for (int i = 0; i < bandSize; i++) {
            for (Side side : {Side::BUY, Side::SELL}) {
                auto& band = bands[side == Side::BUY ? 0 : 1];
                int level = band[i];
                bool populated = !shadow.levelOrders(level).empty();
                switch (op) {
                    case Op::ADD_EXISTING_LEVEL:
                        if (populated) msgs.push_back(addOrder(side, level, ADD_QUANTITY));
                        break;
                    case Op::ADD_NEW_LEVEL:
                        if (!populated) msgs.push_back(addOrder(side, level, ADD_QUANTITY));
                        break;
                    case Op::DELETE_KEEPS_LEVEL:
                    case Op::DELETE_EMPTIES_LEVEL:
                        if (op == Op::DELETE_EMPTIES_LEVEL && i == 0) break;      // not the best
                        msgs.push_back(deleteOrder(shadow.levelOrders(level).front()));
                        break;
                    case Op::DELETE_EMPTIES_BEST:
                        if (i == bandSize - 1) break;                               // keep the outermost level
                        msgs.push_back(deleteOrder(shadow.levelOrders(level).front()));
                        break;
                    case Op::MODIFY_QTY_DOWN:
                    case Op::MODIFY_QTY_UP:
                    case Op::MODIFY_PRICE:
                        msgs.push_back(modifyOrder(shadow.levelOrders(level).front(), (i + MODIFY_PRICE_SHIFT) % bandSize, band));
                        break;
                    case Op::CROSS:
                        break;
                }
            }
        }
        return msgs;
    }

private:
    Op op;
    int k;
    OpShape shape;
    std::array<std::vector<int>, 2> bands;      // BUY, SELL

    Message addOrder(Side side, int level, int quantity) {
        int orderId = shadow.nextOrderId++;
        shadow.add(orderId, side, level, quantity);
        return createAddOrder(orderId, quantity, level, side);
    }

    Message deleteOrder(int orderRefId) {
        shadow.removeById(orderRefId);
        return createDeleteOrder(shadow.nextOrderId++, orderRefId);
    }

    // nothing is ever filled here, so remaining == originalQuantity
    Message modifyOrder(int orderRefId, int shiftedIdx, const std::vector<int>& band) {
        auto [side, level, originalQuantity, remaining] = shadow.order(orderRefId);
        int orderId = shadow.nextOrderId++;
        int liveIdx = shadow.liveIndex(orderRefId);
        if (op == Op::MODIFY_QTY_DOWN) {
            int quantity = originalQuantity / 2;
            shadow.replaceInPlace(orderRefId, orderId, liveIdx, quantity, quantity);
            return createModifyOrder(orderId, orderRefId, quantity, level, side);
        }
        if (op == Op::MODIFY_QTY_UP) {
            int quantity = originalQuantity + 5;
            shadow.moveToBack(orderRefId, orderId, liveIdx, level, quantity, quantity);
            return createModifyOrder(orderId, orderRefId, quantity, level, side);
        }
        int newLevel = band[shiftedIdx];
        shadow.moveToBack(orderRefId, orderId, liveIdx, newLevel, originalQuantity, remaining);
        return createModifyOrder(orderId, orderRefId, originalQuantity, newLevel, side);
    }
};


inline bool bestsMatch(OrderBook& book, const ShadowBook& shadow) {
    return book.getBestBuyLevel() == shadow.bestBuy && book.getBestAskLevel() == shadow.bestAsk;
}

// once per configuration, untimed: shape -> one block -> restore with logs published, to catch validation failures
// (a rejected message would look like a very cheap operation)
bool verifyOp(benchmark::State& state, Op op, int k, MessageQueue& msgQ, LogQueue& logQ) {
    OpBlocks blocks(op, k);
    auto book = std::make_unique<OrderBook>(msgQ, logQ, Tick::ALPHABET, LOWER_CIRCUIT, TICK_SIZE, RANGE, LIVE_ORDER_CAPACITY_OF_BOOK, LOG_RESERVE, true);
    LogConsumer consumer(logQ);

    bool ok = true;
    for (const auto& msg : blocks.restore()) replay(*book, msg);
    ok = ok && bestsMatch(*book, blocks.shadow);
    auto block = blocks.block();
    for (const auto& msg : block) replay(*book, msg);
    ok = ok && bestsMatch(*book, blocks.shadow);
    for (const auto& msg : blocks.restore()) replay(*book, msg);
    ok = ok && bestsMatch(*book, blocks.shadow);

    book.reset();
    consumer.finish();
    if (!ok) {
        state.SkipWithError("verify: book and shadow disagree on the best levels");
        return false;
    }
    if (consumer.validationFailures != 0) {
        state.SkipWithError("verify: validation failures, a block or restore message was rejected");
        return false;
    }
    if (block.empty()) {
        state.SkipWithError("verify: empty block");
        return false;
    }
    return true;
}

// book only (no msgQ, logs not published); k = levels a CROSS fills (unused otherwise)
static void BM_OrderBookOp(benchmark::State& state, Op op, int k) {
    setBenchmarkThreadPriority();
    auto msgQ = std::make_unique<MessageQueue>("orderbook_msg_queue", std::size_t{constants::MESSAGE_QUEUE_SIZE});     // the constructor needs them; unused
    auto logQ = std::make_unique<LogQueue>("orderbook_log_queue", std::size_t{constants::LOG_QUEUE_SIZE});
    if (!verifyOp(state, op, k, *msgQ, *logQ)) return;

    OpBlocks blocks(op, k);
    auto book = std::make_unique<OrderBook>(*msgQ, *logQ, Tick::ALPHABET, LOWER_CIRCUIT, TICK_SIZE, RANGE, LIVE_ORDER_CAPACITY_OF_BOOK, LOG_RESERVE, false);
    for (const auto& msg : blocks.restore()) replay(*book, msg);
    uint64_t totalTicks = 0;
    long totalOps = 0;

    for (auto _ : state) {
        auto block = blocks.block();
        uint64_t start = Utils::ticks();
        for (const auto& msg : block) replay(*book, msg);
        uint64_t end = Utils::ticks();
        state.SetIterationTime(static_cast<double>(end - start) / Utils::ticksPerSecond());
        totalTicks += end - start;
        totalOps += static_cast<long>(block.size());

        bool ok = bestsMatch(*book, blocks.shadow);
        for (const auto& msg : blocks.restore()) replay(*book, msg);
        if (!ok || !bestsMatch(*book, blocks.shadow)) {
            state.SkipWithError("book and shadow disagree on the best levels");
            break;
        }
    }

    double nsPerOp = (static_cast<double>(totalTicks) / Utils::ticksPerSecond()) * 1e9 / static_cast<double>(totalOps);
    state.SetItemsProcessed(totalOps);
    state.counters["ns_per_op"] = nsPerOp;
    state.counters["ops_per_block"] = static_cast<double>(totalOps) / static_cast<double>(state.iterations());
    if (op == Op::CROSS) {
        state.counters["ns_per_level"] = nsPerOp / k;
        state.counters["fills_per_op"] = static_cast<double>(blocks.fills) / static_cast<double>(totalOps);
    }
}

// one name per operation instead of an opaque op number in the args
const int registerOpBenchmarks = [] {
    const std::vector<std::pair<std::string, Op>> ops = {
        {"addExistingLevel", Op::ADD_EXISTING_LEVEL}, {"addNewLevel", Op::ADD_NEW_LEVEL},
        {"deleteKeepsLevel", Op::DELETE_KEEPS_LEVEL}, {"deleteEmptiesLevel", Op::DELETE_EMPTIES_LEVEL},
        {"deleteEmptiesBest", Op::DELETE_EMPTIES_BEST},
        {"modifyQtyDown", Op::MODIFY_QTY_DOWN}, {"modifyQtyUp", Op::MODIFY_QTY_UP}, {"modifyPrice", Op::MODIFY_PRICE}
    };
    std::vector<std::pair<std::string, int>> crosses;
    for (int k : {1, 2, 4, 8, 16}) crosses.push_back({"cross/levels:" + std::to_string(k), k});

    auto configure = [](benchmark::internal::Benchmark* b) {
        b->UseManualTime()->Repetitions(5)->ReportAggregatesOnly(true)->Unit(benchmark::kMicrosecond);
    };
    for (const auto& [name, op] : ops) configure(benchmark::RegisterBenchmark("BM_OrderBookOp/" + name, BM_OrderBookOp, op, 0));
    for (const auto& [name, k] : crosses) configure(benchmark::RegisterBenchmark("BM_OrderBookOp/" + name, BM_OrderBookOp, Op::CROSS, k));
    return 0;
}();


constexpr long RESTING = static_cast<long>(Strategy::RESTING);
constexpr long AGGRESSIVE = static_cast<long>(Strategy::AGGRESSIVE);

BENCHMARK(BM_OrderBookThroughput)
    ->Name("BM_OrderBookThroughput/resting")
    ->ArgNames({"strategy", "samples", "crossPct", "maxLevels"})
    ->ArgsProduct({{RESTING}, {10000, 100000, 1000000}, {0}, {0}})
    ->UseManualTime()
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_OrderBookLatency)
    ->Name("BM_OrderBookLatency/resting")
    ->ArgNames({"strategy", "samples", "crossPct", "maxLevels"})
    ->ArgsProduct({{RESTING}, {10000, 100000, 1000000}, {0}, {0}})
    ->UseManualTime()
    ->Iterations(10)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_OrderBookThroughput)
    ->Name("BM_OrderBookThroughput/aggressive")
    ->ArgNames({"strategy", "samples", "crossPct", "maxLevels"})
    ->ArgsProduct({{AGGRESSIVE}, {1000000}, {5, 10, 25}, {3}})
    ->UseManualTime()
    ->Repetitions(5)
    ->ReportAggregatesOnly(true)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_OrderBookLatency)
    ->Name("BM_OrderBookLatency/aggressive")
    ->ArgNames({"strategy", "samples", "crossPct", "maxLevels"})
    ->ArgsProduct({{AGGRESSIVE}, {1000000}, {5, 10, 25}, {3}})
    ->UseManualTime()
    ->Iterations(10)
    ->Unit(benchmark::kMillisecond);
