#pragma once

#include <string_view>

/*
- should remove INSTR from tick
*/
enum class Tick {
    UNKNOWN,
    APPLE,
    ALPHABET,
    MCSFT,
    INSTR
};


constexpr std::string_view toString(Tick tick) {
    switch (tick) {
        case Tick::ALPHABET: return "ALPHABET";
        case Tick::APPLE: return "APPLE";
        case Tick::MCSFT: return "MCSFT";
        case Tick::INSTR: return "INSTRUMENTATION";
        case Tick::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}