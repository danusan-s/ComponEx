# ===== Configuration =====
BUILD_DIR := build
CMAKE := cmake
CTEST := ctest

BUILD_TYPE ?= Release

# ===== Default =====
.PHONY: all
all: build

# ===== Clean =====
.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR)


# ===== Build (normal) =====
.PHONY: build
build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@$(CMAKE) --build $(BUILD_DIR) -j

# ===== Build (Debug release) =====
.PHONY: debug
debug:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo
	@$(CMAKE) --build $(BUILD_DIR) -j

# ===== Rebuild (clean + build) =====
.PHONY: rebuild
rebuild: clean build

# ===== Run tests =====
.PHONY: run_tests
run_tests: build
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

# ===== Lint (matches the CI lint job) =====
.PHONY: lint
lint: format-check tidy

# Formatting only -- fast, no build required
.PHONY: format-check
format-check:
	@find include/componeng src/componeng tests example \
		-name "*.hpp" -o -name "*.cpp" | \
		xargs clang-format --dry-run --Werror

# Static analysis. Needs compile_commands.json; reuses whatever the build dir
# is already configured as rather than reconfiguring it out from under you.
.PHONY: tidy
tidy:
	@test -f $(BUILD_DIR)/compile_commands.json || $(MAKE) build
	@clang-tidy -p $(BUILD_DIR) --warnings-as-errors='*' \
		$$(find src/componeng -name "*.cpp")

# ===== Format (apply clang-format in place) =====
.PHONY: format
format:
	@find include/componeng src/componeng tests example \
		-name "*.hpp" -o -name "*.cpp" | \
		xargs clang-format -i
