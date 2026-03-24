#include <barrier>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "mutex_queue.hpp"
#include "two_lock_queue.hpp"

struct TestItem {
  std::chrono::steady_clock::time_point start_enqueue_time;
  std::chrono::steady_clock::time_point done_enqueue_time;
  std::chrono::steady_clock::time_point start_dequeue_time;
  std::chrono::steady_clock::time_point done_dequeue_time;
};

std::vector<TestItem*> generate_test_items(unsigned long long count) {
  std::vector<TestItem*> items(count);
  for (unsigned long long i = 0; i < count; i++) {
    items[i] = new TestItem();
  }
  return items;
}

void cleanup_test_items(std::vector<TestItem*>& items) {
  for (unsigned long long i = 0; i < items.size(); i++) {
    delete items[i];
    items[i] = nullptr;
  }
}

template <typename Queue>
void execute_test(std::vector<TestItem*>& items,
                  unsigned long long items_per_producers, int n_producers,
                  int n_consumers) {
  Queue q;
  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;
  std::barrier start_test(n_producers + n_consumers);

  // Each producer enqueues a contiguous section of items
  for (int p = 0; p < n_producers; p++) {
    int start = p * items_per_producers;
    producers.push_back(std::thread([&, start]() {
      start_test.arrive_and_wait();
      for (unsigned long long i = 0; i < items_per_producers; i++) {
        items[start + i]->start_enqueue_time = std::chrono::steady_clock::now();
        q.enqueue(items[start + i]);
        items[start + i]->done_enqueue_time = std::chrono::steady_clock::now();
      }
    }));
  }

  // Each consumer continuously dequeues until nullptr is received
  // Dequeued items are appened to per consumer consumedItems vector
  for (int c = 0; c < n_consumers; c++) {
    consumers.push_back(std::thread([&, c]() {
      start_test.arrive_and_wait();

      TestItem* item;
      while (true) {
        std::chrono::steady_clock::time_point start_dequeue_time =
            std::chrono::steady_clock::now();
        if (q.dequeue(item)) {
          if (item == nullptr) break;

          item->done_dequeue_time = std::chrono::steady_clock::now();
          item->start_dequeue_time = start_dequeue_time;
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
}

void test_summary(std::vector<TestItem*>& items) {
  long long total_latency_ns = 0;
  long long total_enqueue_ns = 0;
  long long total_dequeue_ns = 0;
  std::chrono::steady_clock::time_point global_start =
      items[0]->start_enqueue_time;
  std::chrono::steady_clock::time_point global_end =
      items[0]->done_dequeue_time;

  for (auto* item : items) {
    total_latency_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                            item->done_dequeue_time - item->start_enqueue_time)
                            .count();
    total_enqueue_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                            item->done_enqueue_time - item->start_enqueue_time)
                            .count();
    total_dequeue_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                            item->done_dequeue_time - item->start_dequeue_time)
                            .count();
    if (item->start_enqueue_time < global_start)
      global_start = item->start_enqueue_time;
    if (item->done_dequeue_time > global_end)
      global_end = item->done_dequeue_time;
  }

  double count = static_cast<double>(items.size());

  long long total_time_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(global_end -
                                                           global_start)
          .count();
  double wall_s = static_cast<double>(total_time_ns) / 1e9;

  std::cout << "    avg latency:      " << total_latency_ns / count << " ns\n";
  std::cout << "    avg enqueue time: " << total_enqueue_ns / count << " ns\n";
  std::cout << "    avg dequeue time: " << total_dequeue_ns / count << " ns\n";
  std::cout << "    throughput:       " << count / wall_s / 1e6 << " items/s\n";
}

void export_results(std::vector<TestItem*>& items,
                    const std::string& output_file) {
  std::ofstream f(output_file);
  f << "id,latency_ns,enqueue_delay_ns,dequeue_delay_ns\n";
  for (unsigned long long i = 0; i < items.size(); i++) {
    auto latency =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            items[i]->done_dequeue_time - items[i]->start_enqueue_time)
            .count();
    auto enqueue_delay =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            items[i]->done_enqueue_time - items[i]->start_enqueue_time)
            .count();
    auto dequeue_delay =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            items[i]->done_dequeue_time - items[i]->start_dequeue_time)
            .count();
    f << i << "," << latency << "," << enqueue_delay << "," << dequeue_delay
      << "\n";
  }
}

template <typename Queue>
void run_performance_test(unsigned long long items_per_producer,
                          int n_producers, int n_consumers,
                          const std::string& output_file) {
  unsigned long long count = items_per_producer * n_producers;
  std::cout << "  Test config:\n";
  std::cout << "    Item Count:     " << count << "\n";
  std::cout << "    Num Producers:  " << n_producers << "\n";
  std::cout << "    Num Consumers:  " << n_consumers << "\n";

  std::cout << "  Generating test items" << std::endl;
  std::vector<TestItem*> items = generate_test_items(count);

  std::cout << "  Running test..." << std::endl;
  execute_test<Queue>(items, items_per_producer, n_producers, n_consumers);

  std::cout << "  Test summary:" << std::endl;
  test_summary(items);

  if (!output_file.empty()) {
    std::cout << "  Exporting results to " << output_file << std::endl;
    export_results(items, output_file);
  }

  std::cout << "  Cleaning up test" << std::endl << std::endl;
  cleanup_test_items(items);
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "Usage: " << argv[0]
              << " <queue_type> <n_producers> <n_consumers>"
                 " <items_per_producer> [output_file]\n"
              << "  queue_type: MutexQueue | TwoLockQueue\n";
    return 1;
  }

  std::string queue_type = argv[1];
  int n_producers = std::stoi(argv[2]);
  int n_consumers = std::stoi(argv[3]);
  unsigned long long items_per_producer = std::stoull(argv[4]);
  std::string output_file = (argc >= 6) ? argv[5] : "";

  std::cout << queue_type << "\n";
  if (queue_type == "MutexQueue") {
    run_performance_test<MutexQueue<TestItem>>(items_per_producer, n_producers,
                                               n_consumers, output_file);
  } else if (queue_type == "TwoLockQueue") {
    run_performance_test<TwoLockQueue<TestItem>>(
        items_per_producer, n_producers, n_consumers, output_file);
  } else {
    std::cerr << "Unknown queue type: " << queue_type << "\n";
    return 1;
  }

  return 0;
}