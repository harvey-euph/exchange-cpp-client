CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -MMD -MP
INCLUDES := -Iinclude -I/home/harvey/vcpkg/installed/x64-linux/include

BUILD_DIR := build
SRC_DIR := src
AGENT_DIR := agents
EXAMPLE_DIR := examples
FBS_DIR := fbs
FBS_OUT := include/fbs
LDFLAGS := -L/home/harvey/vcpkg/installed/x64-linux/lib
LDLIBS := -pthread -lrt

BUILD_AGENT_DIR := $(BUILD_DIR)/agents
BUILD_EXAMPLE_DIR := $(BUILD_DIR)/examples

# -----------------------------------------------------------------------------
# FlatBuffers
# -----------------------------------------------------------------------------

FBS_SOURCES := $(wildcard $(FBS_DIR)/*.fbs)
FBS_GENERATED := $(patsubst $(FBS_DIR)/%.fbs,$(FBS_OUT)/%_generated.h,$(FBS_SOURCES))

# -----------------------------------------------------------------------------
# Source Objects
# -----------------------------------------------------------------------------

SRC_SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
SRC_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC_SOURCES))
SRC_DEPS := $(SRC_OBJECTS:.o=.d)

# -----------------------------------------------------------------------------
# Agent Executables
# -----------------------------------------------------------------------------

AGENT_SOURCES := $(wildcard $(AGENT_DIR)/*.cpp)
AGENT_TARGETS := $(patsubst $(AGENT_DIR)/%.cpp,$(BUILD_AGENT_DIR)/%,$(AGENT_SOURCES))

# -----------------------------------------------------------------------------
# Example Executables
# -----------------------------------------------------------------------------

EXAMPLE_SOURCES := $(wildcard $(EXAMPLE_DIR)/*.cpp)
EXAMPLE_TARGETS := $(patsubst $(EXAMPLE_DIR)/%.cpp,$(BUILD_EXAMPLE_DIR)/%,$(EXAMPLE_SOURCES))

# -----------------------------------------------------------------------------
# Default Target
# -----------------------------------------------------------------------------

.PHONY: all
all: $(FBS_GENERATED) $(AGENT_TARGETS) $(EXAMPLE_TARGETS)

# -----------------------------------------------------------------------------
# Build Agents
# -----------------------------------------------------------------------------

$(BUILD_AGENT_DIR)/%: $(AGENT_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_AGENT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Examples
# -----------------------------------------------------------------------------

$(BUILD_EXAMPLE_DIR)/%: $(EXAMPLE_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_EXAMPLE_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Compile Source Objects
# -----------------------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# -----------------------------------------------------------------------------
# Generate FlatBuffers Headers
# -----------------------------------------------------------------------------

$(FBS_OUT)/%_generated.h: $(FBS_DIR)/%.fbs
	@mkdir -p $(FBS_OUT)
	flatc --cpp --gen-mutable --gen-object-api -o $(FBS_OUT) $<

# -----------------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(FBS_OUT)

# -----------------------------------------------------------------------------
# Include Dependency Files
# -----------------------------------------------------------------------------

-include $(SRC_DEPS)
