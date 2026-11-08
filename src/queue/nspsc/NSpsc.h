#pragma once

#include <concepts>
#include <string>
#include <cstddef>
#include "QueueStatus.h"
#include <atomic>
#include <utility>
#include <bit>
#include <vector>
#include "Constants.h"
#include <stdexcept>

/*
    - need to add metrics on the queue size, status metrics like full and empty.
*/
/*
    - size needs to be 2 power
    - T needs to be movable
*/

/*
 - move size out of template argument
 - reject size which not power of two in the constructor
 - make the alignment as a variable one
 - refactoring - PUSH Add headNext
 - remove line 91
*/

/*
- removing alignas for head, tail
*/

template <typename T> 
requires std::movable<T> && std::default_initializable<T> 
class NSpsc {
public:
    explicit NSpsc(std::string name, std::size_t size):  mask(size-1), size(size), name(std::move(name)) {
        if (size < 2 || !std::has_single_bit(size)) {
            throw std::invalid_argument("size should be a power of 2");
        }
        buffer = std::vector<T>(size);
    }

    [[nodiscard]] QueueStatus push(T&& t) {
        std::size_t headLoaded = head.load(std::memory_order_acquire);
        std::size_t headNext = ((headLoaded + 1) & mask);
        if (headNext == tailCached) {
            tailCached = tail.load(std::memory_order_acquire);
            if (headNext == tailCached) return QueueStatus::FULL;
        }
        buffer[headLoaded] = std::move(t);
        head.store(headNext, std::memory_order_release);
        return QueueStatus::PUSHED;
    }

    [[nodiscard]] QueueStatus push(const T& t) {
        std::size_t headLoaded = head.load(std::memory_order_acquire);
        std::size_t headNext = ((headLoaded + 1) & mask);
        if (headNext == tailCached) {
            tailCached = tail.load(std::memory_order_acquire);
            if (headNext == tailCached) return QueueStatus::FULL;
        }
        buffer[headLoaded] = t;
        head.store(headNext, std::memory_order_release);
        return QueueStatus::PUSHED;
    }

    [[nodiscard]] QueueStatus tryPop(T& res) {
        std::size_t tailLoaded = tail.load(std::memory_order_acquire);
        if (tailLoaded == headCached) {
            headCached = (head.load(std::memory_order_acquire));
            if (tailLoaded == headCached) return QueueStatus::EMPTY;
        }
        res = std::move(buffer[tailLoaded]);
        tailLoaded = (tailLoaded + 1) & mask;
        tail.store(tailLoaded, std::memory_order_release);
        return QueueStatus::POPPED;
    }

    [[nodiscard]] QueueStatus tryPopBatch(std::vector<T>& res) {
        std::size_t batchSize = res.size();
        std::size_t tailLoaded = tail.load(std::memory_order_acquire);
        if (((headCached - tailLoaded + size) & mask) < batchSize) {
            headCached = head.load(std::memory_order_acquire);
            if (((headCached - tailLoaded + size) & mask) < batchSize) return QueueStatus::EMPTY;
        }
        for (std::size_t i = 0;i<batchSize; i++) {
            res[i] = std::move(buffer[tailLoaded]);
            tailLoaded = (tailLoaded + 1) & mask;
        }
        tail.store(tailLoaded, std::memory_order_release);
        return QueueStatus::POPPED;
    }


    [[nodiscard]] QueueStatus pushBatch(std::vector<T>&& msgArray) {
        std::size_t batchSize = msgArray.size();
        auto headLoaded = head.load(std::memory_order_acquire);
        if (((headLoaded - tailCached + size) & mask) + batchSize >= size) {
            tailCached = tail.load(std::memory_order_acquire);
            if (((headLoaded - tailCached + size) & mask) + batchSize >= size) return QueueStatus::FULL;
        }
        for (std::size_t i = 0;i<batchSize; i++) {
            buffer[headLoaded] = std::move(msgArray[i]);
            headLoaded = (headLoaded + 1) & mask; 
        }
        head.store(headLoaded, std::memory_order_release);
        return QueueStatus::PUSHED;
    }


    [[nodiscard]] QueueStatus pushBatch(const std::vector<T>& msgArray) {
        std::size_t batchSize = msgArray.size();
        auto headLoaded = head.load(std::memory_order_acquire);
        if (((headLoaded - tailCached + size) & mask) + batchSize >= size) {
            tailCached = tail.load(std::memory_order_acquire);
            if (((headLoaded - tailCached + size) & mask) + batchSize >= size) return QueueStatus::FULL;
        }
        for (std::size_t i = 0;i<batchSize; i++) {
            buffer[headLoaded] = msgArray[i];
            headLoaded = (headLoaded + 1) & mask; 
        }
        head.store(headLoaded, std::memory_order_release);
        return QueueStatus::PUSHED;
    }

    std::size_t getQueueSize() const {
        return (head.load() - tail.load() + size) & (mask);
    }

    std::size_t getQueueCapacity() const {
        return mask;
    }

private:
    alignas(constants::CACHE_LINE_SIZE) std::size_t headCached = 0;
    alignas(constants::CACHE_LINE_SIZE) std::atomic<std::size_t> tail{0};
    alignas(constants::CACHE_LINE_SIZE) std::size_t tailCached = 0;
    alignas(constants::CACHE_LINE_SIZE) std::atomic<std::size_t> head{0};
    alignas(constants::CACHE_LINE_SIZE) std::size_t mask;
    alignas(constants::CACHE_LINE_SIZE) std::size_t size;
    alignas(constants::CACHE_LINE_SIZE) std::vector<T> buffer;
    std::string name;
};