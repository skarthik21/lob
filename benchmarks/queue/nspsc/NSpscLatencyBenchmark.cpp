#include <benchmark/benchmark.h>
#include <atomic>
#include "NSpsc.h"
#include <thread>
#include <chrono>
#include "BenchmarkUtils.h"
#include "Utils.h"
#include <vector>
#include <iostream>
#include <string>
#include <cstdint>


// Message Types
struct alignas(128) Message8Bytes {
    std::uint64_t id;
    std::uint64_t ts;

    Message8Bytes(): id(-1) {};
    Message8Bytes(std::uint64_t id, std::uint64_t ts): id(id), ts(ts) {};
};


struct alignas(128) Message40Bytes {
    std::uint64_t id;
    std::uint64_t a[4];

    Message40Bytes(): id(-1) {};
    Message40Bytes(std::uint64_t id): id(id) {};
};

struct alignas(128) Message56Bytes {
    std::uint64_t id;
    std::uint64_t a[6];

    Message56Bytes(): id(-1) {};
    Message56Bytes(std::uint64_t id): id(id) {};
};

struct alignas(128) Message128Bytes {
    std::uint64_t id;
    std::uint64_t ts;
    std::uint64_t a[14];

    Message128Bytes(): id(-1) {};
    Message128Bytes(std::uint64_t id, std::uint64_t ts): id(id), ts(ts) {};
};

struct alignas(128) Message256Bytes {
    std::uint64_t id;
    std::uint64_t a[31];

    Message256Bytes(): id(-1) {};
    Message256Bytes(std::uint64_t id): id(id) {};
};


template<typename T>
T createT(int id, std::uint64_t ts) {
    return T(id, ts);
}

void computeHistograms(std::vector<std::uint64_t>& freqArray, benchmark::State& state, std::string prefix) {
    std::vector<double> quantiles{0.5, 0.75, 0.9, 0.99, 0.999};
    std::vector<double> res(5);
    double N = Utils::ticksPerSecond();
    long totalSum = 0;
    for (auto freq : freqArray) totalSum += freq;
    for (int i = 0;i<5;i++) {
        long currSum = 0;
        for (std::size_t j = 0; j < freqArray.size();j++) {
            currSum += freqArray[j];
            if (currSum >= totalSum*quantiles[i]) {
                res[i] = j;
                break;
            }
        }
        state.counters[prefix + "_p" + std::to_string(quantiles[i]*100) + "_ns"] = (res[i]/N)*1e9;
    }
    state.counters[prefix + "_tail_count"] = freqArray[freqArray.size()-1];
}

template<typename T>
static void BM_PushAndPopLatency(benchmark::State& state) {
    const long sampleSize = state.range(0);
    const long freq = state.range(1);
    const long warmpUp = state.range(2);
    const std::size_t size = state.range(3);
    auto qp = std::make_unique<NSpsc<T>>("benchmark_batch", size);
    auto& q = *qp;
    long mx = 4097;
    std::vector<std::uint64_t> e2eLatencyHist;
    std::vector<std::uint64_t> producerDelayHist;
    std::vector<std::uint64_t> consumerDelayHist;

    e2eLatencyHist.resize(mx);
    consumerDelayHist.resize(mx);
    producerDelayHist.resize(mx);

    for (auto _ : state) {
        std::atomic<bool> isProducerReady(false), isConsumerReady(false), isReady(false);

        // add the counts for full and empty

        std::thread producer([&]() {
            setBenchmarkThreadPriority();
            isProducerReady.store(true);
            while(!isReady.load()) continue;
            long i = 0;
            std::uint64_t prevTime = Utils::ticks();
            while (i < sampleSize) {
                std::uint64_t nxtTime = prevTime + freq;
                while (nxtTime > Utils::ticks()) continue;
                while(q.push(createT<T>(i, nxtTime)) == QueueStatus::FULL) continue;
                if (i >= warmpUp) producerDelayHist[std::min<uint64_t>(Utils::ticks() - nxtTime, mx - 1)]++;
                prevTime = nxtTime;
                i++;
            }
        });
        

        std::thread consumer([&]() {
            setBenchmarkThreadPriority();
            T popMsg;
            isConsumerReady.store(true);
            while(!isReady.load()) continue;
            long i = 0;
            while (i < sampleSize) {
                auto startTime = Utils::ticks();
                while (q.tryPop(popMsg) == QueueStatus::EMPTY) continue;
                if (popMsg.id != static_cast<std::uint64_t>(i)) std::abort();
                if (i >= warmpUp) {
                    auto endTick = Utils::ticks();
                    e2eLatencyHist[std::min<uint64_t>(endTick - popMsg.ts, mx - 1)]++;
                    consumerDelayHist[std::min<uint64_t>(endTick - startTime, mx - 1)]++;
                }
                benchmark::DoNotOptimize(popMsg);
                i++;
            }
        });

        while(!(isProducerReady.load() && isConsumerReady.load())) continue;
        isReady.store(true);

        if (producer.joinable()) producer.join();
        if (consumer.joinable()) consumer.join();
    }
    computeHistograms(e2eLatencyHist, state, "e2e");
    computeHistograms(producerDelayHist, state, "producerDelay");
    computeHistograms(consumerDelayHist, state, "consumerDelay");
};


//BENCHMARK(BM_PushAndPopLatency<Message8Bytes, 32768>)->Args({100000, 24, 1000})->Args({1000000, 48, 5000})->Repetitions(15)->ReportAggregatesOnly(true)->UseRealTime();
//BENCHMARK(BM_PushAndPopLatency<Message8Bytes, 1048576>)->Args({1000000, 48, 1000})->Args({10000000, 96, 10000})->Repetitions(15)->ReportAggregatesOnly(true)->UseRealTime();

BENCHMARK(BM_PushAndPopLatency<Message8Bytes>)->Args({1000000, 6, 1000, 32768})->Args({1000000, 12, 1000, 32768})->Args({1000000, 24, 1000, 32768})->Args({1000000, 48, 1000, 32768})->Args({1000000, 96, 1000, 32768})->Repetitions(15)->ReportAggregatesOnly(true)->UseRealTime();
BENCHMARK(BM_PushAndPopLatency<Message128Bytes>)->Args({1000000, 6, 1000, 1048576})->Args({1000000, 12, 1000, 1048576})->Args({1000000, 24, 1000, 1048576})->Args({1000000, 48, 1000, 1048576})->Args({1000000, 96, 1000, 1048576})->Repetitions(15)->ReportAggregatesOnly(true)->UseRealTime();
