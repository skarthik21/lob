#include "RequestDispatcher.h"

RequestDispatcher::RequestDispatcher(NSpsc<Message>& reqQueue, const std::unordered_map<Tick, NSpsc<Message>*>& tickMap):reqQueue(reqQueue), tickMap(tickMap){}

RequestDispatcher::~RequestDispatcher() { stop(); }

// it can only be started once
void RequestDispatcher::start() {
    if (t.joinable()) return;
    t = std::thread(&RequestDispatcher::run, this);
}

void RequestDispatcher::stop() {
    stopRequested.store(true, std::memory_order_relaxed);
    if (t.joinable()) t.join();
    flush();
}

// refactoring can be done

void RequestDispatcher::run() {
    while (!stopRequested.load(std::memory_order_relaxed)) {
        Message m;
        if (reqQueue.tryPop(m) == QueueStatus::EMPTY) continue;
        auto it = tickMap.find(m.tick);
        if (it == tickMap.end()) continue;
        if (it->second->push(m) == QueueStatus::FULL) continue;
    }
}

void RequestDispatcher::flush() {
    Message m;
    while (reqQueue.tryPop(m) != QueueStatus::EMPTY) {
        auto it = tickMap.find(m.tick);
        if (it == tickMap.end()) continue;
        if (it->second->push(m) == QueueStatus::FULL) continue;
    }
}