CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pedantic -Wno-missing-field-initializers
# Warnings fail the build; WERROR=0 turns them back into messages (say, while a newer compiler's warning is fixed).
WERROR   ?= 1
ifeq ($(WERROR),1)
CXXFLAGS += -Werror
endif

BUILD    := build

# Each library group has its headers in lib/<group>/inc and its sources in lib/<group>/src.
LIBS     := core engine content
CPPFLAGS += $(LIBS:%=-Ilib/%/inc)
# Third-party headers (nlohmann/json); -isystem keeps their warnings out of ours.
CPPFLAGS += -isystem lib/vendor

SRC         := $(foreach l,$(LIBS),$(wildcard lib/$(l)/src/*.cpp))
OBJ         := $(SRC:%.cpp=$(BUILD)/%.o)
EXAMPLE     := $(wildcard examples/src/*.cpp)
EXAMPLE_OBJ := $(EXAMPLE:%.cpp=$(BUILD)/%.o)
TESTS       := $(wildcard tests/*.cpp)
TEST_OBJ    := $(TESTS:%.cpp=$(BUILD)/%.o)
TOOL        := $(wildcard tools/src/*.cpp)
TOOL_OBJ    := $(TOOL:%.cpp=$(BUILD)/%.o)
# The Foundry converter is unit tested, so the test binary links it too (without the importer's main()).
CONVERTER   := $(BUILD)/tools/src/FoundryImport.o
IMPORTER    := $(BUILD)/import-foundry
COMPDB      := $(BUILD)/compile_commands.json

# make import: Foundry VTT's PF2e data, sparse-cloned (spells and spell effects) into a cache make clean keeps.
# FOUNDRY=<path> uses an existing clone instead, and nothing is fetched.
FOUNDRY_URL   := https://github.com/foundryvtt/pf2e.git
FOUNDRY_CACHE := .cache/foundry-pf2e
FOUNDRY_PATHS := packs/pf2e/spells packs/pf2e/spell-effects
# The commit of a clone, for the import report.
revision = $$(git -C "$(1)" rev-parse --short HEAD 2>/dev/null || echo unknown)

.PHONY: all run test docs clean import

all: $(BUILD)/pf2e_dpr $(COMPDB)

$(BUILD)/pf2e_dpr: $(OBJ) $(EXAMPLE_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/pf2e_tests: $(OBJ) $(TEST_OBJ) $(CONVERTER)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(IMPORTER): $(OBJ) $(TOOL_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/examples/%.o: CPPFLAGS += -Iexamples/inc
$(BUILD)/tools/%.o: CPPFLAGS += -Itools/inc
# doctest is included as <doctest.h> through -isystem, so warnings from it don't count as ours.
$(BUILD)/tests/%.o: CPPFLAGS += -Itools/inc -isystem tests/vendor

# Objects also depend on this Makefile, so changing a flag rebuilds them. Each compile records its
# command in <object>.json for the compilation database below.
$(BUILD)/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
	@printf '{"directory": "%s", "file": "%s", "command": "%s"}\n' \
		"$(CURDIR)" "$<" "$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@" > $@.json

# Compilation database for editors (VS Code C/C++, clangd): the exact commands the build uses.
$(COMPDB): $(OBJ) $(EXAMPLE_OBJ) $(TEST_OBJ) $(TOOL_OBJ)
	@{ echo '['; cat $(addsuffix .json,$^) | sed '$$!s/$$/,/'; echo ']'; } > $@

run: $(BUILD)/pf2e_dpr $(COMPDB)
	./$(BUILD)/pf2e_dpr

test: $(BUILD)/pf2e_tests $(COMPDB)
	./$(BUILD)/pf2e_tests

docs:
	doxygen Doxyfile

# Converts Foundry's spells into data/imported (git-ignored). The only target that uses the network.
import: $(IMPORTER)
ifdef FOUNDRY
	./$(IMPORTER) "$(FOUNDRY)" data data/imported "$(call revision,$(FOUNDRY))"
else
	@if [ -d $(FOUNDRY_CACHE)/.git ]; then \
		echo "Updating $(FOUNDRY_CACHE)"; \
		git -C $(FOUNDRY_CACHE) sparse-checkout set $(FOUNDRY_PATHS) && \
		git -C $(FOUNDRY_CACHE) fetch --quiet --depth 1 origin HEAD && \
		git -C $(FOUNDRY_CACHE) reset --quiet --hard FETCH_HEAD; \
	else \
		echo "Cloning Foundry's spells into $(FOUNDRY_CACHE)"; \
		git clone --quiet --depth 1 --filter=blob:none --sparse $(FOUNDRY_URL) $(FOUNDRY_CACHE) && \
		git -C $(FOUNDRY_CACHE) sparse-checkout set $(FOUNDRY_PATHS); \
	fi
	./$(IMPORTER) $(FOUNDRY_CACHE) data data/imported "$(call revision,$(FOUNDRY_CACHE))"
endif

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d) $(EXAMPLE_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(TOOL_OBJ:.o=.d)
