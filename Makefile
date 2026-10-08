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
COMPDB      := $(BUILD)/compile_commands.json

.PHONY: all run test docs clean

all: $(BUILD)/pf2e_dpr $(COMPDB)

$(BUILD)/pf2e_dpr: $(OBJ) $(EXAMPLE_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/pf2e_tests: $(OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/examples/%.o: CPPFLAGS += -Iexamples/inc

# Objects also depend on this Makefile, so changing a flag rebuilds them. Each compile records its
# command in <object>.json for the compilation database below.
$(BUILD)/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
	@printf '{"directory": "%s", "file": "%s", "command": "%s"}\n' \
		"$(CURDIR)" "$<" "$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@" > $@.json

# Compilation database for editors (VS Code C/C++, clangd): the exact commands the build uses.
$(COMPDB): $(OBJ) $(EXAMPLE_OBJ) $(TEST_OBJ)
	@{ echo '['; cat $(addsuffix .json,$^) | sed '$$!s/$$/,/'; echo ']'; } > $@

run: $(BUILD)/pf2e_dpr $(COMPDB)
	./$(BUILD)/pf2e_dpr

test: $(BUILD)/pf2e_tests $(COMPDB)
	./$(BUILD)/pf2e_tests

docs:
	doxygen Doxyfile

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d) $(EXAMPLE_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
