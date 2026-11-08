#pragma once

#include "NSpsc.h"
#include <sys/types.h>
#include <atomic>
#include <thread>
#include "InstrumentationEvent.h"
#include "Message.h"

// not a template: the queue sizes are compile-time constants shared by every component, so a template parameter gains nothing
class OrderController {
public:
    OrderController(int port, NSpsc<Message>& requestQueue, NSpsc<InstrumentationEvent>& instrQueue);
    
    void start();
    void stop();        // safe to call more than once; the destructor calls it
    ~OrderController();

    OrderController(const OrderController& orderController) = delete;
    OrderController(OrderController&& orderController) = delete;
    OrderController& operator=(const OrderController& orderController) = delete;
    OrderController& operator=(OrderController&& orderController) = delete;
private:
    int listenFd = -1;
    std::thread t;

    // Queues
    NSpsc<Message>& requestQueue;
    NSpsc<InstrumentationEvent>& instrQueue;

    // counters
    long reqTotalCount = 0;
    long errCountRcvFailure = 0;
    long errCountRcvLessFailure = 0;
    long errCountQueueFull = 0;
    long errCountValidationFailure = 0;
    long errCountInstrQueueFull = 0;     // metrics lost because instrQueue was full

    std::atomic_bool stopRequested = false;
    void run();
    void pushInstru(long orderId, long val);

    void handleSysCallError(ssize_t res, const char* name, const char* exception);
};