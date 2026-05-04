CXX       := g++
CXXFLAGS  := -std=c++23 -O3 -march=native -mcx16 -Wall -Wextra -pthread
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

correctness: $(BIN_DIR)/correctness

$(BIN_DIR)/benchmark: ./src/benchmark.cc $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDLIBS)

benchmark: $(BIN_DIR)/benchmark

$(BIN_DIR)/calibrate: ./src/calibrate.cc include/work.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDLIBS)

calibrate: $(BIN_DIR)/calibrate
