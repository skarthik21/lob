#pragma once

namespace constants
{
    inline constexpr int OVERALL_THROUGHPUT_ORDERID = 1;
    inline constexpr int OVERALL_LATENCY_ORDERID = 2;
    inline constexpr int RCV_THROUGHPUT_LESS_FAILURE_ORDERID = 3;
    inline constexpr int RCV_THROUGHPUT_FAILURE_ORDERID = 4;
    inline constexpr int RCV_THROUGHPUT_VALIDATION_FAILURE_ORDERID = 5;
    inline constexpr int RCV_THROUGHPUT_QUEUE_FULL_FAILURE_ORDERID = 6;
    inline constexpr int THROUGHPUT_COUNT_LIMIT = 50;
    inline constexpr int MESSAGE_QUEUE_SIZE = 1 << 16;
    inline constexpr int LOG_QUEUE_SIZE = 1 << 16;
    inline constexpr int REQUEST_QUEUE_SIZE = 1 << 16;

    // 128 B: Apple Silicon's cache line. On x86 (64 B lines) it still avoids false sharing,
    // because the adjacent-line prefetcher pulls cache lines in 128 B pairs.
    inline constexpr int CACHE_LINE_SIZE = 128;
}
