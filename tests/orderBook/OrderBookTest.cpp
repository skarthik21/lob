#include <gtest/gtest.h>
#include "OrderBook.h"
#include "Utils.h"


/*

(Spsc<Message, constants::MESSAGE_QUEUE_SIZE>& messageQueue, Spsc<LogMessage, constants::LOG_QUEUE_SIZE>& logQueue, Tick tick, int lowerCircuit, int tickSize, int range);
*/

/*
int id;
    int orderId;
    Tick tick;
    int quantity;
    int price;
    int scale;
    int orderRefId;
    MessageType messageType;
    Side side;
*/

/*
int orderId;
    int fillOrderId;
    int fillQuantity;
    MessageType messageType;
    LogEvent logEvent;
    long ts;
*/


// passing rvalue to non const lvalue
bool compareLogMessages(const LogMessage& m1, const LogMessage& m2) {
    return (m1.orderId == m2.orderId) && (m1.fillOrderId == m2.fillOrderId) && (m1.fillQuantity == m2.fillQuantity) && (m1.logEvent == m2.logEvent) && (m1.messageType == m2.messageType);
}


// messages
Message createAddOrder(int orderId, int quantity, int price, Side side, Tick tick = Tick::ALPHABET) {
    return Message(orderId, orderId, tick, quantity, price, 100, -1, MessageType::ADD_ORDER, side);
}

Message createModifyOrder(int orderId, int orderRefId, int quantity, int price, Side side, Tick tick = Tick::ALPHABET) {
    return Message(orderId, orderId, tick, quantity, price, 100, orderRefId, MessageType::MODIFY_ORDER, side);
}

Message createDeleteOrder(int orderId, int orderRefId, Tick tick = Tick::ALPHABET) {
    return Message(orderId, orderId, tick, -1, -1, 100, orderRefId, MessageType::DELETE_ORDER, Side::UNKNOWN);
}

// log messages
LogMessage createOrderAddedLogMessage(int orderId, int remaining) {
    return LogMessage(orderId, -1, remaining, MessageType::ADD_ORDER, LogEvent::ORDER_ADDED, Utils::getTS());
}

LogMessage createAddOrderFilledLogMessage(int orderId, int fillOrderId, int fillQuantity) {
    return LogMessage(orderId, fillOrderId, fillQuantity, MessageType::ADD_ORDER, LogEvent::ORDER_FILLED, Utils::getTS());
}

LogMessage createModifyOrderFilledLogMessage(int orderId, int fillOrderId, int fillQuantity) {
    return LogMessage(orderId, fillOrderId, fillQuantity, MessageType::MODIFY_ORDER, LogEvent::ORDER_FILLED, Utils::getTS());
}

LogMessage createModifyOrderDeletedLogMessage(int orderRefId, int orderId) {
    return LogMessage(orderRefId, orderId, -1, MessageType::MODIFY_ORDER, LogEvent::ORDER_DELETED, Utils::getTS());
}

LogMessage createModifiedOrderLogMessage(int orderRefId, int orderId) {
    return LogMessage(orderRefId, orderId, -1, MessageType::MODIFY_ORDER, LogEvent::MODIFIED_ORDER, Utils::getTS());
}

LogMessage createValidationFailedLogMessage(int orderId, MessageType messageType) {
    return LogMessage(orderId, -1, -1, messageType, LogEvent::VALIDATION_FAILED, Utils::getTS());
}

LogMessage createDeleteOrderDeletedLogMessage(int orderRefId, int orderId) {
    return LogMessage(orderRefId, orderId, -1, MessageType::DELETE_ORDER, LogEvent::ORDER_DELETED, Utils::getTS());
}



TEST(OrderBookTest, AddSellOrdersOnAEmptyBook) {
    NSpsc<Message> msgQ = NSpsc<Message>("orderbook_msg_queue", constants::MESSAGE_QUEUE_SIZE);
    NSpsc<LogMessage> logQ = NSpsc<LogMessage>("orderbook_log_queue", constants::LOG_QUEUE_SIZE);
    OrderBook book = OrderBook(msgQ, logQ, Tick::ALPHABET, 100, 5, 100, 1e3, 1);

    ASSERT_EQ(msgQ.push(createAddOrder(1, 10, 500, Side::SELL)), QueueStatus::PUSHED);
    book.processMessageSync();
    ASSERT_EQ(book.getBestAskLevel(), 80);
    ASSERT_EQ(book.getBestBuyLevel(), -1);
    LogMessage poppedMsg;
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createOrderAddedLogMessage(1, 10)), true);

    ASSERT_EQ(msgQ.push(createAddOrder(2, 10, 500, Side::BUY)), QueueStatus::PUSHED);
    book.processMessageSync();
    ASSERT_EQ(book.getBestBuyLevel(), -1);
    ASSERT_EQ(book.getBestAskLevel(), -1);
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createAddOrderFilledLogMessage(1, 2, 10)), true);
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createOrderAddedLogMessage(2, 0)), true);

}

// a rejected repeat orderId must leave the original order in the book, with its full quantity
TEST(OrderBookTest, AddOrderRepeatOrderKeepsOriginal) {
    NSpsc<Message> msgQ = NSpsc<Message>("orderbook_msg_queue", constants::MESSAGE_QUEUE_SIZE);
    NSpsc<LogMessage> logQ = NSpsc<LogMessage>("orderbook_log_queue", constants::LOG_QUEUE_SIZE);
    OrderBook book = OrderBook(msgQ, logQ, Tick::ALPHABET, 100, 5, 100, 1e3, 1);

    ASSERT_EQ(msgQ.push(createAddOrder(1, 10, 500, Side::BUY)), QueueStatus::PUSHED);
    book.processMessageSync();
    ASSERT_EQ(book.getBestBuyLevel(), 80);
    ASSERT_EQ(book.getBestAskLevel(), -1);
    LogMessage poppedMsg;
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createOrderAddedLogMessage(1, 10)), true);

    ASSERT_EQ(msgQ.push(createAddOrder(1, 10, 500, Side::BUY)), QueueStatus::PUSHED);
    book.processMessageSync();
    ASSERT_EQ(book.getBestBuyLevel(), 80);
    ASSERT_EQ(book.getBestAskLevel(), -1);
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createValidationFailedLogMessage(1, MessageType::ADD_ORDER)), true);

    ASSERT_EQ(msgQ.push(createAddOrder(2, 10, 500, Side::SELL)), QueueStatus::PUSHED);
    book.processMessageSync();
    ASSERT_EQ(book.getBestBuyLevel(), -1);
    ASSERT_EQ(book.getBestAskLevel(), -1);
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createAddOrderFilledLogMessage(1, 2, 10)), true);
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createOrderAddedLogMessage(2, 0)), true);
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::EMPTY);
}



// book: buy order 1 (10 @ 250), sell order 2 (10 @ 500)
// every message should fail validation and leave the book unchanged
class OrderValidationFixture : public ::testing::TestWithParam<Message> {
protected:
    OrderValidationFixture(){};

    void SetUp() override {
        ASSERT_EQ(msgQ.push(createAddOrder(1, 10, 250, Side::BUY)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 30);
        ASSERT_EQ(book.getBestAskLevel(), -1);

        ASSERT_EQ(msgQ.push(createAddOrder(2, 10, 500, Side::SELL)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 30);
        ASSERT_EQ(book.getBestAskLevel(), 80);

        // both setup orders rest in full
        LogMessage poppedMsg;
        for (int orderId : {1, 2}) {
            ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
            ASSERT_EQ(compareLogMessages(poppedMsg, createOrderAddedLogMessage(orderId, 10)), true);
        }
        ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::EMPTY);
    }

    NSpsc<Message> msgQ = NSpsc<Message>("orderbook_msg_queue", constants::MESSAGE_QUEUE_SIZE);
    NSpsc<LogMessage> logQ = NSpsc<LogMessage>("orderbook_log_queue", constants::LOG_QUEUE_SIZE);
    OrderBook book = OrderBook(msgQ, logQ, Tick::ALPHABET, 100, 5, 100, 1e3, 1);
};

TEST_P(OrderValidationFixture, OrderValidationFails) {
    Message msg = GetParam();
    ASSERT_EQ(msgQ.push(msg), QueueStatus::PUSHED);
    book.processMessageSync();
    ASSERT_EQ(book.getBestBuyLevel(), 30);
    ASSERT_EQ(book.getBestAskLevel(), 80);
    LogMessage poppedMsg;
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
    ASSERT_EQ(compareLogMessages(poppedMsg, createValidationFailedLogMessage(msg.orderId, msg.messageType)), true);
    ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::EMPTY);
}

INSTANTIATE_TEST_SUITE_P(AddOrderValidationTests, OrderValidationFixture, ::testing::Values(
    // price (above upper circuit, below lower circuit)
    createAddOrder(3, 10, 605, Side::BUY),
    createAddOrder(3, 10, 75, Side::SELL),
    // price not on the tick grid (tickSize 5)
    createAddOrder(3, 10, 252, Side::BUY),
    // side
    createAddOrder(3, 10, 500, Side::UNKNOWN),
    // same tick
    createAddOrder(3, 10, 350, Side::BUY, Tick::MCSFT),
    // repeat orderId (on the same side, on the other side)
    createAddOrder(1, 10, 250, Side::BUY),
    createAddOrder(2, 10, 250, Side::BUY)
));

INSTANTIATE_TEST_SUITE_P(ModifyOrderValidationTests, OrderValidationFixture, ::testing::Values(
    // same tick
    createModifyOrder(3, 1, 10, 275, Side::BUY, Tick::MCSFT),
    // referenced order should be present
    createModifyOrder(3, 9, 10, 275, Side::BUY),
    // no duplicate order with the same id (another order's id, and the referenced order's own id)
    createModifyOrder(2, 1, 10, 275, Side::BUY),
    createModifyOrder(1, 1, 10, 275, Side::BUY),
    // side should not change
    createModifyOrder(3, 1, 10, 275, Side::SELL),
    createModifyOrder(3, 2, 10, 475, Side::BUY),
    // price validation if it is changed (below lower circuit, above upper circuit)
    createModifyOrder(3, 1, 10, 95, Side::BUY),
    createModifyOrder(3, 2, 10, 600, Side::SELL),
    // price not on the tick grid (tickSize 5)
    createModifyOrder(3, 1, 10, 277, Side::BUY)
));

INSTANTIATE_TEST_SUITE_P(DeleteOrderValidationTests, OrderValidationFixture, ::testing::Values(
    // same tick
    createDeleteOrder(3, 1, Tick::MCSFT),
    // referenced order should be present
    createDeleteOrder(3, 9),
    // no duplicate order with the same id (another order's id, and the referenced order's own id)
    createDeleteOrder(2, 1),
    createDeleteOrder(1, 1)
));





struct OrderTestParam {
    std::vector<Message> addOrders;
    std::vector<int> resBestBuys;
    std::vector<int> resBestAsks;
    std::vector<std::vector<LogMessage>> resLogMessages;
};

class OrderBookParamFixture : public ::testing::TestWithParam<OrderTestParam> {
protected:
    OrderBookParamFixture(){};

    void SetUp() override {
        ASSERT_EQ(msgQ.push(createAddOrder(1, 10, 250, Side::BUY)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 30);
        ASSERT_EQ(book.getBestAskLevel(), -1);

        ASSERT_EQ(msgQ.push(createAddOrder(2, 10, 500, Side::SELL)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 30);
        ASSERT_EQ(book.getBestAskLevel(), 80);

        ASSERT_EQ(msgQ.push(createAddOrder(3, 10, 100, Side::BUY)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 30);
        ASSERT_EQ(book.getBestAskLevel(), 80);

        ASSERT_EQ(msgQ.push(createAddOrder(4, 10, 300, Side::BUY)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 40);
        ASSERT_EQ(book.getBestAskLevel(), 80);

        ASSERT_EQ(msgQ.push(createAddOrder(5, 10, 300, Side::BUY)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 40);
        ASSERT_EQ(book.getBestAskLevel(), 80);


        ASSERT_EQ(msgQ.push(createAddOrder(6, 10, 595, Side::SELL)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 40);
        ASSERT_EQ(book.getBestAskLevel(), 80);

        ASSERT_EQ(msgQ.push(createAddOrder(7, 10, 400, Side::SELL)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 40);
        ASSERT_EQ(book.getBestAskLevel(), 60);

        ASSERT_EQ(msgQ.push(createAddOrder(8, 10, 400, Side::SELL)), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), 40);
        ASSERT_EQ(book.getBestAskLevel(), 60);

        // all eight setup orders rest in full
        LogMessage poppedMsg;
        for (int orderId = 1; orderId <= 8; orderId++) {
            ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
            ASSERT_EQ(compareLogMessages(poppedMsg, createOrderAddedLogMessage(orderId, 10)), true);
        }
        ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::EMPTY);
    }

    NSpsc<Message> msgQ = NSpsc<Message>("orderbook_msg_queue", constants::MESSAGE_QUEUE_SIZE);
    NSpsc<LogMessage> logQ = NSpsc<LogMessage>("orderbook_log_queue", constants::LOG_QUEUE_SIZE);
    OrderBook book = OrderBook(msgQ, logQ, Tick::ALPHABET, 100, 5, 100, 1e3, 1);
};

/*
struct OrderTestParam {
    std::vector<Message> addOrders;
    std::vector<int> resBestBuys;
    std::vector<int> resBestAsks;
    std::vector<std::vector<LogMessage>> resLogMessages;
};
*/


TEST_P(OrderBookParamFixture, AddOrderEffectOnBook) {
    auto batchParams = GetParam();
    int sz = batchParams.addOrders.size();
    auto orders = batchParams.addOrders;
    for (int i = 0;i<sz;i++) {
        ASSERT_EQ(msgQ.push(orders[i]), QueueStatus::PUSHED);
        book.processMessageSync();
        ASSERT_EQ(book.getBestBuyLevel(), batchParams.resBestBuys[i]);
        ASSERT_EQ(book.getBestAskLevel(), batchParams.resBestAsks[i]);
        LogMessage poppedMsg;
        auto resLogMessages = batchParams.resLogMessages[i];
        for (auto resLogMessage : resLogMessages) {
            ASSERT_EQ(logQ.tryPop(poppedMsg), QueueStatus::POPPED);
            ASSERT_EQ(compareLogMessages(poppedMsg, resLogMessage), true);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(AddOrderEffectTests, OrderBookParamFixture, ::testing::Values(
    OrderTestParam({createAddOrder(9, 25, 300, Side::SELL)}, {30}, {40}, {{createAddOrderFilledLogMessage(4, 9, 10), createAddOrderFilledLogMessage(5, 9, 10), createOrderAddedLogMessage(9, 5)}}),
    OrderTestParam({createAddOrder(9, 35, 200, Side::SELL)}, {0}, {20}, {{createAddOrderFilledLogMessage(4, 9, 10), createAddOrderFilledLogMessage(5, 9, 10), createAddOrderFilledLogMessage(1, 9, 10), createOrderAddedLogMessage(9, 5)}}),
    OrderTestParam({createAddOrder(9, 15, 300, Side::SELL)}, {40}, {60}, {{createAddOrderFilledLogMessage(4, 9, 10), createAddOrderFilledLogMessage(5, 9, 5), createOrderAddedLogMessage(9, 0)}}),
    OrderTestParam({createAddOrder(9, 25, 200, Side::SELL)}, {30}, {60}, {{createAddOrderFilledLogMessage(4, 9, 10), createAddOrderFilledLogMessage(5, 9, 10), createAddOrderFilledLogMessage(1, 9, 5), createOrderAddedLogMessage(9, 0)}}),
    // OrderCompleteBuySideClear
    OrderTestParam({createAddOrder(9, 45, 100, Side::SELL)}, {-1}, {0}, {{createAddOrderFilledLogMessage(4, 9, 10), createAddOrderFilledLogMessage(5, 9, 10), createAddOrderFilledLogMessage(1, 9, 10), createAddOrderFilledLogMessage(3, 9, 10), createOrderAddedLogMessage(9, 5)}}),
    // PartialFilledOrderOnSellSideSingleLevel
    OrderTestParam({createAddOrder(9, 25, 400, Side::BUY)}, {60}, {80}, {{createAddOrderFilledLogMessage(7, 9, 10), createAddOrderFilledLogMessage(8, 9, 10), createOrderAddedLogMessage(9, 5)}}),
    // PartialFilledOrderOnSellSideMultiLevel
    OrderTestParam({createAddOrder(9, 35, 550, Side::BUY)}, {90}, {99}, {{createAddOrderFilledLogMessage(7, 9, 10), createAddOrderFilledLogMessage(8, 9, 10), createAddOrderFilledLogMessage(2, 9, 10), createOrderAddedLogMessage(9, 5)}}),
    // CompleteFilledOrderOnSellSideSingleLevel
    OrderTestParam({createAddOrder(9, 15, 400, Side::BUY)}, {40}, {60}, {{createAddOrderFilledLogMessage(7, 9, 10), createAddOrderFilledLogMessage(8, 9, 5), createOrderAddedLogMessage(9, 0)}}),
    // CompleteFilledOrderOnSellSideMultiLevel
    OrderTestParam({createAddOrder(9, 25, 500, Side::BUY)}, {40}, {80}, {{createAddOrderFilledLogMessage(7, 9, 10), createAddOrderFilledLogMessage(8, 9, 10), createAddOrderFilledLogMessage(2, 9, 5), createOrderAddedLogMessage(9, 0)}}),
    // OrderCompleteSellSideClear
    OrderTestParam({createAddOrder(9, 45, 595, Side::BUY)}, {99}, {-1}, {{createAddOrderFilledLogMessage(7, 9, 10), createAddOrderFilledLogMessage(8, 9, 10), createAddOrderFilledLogMessage(2, 9, 10), createAddOrderFilledLogMessage(6, 9, 10), createOrderAddedLogMessage(9, 5)}})
));



INSTANTIATE_TEST_SUITE_P(ModifyOrderEffectTests, OrderBookParamFixture, ::testing::Values(
    // independent price change: 250 -> 275 (buy), 500 -> 475 (sell)
    OrderTestParam({createModifyOrder(9, 1, 10, 275, Side::BUY), createModifyOrder(10, 2, 10, 475, Side::SELL)}, {40, 40}, {60, 60}, {{createModifyOrderDeletedLogMessage(1, 9)}, {createModifyOrderDeletedLogMessage(2, 10)}}),
    // independent price change: 250 -> 325 (buy), 500 -> 375 (sell)
    OrderTestParam({createModifyOrder(9, 1, 10, 325, Side::BUY), createModifyOrder(10, 2, 10, 375, Side::SELL)}, {45, 45}, {60, 55}, {{createModifyOrderDeletedLogMessage(1, 9)}, {createModifyOrderDeletedLogMessage(2, 10)}}),
    // independent price change: 250 -> 225 (buy), 500 -> 525 (sell)
    OrderTestParam({createModifyOrder(9, 1, 10, 225, Side::BUY), createModifyOrder(10, 2, 10, 525, Side::SELL)}, {40, 40}, {60, 60}, {{createModifyOrderDeletedLogMessage(1, 9)}, {createModifyOrderDeletedLogMessage(2, 10)}}),
    // independent price change that crosses the book: 250 -> 400 (buy), 500 -> 300 (sell)
    OrderTestParam({createModifyOrder(9, 1, 10, 400, Side::BUY), createModifyOrder(10, 2, 10, 300, Side::SELL)}, {40, 40}, {60, 60}, {{createModifyOrderDeletedLogMessage(1, 9), createModifyOrderFilledLogMessage(7, 9, 10)}, {createModifyOrderDeletedLogMessage(2, 10), createModifyOrderFilledLogMessage(4, 10, 10)}}),

    // partial fill of order 4 (10 -> 5 left), quantity increase 10 -> 15: moves to the back of level 300
    OrderTestParam({createAddOrder(9, 5, 300, Side::SELL), createModifyOrder(10, 4, 15, 300, Side::BUY), createAddOrder(11, 20, 300, Side::SELL)}, {40, 40, 30}, {60, 60, 60}, {{createAddOrderFilledLogMessage(4, 9, 5), createOrderAddedLogMessage(9, 0)}, {createModifyOrderDeletedLogMessage(4, 10)}, {createAddOrderFilledLogMessage(5, 11, 10), createAddOrderFilledLogMessage(10, 11, 10), createOrderAddedLogMessage(11, 0)}}),
    // partial fill of order 4 (10 -> 5 left), quantity decrease 10 -> 8: keeps its place at the front of level 300 with 3 left
    OrderTestParam({createAddOrder(9, 5, 300, Side::SELL), createModifyOrder(10, 4, 8, 300, Side::BUY), createAddOrder(11, 13, 300, Side::SELL)}, {40, 40, 30}, {60, 60, 60}, {{createAddOrderFilledLogMessage(4, 9, 5), createOrderAddedLogMessage(9, 0)}, {createModifiedOrderLogMessage(4, 10)}, {createAddOrderFilledLogMessage(10, 11, 3), createAddOrderFilledLogMessage(5, 11, 10), createOrderAddedLogMessage(11, 0)}}),

    // partial fill of order 4 (10 -> 5 left), price increase 300 -> 325, quantity increase 10 -> 15 (10 left)
    OrderTestParam({createAddOrder(9, 5, 300, Side::SELL), createModifyOrder(10, 4, 15, 325, Side::BUY), createAddOrder(11, 10, 325, Side::SELL)}, {40, 45, 40}, {60, 60, 60}, {{createAddOrderFilledLogMessage(4, 9, 5), createOrderAddedLogMessage(9, 0)}, {createModifyOrderDeletedLogMessage(4, 10)}, {createAddOrderFilledLogMessage(10, 11, 10), createOrderAddedLogMessage(11, 0)}}),
    // partial fill of order 4 (10 -> 5 left), price increase 300 -> 325, quantity decrease 10 -> 8 (3 left)
    OrderTestParam({createAddOrder(9, 5, 300, Side::SELL), createModifyOrder(10, 4, 8, 325, Side::BUY), createAddOrder(11, 3, 325, Side::SELL)}, {40, 45, 40}, {60, 60, 60}, {{createAddOrderFilledLogMessage(4, 9, 5), createOrderAddedLogMessage(9, 0)}, {createModifyOrderDeletedLogMessage(4, 10)}, {createAddOrderFilledLogMessage(10, 11, 3), createOrderAddedLogMessage(11, 0)}}),
    // partial fill of order 4 (10 -> 5 left), price decrease 300 -> 275, quantity increase 10 -> 15 (10 left)
    OrderTestParam({createAddOrder(9, 5, 300, Side::SELL), createModifyOrder(10, 4, 15, 275, Side::BUY), createAddOrder(11, 20, 275, Side::SELL)}, {40, 40, 30}, {60, 60, 60}, {{createAddOrderFilledLogMessage(4, 9, 5), createOrderAddedLogMessage(9, 0)}, {createModifyOrderDeletedLogMessage(4, 10)}, {createAddOrderFilledLogMessage(5, 11, 10), createAddOrderFilledLogMessage(10, 11, 10), createOrderAddedLogMessage(11, 0)}}),
    // partial fill of order 4 (10 -> 5 left), price decrease 300 -> 275, quantity decrease 10 -> 8 (3 left)
    OrderTestParam({createAddOrder(9, 5, 300, Side::SELL), createModifyOrder(10, 4, 8, 275, Side::BUY), createAddOrder(11, 13, 275, Side::SELL)}, {40, 40, 30}, {60, 60, 60}, {{createAddOrderFilledLogMessage(4, 9, 5), createOrderAddedLogMessage(9, 0)}, {createModifyOrderDeletedLogMessage(4, 10)}, {createAddOrderFilledLogMessage(5, 11, 10), createAddOrderFilledLogMessage(10, 11, 3), createOrderAddedLogMessage(11, 0)}})
));

INSTANTIATE_TEST_SUITE_P(DeleteOrderEffectTests, OrderBookParamFixture, ::testing::Values(
    // delete an order away from the best bid (order 1 @ 250) -> a sell sweeping down to 200 no longer fills it, 5 rest @ 200
    OrderTestParam({createDeleteOrder(9, 1), createAddOrder(10, 25, 200, Side::SELL)}, {40, 0}, {60, 20}, {{createDeleteOrderDeletedLogMessage(1, 9)}, {createAddOrderFilledLogMessage(4, 10, 10), createAddOrderFilledLogMessage(5, 10, 10), createOrderAddedLogMessage(10, 5)}}),
    // delete an order away from the best ask (order 2 @ 500) -> a buy sweeping up to 550 no longer fills it, 5 rest @ 550
    OrderTestParam({createDeleteOrder(9, 2), createAddOrder(10, 25, 550, Side::BUY)}, {40, 90}, {60, 99}, {{createDeleteOrderDeletedLogMessage(2, 9)}, {createAddOrderFilledLogMessage(7, 10, 10), createAddOrderFilledLogMessage(8, 10, 10), createOrderAddedLogMessage(10, 5)}}),

    // delete the first order at the best bid (order 4 @ 300) -> best bid stays, a sell fills only order 5, 5 rest @ 300
    OrderTestParam({createDeleteOrder(9, 4), createAddOrder(10, 15, 300, Side::SELL)}, {40, 30}, {60, 40}, {{createDeleteOrderDeletedLogMessage(4, 9)}, {createAddOrderFilledLogMessage(5, 10, 10), createOrderAddedLogMessage(10, 5)}}),
    // delete the last order at the best ask (order 8 @ 400) -> best ask stays, a buy fills only order 7, 5 rest @ 400
    OrderTestParam({createDeleteOrder(9, 8), createAddOrder(10, 15, 400, Side::BUY)}, {40, 60}, {60, 80}, {{createDeleteOrderDeletedLogMessage(8, 9)}, {createAddOrderFilledLogMessage(7, 10, 10), createOrderAddedLogMessage(10, 5)}}),

    // delete the middle order of a level (orders 4, 5, 9 @ 300, delete 5) -> a sell fills 4 then 9, 5 rest @ 300
    OrderTestParam({createAddOrder(9, 10, 300, Side::BUY), createDeleteOrder(10, 5), createAddOrder(11, 25, 300, Side::SELL)}, {40, 40, 30}, {60, 60, 40}, {{createOrderAddedLogMessage(9, 10)}, {createDeleteOrderDeletedLogMessage(5, 10)}, {createAddOrderFilledLogMessage(4, 11, 10), createAddOrderFilledLogMessage(9, 11, 10), createOrderAddedLogMessage(11, 5)}}),
    // delete the middle order of a level (orders 7, 8, 9 @ 400, delete 8) -> a buy fills 7 then 9, 5 rest @ 400
    OrderTestParam({createAddOrder(9, 10, 400, Side::SELL), createDeleteOrder(10, 8), createAddOrder(11, 25, 400, Side::BUY)}, {40, 40, 60}, {60, 60, 80}, {{createOrderAddedLogMessage(9, 10)}, {createDeleteOrderDeletedLogMessage(8, 10)}, {createAddOrderFilledLogMessage(7, 11, 10), createAddOrderFilledLogMessage(9, 11, 10), createOrderAddedLogMessage(11, 5)}}),

    // delete all orders at the best bid (orders 4, 5 @ 300) -> best bid moves 40 -> 30
    OrderTestParam({createDeleteOrder(9, 4), createDeleteOrder(10, 5)}, {40, 30}, {60, 60}, {{createDeleteOrderDeletedLogMessage(4, 9)}, {createDeleteOrderDeletedLogMessage(5, 10)}}),
    // delete all orders at the best ask (orders 7, 8 @ 400) -> best ask moves 60 -> 80
    OrderTestParam({createDeleteOrder(9, 7), createDeleteOrder(10, 8)}, {40, 40}, {60, 80}, {{createDeleteOrderDeletedLogMessage(7, 9)}, {createDeleteOrderDeletedLogMessage(8, 10)}})
));

INSTANTIATE_TEST_SUITE_P(OrderActivityTests, OrderBookParamFixture, ::testing::Values(
    // modifying a deleted order -> validation fails
    OrderTestParam({createDeleteOrder(9, 1), createModifyOrder(10, 1, 10, 275, Side::BUY)}, {40, 40}, {60, 60}, {{createDeleteOrderDeletedLogMessage(1, 9)}, {createValidationFailedLogMessage(10, MessageType::MODIFY_ORDER)}}),
    // deleting a modified order (price change: order 1 250 -> 275 becomes order 9) -> delete by the new id works, by the old id fails
    OrderTestParam({createModifyOrder(9, 1, 10, 275, Side::BUY), createDeleteOrder(10, 9), createDeleteOrder(11, 1)}, {40, 40, 40}, {60, 60, 60}, {{createModifyOrderDeletedLogMessage(1, 9)}, {createDeleteOrderDeletedLogMessage(9, 10)}, {createValidationFailedLogMessage(11, MessageType::DELETE_ORDER)}}),
    // deleting a modified order (quantity decrease in place: order 4 10 -> 8 becomes order 9) -> delete by the new id works, by the old id fails, a sell then fills only order 5
    OrderTestParam({createModifyOrder(9, 4, 8, 300, Side::BUY), createDeleteOrder(10, 9), createDeleteOrder(11, 4), createAddOrder(12, 10, 300, Side::SELL)}, {40, 40, 40, 30}, {60, 60, 60, 60}, {{createModifiedOrderLogMessage(4, 9)}, {createDeleteOrderDeletedLogMessage(9, 10)}, {createValidationFailedLogMessage(11, MessageType::DELETE_ORDER)}, {createAddOrderFilledLogMessage(5, 12, 10), createOrderAddedLogMessage(12, 0)}})
));
