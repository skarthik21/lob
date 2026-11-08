#include "OrderBook.h"
#include <bit>
#include <algorithm>
#include <unordered_map>
#include "Utils.h"


/*
 - allocating in the first request vs compile time
 - we won't be doing the allocation in the hot path - isn't that good

*/

/*
- you are forgetting the principle of five

*/



OrderBook::OrderBook(NSpsc<Message>& messageQueue, NSpsc<LogMessage>& logQueue, Tick tick, int lowerCircuit, int tickSize, int range, int poolSize, int logMessageReserveSize, bool publishLogs): lowerCircuit(lowerCircuit), 
upperCircuit(lowerCircuit + tickSize*(range - 1)), tickSize(tickSize), logMessageReserveSize(logMessageReserveSize), messageQueue(messageQueue), logQueue(logQueue), nodes{poolSize}, book(range), buyWordHasLevels((range + 63)/64), askWordHasLevels((range + 63)/64), buyLevelBits((range + 63)/64), askLevelBits((range + 63)/64), tick(tick), publishLogs(publishLogs) {
    if (tickSize <= 0 || range <= 0 || poolSize <= 0 || logMessageReserveSize <= 0) {
        throw std::invalid_argument("tickSize, range, poolSize and logMessageReserveSize must be positive");
    }
    logMessageReserve.resize(logMessageReserveSize);
}

OrderBook::~OrderBook() {
    stop();
    for (auto [head, tail] : book) {
        while(head != nullptr) {
            auto temp = head;
            head = head->next;
            freeNode(temp);
        }
    }
}

void OrderBook::start() {
    if (t.joinable()) return;
    t = std::thread([this]() {while (!stopRequested.load(std::memory_order_relaxed)) onMessage();});
}

void OrderBook::processMessageSync() {
    if (t.joinable()) return;
    onMessage();
}

void OrderBook::onMessage() {
    Message m;
    if (messageQueue.tryPop(m) == QueueStatus::EMPTY) return;
    walkBook(m);
}

void OrderBook::stop() {
    stopRequested.store(true, std::memory_order_relaxed);
    if (t.joinable()) t.join();
    flush();
}

// this is only correct when it is invoked after stop 
int OrderBook::getBestBuyLevel() const {
    return bestBuy;
}

// this is only correct when it is invoked after stop 
int OrderBook::getBestAskLevel() const {
    return bestAsk;
}

void OrderBook::flush() {
    Message m;
    while(messageQueue.tryPop(m) != QueueStatus::EMPTY) {
        walkBook(m);
    }

    if (!publishLogs) return;
    int idx = 0;
    while(idx < logMessageReserveIdx) {
        while(logQueue.push(logMessageReserve[idx]) != QueueStatus::PUSHED) {};
        idx++;
    }
    logMessageReserveIdx = 0;
}


void OrderBook::logMsg(int orderId, int fillOrderId, int fillQuantity, MessageType msgType, LogEvent logEvent, bool forcePush) {
    logMessageReserve[logMessageReserveIdx].orderId = orderId;
    logMessageReserve[logMessageReserveIdx].fillOrderId = fillOrderId;
    logMessageReserve[logMessageReserveIdx].fillQuantity = fillQuantity;
    logMessageReserve[logMessageReserveIdx].messageType = msgType;
    logMessageReserve[logMessageReserveIdx].logEvent = logEvent;
    logMessageReserve[logMessageReserveIdx].ts = Utils::ticks();
    logMessageReserveIdx++;
    if (logMessageReserveIdx == logMessageReserveSize) {
        logMessageReserveIdx = 0;
        if (!publishLogs) return;
        do {
            if (logQueue.pushBatch(logMessageReserve) == QueueStatus::PUSHED) break;
        } while(forcePush);
    }
}

void OrderBook::walkBook(const Message& msg) {
    switch (msg.messageType)    {
        case MessageType::ADD_ORDER:
            addOrder(msg);
            break;
        case MessageType::DELETE_ORDER:
            deleteOrder(msg);
            break;
        case MessageType::MODIFY_ORDER:
            modifyOrder(msg);
            break;
        default:
            break;
    }
}


bool OrderBook::validateModifyOrder(const Message& msg, OrderIdToLevelMap::iterator it) {
    if (!msg.validateMsg()) return false;
    if (msg.tick != tick) return false;
    if (it == orderIdToLevelMap.end()) return false;
    if (orderIdToLevelMap.contains(msg.orderId)) return false;
    auto [originalOrderRef, originalOrderPrice] = it->second;
    auto originalSide = (bestBuy >= originalOrderPrice)? Side::BUY : Side::SELL;
    bool quantityChanged = ((msg.quantity > 0) && (originalOrderRef->originalQuantity != msg.quantity));
    if ((msg.side != Side::UNKNOWN) && (msg.side != originalSide)) return false;
    if ((msg.price > 0) && ( (msg.price < lowerCircuit) || (msg.price > upperCircuit) || ((msg.price - lowerCircuit) % tickSize != 0))) return false;
    if (quantityChanged && (originalOrderRef->originalQuantity > msg.quantity) && ((originalOrderRef->originalQuantity - originalOrderRef->quantity) >= msg.quantity)) return false;
    return true;
}

void OrderBook::modifyOrder(const Message& msg) {
    auto it = orderIdToLevelMap.find(msg.orderRefId);
    if (!validateModifyOrder(msg, it)) {
        logMsg(msg.orderId, -1, -1, MessageType::MODIFY_ORDER, LogEvent::VALIDATION_FAILED);
        return;
    }
    auto [originalOrderRef, originalOrderPrice] = it->second;
    auto originalSide = (bestBuy >= originalOrderPrice)? Side::BUY : Side::SELL;
    auto priceIdx = (msg.price > 0) ? ((msg.price) - lowerCircuit)/tickSize : -1;
    bool priceChanged = ((msg.price >0) && (originalOrderPrice != priceIdx));
    bool quantityChanged = ((msg.quantity > 0) && (originalOrderRef->originalQuantity != msg.quantity));

    if (!(priceChanged || quantityChanged)) {
        logMsg(msg.orderRefId, msg.orderId, -1, MessageType::MODIFY_ORDER, LogEvent::MODIFIED_FAILED_DUMMY_ORDER);
        return;
    }
    
    if (priceChanged) {
        int quantity = quantityChanged ? (msg.quantity - (originalOrderRef->originalQuantity - originalOrderRef->quantity)) : originalOrderRef->quantity;
        int originalQuantity = quantityChanged ? msg.quantity : originalOrderRef->originalQuantity;
        deleteOrderHelper(msg.orderRefId, msg.orderId, MessageType::MODIFY_ORDER, it);
        addOrderHelper(msg.orderId, priceIdx, originalQuantity, quantity, originalSide, MessageType::MODIFY_ORDER);
        return;
    }

    if (quantityChanged) {
        int quantity = (msg.quantity - (originalOrderRef->originalQuantity - originalOrderRef->quantity));
        if (msg.quantity > originalOrderRef->originalQuantity) {
            deleteOrderHelper(msg.orderRefId, msg.orderId, MessageType::MODIFY_ORDER, it);
            addOrderHelper(msg.orderId, originalOrderPrice, msg.quantity, quantity, originalSide, MessageType::MODIFY_ORDER);
        } else {
            originalOrderRef->originalQuantity = msg.quantity;
            originalOrderRef->orderId = msg.orderId;
            originalOrderRef->quantity = quantity;
            orderIdToLevelMap[msg.orderId] = {originalOrderRef, originalOrderPrice};
            orderIdToLevelMap.erase(msg.orderRefId);
            logMsg(msg.orderRefId, msg.orderId, -1, MessageType::MODIFY_ORDER, LogEvent::MODIFIED_ORDER);
        }
    }
}


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
- choice of using raw pointer vs smart for linked list and map.

*/

bool OrderBook::validateDeleteOrder(const Message& msg, OrderIdToLevelMap::iterator it) {
    if (!msg.validateMsg()) return false;
    if (msg.tick != tick) return false;
    if (it == orderIdToLevelMap.end()) return false;
    if (orderIdToLevelMap.contains(msg.orderId)) return false;
    return true;
}

void OrderBook::deleteOrderHelper(int orderRefId, int orderId, MessageType msgType, OrderIdToLevelMap::iterator it) {
    auto [originalOrderRef, originalOrderPrice] = it->second;
    auto [head, tail] = book[originalOrderPrice];
    if (originalOrderRef == tail) {
        tail = originalOrderRef->prev;
        if (tail != nullptr) tail->next = nullptr;
        if (originalOrderRef == head) head = nullptr;
    } else if (originalOrderRef == head) {
        head = originalOrderRef->next;
        head->prev = nullptr;
    } else {
        auto temp = originalOrderRef->next;
        temp->prev = originalOrderRef->prev;
        originalOrderRef->prev->next = temp;
    }
    logMsg(orderRefId, orderId, -1, msgType, LogEvent::ORDER_DELETED);
    orderIdToLevelMap.erase(orderRefId);
    freeNode(originalOrderRef);
    book[originalOrderPrice] = {head, tail};
    if (bestBuy >= originalOrderPrice) {
        updateIndexes(Side::BUY, originalOrderPrice); 
    } else {
        updateIndexes(Side::SELL, originalOrderPrice);
    }
    if (bestBuy == originalOrderPrice) {
        getNextIndex(Side::BUY);
    } else if (bestAsk == originalOrderPrice) {
        getNextIndex(Side::SELL);
    }
}

/*
 - choice for have the pair vs just level and iterating 
    - what if the head, tail are pointer references -> dangerous ( i use it and i own it and i know this exists kind of thing)
    - we would need to store the pair
- choice of having the raw pointers vs shared pointer for the list
    - we can't have unique pointer for prev and next since we are using head, tail
    - we can't have shared pointer because of space overhead and we are not getting any benfits ( we are making sure we delete the refs)
*/
void OrderBook::deleteOrder(const Message& msg) {
    auto it = orderIdToLevelMap.find(msg.orderRefId);
    if (!validateDeleteOrder(msg, it)) {
        logMsg(msg.orderId, -1, -1, MessageType::DELETE_ORDER, LogEvent::VALIDATION_FAILED);
        return;
    }
    deleteOrderHelper(msg.orderRefId, msg.orderId, MessageType::DELETE_ORDER, it);
}

/*
 - you have two options:
    - store the upperCircuit but this will cost you 4 bytes or one register
    - calculate the upperCircuit which will cost you one cycle since lowerCircuit needs to be present, tickSize, book.size 
- i think it is better to store the upperCircuit reg itself since in the other cases we need three things in regs
*/

/*
- won't it be better to move the validation above (in the controller)
*/

bool OrderBook::validateAddOrder(const Message& msg) {
    if (!msg.validateMsg()) return false;
    if (msg.tick != tick) return false;
    if ((msg.price < lowerCircuit) || (msg.price > upperCircuit) || ((msg.price - lowerCircuit) % tickSize != 0)) return false;
    if (orderIdToLevelMap.contains(msg.orderId)) return false;
    return true;
}

int OrderBook::addOrderHelper(int orderId, int orderPrice, int originalQuantity, int quantity, Side side, MessageType msgType) {
    switch (side) {
        case Side::SELL:
            while (bestBuy >= orderPrice) {
                if (walkLevel(orderId, quantity, Side::BUY, msgType, bestBuy)) break;
            }
            if (quantity == 0) break;
            insertOrder(orderId, orderPrice, originalQuantity, quantity, Side::SELL);
            if ((bestAsk == -1) || (bestAsk >= orderPrice)) {
                bestAsk = orderPrice;
            }
            break;
        case Side::BUY:
            while ((bestAsk != -1) && (bestAsk <= orderPrice)) {
                if (walkLevel(orderId, quantity, Side::SELL, msgType, bestAsk)) break;
            }
            if (quantity == 0) break;
            insertOrder(orderId, orderPrice, originalQuantity, quantity, Side::BUY);
            if (bestBuy <= orderPrice) {
                bestBuy = orderPrice;
            }
            break;
        default:
            break;
    }
    return quantity;
}

bool OrderBook::walkLevel(int orderId, int& quantity, Side side, MessageType msgType, int idx) {
    auto [head, tail] = book[idx];
    while((head != nullptr) && (quantity > 0)) {
        int fillQuantity = std::min(quantity, head->quantity);
        head->quantity -= fillQuantity;
        quantity -= fillQuantity;
        logMsg(head->orderId, orderId, fillQuantity, msgType, LogEvent::ORDER_FILLED);
        if (head->quantity == 0) {
            auto temp = head;
            if (head == tail) tail = nullptr;
            head = head->next;
            if (head != nullptr) head->prev = nullptr;
            orderIdToLevelMap.erase(temp->orderId);
            freeNode(temp);
        }
    }
    book[idx] = {head, tail};
    if (head == nullptr) {
        updateIndexes(side, idx);
        getNextIndex(side);
    }
    if (quantity == 0) return true;
    return false;

}
 


/*
- do we have to inline the helper function we are calling in this or compiler is smart enough to take care of that.
*/

/*
checkBox:
 - no hot path copies
 - no hot path allocations -> insertOrder
*/

/*
- int orderId, int orderPrice, int quantity, Side side, MessageType msgType
*/
void OrderBook::addOrder(const Message& msg) {
    if (!validateAddOrder(msg)) {
        logMsg(msg.orderId, -1, -1, MessageType::ADD_ORDER, LogEvent::VALIDATION_FAILED);
        return;
    }
    int orderIdx = (msg.price - lowerCircuit)/tickSize;
    int remaining = addOrderHelper(msg.orderId, orderIdx, msg.quantity, msg.quantity, msg.side, MessageType::ADD_ORDER);
    // logged after matching, so it is the last event of an add: 0 = fully filled, > 0 = the quantity that rested
    logMsg(msg.orderId, -1, remaining, MessageType::ADD_ORDER, LogEvent::ORDER_ADDED);
}


void OrderBook::insertOrder(int orderId, int idx, int originalQuantity, int quantity, Side side) {
    auto [head, tail] = book[idx];
    Node* node = getNode(orderId, originalQuantity, quantity);
    if (head == nullptr) {
        head = node;
        tail = node;
    } else {
        tail->next = node;
        node->prev = tail;
        tail = node;
    }
    orderIdToLevelMap[orderId] = {node, idx};
    int secondaryIndex = idx/64;
    int offset = (idx)%64;
    auto flgBuy = uint64_t{1}<<(63-offset);
    auto flgSell = uint64_t{1}<<(offset);
    if (side == Side::BUY) buyWordHasLevels[secondaryIndex] = true; else askWordHasLevels[secondaryIndex] = true;
    if (side == Side::BUY) buyLevelBits[secondaryIndex] |= flgBuy; else askLevelBits[secondaryIndex] |= flgSell;
    book[idx] = {head, tail};
}

void OrderBook::updateIndexes(Side side, int idx) {
    auto [head, tail] = book[idx];
    if (head != nullptr) return;
    int secondaryIndex = idx/64;
    int secondaryOffset = idx%64;
    switch (side) {
        case Side::BUY: {
            auto flg = ~(uint64_t{1}<<(63-secondaryOffset));
            buyLevelBits[secondaryIndex] = (buyLevelBits[secondaryIndex] & flg);
            if (buyLevelBits[secondaryIndex] == 0)  buyWordHasLevels[secondaryIndex] = false;
            break;
        }
        case Side::SELL: {
            auto flg = ~(uint64_t{1}<<(secondaryOffset));
            askLevelBits[secondaryIndex] = (askLevelBits[secondaryIndex] & flg);
            if (askLevelBits[secondaryIndex] == 0)  askWordHasLevels[secondaryIndex] = false;
            break;
        }
        default:
            break;
    }
}


void OrderBook::getNextIndex(Side side) {
    switch (side) {
        case Side::BUY: {
            int secondaryIndex = bestBuy/64;
            while (secondaryIndex >=0) { if (buyWordHasLevels[secondaryIndex]) break; else secondaryIndex--; }
            if (secondaryIndex == -1) {
                bestBuy = -1;
                break;
            }
            int offset = std::countr_zero(buyLevelBits[secondaryIndex]); 
            bestBuy = secondaryIndex*64 + (63 - offset);
            break;
        }
        case Side::SELL: {
            std::size_t secondaryIndex = bestAsk/64;
            while (secondaryIndex < askWordHasLevels.size()) { if (askWordHasLevels[secondaryIndex]) break; else secondaryIndex++; }
            if (secondaryIndex == askWordHasLevels.size()) {
                bestAsk = -1;
                break;
            }
            int offset = std::countr_zero(askLevelBits[secondaryIndex]); 
            bestAsk = secondaryIndex*64 + offset;
            break;
        }
        default:
            break;
    }
}

Node* OrderBook::getNode(int orderId, int originalQuantity, int quantity) {
    Node* node = nodes.getNode();
    node->orderId = orderId;
    node->originalQuantity = originalQuantity; // RDX
    node->quantity = quantity;
    node->next = nullptr;
    node->prev = nullptr;
    return node;
}

void OrderBook::freeNode(Node* node) {
    nodes.freeNode(node);
}