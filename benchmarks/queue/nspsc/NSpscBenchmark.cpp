#include <benchmark/benchmark.h>
#include <atomic>
#include "NSpsc.h"
#include <thread>
#include <chrono>
#include "BenchmarkUtils.h"
#include <vector>
#include <iostream>


// Message Types
struct alignas(128) Message8Bytes {
    std::uint64_t id;

    Message8Bytes(): id(-1) {};
    Message8Bytes(std::uint64_t id): id(id) {};
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
    std::uint64_t a[15];

    Message128Bytes(): id(-1) {};
    Message128Bytes(std::uint64_t id): id(id) {};
};

struct alignas(128) Message256Bytes {
    std::uint64_t id;
    std::uint64_t a[31];

    Message256Bytes(): id(-1) {};
    Message256Bytes(std::uint64_t id): id(id) {};
};


template<typename T>
T createT(int id) {
    return T(id);
}

template<typename T>
static void BM_PushAndPopBatch(benchmark::State& state) {
    const long sampleRunSize = state.range(0);
    const long sampleBatchSize = state.range(1);
    const std::size_t size = state.range(2);
    auto qp = std::make_unique<NSpsc<T>>("benchmark_batch", size);
    auto& q = *qp;
    
    for (auto _ : state) {
        std::atomic<bool> isProducerReady(false), isConsumerReady(false), isReady(false);
        auto end = std::chrono::steady_clock::now();
        std::vector<T> pushBatch;
        pushBatch.resize(sampleBatchSize);
        std::vector<T> popBatch;
        popBatch.resize(sampleBatchSize);

        // add the counts for full and empty

        std::thread producer([&]() {
            setBenchmarkThreadPriority();
            isProducerReady.store(true);
            while(!isReady.load()) continue;
            for (long i = 0;i<sampleRunSize;) {
                for (long j = 0;j<sampleBatchSize;j++) {
                    pushBatch[j] = createT<T>(i);
                    i++;
                }
                while(q.pushBatch(pushBatch) == QueueStatus::FULL) continue;
            }
        });

        std::thread consumer([&]() {
            setBenchmarkThreadPriority();
            isConsumerReady.store(true);
            while(!isReady.load()) continue;
            for (long i = 0;i<sampleRunSize;) {
                while(q.tryPopBatch(popBatch) == QueueStatus::EMPTY) continue;
                for (long j = 0;j<sampleBatchSize;j++) {
                    if (popBatch[j].id != static_cast<std::uint64_t>(i)) {
                        std::abort();
                    }
                    i++;
                }
                benchmark::DoNotOptimize(popBatch);
            }
            end = std::chrono::steady_clock::now();
        });

        while(!(isProducerReady.load() && isConsumerReady.load())) continue;
        auto start = std::chrono::steady_clock::now();
        isReady.store(true);

        if (producer.joinable()) producer.join();
        if (consumer.joinable()) consumer.join();
        state.SetIterationTime(std::chrono::duration<double>(end - start).count());
    }
    state.SetItemsProcessed(sampleRunSize * state.iterations());

}


template<typename T>
static void BM_PushAndPop(benchmark::State& state) {
    const long sampleRunSize = state.range(0);
    const std::size_t size = state.range(1);
    auto qp = std::make_unique<NSpsc<T>>("benchmark", size);
    auto& q = *qp;
    //Spsc<T, size> q("benchmark");
    for (auto _ : state) {
        std::atomic<bool> isProducerReady(false), isConsumerReady(false), isReady(false);
        auto end = std::chrono::steady_clock::now();


        std::thread producer([&]() {
            setBenchmarkThreadPriority();
            isProducerReady.store(true);
            while(!isReady.load()) continue;
            for (long i = 0;i<sampleRunSize;i++) {
                while(q.push(createT<T>(i)) == QueueStatus::FULL) continue;
            }
        });

        std::thread consumer([&]() {
            setBenchmarkThreadPriority();
            T popMsg;
            isConsumerReady.store(true);
            while(!isReady.load()) continue;
            for (long i = 0;i<sampleRunSize;i++) {
                while (q.tryPop(popMsg) == QueueStatus::EMPTY) continue;
                if (popMsg.id != static_cast<std::uint64_t>(i)) std::abort();
                benchmark::DoNotOptimize(popMsg);
            }
            end = std::chrono::steady_clock::now();
        });

        while(!(isProducerReady.load() && isConsumerReady.load())) continue;
        auto start = std::chrono::steady_clock::now();
        isReady.store(true);

        if (producer.joinable()) producer.join();
        if (consumer.joinable()) consumer.join();
        state.SetIterationTime(std::chrono::duration<double>(end - start).count());
    }
    state.SetItemsProcessed(sampleRunSize * state.iterations());

}




// BENCHMARK BATCH
BENCHMARK(BM_PushAndPopBatch<Message8Bytes>)->Args({10000, 100, 32768})->Args({1000000, 1000, 32768})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPopBatch<Message8Bytes>)->Args({1000000, 1000, 1048576})->Args({10000000, 10000, 1048576})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();


BENCHMARK(BM_PushAndPopBatch<Message40Bytes>)->Args({10000, 100, 32768})->Args({1000000, 1000, 32768})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPopBatch<Message40Bytes>)->Args({1000000, 1000, 1048576})->Args({10000000, 10000, 1048576})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();

//BENCHMARK(BM_PushAndPop<Message56Bytes, 32768>)->Arg(10000)->Arg(1000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message56Bytes, 1048576>)->Arg(1000000)->Arg(10000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();

BENCHMARK(BM_PushAndPopBatch<Message128Bytes>)->Args({10000, 100, 32768})->Args({1000000, 1000, 32768})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPopBatch<Message128Bytes>)->Args({1000000, 1000, 1048576})->Args({10000000, 10000, 1048576})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();


BENCHMARK(BM_PushAndPopBatch<Message256Bytes>)->Args({10000, 100, 32768})->Args({1000000, 1000, 32768})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPopBatch<Message256Bytes>)->Args({1000000, 1000, 1048576})->Args({10000000, 10000, 1048576})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();



// Msg : 8 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message8Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message8Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message8Bytes>)->Args({10000, 32768})->Args({1000000, 32768})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message8Bytes>)->Args({1000000, 1048576})->Args({10000000, 1048576})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();

// Msg : 40 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message40Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message40Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message40Bytes, 32768>)->Arg(10000)->Arg(1000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message40Bytes, 1048576>)->Arg(1000000)->Arg(10000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();

// Msg : 56 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message40Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message40Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message56Bytes, 32768>)->Arg(10000)->Arg(1000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message56Bytes, 1048576>)->Arg(1000000)->Arg(10000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();


// Msg : 128 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message128Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message128Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message128Bytes>)->Args({10000, 32768})->Args({1000000, 32768})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message128Bytes>)->Args({1000000, 1048576})->Args({10000000, 1048576})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();


// Msg : 256 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message256Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message256Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message256Bytes>)->Args({10000, 32768})->Args({1000000, 32768})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message256Bytes>)->Args({1000000, 1048576})->Args({10000000, 1048576})->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();