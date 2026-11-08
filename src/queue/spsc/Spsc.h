#pragma once

#include <concepts>
#include <string>
#include <cstddef>
#include "QueueStatus.h"
#include <atomic>
#include <array>
#include <utility>
#include <bit>
#include <vector>

/*
    - need to add metrics on the queue size, status metrics like full and empty.
*/
/*
    - size needs to be 2 power
    - T needs to be movable
*/

template <std::size_t size>
concept powOfTwo = std::has_single_bit(size);



template <typename T, std::size_t size> 
requires powOfTwo<size> && std::movable<T> && std::default_initializable<T> 
class Spsc {
public:
    explicit Spsc(std::string name): name(std::move(name)) {};

    QueueStatus push(T&& t) {
        if (currSize.load(std::memory_order_acquire) == size) return QueueStatus::FULL;
        buffer[head] = std::move(t);
        head = (head + 1) & (size - 1);
        currSize.fetch_add(1, std::memory_order_release);
        return QueueStatus::PUSHED;
    }

    QueueStatus push(const T& t) {
        if (currSize.load(std::memory_order_acquire) == size) return QueueStatus::FULL;
        buffer[head] = t;
        head = (head + 1) & (size - 1);
        currSize.fetch_add(1, std::memory_order_release);
        return QueueStatus::PUSHED;
    }

    QueueStatus tryPop(T& res) {
        if (currSize.load(std::memory_order_acquire) == 0) return QueueStatus::EMPTY;
        res = std::move(buffer[tail]);
        tail = (tail + 1) & (size - 1);
        currSize.fetch_sub(1, std::memory_order_release);
        return QueueStatus::POPPED;
    }

    QueueStatus tryPopBatch(std::vector<T>& res) {
        std::size_t batchSize = res.size();
        if (currSize.load(std::memory_order_acquire) < batchSize) return QueueStatus::EMPTY;
        int tempTail = tail;
        for (std::size_t i = 0;i<batchSize; i++) {
            res[i] = std::move(buffer[tempTail]);
            tempTail = (tempTail + 1) & (size - 1);
        }
        tail = tempTail;
        currSize.fetch_sub(batchSize, std::memory_order_release);
        return QueueStatus::POPPED;
    }


    QueueStatus pushBatch(std::vector<T>&& msgArray) {
        std::size_t batchSize = msgArray.size();
        if ((size < batchSize) || currSize.load(std::memory_order_acquire) > (size - batchSize)) return QueueStatus::FULL;
        int tempHead = head;
        for (std::size_t i = 0;i<batchSize; i++) {
            buffer[tempHead] = std::move(msgArray[i]);
            tempHead = (tempHead + 1) & (size - 1); 
        }
        head = tempHead;
        currSize.fetch_add(batchSize, std::memory_order_release);
        return QueueStatus::PUSHED;
    }


    QueueStatus pushBatch(const std::vector<T>& msgArray) {
        std::size_t batchSize = msgArray.size();
        if ((size < batchSize) || currSize.load(std::memory_order_acquire) > (size - batchSize)) return QueueStatus::FULL;
        int tempHead = head;
        for (std::size_t i = 0;i<batchSize; i++) {
            buffer[tempHead] = msgArray[i];
            tempHead = (tempHead + 1) & (size - 1); 
        }
        head = tempHead;
        currSize.fetch_add(batchSize, std::memory_order_release);
        return QueueStatus::PUSHED;
    }

    std::size_t getQueueSize() {
        return currSize.load();
    }

private:
    alignas(64) std::size_t head = 0;
    alignas(64) std::size_t tail = 0;
    alignas(64) std::atomic<std::size_t> currSize{0};
    std::array<T, size> buffer;
    std::string name;
};