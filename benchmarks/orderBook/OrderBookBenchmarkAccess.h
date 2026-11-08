#pragma once

#include "OrderBook.h"
#include "Message.h"

// benchmarks only: feeds a message straight to the book, without msgQ (OrderBook declares this class a friend)
class OrderBookBenchmarkAccess {
public:
    static void process(OrderBook& book, Message& msg) { book.walkBook(msg); }
};
