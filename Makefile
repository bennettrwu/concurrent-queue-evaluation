CXX       := g++
CXXFLAGS  := -std=c++23 -O3 -march=native -mcx16 -Wall -Wextra -pthread
SANFLAGS  := -std=c++23 -O0 -Wall -Wextra -pthread -g
LDLIBS    := -latomic
INCLUDES  := -I include
BIN_DIR   := bin
HEADERS   := $(wildcard include/*.hpp)

.PHONY: all clean correctness benchmark calibrate

all: correctness benchmark calibrate

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -r $(BIN_DIR)

$(BIN_DIR)/correctness: ./src/correctness.cc $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDLIBS)

$(BIN_DIR)/correctness_tsan: ./src/correctness.cc $(HEADERS) | $(BIN_DIR)
	$(CXX) $(SANFLAGS) -mcx16 -fsanitize=thread $(INCLUDES) $< -o $@ $(LDLIBS)

$(BIN_DIR)/correctness_asan: ./src/correctness.cc $(HEADERS) | $(BIN_DIR)
	$(CXX) $(SANFLAGS) -mcx16 -fsanitize=address,undefined $(INCLUDES) $< -o $@ $(LDLIBS)

correctness: $(BIN_DIR)/correctness $(BIN_DIR)/correctness_tsan $(BIN_DIR)/correctness_asan

$(BIN_DIR)/benchmark: ./src/benchmark.cc $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDLIBS)

benchmark: $(BIN_DIR)/benchmark

$(BIN_DIR)/calibrate: ./src/calibrate.cc include/work.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDLIBS)

calibrate: $(BIN_DIR)/calibrate
