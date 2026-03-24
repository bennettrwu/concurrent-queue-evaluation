#pragma once
#include <atomic>
#include <cstdint>

// Nonblocking shared queue based on:
//   S. Prakash, Y. H. Lee, T. Johnson,
//   "A Nonblocking Algorithm for Shared Queues Using Compare-and-Swap",
//   IEEE Transactions on Computers, vol. 43, no. 5, pp. 548-559, 1994.
//
// Key invariants (from the paper):
//   - The queue is a singly-linked list with a dummy head node (sentinel).
//   - head_ always points to the dummy; the first real item is head_->next.
//   - tail_ is a tagged pointer encoded in a single uintptr_t word:
//       bit 0 == 0  ->  STABLE   (no enqueue in progress)
//       bit 0 == 1  ->  UNSTABLE (new node linked but tail_ not yet swung)
//     A single native word avoids 16-byte atomics.
//   - Dequeue never touches tail_; enqueue never touches head_.


// uintptr_t used to represent a Node*, this allows us to do bitwise ops to store state in least significant bits

template <typename T>
class PLJQueue
{
private:
    struct Node
    {
        T                   *item; // Pointer to data
        std::atomic<Node *>  next; // Pointer to next item
        Node                *free_next; // free-list link (not atomic)
        Node(T *i = nullptr) : item(i), next(nullptr), free_next(nullptr) {}
    };

    // Node* is always at least 4-byte aligned (0x04, 0x08, 0x3C), so last bit is free and can be used as a tag
    static constexpr uintptr_t TAG_BIT = 1u;

    // Cast Node* to a uintptr_t to store its state
    static uintptr_t to_raw(Node *p, bool unstable) noexcept {
        return reinterpret_cast<uintptr_t>(p) | (unstable ? TAG_BIT : 0u);
    }
    // Remove tag bit to get Node* to access node
    static Node *get_ptr(uintptr_t raw) noexcept {
        return reinterpret_cast<Node *>(raw & ~TAG_BIT);
    }
    // Check if current node is stable or not
    static bool is_unstable(uintptr_t raw) noexcept {
        return (raw & TAG_BIT) != 0u;
    }

    // alignas(64) keeps head_ and tail_ on separate cache lines.
    alignas(64) std::atomic<Node *>     head_;
    alignas(64) std::atomic<uintptr_t>  tail_;

    // Free-list: retired dummy nodes accumulate here and are deleted in ~PLJQueue.
    std::atomic<Node *> free_list_;

    void retire(Node *node) noexcept
    {
        // Push node onto the free-list with a CAS loop.
        Node *old_head = free_list_.load(std::memory_order_relaxed);
        do {
            node->free_next = old_head;
        } while (!free_list_.compare_exchange_weak(old_head, node, std::memory_order_release, std::memory_order_relaxed));
    }

    // If tail_ is UNSTABLE, finish the pending tail swing before proceeding.
    void help_finish_enqueue() noexcept
    {
        uintptr_t t = tail_.load(std::memory_order_acquire);
        if (is_unstable(t))
        {
            Node *tail_node = get_ptr(t);
            Node *next      = tail_node->next.load(std::memory_order_acquire);
            if (next != nullptr) {
                uintptr_t desired = to_raw(next, /*unstable=*/false);
                tail_.compare_exchange_strong(t, desired, std::memory_order_release, std::memory_order_relaxed);
            }
        }
    }

public:
    PLJQueue() : free_list_(nullptr) {
        Node *dummy = new Node();
        head_.store(dummy,                std::memory_order_relaxed);
        tail_.store(to_raw(dummy, false), std::memory_order_relaxed);
    }

    PLJQueue(const PLJQueue &) = delete;
    PLJQueue &operator=(const PLJQueue &) = delete;

    ~PLJQueue()
    {
        // Drain the active list.
        Node *curr = head_.load(std::memory_order_relaxed);
        while (curr) {
            Node *next = curr->next.load(std::memory_order_relaxed);
            delete curr;
            curr = next;
        }
        // Drain the free-list of retired dummy nodes.
        Node *f = free_list_.load(std::memory_order_relaxed);
        while (f) {
            Node *next = f->free_next;
            delete f;
            f = next;
        }
    }

    // Enqueue (Figure 4 in the paper).
    void enqueue(T *item)
    {
        while (true)
        {
            help_finish_enqueue();

            uintptr_t t         = tail_.load(std::memory_order_acquire);
            Node     *tail_node = get_ptr(t);

            // Step 1: Link a new node after the current tail node.
            Node *node          = new Node(item);
            Node *expected_next = nullptr;
            if (!tail_node->next.compare_exchange_strong(expected_next, node, std::memory_order_release, std::memory_order_relaxed)) {
                delete node;  // lost the race; another node was linked first
                continue;
            }

            // Step 2: Mark UNSTABLE.
            uintptr_t unstable = to_raw(tail_node, /*unstable=*/true);
            tail_.compare_exchange_strong(t, unstable, std::memory_order_release, std::memory_order_relaxed);

            // Step 3: Swing tail_ to the new node and mark STABLE.
            uintptr_t stable = to_raw(node, /*unstable=*/false);
            tail_.compare_exchange_strong(unstable, stable, std::memory_order_release, std::memory_order_relaxed);
            return;
        }
    }

    bool dequeue(T *&item)
    {
        while (true)
        {
            help_finish_enqueue();

            Node *h    = head_.load(std::memory_order_acquire);
            Node *next = h->next.load(std::memory_order_acquire);

            if (next == nullptr) return false;  // queue is empty

            uintptr_t t = tail_.load(std::memory_order_acquire);
            if (h == get_ptr(t)) continue;  // tail_ is still lagging; help and retry

            item = next->item;

            // Attempt to swing head_ from old dummy (h) to next (new dummy).
            if (head_.compare_exchange_strong(h, next, std::memory_order_release, std::memory_order_relaxed)) {
                retire(h);
                return true;
            }
            // CAS failed: Loop and reload everything from scratch.
        }
    }
};