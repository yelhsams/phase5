CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic -Isrc/

SRC_DIR = src
BUILD_DIR = build
RELEASE_OBJ_DIR = $(BUILD_DIR)/release/obj
DEBUG_OBJ_DIR = $(BUILD_DIR)/debug/obj

SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
RELEASE_OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(RELEASE_OBJ_DIR)/%.o)
DEBUG_OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(DEBUG_OBJ_DIR)/%.o)

RELEASE_TARGET = $(BUILD_DIR)/release/mitscript
DEBUG_TARGET = $(BUILD_DIR)/debug/mitscript

all: release debug

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(RELEASE_OBJ_DIR)
	mkdir -p $(DEBUG_OBJ_DIR)

release: CXXFLAGS += -O3 -DNDEBUG
release: $(RELEASE_TARGET)

$(RELEASE_TARGET): $(RELEASE_OBJS) | $(BUILD_DIR)
	$(CXX) $(RELEASE_OBJS) -o $@
	strip $@

debug: CXXFLAGS += -g -O0 -DDEBUG -fsanitize=address -fsanitize=undefined
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): $(DEBUG_OBJS) | $(BUILD_DIR)
	$(CXX) $(DEBUG_OBJS) $(LDFLAGS) -o $@

$(RELEASE_OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(SRC_DIR)/%.hpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DEBUG_OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(SRC_DIR)/%.hpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RELEASE_OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DEBUG_OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Available targets:"
	@echo "  all          - Build both release and debug versions"
	@echo "  release      - Build optimized release version"
	@echo "  debug        - Build debug version with sanitizers (no optimization)"
	@echo "  clean        - Remove build artifacts"
	@echo "  help         - Show this help message"

.PHONY: all release debug clean help