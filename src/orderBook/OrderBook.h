#pragma once

#include "Tick.h"
#include "NSpsc.h"
#include "Message.h"
#include <thread>
#include "Constants.h"
#include "LogMessage.h"
#include <vector>
#include <atomic>
#include <utility>
#include <unordered_map>
#include <cstdint>
#include "Node.h"
#include "NodePool.h"

class OrderBook {
    friend class OrderBookBenchmarkAccess;
public:
    OrderBook(NSpsc<Message>& messageQueue, NSpsc<LogMessage>& logQueue, Tick tick, int lowerCircuit, int tickSize, int range, int poolSize = 1e4, int logMessageReserveSize = 50, bool publishLogs = true);
    ~OrderBook();

    OrderBook(const OrderBook& orderBook) = delete;
    OrderBook(OrderBook&& OrderBook) = delete;
    OrderBook& operator=(const OrderBook& orderBook) = delete;
    OrderBook& operator=(OrderBook&& orderBook) = delete;

    void start();
    void stop();
    void processMessageSync();
    int getBestBuyLevel() const;
    int getBestAskLevel() const;
private:
    using OrderIdToLevelMap = std::unordered_map<int, std::pair<Node*, int>>;
    int bestBuy = -1;
    int bestAsk = -1;
    int lowerCircuit;
    int upperCircuit;
    int tickSize;
    int logMessageReserveSize;
    int logMessageReserveIdx = 0;
    std::atomic_bool stopRequested = false;
    NSpsc<Message>& messageQueue;
    NSpsc<LogMessage>& logQueue;
    std::thread t;
    std::vector<LogMessage> logMessageReserve;
    OrderIdToLevelMap orderIdToLevelMap;
    NodePool<Node> nodes;
    std::vector<std::pair<Node*, Node*>> book;
    std::vector<bool> buyWordHasLevels;
    std::vector<bool> askWordHasLevels;
    std::vector<uint64_t> buyLevelBits;
    std::vector<uint64_t> askLevelBits;
    Tick tick;
    bool publishLogs;

    void flush();
    void logMsg(int orderId, int fillOrderId, int fillQuantity, MessageType msgType, LogEvent logEvent, bool forcePush = false);
    void onMessage();
    void walkBook(const Message& m);
    bool validateModifyOrder(const Message& msg, OrderIdToLevelMap::iterator);
    void modifyOrder(const Message& msg);
    bool validateDeleteOrder(const Message& msg, OrderIdToLevelMap::iterator);
    void deleteOrderHelper(int orderRefId, int orderId, MessageType msgType, OrderIdToLevelMap::iterator);
    void deleteOrder(const Message& msg);
    bool validateAddOrder(const Message& msg);
    int addOrderHelper(int orderId, int orderPrice, int originalQuantity, int quantity, Side side, MessageType msgType);  // returns the unfilled quantity
    bool walkLevel(int orderId, int& quantity, Side side, MessageType msgType, int idx);
    void addOrder(const Message& msg);
    void insertOrder(int orderId, int idx, int originalQuantity, int quantity, Side side);
    void updateIndexes(Side side, int idx);
    void getNextIndex(Side side);
    Node* getNode(int orderId, int originalQuantity, int quantity);
    void freeNode(Node* node);


};