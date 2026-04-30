#pragma once
#include <atomic>

template <typename T>
class MSQueue {
private:
    struct Node;

    // structure pointer_t {ptr: pointer to node_t, count: unsigned integer}
    struct pointer_t {
        Node* ptr;
        unsigned int count;

        // Equality operator required to verify if the atomic state has changed
        bool operator==(const pointer_t& other) const {
            return ptr == other.ptr && count == other.count;
        }
    };

    // structure node_t {value: data type, next: pointer_t}
    struct Node {
        T* value;
        std::atomic<pointer_t> next;

        Node() : value(nullptr) {
            next.store({nullptr, 0});
        }
    };

    // structure queue_t {Head: pointer_t, Tail: pointer_t}
    std::atomic<pointer_t> Head;
    std::atomic<pointer_t> Tail;

public:
    // initialize(Q: pointer to queue_t)
    MSQueue() {
        // Allocate a free node
        Node* node = new Node();
        // Make it the only node in the linked list
        // Both Head and Tail point to it
        pointer_t initial_ptr = {node, 0};
        Head.store(initial_ptr);
        Tail.store(initial_ptr);
    }

    ~MSQueue() {
        // Basic cleanup to prevent memory leaks during testing
        T* dummy;
        while (dequeue(dummy)) {}
        pointer_t head = Head.load();
        delete head.ptr;
    }

    // enqueue(Q: pointer to queue_t, value: data type)
    void enqueue(T* value) {
        // E1: Allocate a new node from the free list
        Node* node = new Node();
        // E2: Copy enqueued value into node
        node->value = value;
        // E3: Set next pointer of node to NULL
        node->next.store({nullptr, 0});

        pointer_t tail, next;
        
        // E4: loop
        while (true) {
            // E5: Read Tail.ptr and Tail.count together
            tail = Tail.load();
            // E6: Read next ptr and count fields together
            next = tail.ptr->next.load();

            // E7: Are tail and next consistent?
            if (tail == Tail.load()) {
                // E8: Was Tail pointing to the last node?
                if (next.ptr == nullptr) {
                    // E9: Try to link node at the end of the linked list
                    pointer_t new_next = {node, next.count + 1};
                    if (tail.ptr->next.compare_exchange_strong(next, new_next)) {
                        // E10: break (Enqueue is done. Exit loop)
                        break;
                    }
                } else {
                    // E12: else (Tail was not pointing to the last node)
                    // E13: Try to swing Tail to the next node
                    pointer_t new_tail = {next.ptr, tail.count + 1};
                    Tail.compare_exchange_strong(tail, new_tail);
                }
            }
        }
        // E17: Enqueue is done. Try to swing Tail to the inserted node
        pointer_t final_tail = {node, tail.count + 1};
        Tail.compare_exchange_strong(tail, final_tail);
    }

    // dequeue(Q: pointer to queue_t, pvalue: pointer to data type): boolean
    bool dequeue(T*& pvalue) {
        pointer_t head, tail, next;

        // D1: loop
        while (true) {
            // D2: Read Head
            head = Head.load();
            // D3: Read Tail
            tail = Tail.load();
            // D4: Read Head.ptr->next
            next = head.ptr->next.load();

            // D5: Are head, tail, and next consistent?
            if (head == Head.load()) {
                // D6: Is queue empty or Tail falling behind?
                if (head.ptr == tail.ptr) {
                    // D7: Is queue empty?
                    if (next.ptr == nullptr) {
                        // D8: return FALSE (Queue is empty, couldn't dequeue)
                        return false;
                    }
                    // D10: Tail is falling behind. Try to advance it
                    pointer_t new_tail = {next.ptr, tail.count + 1};
                    Tail.compare_exchange_strong(tail, new_tail);
                } else {
                    // D11: else (No need to deal with Tail)
                    // D12: Read value before CAS, otherwise another dequeue might free the next node
                    pvalue = next.ptr->value;
                    
                    // D13: Try to swing Head to the next node
                    pointer_t new_head = {next.ptr, head.count + 1};
                    if (Head.compare_exchange_strong(head, new_head)) {
                        // D14: break (Dequeue is done. Exit loop)
                        break;
                    }
                }
            }
        }
        // D19: The original algorithm frees head.ptr here, but a concurrent
        // enqueue or dequeue may still be reading head.ptr->next. Leak for now.
        // Similar issue as in PLJ queue
        // delete head.ptr;
       
        // D20: return TRUE (Queue was not empty, dequeue succeeded)
        return true;
    }
};