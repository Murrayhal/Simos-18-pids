# Host build of the portable core plus its unit tests.
#
#   make test     compile and run everything
#   make clean
#
# The ESP32 firmware itself is built with PlatformIO:
#   pio run -e esp32dev -d firmware

CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -O1 -g
CORE_DIR  = firmware/lib/valvecore/src
TEST_DIR  = firmware/test/test_core
BUILD_DIR = build

SRCS  = $(wildcard $(CORE_DIR)/*.cpp) $(wildcard $(TEST_DIR)/*.cpp)
BIN   = $(BUILD_DIR)/valvecore_tests

.PHONY: test build clean

test: build
	@$(BIN)

build: $(BIN)

$(BIN): $(SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(CORE_DIR) -I$(TEST_DIR) $(SRCS) -o $@

clean:
	rm -rf $(BUILD_DIR)
