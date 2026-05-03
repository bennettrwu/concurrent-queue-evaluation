#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>

static inline uint64_t do_work(uint64_t state, uint64_t iters) {
  for (uint64_t i = 0; i < iters; i++) {
    state = state * 6364136223846793005ULL + 9754186451795953191ULL;
  }
  return state;
}

static inline uint64_t calibrate_work_iters(uint64_t target_ns) {
  if (target_ns == 0) return 0;
  constexpr uint64_t cal_iters = 2'000'000;

  // Warm up
  uint64_t state = do_work(22ULL, cal_iters);

  std::chrono::time_point t0 = std::chrono::steady_clock::now();
  state = do_work(state, cal_iters);
  std::chrono::time_point t1 = std::chrono::steady_clock::now();

  if (state == 0) std::cerr << "";  // prevent loop from being optimized away

  long long ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  uint64_t iters = (cal_iters * target_ns) / static_cast<uint64_t>(ns);
  return iters;
}
