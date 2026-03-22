#pragma once
#include <atomic>
#include <mutex>

template <typename T>
class TwoLockQueue
{
private:
    struct Node
    {
        T *item;
        std::atomic<Node *> next;
        Node(T *i) : item(i), next(nullptr) {}
    };

    // alignas(64) separates head-side and tail-side onto different cache lines.
    // Hence no unnecessary invalidation of tail on updating head and of head on updating tail.
    alignas(64) Node *head_;
    std::mutex head_lock_;

    alignas(64) Node *tail_;
    std::mutex tail_lock_;

public:
    TwoLockQueue()
    {
        Node *dummy = new Node(nullptr);
        head_ = dummy;
        tail_ = dummy;
    }

    TwoLockQueue(const TwoLockQueue &) = delete;
    TwoLockQueue &operator=(const TwoLockQueue &) = delete;

    ~TwoLockQueue()
    {
        Node *curr = head_;
        while (curr)
        {
            Node *next = curr->next.load();
            delete curr;
            curr = next;
        }
    }

    void enqueue(T *item)
    {
        Node *node = new Node(item);

        tail_lock_.lock();
        tail_->next.store(node);
        tail_ = node;
        tail_lock_.unlock();
    }

    bool dequeue(T *&item)
    {
        head_lock_.lock();

        Node *dummy = head_;
        Node *new_head = dummy->next.load();

        if (new_head == nullptr)
        {
            head_lock_.unlock();
            return false;
        }

        item = new_head->item;
        head_ = new_head;

        head_lock_.unlock();

        // Safe to free outside the lock, because once head_ advances past the old
        // dummy, no other thread can reach it. Enqueue never touches head_,
        // and any subsequent Dequeue reads the new dummy's next, not this node.
        delete dummy;
        return true;
    }
};