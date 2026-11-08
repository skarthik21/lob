#pragma once

#include "Message.h"
#include "LogEvent.h"
#include <string>
#include <format>
#include <cstdint>
#include <compare>

/*
timestamp in log message, implement print method.
*/

struct LogMessage {
    int orderId;
    int fillOrderId;
    int fillQuantity;
    MessageType messageType;
    LogEvent logEvent;
    uint64_t ts;    // Utils::ticks(): cntvct_el0 counter ticks (24 MHz on Apple Silicon, 41.67 ns each), not wall-clock time

    auto operator<=>(const LogMessage& message) const = default;
    bool operator==(const LogMessage& message) const = default;


    void print(std::string& out) const {
        std::format_to(std::back_inserter(out), "msg: orderId: {}, fillOrderId: {}, fillQuantity: {}, messageType: {}, logEvent: {}, ts: {}\n", orderId, fillOrderId, fillQuantity, toString(messageType), toString(logEvent), ts);
    }


};