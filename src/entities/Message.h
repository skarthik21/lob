#pragma once

#include "Side.h"
#include "Tick.h"
#include <type_traits>
#include <compare>
#include <string_view>

/*
 - what do you expect from a entity 
   - this should be only a data fields 
   - with the five function being default
   - the operations on these should be default
*/

enum class MessageType { ADD_ORDER, MODIFY_ORDER, DELETE_ORDER };

constexpr std::string_view toString(MessageType msgType) {
    switch (msgType) {
        case MessageType::ADD_ORDER: return "ADD_ORDER";
        case MessageType::MODIFY_ORDER: return "MODIFY_ORDER";
        case MessageType::DELETE_ORDER: return "DELETE_ORDER";
    }
    return "UNKNOWN";
}

// bytes from the network can hold any integer, so "!= UNKNOWN" isn't enough: list the valid values
// (a switch without default: -Wswitch flags it when a new enumerator is added)
inline bool isTradable(Tick tick) {
    switch (tick) {
        case Tick::APPLE:
        case Tick::ALPHABET:
        case Tick::MCSFT:
            return true;
        case Tick::UNKNOWN:
        case Tick::INSTR:
            return false;
    }
    return false;
}

inline bool isBuyOrSell(Side side) {
    return side == Side::BUY || side == Side::SELL;
}

struct Message {
    int id;
    int orderId;
    Tick tick;
    int quantity;
    int price;
    int scale;
    int orderRefId;
    MessageType messageType;
    Side side;

    auto operator<=>(const Message& message) const = default;
    bool operator==(const Message& message) const = default;

    bool validateMsg() const {
        switch (this->messageType) {
            case MessageType::ADD_ORDER : {
                if ((id > 0) && (orderId > 0) && isTradable(tick) && (quantity > 0) && (price > 0) && isBuyOrSell(side)) return true;
                return false;
            }
            case MessageType::DELETE_ORDER : {
                if ((id > 0) && (orderId > 0) && isTradable(tick) && (orderRefId > 0)) return true;
                return false;
            }
            case MessageType::MODIFY_ORDER: {
                if ((id > 0) && (orderId > 0) && isTradable(tick) && (orderRefId > 0) && ((price > 0) ||(quantity > 0))) return true;
                return false;
            }
            default:
                return false;
        }
    }

};

// the wire format is the in-memory layout: OrderController memcpy's received bytes into a Message,
// so the sender must use the same field order, sizes, padding and byte order
static_assert(std::is_trivially_copyable_v<Message>);
static_assert(sizeof(Message) == 36);
