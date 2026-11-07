BUILD_DIR := build
CMAKE := cmake
CTEST := ctest
BUILD_TYPE ?= Release

.PHONY: all clean configure build rebuild run_tests

all: build
	@:

clean:
	@rm -rf $(BUILD_DIR)

configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

# Everything: the engine lib, every example and the engine's test executables.
build: configure
	@$(CMAKE) --build $(BUILD_DIR) -j

rebuild: clean build

run_tests: build
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

# Per-target build+run: make AimTrain / make PlanetarySim / make ForestDemo
# Builds only that example and the engine lib it links (--target pulls in just
# its dependencies), so the engine's test executables are never compiled.
define EXECUTABLE_template
.PHONY: $(1)
$(1): configure
	@$(CMAKE) --build $(BUILD_DIR) --target $(1) -j
	@$(BUILD_DIR)/$(2)/$(1)
endef

$(eval $(call EXECUTABLE_template,AimTrain,aim_train))
$(eval $(call EXECUTABLE_template,PlanetarySim,planetary_simulation))
$(eval $(call EXECUTABLE_template,ForestDemo,forest))
