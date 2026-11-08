#include <benchmark/benchmark.h>
#include "OrderBookBenchmarkUtils.h"
#include "Constants.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstdint>

// ---------- end to end, open loop ----------
// producer (this thread) -> msgQ -> book thread -> logQ -> consumer thread
// The producer sends measured message i at its scheduled time t0 + i / rate, whether or not the book has caught up
// (open loop), so a backlog shows up as latency instead of slowing the sender down.
// Per measured message, from its scheduled time:
//   e2e     = consumer pops the message's last log event   (the end of its processing)
//   book    = the book stamps that event (LogMessage.ts)    (msgQ wait + matching)
//   logHop  = e2e - book                                    (logQ hop)
// The book publishes every log event as it is produced (batch size 1), so log batching doesn't add latency.

constexpr int E2E_LOG_BATCH = 1;
constexpr std::size_t E2E_HIST_SIZE = (1 << 16) + 1;      // 1-tick buckets; last = 65536+ ticks (2.73 ms on Apple Silicon)
constexpr auto DRAIN_TIMEOUT = std::chrono::seconds(5);   // no progress for this long = log events were dropped
constexpr long MAX_OUTSTANDING_EVENTS = constants::LOG_QUEUE_SIZE / 2;    // prefill only: pushed but not yet consumed

struct Histogram {
    std::vector<uint64_t> buckets = std::vector<uint64_t>(E2E_HIST_SIZE);
    uint64_t maxTicks = 0;

    void add(uint64_t ticks) {
        buckets[std::min<uint64_t>(ticks, E2E_HIST_SIZE - 1)]++;
        maxTicks = std::max(maxTicks, ticks);
    }
};

// a - b, or 0 if b is later (never expected: every stamp is taken after the previous one)
inline uint64_t ticksSince(uint64_t a, uint64_t b) {
    return a > b ? a - b : 0;
}

// polls flag until it is set or `progress` stops moving for DRAIN_TIMEOUT; false on timeout
inline bool waitFor(const std::atomic<bool>& flag, const std::atomic<long>& progress) {
    long last = progress.load(std::memory_order_relaxed);
    auto lastChange = std::chrono::steady_clock::now();
    while (!flag.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        long now = progress.load(std::memory_order_relaxed);
        if (now != last) {
            last = now;
            lastChange = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - lastChange > DRAIN_TIMEOUT) {
            return false;
        }
    }
    return true;
}


// args: strategy, sampleSize, crossPct, maxLevels (the last two only for AGGRESSIVE), rate (messages per second)
static void BM_OrderBookE2ELatency(benchmark::State& state) {
    setBenchmarkThreadPriority();
    auto strategy = static_cast<Strategy>(state.range(0));
    long sampleSize = state.range(1);
    const uint64_t rate = static_cast<uint64_t>(state.range(4));
    const uint64_t tps = Utils::ticksPerSecond();
    auto msgQ = std::make_unique<MessageQueue>("e2e_msg_queue", std::size_t{constants::MESSAGE_QUEUE_SIZE});
    auto logQ = std::make_unique<LogQueue>("e2e_log_queue", std::size_t{constants::LOG_QUEUE_SIZE});
    Load load = generateLoad(strategy, sampleSize, static_cast<int>(state.range(2)), static_cast<int>(state.range(3)));
    if (!verifyLoad(state, load, *msgQ, *logQ)) return;

    long prefillLogs = 0;
    for (int logs : load.prefillLogs) prefillLogs += logs;
    const std::size_t n = load.measured.size();

    Histogram e2e, book, logHop, producerDelay;
    long fullRetries = 0;

    for (auto _ : state) {
        auto orderBook = std::make_unique<OrderBook>(*msgQ, *logQ, Tick::ALPHABET, LOWER_CIRCUIT, TICK_SIZE, RANGE, LIVE_ORDER_CAPACITY_OF_BOOK, E2E_LOG_BATCH, true);
        std::atomic<bool> stopRequested{false};
        std::atomic<bool> prefillDone{false}, measuredDone{false}, startMeasured{false};
        std::atomic<long> eventsSeen{0};        // consumer progress, for the drop timeout
        uint64_t t0 = 0;                        // written before startMeasured (release), read after it (acquire)
        uint64_t lastEnd = 0;                   // written by the consumer, read after join

        // same loop as OrderBook::start(), on a thread that gets the benchmark priority
        std::thread bookThread([&]() {
            setBenchmarkThreadPriority();
            while (!stopRequested.load(std::memory_order_relaxed)) orderBook->processMessageSync();
        });

        std::thread consumer([&]() {
            setBenchmarkThreadPriority();
            LogMessage ev;
            // prefill: count its events, no timing
            long seen = 0;
            while (seen < prefillLogs) {
                if (logQ->tryPop(ev) == QueueStatus::EMPTY) {
                    if (stopRequested.load(std::memory_order_relaxed)) return;
                    continue;
                }
                eventsSeen.store(++seen, std::memory_order_relaxed);
            }
            prefillDone.store(true, std::memory_order_release);
            while (!startMeasured.load(std::memory_order_acquire)) {
                if (stopRequested.load(std::memory_order_relaxed)) return;
            }

            // one book thread processes messages in order, so events arrive in order:
            // message i owns the next measuredLogs[i] events, and its last one ends it
            std::size_t i = 0;
            int left = load.measuredLogs[0];
            while (i < n) {
                if (logQ->tryPop(ev) == QueueStatus::EMPTY) {
                    if (stopRequested.load(std::memory_order_relaxed)) return;
                    continue;
                }
                uint64_t end = Utils::ticks();
                eventsSeen.store(++seen, std::memory_order_relaxed);
                if (--left > 0) continue;
                uint64_t scheduled = t0 + (i * tps) / rate;
                e2e.add(ticksSince(end, scheduled));
                book.add(ticksSince(ev.ts, scheduled));
                logHop.add(ticksSince(end, ev.ts));
                lastEnd = end;
                if (++i < n) left = load.measuredLogs[i];
            }
            measuredDone.store(true, std::memory_order_release);
        });

        // prefill (book shape + warm-up), unpaced: a full msgQ just waits for the book, and the producer holds back
        // while more than half a logQ of events is outstanding, so a stalled consumer can't make the book drop logs
        long pushedEvents = 0;
        for (std::size_t i = 0; i < load.prefill.size(); i++) {
            pushedEvents += load.prefillLogs[i];
            while (pushedEvents - eventsSeen.load(std::memory_order_relaxed) > MAX_OUTSTANDING_EVENTS) {}
            while (msgQ->push(load.prefill[i]) != QueueStatus::PUSHED) {}
        }
        bool ok = waitFor(prefillDone, eventsSeen);

        if (ok) {
            t0 = Utils::ticks() + tps / 1000;    // start 1 ms from now
            startMeasured.store(true, std::memory_order_release);
            for (std::size_t i = 0; i < n; i++) {
                uint64_t scheduled = t0 + (i * tps) / rate;
                uint64_t now;
                while ((now = Utils::ticks()) < scheduled) {}
                producerDelay.add(now - scheduled);
                while (msgQ->push(load.measured[i]) != QueueStatus::PUSHED) fullRetries++;
            }
            ok = waitFor(measuredDone, eventsSeen);
        }

        stopRequested.store(true, std::memory_order_relaxed);
        bookThread.join();
        consumer.join();

        if (!ok) {
            state.SkipWithError("log events dropped (logQ was full): the consumer stopped receiving events");
            break;
        }
        if ((orderBook->getBestBuyLevel() != load.finalBestBuy) || (orderBook->getBestAskLevel() != load.finalBestAsk)) {
            state.SkipWithError("book and shadow disagree on the best levels");
            break;
        }
        state.SetIterationTime(static_cast<double>(ticksSince(lastEnd, t0)) / static_cast<double>(tps));
    }

    state.SetItemsProcessed(state.iterations() * sampleSize);
    state.counters["rate"] = static_cast<double>(rate);
    state.counters["msgQ_full_retries"] = static_cast<double>(fullRetries);
    reportTagCounts(state, load);
    reportHistogram(state, e2e.buckets, e2e.maxTicks, "e2e");
    reportHistogram(state, book.buckets, book.maxTicks, "book");
    reportHistogram(state, logHop.buckets, logHop.maxTicks, "logHop");
    reportHistogram(state, producerDelay.buckets, producerDelay.maxTicks, "producerDelay");
}


constexpr long RESTING = static_cast<long>(Strategy::RESTING);
constexpr long AGGRESSIVE = static_cast<long>(Strategy::AGGRESSIVE);

BENCHMARK(BM_OrderBookE2ELatency)
    ->Name("BM_OrderBookE2ELatency/resting")
    ->ArgNames({"strategy", "samples", "crossPct", "maxLevels", "rate"})
    ->ArgsProduct({{RESTING}, {1000000}, {0}, {0}, {100000, 500000, 1000000, 2000000, 5000000}})
    ->UseManualTime()
    ->Iterations(1)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_OrderBookE2ELatency)
    ->Name("BM_OrderBookE2ELatency/aggressive")
    ->ArgNames({"strategy", "samples", "crossPct", "maxLevels", "rate"})
    ->ArgsProduct({{AGGRESSIVE}, {1000000}, {10}, {3}, {100000, 500000, 1000000, 2000000, 5000000}})
    ->UseManualTime()
    ->Iterations(1)
    ->Unit(benchmark::kMillisecond);
