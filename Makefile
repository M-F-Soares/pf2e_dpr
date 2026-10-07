CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pedantic -Wno-missing-field-initializers
BUILD    := build

SRC      := $(wildcard src/*.cpp src/content/*.cpp)
OBJ      := $(SRC:%.cpp=$(BUILD)/%.o)
TESTS    := $(wildcard tests/*.cpp)
TEST_OBJ := $(TESTS:%.cpp=$(BUILD)/%.o)

.PHONY: all run test clean

all: $(BUILD)/pf2e_dpr

$(BUILD)/pf2e_dpr: $(OBJ) $(BUILD)/examples/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/pf2e_tests: $(OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: $(BUILD)/pf2e_dpr
	./$(BUILD)/pf2e_dpr

test: $(BUILD)/pf2e_tests
	./$(BUILD)/pf2e_tests

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(BUILD)/examples/main.d
