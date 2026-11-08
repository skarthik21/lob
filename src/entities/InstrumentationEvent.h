#pragma once

#include <string>
#include <format>

/*
- implement print method.
*/
struct InstrumentationEvent {
    long orderId;
    long ts;    // Utils::getTS(): steady_clock milliseconds since boot, not a calendar date
    long val;
    InstrumentationEvent() = default;
    InstrumentationEvent(long orderId, long ts, long val): orderId{orderId}, ts{ts}, val{val} {}

    std::string print() const {
        return std::format("orderId: {}, ts: {}, val: {} \n", orderId, ts, val);
    }
};