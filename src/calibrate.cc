#include <cstdint>
#include <iostream>
#include <string>

#include "work.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <target_ns>\n";
    return 1;
  }
  uint64_t target_ns = std::stoull(argv[1]);
  uint64_t iters = calibrate_work_iters(target_ns);
  std::cout << iters << "\n";
  return 0;
}
