#pragma once

struct Node {
    int orderId;
    int quantity;
    int originalQuantity;
    Node* next = nullptr;
    Node* prev = nullptr;
    Node():orderId(-1), quantity(-1), originalQuantity(-1) {}
};