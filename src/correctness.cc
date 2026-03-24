#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include "mutex_queue.hpp"
#include "two_lock_queue.hpp"
#include "plj_queue.hpp"

template <typename Queue>
void test_dequeue_empty_returns_false() {
  std::cout << "\ttest_dequeue_empty_returns_false";

  Queue q;
  int* item;
  assert(q.dequeue(item) == false);

  std::cout << " - passed" << std::endl;
}

template <typename Queue>
void test_single_enqueue_dequeue() {
  std::cout << "\ttest_single_enqueue_dequeue";

  Queue q;
  int a = 10;
  int* item;

  q.enqueue(&a);
  assert(q.dequeue(item) == true && *item == a);
  assert(q.dequeue(item) == false);

  std::cout << " - passed" << std::endl;
}

template <typename Queue>
void test_queue_is_fifo() {
  std::cout << "\ttest_queue_is_fifo";

  Queue q;
  int N = 1000;
  std::vector<int> items;
  for (int i = 0; i < N; i++) items.push_back(i);

  for (int i = 0; i < N; i++) q.enqueue(&items[i]);

  for (int i = 0; i < N; i++) {
    int* item;
    assert(q.dequeue(item) == true);
    assert(*item == i);
  }

  std::cout << " - passed" << std::endl;
}

template <typename Queue>
void test_enqueue_after_empty() {
  std::cout << "\ttest_enqueue_after_empty";

  Queue q;
  int a = 1, b = 2, c = 3;
  int* item;

  q.enqueue(&a);
  q.enqueue(&b);
  assert(q.dequeue(item) == true && *item == a);
  assert(q.dequeue(item) == true && *item == b);
  assert(q.dequeue(item) == false);

  q.enqueue(&c);
  assert(q.dequeue(item) == true && *item == c);
  assert(q.dequeue(item) == false);

  std::cout << " - passed" << std::endl;
}

static int ITEMS_PER_PRODUCER = 100000;
template <typename Queue>
void test_n_producers_n_consumers(int n_producers, int n_consumers) {
  std::cout << "\ttest_" << n_producers << "_producers_" << n_consumers
            << "_consumers";

  Queue q;
  std::vector<int> items;
  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;
  std::vector<std::vector<int>> consumedItems(n_consumers);

  // Generate items to pass through queue
  for (int i = 0; i < n_producers * ITEMS_PER_PRODUCER; i++) {
    items.push_back(i);
  }

  // Each producer enqueues a contiguous section of items
  for (int p = 0; p < n_producers; p++) {
    int start = p * ITEMS_PER_PRODUCER;
    producers.push_back(std::thread([&, start]() {
      for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        q.enqueue(&items[start + i]);
      }
    }));
  }

  // Each consumer continuously dequeues until nullptr is received
  // Dequeued items are appened to per consumer consumedItems vector
  for (int c = 0; c < n_consumers; c++) {
    consumers.push_back(std::thread([&, c]() {
      int* item;
      while (true) {
        if (q.dequeue(item)) {
          if (item == nullptr) break;
          consumedItems[c].push_back(*item);
        }
      }
    }));
  }

  for (auto& t : producers) t.join();
  // Send termination signal to consumers after all producers are finished
  for (int c = 0; c < n_consumers; c++) {
    q.enqueue(nullptr);
  }
  for (auto& t : consumers) t.join();

  // Flatten consumedItems
  std::vector<int> allConsumedItems;
  for (auto& v : consumedItems) {
    allConsumedItems.insert(allConsumedItems.end(), v.begin(), v.end());
  }
  std::sort(allConsumedItems.begin(), allConsumedItems.end());

  assert(allConsumedItems == items);

  std::cout << " - passed" << std::endl;
}

template <typename Queue>
void run_all_tests() {
  test_dequeue_empty_returns_false<Queue>();
  test_single_enqueue_dequeue<Queue>();
  test_queue_is_fifo<Queue>();
  test_enqueue_after_empty<Queue>();
  test_n_producers_n_consumers<Queue>(1, 1);
  test_n_producers_n_consumers<Queue>(10, 1);
  test_n_producers_n_consumers<Queue>(1, 10);
  test_n_producers_n_consumers<Queue>(10, 10);
}

int main() {
  std::cout << "MutexQueue" << std::endl;
  run_all_tests<MutexQueue<int>>();

  std::cout << "TwoLockQueue" << std::endl;
  run_all_tests<TwoLockQueue<int>>();

  std::cout << "PLJQueue" << std::endl;
  run_all_tests<PLJQueue<int>>();

  return 0;
}