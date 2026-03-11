#pragma once
#include <mutex>

template <typename T>
class MutexQueue {
 private:
  struct Node {
    T* item;
    Node* next;
    Node(T* i) : item(i), next(nullptr) {}
  };

  Node* head_ = nullptr;
  Node* tail_ = nullptr;
  std::mutex mutex_;

 public:
  MutexQueue() : head_(nullptr), tail_(nullptr) {}
  MutexQueue(const MutexQueue&) = delete;
  MutexQueue& operator=(const MutexQueue&) = delete;
  ~MutexQueue() {
    while (head_ != nullptr) {
      Node* tmp = head_;
      head_ = head_->next;
      delete tmp;
    }
    tail_ = nullptr;
  }

  /**
   * Enqueue pointer to some item
   * Block/retry until successful
   */
  void enqueue(T* item) {
    Node* node = new Node(item);

    mutex_.lock();

    if (head_ == nullptr) {
      head_ = node;
      tail_ = node;
    } else {
      tail_->next = node;
      tail_ = node;
    }

    mutex_.unlock();
  }

  /**
   * Attempt to dequeue an item from the queue
   * Return true if successful, false if empty
   */
  bool dequeue(T*& item) {
    mutex_.lock();

    if (head_ == nullptr) {
      mutex_.unlock();
      return false;
    }

    Node* tmp = head_;
    head_ = head_->next;
    if (head_ == nullptr) tail_ = nullptr;

    mutex_.unlock();

    item = tmp->item;
    delete tmp;
    return true;
  }
};
