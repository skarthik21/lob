#include <benchmark/benchmark.h>
#include <atomic>
#include "Spsc.h"
#include <thread>
#include <chrono>
#include "BenchmarkUtils.h"


// Message Types
struct Message8Bytes {
    std::uint64_t id;

    Message8Bytes(): id(-1) {};
    Message8Bytes(std::uint64_t id): id(id) {};
};


struct Message40Bytes {
    std::uint64_t id;
    std::uint64_t a[4];

    Message40Bytes(): id(-1) {};
    Message40Bytes(std::uint64_t id): id(id) {};
};

struct Message128Bytes {
    std::uint64_t id;
    std::uint64_t a[15];

    Message128Bytes(): id(-1) {};
    Message128Bytes(std::uint64_t id): id(id) {};
};

struct Message256Bytes {
    std::uint64_t id;
    std::uint64_t a[31];

    Message256Bytes(): id(-1) {};
    Message256Bytes(std::uint64_t id): id(id) {};
};


template<typename T>
T createT(int id) {
    return T(id);
}


template<typename T, std::size_t size>
static void BM_PushAndPop(benchmark::State& state) {
    auto qp = std::make_unique<Spsc<T, size>>("benchmark");
    auto& q = *qp;
    //Spsc<T, size> q("benchmark");
    const long sampleRunSize = state.range(0);
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
                if (popMsg.id != i) std::abort();
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

constexpr std::array<std::size_t, 4> queueSizes{512, 1024, 32768, 1048576};



// Msg : 8 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message8Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message8Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message8Bytes, 32768>)->Arg(10000)->Arg(100000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message8Bytes, 1048576>)->Arg(100000)->Arg(1000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();

// Msg : 40 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message40Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message40Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message40Bytes, 32768>)->Arg(10000)->Arg(100000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message40Bytes, 1048576>)->Arg(100000)->Arg(1000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();


// Msg : 128 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message128Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message128Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message128Bytes, 32768>)->Arg(10000)->Arg(100000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message128Bytes, 1048576>)->Arg(100000)->Arg(1000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();


// Msg : 256 bytes, queue size : [512, 1024, 32768, 1048576]
//BENCHMARK(BM_PushAndPop<Message256Bytes, 512>)->Arg(1)->Arg(1000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message256Bytes, 1024>)->Arg(1)->Arg(1000)->Arg(10000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
//BENCHMARK(BM_PushAndPop<Message256Bytes, 32768>)->Arg(10000)->Arg(100000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();
BENCHMARK(BM_PushAndPop<Message256Bytes, 1048576>)->Arg(100000)->Arg(1000000)->MinWarmUpTime(0.1)->MinTime(0.5)->Repetitions(15)->ReportAggregatesOnly(true)->UseManualTime();