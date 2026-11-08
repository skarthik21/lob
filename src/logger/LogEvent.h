#pragma once

#include <string_view>

enum class LogEvent {
    VALIDATION_FAILED,
    ORDER_ADDED,
    ORDER_FILLED,
    ORDER_DELETED,
    MODIFIED_ORDER,
    MODIFIED_FAILED_DUMMY_ORDER
};

constexpr std::string_view toString(LogEvent logEvent) {
    switch (logEvent) {
        case LogEvent::VALIDATION_FAILED: return "VALIDATION_FAILED";
        case LogEvent::ORDER_ADDED: return "ORDER_ADDED";
        case LogEvent::ORDER_FILLED: return "ORDER_FILLED";
        case LogEvent::ORDER_DELETED: return "ORDER_DELETED";
        case LogEvent::MODIFIED_ORDER: return "MODIFIED_ORDER";
        case LogEvent::MODIFIED_FAILED_DUMMY_ORDER: return "MODIFIED_FAILED_DUMMY_ORDER";
    }
    return "UNKNOWN";
}