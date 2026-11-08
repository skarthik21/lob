#pragma once

#include <chrono>
#include <cstdint>

class Utils {
public:

    // aarch64: reads the generic timer directly (24 MHz on Apple Silicon, 41.67 ns per tick).
    // elsewhere: falls back to steady_clock nanoseconds, so one tick = 1 ns.
    static inline uint64_t ticks() {
#if defined(__aarch64__)
        uint64_t v;
        asm volatile("mrs %0, cntvct_el0" : "=r"(v) :: "memory");
        return v;
#else
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
    }

    static inline uint64_t ticksPerSecond() {           // 24'000'000 on Apple Silicon
#if defined(__aarch64__)
        uint64_t f;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
        return f;
#else
        return 1'000'000'000;
#endif
    }

    static long getTS() {
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
};
