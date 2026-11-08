#pragma once

#include "NSpsc.h"
#include "Message.h"
#include <unordered_map>
#include "Tick.h"
#include <thread>
#include <atomic>


class RequestDispatcher {
public:
    RequestDispatcher(NSpsc<Message>& reqQueue, const std::unordered_map<Tick, NSpsc<Message>*>& tickMap);
    
    ~RequestDispatcher();

    void start();
    void stop();

    RequestDispatcher(const RequestDispatcher& requestDispatcher) = delete;
    RequestDispatcher(RequestDispatcher&& requestDispatcher) = delete;
    RequestDispatcher& operator=(const RequestDispatcher& requestDispatcher) = delete;
    RequestDispatcher& operator=(RequestDispatcher&& requestDispatcher) = delete;

private:
    NSpsc<Message>& reqQueue;
    const std::unordered_map<Tick, NSpsc<Message>*> tickMap;
    std::thread t;
    std::atomic_bool stopRequested = false;

    void run();
    void flush();

};