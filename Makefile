CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -MMD -MP
INCLUDES := -Iinclude -I/home/harvey/vcpkg/installed/x64-linux/include

BUILD_DIR := build
SRC_DIR := src
APP_DIR := app
FBS_DIR := fbs
FBS_OUT := include/fbs
LDFLAGS := -L/home/harvey/vcpkg/installed/x64-linux/lib
LDLIBS := -pthread -lrt

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
# App Executables
# -----------------------------------------------------------------------------

APP_SOURCES := $(wildcard $(APP_DIR)/*.cpp)
APP_TARGETS := $(patsubst $(APP_DIR)/%.cpp,$(BUILD_DIR)/%,$(APP_SOURCES))

# -----------------------------------------------------------------------------
# Default Target
# -----------------------------------------------------------------------------

.PHONY: all
all: $(FBS_GENERATED) $(APP_TARGETS)

# -----------------------------------------------------------------------------
# Build Apps
# -----------------------------------------------------------------------------

$(BUILD_DIR)/%: $(APP_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)
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
