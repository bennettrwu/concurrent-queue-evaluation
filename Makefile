CXX       := g++
CXXFLAGS  := -std=c++23 -O3 -march=native -Wall -Wextra -pthread
SANFLAGS  := -std=c++23 -O0 -Wall -Wextra -pthread -g
INCLUDES  := -I include
BIN_DIR   := bin

.PHONY: all clean correctness

all: correctness

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -r $(BIN_DIR)

$(BIN_DIR)/correctness: ./src/correctness.cc | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BIN_DIR)/correctness_tsan: ./src/correctness.cc | $(BIN_DIR)
	$(CXX) $(SANFLAGS) -fsanitize=thread $(INCLUDES) $< -o $@

$(BIN_DIR)/correctness_asan: ./src/correctness.cc | $(BIN_DIR)
	$(CXX) $(SANFLAGS) -fsanitize=address,undefined $(INCLUDES) $< -o $@

correctness: $(BIN_DIR)/correctness $(BIN_DIR)/correctness_tsan $(BIN_DIR)/correctness_asan

