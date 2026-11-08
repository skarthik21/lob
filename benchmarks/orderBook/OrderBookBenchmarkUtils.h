#pragma once

// shared by the order book benchmarks: book parameters, the load generator (with its shadow book),
// a log consumer thread, load verification and histogram reporting

#include <benchmark/benchmark.h>
#include "OrderBook.h"
#include "OrderBookBenchmarkAccess.h"
#include "NSpsc.h"
#include "Message.h"
#include "LogMessage.h"
#include "BenchmarkUtils.h"
#include "Utils.h"
#include "Constants.h"
#include <atomic>
#include <thread>
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

using MessageQueue = NSpsc<Message>;
using LogQueue = NSpsc<LogMessage>;

// book parameters (same as the tests)
constexpr int LOWER_CIRCUIT = 100;
constexpr int TICK_SIZE = 5;
constexpr int RANGE = 100;

// book shape and mix (both strategies)
constexpr int BUY_BAND_LO = 0, BUY_BAND_HI = 48;
constexpr int SELL_BAND_LO = 51, SELL_BAND_HI = 99;
constexpr int ORDERS_PER_LEVEL = 2;
constexpr int TARGET_PER_SIDE = (BUY_BAND_HI - BUY_BAND_LO + 1) * ORDERS_PER_LEVEL;   // 98
constexpr int DELTA = 10;
constexpr double P_ADD = 0.50, P_DELETE = 0.45;                                    // modify = the rest
constexpr int ADD_QUANTITY = 10;

// aggressive strategy: share of resting adds sent to the nearest empty level from the inner edge of the band
constexpr double AGGRESSIVE_REFILL_PCT = 0.5;

constexpr int LIVE_ORDER_CAPACITY_OF_BOOK = 1e4;

// book-only runs record log messages but don't publish them, so the batch size only sets the buffer the book writes into
constexpr int LOG_RESERVE = 64;

constexpr uint64_t SEED = 42;

enum class Strategy { RESTING = 0, AGGRESSIVE = 1 };

struct StrategyConfig {
    double crossPct = 0;        // share of (sampled) adds that cross
    int maxLevels = 0;          // a crossing add sweeps k = 1..maxLevels levels, uniform
    double refillPct = 0;       // share of resting adds that go to the nearest empty level
};

// what the book has to do for a message; each tag gets its own latency histogram
enum Tag : int {
    ADD_NEW_LEVEL,          // add that creates a level
    ADD_EXISTING_LEVEL,     // add that joins a level
    ADD_CROSS_1,            // crossing add, ends inside the best level
    ADD_CROSS_2,            // empties 1 level, ends inside the 2nd
    ADD_CROSS_3_PLUS,       // empties k-1 levels (k >= 3), ends inside the k-th
    DELETE_KEEPS_LEVEL,     // other orders stay at the level
    DELETE_EMPTIES_LEVEL,   // last order at a level that isn't the best
    DELETE_EMPTIES_BEST,    // last order at the best level -> next level search
    MODIFY_QTY_DOWN,        // keeps its place
    MODIFY_QTY_UP,          // moves to the back of the level
    MODIFY_PRICE,           // moves to another level in its band
    TAG_COUNT
};

const std::array<std::string, TAG_COUNT> TAG_NAMES = {
    "addNewLevel", "addExistingLevel", "addCross1", "addCross2", "addCross3plus",
    "deleteKeepsLevel", "deleteEmptiesLevel", "deleteEmptiesBest",
    "modifyQtyDown", "modifyQtyUp", "modifyPrice"
};

struct Load {
    std::vector<Message> prefill;        // book shape + warm-up, untimed
    std::vector<int> prefillLogs;        // log messages each prefill message produces
    std::vector<Message> measured;
    std::vector<Tag> measuredTags;
    std::vector<int> measuredLogs;       // log messages each measured message produces (its last one ends its processing)
    std::array<long, TAG_COUNT> tagCounts{};
    long measuredFills = 0;              // fills caused by the measured messages
    long expectedLogs = 0;               // log messages the book should produce (prefill + measured)
    int finalBestBuy = -1;               // shadow's best levels after the measured part
    int finalBestAsk = -1;
};


// std::uniform_int_distribution differs between libc++ and libstdc++; these give the same load everywhere
inline int uniformInt(std::mt19937_64& rng, int n) {
    return static_cast<int>(rng() % static_cast<uint64_t>(n));
}

inline double uniform01(std::mt19937_64& rng) {
    return (rng() >> 11) * 0x1.0p-53;
}


// mirror of the book: the generator uses it to pick live orders, to know the best levels and to simulate fills
class ShadowBook {
public:
    ShadowBook(): levels(RANGE) {};

    struct ShadowOrder {
        Side side;
        int level;
        int originalQuantity;    // the book's originalQuantity: what modify validation compares against
        int remaining;
    };

    int bestBuy = -1;
    int bestAsk = -1;
    int nextOrderId = 1;

    std::vector<int>& live(Side side) { return side == Side::BUY ? liveBuy : liveSell; }
    const ShadowOrder& order(int orderId) { return orders.at(orderId); }
    std::size_t levelSize(int level) { return levels[level].size(); }
    const std::list<int>& levelOrders(int level) { return levels[level]; }

    // position of orderId in live(its side)
    int liveIndex(int orderId) {
        auto& l = live(orders.at(orderId).side);
        return static_cast<int>(std::find(l.begin(), l.end(), orderId) - l.begin());
    }

    void removeById(int orderId) { remove(orderId, liveIndex(orderId)); }

    int levelQuantity(int level) {
        int total = 0;
        for (int orderId : levels[level]) total += orders.at(orderId).remaining;
        return total;
    }

    // populated levels of a side, from its best outwards, at most k
    std::vector<int> bestLevels(Side side, int k) {
        std::vector<int> res;
        if (side == Side::BUY) {
            for (int level = bestBuy; level >= 0 && static_cast<int>(res.size()) < k; level--) if (!levels[level].empty()) res.push_back(level);
        } else if (bestAsk != -1) {
            for (int level = bestAsk; level < RANGE && static_cast<int>(res.size()) < k; level++) if (!levels[level].empty()) res.push_back(level);
        }
        return res;
    }

    // first empty level from the inner edge of the band outwards, -1 if the band is full
    int nearestEmptyLevel(Side side) {
        if (side == Side::BUY) {
            for (int level = BUY_BAND_HI; level >= BUY_BAND_LO; level--) if (levels[level].empty()) return level;
        } else {
            for (int level = SELL_BAND_LO; level <= SELL_BAND_HI; level++) if (levels[level].empty()) return level;
        }
        return -1;
    }

    void add(int orderId, Side side, int level, int quantity) {
        levels[level].push_back(orderId);
        orders[orderId] = {side, level, quantity, quantity};
        live(side).push_back(orderId);
        if (side == Side::BUY && level > bestBuy) bestBuy = level;
        if (side == Side::SELL && (bestAsk == -1 || level < bestAsk)) bestAsk = level;
    }

    // liveIdx: position of orderId in live(side)
    void remove(int orderId, int liveIdx) {
        auto [side, level, originalQuantity, remaining] = orders.at(orderId);
        levels[level].remove(orderId);
        orders.erase(orderId);
        auto& l = live(side);
        l[liveIdx] = l.back();
        l.pop_back();
        if (levels[level].empty()) updateBest(side, level);
    }

    // quantity decrease: the new id takes the old id's place in the level
    void replaceInPlace(int orderRefId, int orderId, int liveIdx, int originalQuantity, int remaining) {
        auto [side, level, oldOrgQuantity, oldRemaining] = orders.at(orderRefId);
        std::replace(levels[level].begin(), levels[level].end(), orderRefId, orderId);
        orders.erase(orderRefId);
        orders[orderId] = {side, level, originalQuantity, remaining};
        live(side)[liveIdx] = orderId;
    }

    // quantity increase or price change: the book deletes and re-adds, so the new order goes to the
    // back of its (new) level; it keeps the order's total (originalQuantity), so filled = originalQuantity - remaining carries over
    // (live order doesn't matter: picks are uniform)
    void moveToBack(int orderRefId, int orderId, int liveIdx, int level, int originalQuantity, int remaining) {
        Side side = orders.at(orderRefId).side;
        remove(orderRefId, liveIdx);
        add(orderId, side, level, remaining);
        orders.at(orderId).originalQuantity = originalQuantity;
    }

    // a crossing order of `quantity` against `side`, oldest order first at each level; returns the number of fills
    int fill(Side side, int quantity) {
        int fills = 0;
        while (quantity > 0) {
            int level = (side == Side::BUY) ? bestBuy : bestAsk;
            if (level == -1) std::abort();      // generator asked for more than the side holds
            int orderId = levels[level].front();
            auto& o = orders.at(orderId);
            int filled = std::min(quantity, o.remaining);
            o.remaining -= filled;
            quantity -= filled;
            fills++;
            if (o.remaining == 0) removeById(orderId);
        }
        return fills;
    }

private:
    std::vector<std::list<int>> levels;                 // level -> order ids, arrival order
    std::unordered_map<int, ShadowOrder> orders;        // order id -> side, level, quantities
    std::vector<int> liveBuy;
    std::vector<int> liveSell;

    void updateBest(Side side, int level) {
        if (side == Side::BUY && level == bestBuy) {
            while (bestBuy >= 0 && levels[bestBuy].empty()) bestBuy--;
        }
        if (side == Side::SELL && level == bestAsk) {
            while (bestAsk < RANGE && levels[bestAsk].empty()) bestAsk++;
            if (bestAsk == RANGE) bestAsk = -1;
        }
    }
};


inline int levelToPrice(int level) {
    return LOWER_CIRCUIT + level * TICK_SIZE;
}

inline Message createAddOrder(int orderId, int quantity, int level, Side side) {
    return Message(orderId, orderId, Tick::ALPHABET, quantity, levelToPrice(level), 100, -1, MessageType::ADD_ORDER, side);
}

inline Message createModifyOrder(int orderId, int orderRefId, int quantity, int level, Side side) {
    return Message(orderId, orderId, Tick::ALPHABET, quantity, levelToPrice(level), 100, orderRefId, MessageType::MODIFY_ORDER, side);
}

inline Message createDeleteOrder(int orderId, int orderRefId) {
    return Message(orderId, orderId, Tick::ALPHABET, -1, -1, 100, orderRefId, MessageType::DELETE_ORDER, Side::UNKNOWN);
}

inline Side otherSide(Side side) {
    return side == Side::BUY ? Side::SELL : Side::BUY;
}


struct Generated {
    Message msg;
    Tag tag;
    int fills = 0;
};

// crossing add by `side`: sweeps k populated levels of the other side, empties the first k-1
// and ends inside the k-th, so nothing rests
inline Generated crossingAdd(ShadowBook& shadow, std::mt19937_64& rng, const StrategyConfig& config, Side side, int orderId) {
    Side other = otherSide(side);
    auto levels = shadow.bestLevels(other, 1 + uniformInt(rng, config.maxLevels));
    int k = static_cast<int>(levels.size());
    int quantity = 0;
    for (int i = 0; i < k - 1; i++) quantity += shadow.levelQuantity(levels[i]);
    int lastLevelQuantity = shadow.levelQuantity(levels[k - 1]);
    // part of the k-th level: 1 .. lastLevelQuantity-1 (all of it if it holds only 1)
    quantity += (lastLevelQuantity < 2) ? lastLevelQuantity : 1 + uniformInt(rng, lastLevelQuantity - 1);

    int fills = shadow.fill(other, quantity);
    Tag tag = (k == 1) ? ADD_CROSS_1 : (k == 2) ? ADD_CROSS_2 : ADD_CROSS_3_PLUS;
    return {createAddOrder(orderId, quantity, levels[k - 1], side), tag, fills};
}

// one message; updates the shadow and returns the message, its tag and the fills it causes
inline Generated nextMessage(ShadowBook& shadow, std::mt19937_64& rng, const StrategyConfig& config, long& expectedLogs) {
    Side side = uniform01(rng) < 0.5 ? Side::BUY : Side::SELL;
    int bandLo = (side == Side::BUY) ? BUY_BAND_LO : SELL_BAND_LO;
    int bandHi = (side == Side::BUY) ? BUY_BAND_HI : SELL_BAND_HI;
    auto& live = shadow.live(side);
    int n = static_cast<int>(live.size());

    // mean reversion keeps each side at TARGET_PER_SIDE +- DELTA, otherwise sample the split
    MessageType type;
    bool sampled = false;
    if (n < TARGET_PER_SIDE - DELTA || n == 0) type = MessageType::ADD_ORDER;
    else if (n > TARGET_PER_SIDE + DELTA) type = MessageType::DELETE_ORDER;
    else {
        double u = uniform01(rng);
        type = (u < P_ADD) ? MessageType::ADD_ORDER : (u < P_ADD + P_DELETE) ? MessageType::DELETE_ORDER : MessageType::MODIFY_ORDER;
        sampled = true;
    }

    int orderId = shadow.nextOrderId++;

    if (type == MessageType::ADD_ORDER) {
        // a forced add has to grow this side, so only sampled adds cross;
        // and only if the other side is above its lower limit
        if (config.crossPct > 0 && sampled && uniform01(rng) < config.crossPct
                && static_cast<int>(shadow.live(otherSide(side)).size()) > TARGET_PER_SIDE - DELTA) {
            Generated g = crossingAdd(shadow, rng, config, side, orderId);
            expectedLogs += g.fills + 1;    // one ORDER_FILLED per fill, then ORDER_ADDED
            return g;
        }
        int level = -1;
        if (config.refillPct > 0 && uniform01(rng) < config.refillPct) level = shadow.nearestEmptyLevel(side);
        if (level == -1) level = bandLo + uniformInt(rng, bandHi - bandLo + 1);
        Tag tag = shadow.levelSize(level) == 0 ? ADD_NEW_LEVEL : ADD_EXISTING_LEVEL;
        shadow.add(orderId, side, level, ADD_QUANTITY);
        expectedLogs++;     // ORDER_ADDED
        return {createAddOrder(orderId, ADD_QUANTITY, level, side), tag};
    }

    int liveIdx = uniformInt(rng, n);
    int orderRefId = live[liveIdx];
    auto [refSide, level, originalQuantity, remaining] = shadow.order(orderRefId);
    expectedLogs++;     // delete: ORDER_DELETED, modify: ORDER_DELETED or MODIFIED_ORDER

    if (type == MessageType::DELETE_ORDER) {
        int best = (side == Side::BUY) ? shadow.bestBuy : shadow.bestAsk;
        Tag tag = (shadow.levelSize(level) > 1) ? DELETE_KEEPS_LEVEL : (level == best) ? DELETE_EMPTIES_BEST : DELETE_EMPTIES_LEVEL;
        shadow.remove(orderRefId, liveIdx);
        return {createDeleteOrder(orderId, orderRefId), tag};
    }

    // modify: quantity down / quantity up / price, equal shares
    // quantities in a modify are the new originalQuantity; the book keeps filled = originalQuantity - remaining
    int filled = originalQuantity - remaining;
    int kind = uniformInt(rng, 3);
    int newOrgQuantity = std::max(filled + 1, originalQuantity / 2);
    if (kind == 0 && newOrgQuantity >= originalQuantity) kind = 1;     // no room to decrease above what's filled

    if (kind == 0) {
        shadow.replaceInPlace(orderRefId, orderId, liveIdx, newOrgQuantity, newOrgQuantity - filled);
        return {createModifyOrder(orderId, orderRefId, newOrgQuantity, level, side), MODIFY_QTY_DOWN};
    }
    if (kind == 1) {
        newOrgQuantity = originalQuantity + 5;
        shadow.moveToBack(orderRefId, orderId, liveIdx, level, newOrgQuantity, newOrgQuantity - filled);
        return {createModifyOrder(orderId, orderRefId, newOrgQuantity, level, side), MODIFY_QTY_UP};
    }
    int newLevel = bandLo + uniformInt(rng, bandHi - bandLo);   // any other level in the band
    if (newLevel >= level) newLevel++;
    shadow.moveToBack(orderRefId, orderId, liveIdx, newLevel, originalQuantity, remaining);
    // originalQuantity unchanged, so the book sees a price change only
    return {createModifyOrder(orderId, orderRefId, originalQuantity, newLevel, side), MODIFY_PRICE};
}


inline StrategyConfig strategyConfig(Strategy strategy, int crossPct, int maxLevels) {
    switch (strategy) {
        case Strategy::RESTING:
            return {};
        case Strategy::AGGRESSIVE:
            return {crossPct / 100.0, maxLevels, AGGRESSIVE_REFILL_PCT};
    }
    return {};
}

inline Load generateLoad(Strategy strategy, long sampleSize, int crossPct, int maxLevels) {
    Load load;
    ShadowBook shadow;
    std::mt19937_64 rng(SEED);
    StrategyConfig config = strategyConfig(strategy, crossPct, maxLevels);
    if (config.crossPct > 0 && config.maxLevels < 1) {
        std::fprintf(stderr, "crossing orders need maxLevels >= 1\n");
        std::abort();
    }

    // book shape: ORDERS_PER_LEVEL orders on every level of both bands
    for (int level = BUY_BAND_LO; level <= BUY_BAND_HI; level++) {
        for (int i = 0; i < ORDERS_PER_LEVEL; i++) {
            int orderId = shadow.nextOrderId++;
            shadow.add(orderId, Side::BUY, level, ADD_QUANTITY);
            load.prefill.push_back(createAddOrder(orderId, ADD_QUANTITY, level, Side::BUY));
        }
    }
    for (int level = SELL_BAND_LO; level <= SELL_BAND_HI; level++) {
        for (int i = 0; i < ORDERS_PER_LEVEL; i++) {
            int orderId = shadow.nextOrderId++;
            shadow.add(orderId, Side::SELL, level, ADD_QUANTITY);
            load.prefill.push_back(createAddOrder(orderId, ADD_QUANTITY, level, Side::SELL));
        }
    }

    load.expectedLogs += static_cast<long>(load.prefill.size());     // ORDER_ADDED per shape order
    load.prefillLogs.assign(load.prefill.size(), 1);

    // warm-up as long as the measured part
    for (long i = 0; i < sampleSize; i++) {
        long logsBefore = load.expectedLogs;
        load.prefill.push_back(nextMessage(shadow, rng, config, load.expectedLogs).msg);
        load.prefillLogs.push_back(static_cast<int>(load.expectedLogs - logsBefore));
    }
    load.measured.reserve(sampleSize);
    load.measuredTags.reserve(sampleSize);
    load.measuredLogs.reserve(sampleSize);
    for (long i = 0; i < sampleSize; i++) {
        long logsBefore = load.expectedLogs;
        Generated g = nextMessage(shadow, rng, config, load.expectedLogs);
        load.measuredLogs.push_back(static_cast<int>(load.expectedLogs - logsBefore));
        load.measured.push_back(g.msg);
        load.measuredTags.push_back(g.tag);
        load.tagCounts[g.tag]++;
        load.measuredFills += g.fills;
    }

    load.finalBestBuy = shadow.bestBuy;
    load.finalBestAsk = shadow.bestAsk;
    return load;
}


// drains logQ on its own thread, like the logger in the real system
class LogConsumer {
public:
    explicit LogConsumer(LogQueue& logQ): logQ(logQ) {
        t = std::thread([this]() {
            setBenchmarkThreadPriority();
            // whole batches (the size the book pushes) keep up with the book; single pops drain a partial last batch
            std::vector<LogMessage> batch(LOG_RESERVE);
            LogMessage msg;
            auto record = [this](const LogMessage& m) {
                received.fetch_add(1, std::memory_order_relaxed);
                if (m.logEvent == LogEvent::VALIDATION_FAILED) validationFailures++;
            };
            isReady.store(true);
            while (!stopRequested.load(std::memory_order_relaxed)) {
                if (this->logQ.tryPopBatch(batch) == QueueStatus::POPPED) {
                    for (const auto& m : batch) record(m);
                    continue;
                }
                if (this->logQ.tryPop(msg) == QueueStatus::EMPTY) continue;
                record(msg);
            }
        });
        while (!isReady.load()) continue;
    }

    ~LogConsumer() {
        finish();
    }

    // call after the book is destroyed (its destructor flushes the last batch)
    void finish() {
        if (!t.joinable()) return;
        while (logQ.getQueueSize() != 0) continue;
        stopRequested.store(true);
        t.join();
    }

    std::atomic<long> received{0};  // read while running, for backpressure (verifyLoad)
    long validationFailures = 0;    // read only after finish()

private:
    LogQueue& logQ;
    std::thread t;
    std::atomic<bool> isReady{false};
    std::atomic<bool> stopRequested{false};
};


// book only: no msgQ, the message is copied out like onMessage() does after tryPop
inline void replay(OrderBook& book, const Message& msg) {
    Message m = msg;
    OrderBookBenchmarkAccess::process(book, m);
}


// once per configuration, untimed: the same load with logs published to a consumer thread,
// to check what the timed runs can't (they publish nothing)
// returns false (after SkipWithError) if the book and the shadow disagree or logs went missing
inline bool verifyLoad(benchmark::State& state, const Load& load, MessageQueue& msgQ, LogQueue& logQ) {
    auto book = std::make_unique<OrderBook>(msgQ, logQ, Tick::ALPHABET, LOWER_CIRCUIT, TICK_SIZE, RANGE, LIVE_ORDER_CAPACITY_OF_BOOK, LOG_RESERVE, true);
    LogConsumer consumer(logQ);

    // backpressure: stay at most half a logQ ahead of the consumer, so a descheduled consumer can't make the
    // book drop a batch (this phase is untimed; the book's output depends only on message order, not timing)
    constexpr long MAX_OUTSTANDING_LOGS = constants::LOG_QUEUE_SIZE / 2;
    long producedLogs = 0;
    auto replayWithBackpressure = [&](const Message& msg, int logs) {
        producedLogs += logs;
        while (producedLogs - consumer.received.load(std::memory_order_relaxed) > MAX_OUTSTANDING_LOGS) {}
        replay(*book, msg);
    };
    for (std::size_t i = 0; i < load.prefill.size(); i++) replayWithBackpressure(load.prefill[i], load.prefillLogs[i]);
    for (std::size_t i = 0; i < load.measured.size(); i++) replayWithBackpressure(load.measured[i], load.measuredLogs[i]);

    bool bestsMatch = (book->getBestBuyLevel() == load.finalBestBuy) && (book->getBestAskLevel() == load.finalBestAsk);
    book.reset();           // flushes the log messages still in the book's buffer
    consumer.finish();

    if (!bestsMatch) {
        state.SkipWithError("verify: book and shadow disagree on the best levels");
        return false;
    }
    if (consumer.validationFailures != 0) {
        state.SkipWithError("verify: validation failures, the generator produced an invalid message");
        return false;
    }
    if (consumer.received.load() != load.expectedLogs) {
        state.SkipWithError("verify: log messages dropped or unexpected (received != expected)");
        return false;
    }
    return true;
}


// fresh book (logs recorded, not published) -> prefill (untimed) -> measured part (timed by replayMeasured) -> check
// returns false (after SkipWithError) if the book and the shadow disagree

inline void reportTagCounts(benchmark::State& state, const Load& load) {
    for (int tag = 0; tag < TAG_COUNT; tag++) state.counters["n_" + TAG_NAMES[tag]] = load.tagCounts[tag];
    long crossing = load.tagCounts[ADD_CROSS_1] + load.tagCounts[ADD_CROSS_2] + load.tagCounts[ADD_CROSS_3_PLUS];
    long adds = crossing + load.tagCounts[ADD_NEW_LEVEL] + load.tagCounts[ADD_EXISTING_LEVEL];
    state.counters["n_fills"] = load.measuredFills;
    if (crossing > 0) state.counters["fills_per_cross"] = static_cast<double>(load.measuredFills) / crossing;
    // actual share: below crossPct because forced (mean-reversion) adds never cross
    if (adds > 0) state.counters["cross_share_of_adds"] = static_cast<double>(crossing) / adds;
}

inline void reportHistogram(benchmark::State& state, const std::vector<uint64_t>& hist, uint64_t maxTicks, const std::string& prefix) {
    uint64_t total = 0;
    for (auto count : hist) total += count;
    if (total == 0) return;
    const std::array<double, 4> quantiles = {0.5, 0.9, 0.99, 0.999};
    const std::array<std::string, 4> names = {"p50", "p90", "p99", "p99.9"};
    double nsPerTick = 1e9 / Utils::ticksPerSecond();
    for (std::size_t i = 0; i < quantiles.size(); i++) {
        uint64_t runningTotal = 0;
        for (std::size_t j = 0; j < hist.size(); j++) {
            runningTotal += hist[j];
            if (runningTotal >= total * quantiles[i]) {
                state.counters[prefix + "_" + names[i] + "_ns"] = j * nsPerTick;
                break;
            }
        }
    }
    state.counters[prefix + "_max_ns"] = maxTicks * nsPerTick;
    state.counters[prefix + "_overflow"] = hist.back();
}
