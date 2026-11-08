#pragma once

#include <vector>
#include <stdexcept>
#include <functional>
 #include <concepts>


template<typename T>
requires std::default_initializable<T>
class NodePool {
public:
    NodePool(int size): idx(size-1), size(size) {
        if (size <=0) {
            throw std::invalid_argument("size should be positive");
        }
        poolBackend = std::vector<T>(size, T());
        pool.resize(size);
        for (int i = 0;i<size;i++) {
            pool[i] = &poolBackend[i];
        }
    }

    ~NodePool() = default;
    
    NodePool(const NodePool& nodepool) = delete;
    NodePool(NodePool&& nodepool) = delete;
    NodePool& operator=(const NodePool& nodepool) = delete;
    NodePool& operator=(NodePool&& nodepool) = delete;

    T* getNode() {
        if (idx < 0) {
            return new T();
        }
        idx--;
        return pool[idx+1];
    }

    void freeNode(T* node) {
        if (isHeapAllocated(node)) {
            delete node;
            return;
        }
        idx++;
        pool[idx] = node;
    }

private:
    std::vector<T*> pool;
    std::vector<T>  poolBackend;
    int idx;
    int size;

    bool isHeapAllocated(T* node) {
        return !(std::less_equal<T*>{}(node, &poolBackend[size-1]) && std::greater_equal<T*>{}(node, &poolBackend[0]));
    }
};