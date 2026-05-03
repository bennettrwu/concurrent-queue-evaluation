#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "lcr_queue.hpp"
#include "lpr_queue.hpp"
#include "ms_queue.hpp"
#include "mutex_queue.hpp"
#include "plj_queue.hpp"
#include "two_lock_queue.hpp"
#include "valois_queue.hpp"
#include "work.hpp"

struct alignas(64) TestItem {
  std::chrono::steady_clock::time_point enqueue_start;
  std::chrono::steady_clock::time_point enqueue_done;
  std::chrono::steady_clock::time_point dequeue_start;
  std::chrono::steady_clock::time_point dequeue_done;
  bool should_sample;
};

static void pin_to_cpu(int cpu_id) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu_id, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

template <typename Queue>
void execute_test(std::vector<TestItem>& items,
                  unsigned long long ops_per_thread, int n_threads, int n_cpus,
                  uint64_t work_iters,
                  std::chrono::steady_clock::time_point& wall_start,
                  std::chrono::steady_clock::time_point& wall_end) {
  Queue q;
  std::vector<std::thread> workers;
  std::vector<std::chrono::steady_clock::time_point> starts(n_threads);
  std::vector<std::chrono::steady_clock::time_point> ends(n_threads);
  std::barrier start_test(n_threads);

  // Each worker enqueues a contiguous section of items.
  for (int t = 0; t < n_threads; t++) {
    unsigned long long base =
        static_cast<unsigned long long>(t) * ops_per_thread;
    workers.push_back(std::thread([&, t, base]() {
      pin_to_cpu(t % n_cpus);
      uint64_t rng = (static_cast<uint64_t>(t) + 1) * 0x9E3779B97F4A7C15ULL;

      start_test.arrive_and_wait();
      starts[t] = std::chrono::steady_clock::now();

      for (unsigned long long i = 0; i < ops_per_thread; i++) {
        TestItem* my_item = &items[base + i];
        if (my_item->should_sample) {
          my_item->enqueue_start = std::chrono::steady_clock::now();
          q.enqueue(my_item);
          my_item->enqueue_done = std::chrono::steady_clock::now();
        } else {
          q.enqueue(my_item);
        }

        rng = do_work(rng, work_iters);

        TestItem* got = nullptr;
        while (true) {
          std::chrono::time_point t0 = std::chrono::steady_clock::now();
          if (q.dequeue(got)) {
            if (got->should_sample) {
              got->dequeue_start = t0;
              got->dequeue_done = std::chrono::steady_clock::now();
            }
            break;
          }
        }

        rng = do_work(rng, work_iters);
      }

      ends[t] = std::chrono::steady_clock::now();
      // Prevent the compiler from optimizing do_work away.
      if (rng == 0) std::cerr << "";
    }));
  }

  for (auto& t : workers) t.join();

  wall_start = *std::min_element(starts.begin(), starts.end());
  wall_end = *std::max_element(ends.begin(), ends.end());
}

void test_summary(const std::vector<TestItem>& items,
                  std::chrono::steady_clock::time_point wall_start,
                  std::chrono::steady_clock::time_point wall_end,
                  unsigned long long ops_per_thread, int n_threads) {
  long long total_enqueue_ns = 0;
  long long total_dequeue_ns = 0;
  long long total_latency_ns = 0;
  unsigned long long sampled = 0;
  for (const auto& item : items) {
    if (!item.should_sample) continue;
    sampled++;
    total_enqueue_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                            item.enqueue_done - item.enqueue_start)
                            .count();
    total_dequeue_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                            item.dequeue_done - item.dequeue_start)
                            .count();
    total_latency_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                            item.dequeue_done - item.enqueue_start)
                            .count();
  }

  unsigned long long total_pairs =
      ops_per_thread * static_cast<unsigned long long>(n_threads);
  double pairs = static_cast<double>(total_pairs);
  double ops = 2.0 * pairs;
  double sampled_d = sampled > 0 ? static_cast<double>(sampled) : 1.0;
  long long wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          wall_end - wall_start)
                          .count();
  double wall_s = static_cast<double>(wall_ns) / 1e9;

  std::cout << "    wall time:        " << wall_s << " s\n";
  std::cout << "    sampled pairs:    " << sampled << " / " << total_pairs
            << "\n";
  std::cout << "    avg latency:      " << total_latency_ns / sampled_d
            << " ns\n";
  std::cout << "    avg enqueue time: " << total_enqueue_ns / sampled_d
            << " ns\n";
  std::cout << "    avg dequeue time: " << total_dequeue_ns / sampled_d
            << " ns\n";
  std::cout << "    throughput:       " << ops / wall_s / 1e6 << " Mops/s\n";
  std::cout << "    pair throughput:  " << pairs / wall_s / 1e6
            << " Mpairs/s\n";
}

void export_results(const std::vector<TestItem>& items,
                    std::chrono::steady_clock::time_point wall_start,
                    std::chrono::steady_clock::time_point wall_end,
                    const std::string& queue_type,
                    unsigned long long ops_per_thread, int n_threads,
                    uint64_t work_iters, uint64_t max_samples,
                    const std::string& output_file) {
  std::ofstream f(output_file);
  long long wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          wall_end - wall_start)
                          .count();
  double wall_s = static_cast<double>(wall_ns) / 1e9;

  f << "# queue=" << queue_type << " n_threads=" << n_threads
    << " ops_per_thread=" << ops_per_thread << " wall_s=" << wall_s
    << " work_iters=" << work_iters << " max_samples=" << max_samples << "\n";
  f << "e2e_ns,enqueue_ns,dequeue_ns\n";
  for (const auto& item : items) {
    if (!item.should_sample) continue;
    auto e2e = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   item.dequeue_done - item.enqueue_start)
                   .count();
    auto enq = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   item.enqueue_done - item.enqueue_start)
                   .count();
    auto deq = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   item.dequeue_done - item.dequeue_start)
                   .count();
    f << e2e << "," << enq << "," << deq << "\n";
  }
}

template <typename Queue>
void run_performance_test(const std::string& queue_type,
                          unsigned long long ops_per_thread, int n_threads,
                          uint64_t work_iters, uint64_t max_samples,
                          const std::string& output_file) {
  int n_cpus = static_cast<int>(std::thread::hardware_concurrency());
  if (n_cpus <= 0) n_cpus = 1;

  unsigned long long count =
      ops_per_thread * static_cast<unsigned long long>(n_threads);

  // Mark a deterministic, evenly-spaced subset of items to sample latency
  unsigned long long stride = 1;
  if (max_samples > 0 && count > max_samples) {
    stride = count / max_samples;
  }

  std::cout << "  Test config:\n";
  std::cout << "    Threads:          " << n_threads << "\n";
  std::cout << "    Ops per thread:   " << ops_per_thread << "\n";
  std::cout << "    Total pairs:      " << count << "\n";
  std::cout << "    Hardware CPUs:    " << n_cpus << "\n";
  std::cout << "    Work iters:       " << work_iters << "\n";
  std::cout << "    Sample stride:    " << stride << " (" << (count / stride)
            << " samples)\n";

  std::cout << "  Generating test items" << std::endl;
  std::vector<TestItem> items(count);
  for (unsigned long long i = 0; i < count; i++) {
    items[i].should_sample = (i % stride == 0);
  }

  std::cout << "  Running test..." << std::endl;
  std::chrono::steady_clock::time_point wall_start, wall_end;
  execute_test<Queue>(items, ops_per_thread, n_threads, n_cpus, work_iters,
                      wall_start, wall_end);

  std::cout << "  Test summary:" << std::endl;
  test_summary(items, wall_start, wall_end, ops_per_thread, n_threads);

  if (!output_file.empty()) {
    std::cout << "  Exporting samples to " << output_file << std::endl;
    export_results(items, wall_start, wall_end, queue_type, ops_per_thread,
                   n_threads, work_iters, max_samples, output_file);
  }
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "Usage: " << argv[0]
              << " <ops_per_thread> <work_iters> <queue_type> <n_threads> "
                 "[output_file] [max_samples]\n"
              << "  queue_type:  MutexQueue | TwoLockQueue | PLJQueue | "
                 "MSQueue | ValoisQueue | LCRQueue | LPRQueue\n";
    return 1;
  }
  unsigned long long ops_per_thread = std::stoull(argv[1]);
  uint64_t work_iters = std::stoull(argv[2]);

  std::string queue_type = argv[3];
  int n_threads = std::stoi(argv[4]);

  std::string output_file = (argc > 5) ? argv[5] : "";
  uint64_t max_samples = (argc > 6) ? std::stoull(argv[6]) : 0;

  std::cout << queue_type << "\n";
  if (queue_type == "MutexQueue") {
    run_performance_test<MutexQueue<TestItem>>(queue_type, ops_per_thread,
                                               n_threads, work_iters,
                                               max_samples, output_file);
  } else if (queue_type == "TwoLockQueue") {
    run_performance_test<TwoLockQueue<TestItem>>(queue_type, ops_per_thread,
                                                 n_threads, work_iters,
                                                 max_samples, output_file);
  } else if (queue_type == "PLJQueue") {
    run_performance_test<PLJQueue<TestItem>>(queue_type, ops_per_thread,
                                             n_threads, work_iters, max_samples,
                                             output_file);
  } else if (queue_type == "MSQueue") {
    run_performance_test<MSQueue<TestItem>>(queue_type, ops_per_thread,
                                            n_threads, work_iters, max_samples,
                                            output_file);
  } else if (queue_type == "ValoisQueue") {
    run_performance_test<ValoisQueue<TestItem>>(queue_type, ops_per_thread,
                                                n_threads, work_iters,
                                                max_samples, output_file);
  } else if (queue_type == "LCRQueue") {
    run_performance_test<LCRQueue<TestItem>>(queue_type, ops_per_thread,
                                             n_threads, work_iters, max_samples,
                                             output_file);
  } else if (queue_type == "LPRQueue") {
    run_performance_test<LPRQueue<TestItem>>(queue_type, ops_per_thread,
                                             n_threads, work_iters, max_samples,
                                             output_file);
  } else {
    std::cerr << "Unknown queue type: " << queue_type << "\n";
    return 1;
  }

  return 0;
}
