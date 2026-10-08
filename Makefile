CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pedantic -Wno-missing-field-initializers
BUILD    := build

# Each library group has its headers in lib/<group>/inc and its sources in lib/<group>/src.
LIBS     := core engine content
CPPFLAGS += $(LIBS:%=-Ilib/%/inc)

SRC         := $(foreach l,$(LIBS),$(wildcard lib/$(l)/src/*.cpp))
OBJ         := $(SRC:%.cpp=$(BUILD)/%.o)
EXAMPLE     := $(wildcard examples/src/*.cpp)
EXAMPLE_OBJ := $(EXAMPLE:%.cpp=$(BUILD)/%.o)
TESTS       := $(wildcard tests/*.cpp)
TEST_OBJ    := $(TESTS:%.cpp=$(BUILD)/%.o)

.PHONY: all run test docs clean

all: $(BUILD)/pf2e_dpr

$(BUILD)/pf2e_dpr: $(OBJ) $(EXAMPLE_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/pf2e_tests: $(OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/examples/%.o: CPPFLAGS += -Iexamples/inc

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: $(BUILD)/pf2e_dpr
	./$(BUILD)/pf2e_dpr

test: $(BUILD)/pf2e_tests
	./$(BUILD)/pf2e_tests

docs:
	doxygen Doxyfile

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d) $(EXAMPLE_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
