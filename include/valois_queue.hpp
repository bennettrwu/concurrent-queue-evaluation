#pragma once
#include <atomic>

template <typename T>
class ValoisQueue {
 private:
  struct Node {
    T* item;
    std::atomic<Node*> next;
    std::atomic<Node*> retired_next;

    explicit Node(T* value) : item(value), next(nullptr), retired_next(nullptr) {}
  };

  alignas(64) std::atomic<Node*> head_;
  alignas(64) std::atomic<Node*> tail_;
  std::atomic<Node*> retired_;

  void retire(Node* node) {
    Node* old_retired = retired_.load(std::memory_order_relaxed);
    do {
      node->retired_next.store(old_retired, std::memory_order_relaxed);
    } while (!retired_.compare_exchange_weak(old_retired, node,
                                             std::memory_order_release,
                                             std::memory_order_relaxed));
  }

  static void delete_chain(Node* curr) {
    while (curr != nullptr) {
      Node* next = curr->next.load(std::memory_order_relaxed);
      delete curr;
      curr = next;
    }
  }

  static void delete_retired(Node* curr) {
    while (curr != nullptr) {
      Node* next = curr->retired_next.load(std::memory_order_relaxed);
      delete curr;
      curr = next;
    }
  }

 public:
  ValoisQueue() {
    Node* dummy = new Node(nullptr);
    head_.store(dummy, std::memory_order_relaxed);
    tail_.store(dummy, std::memory_order_relaxed);
    retired_.store(nullptr, std::memory_order_relaxed);
  }

  ValoisQueue(const ValoisQueue&) = delete;
  ValoisQueue& operator=(const ValoisQueue&) = delete;

  ~ValoisQueue() {
    delete_chain(head_.load(std::memory_order_relaxed));
    delete_retired(retired_.load(std::memory_order_relaxed));
  }

  void enqueue(T* item) {
    Node* node = new Node(item);

    while (true) {
      Node* tail = tail_.load(std::memory_order_acquire);
      Node* next = tail->next.load(std::memory_order_acquire);

      if (tail != tail_.load(std::memory_order_acquire)) continue;

      if (next == nullptr) {
        if (tail->next.compare_exchange_strong(next, node,
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
          tail_.compare_exchange_strong(tail, node, std::memory_order_release,
                                        std::memory_order_relaxed);
          return;
        }
      } else {
        // Help complete an enqueue whose node is linked but not yet the tail.
        tail_.compare_exchange_strong(tail, next, std::memory_order_release,
                                      std::memory_order_relaxed);
      }
    }
  }

  bool dequeue(T*& item) {
    while (true) {
      Node* head = head_.load(std::memory_order_acquire);
      Node* tail = tail_.load(std::memory_order_acquire);
      Node* next = head->next.load(std::memory_order_acquire);

      if (head != head_.load(std::memory_order_acquire)) continue;

      if (next == nullptr) {
        return false;
      }

      if (head == tail) {
        tail_.compare_exchange_strong(tail, next, std::memory_order_release,
                                      std::memory_order_relaxed);
        continue;
      }

      item = next->item;
      if (head_.compare_exchange_strong(head, next, std::memory_order_release,
                                        std::memory_order_relaxed)) {
        // Do not delete `head` yet: another thread may have already loaded it.
        // Reclaim retired dummy nodes only when the queue is destroyed.
        retire(head);
        return true;
      }
    }
  }
};
