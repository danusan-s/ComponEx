BUILD_DIR := build
CMAKE := cmake
CTEST := ctest
BUILD_TYPE ?= Release

.PHONY: all clean rebuild run_tests

all: build
	@:

clean:
	@rm -rf $(BUILD_DIR)

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@$(CMAKE) --build $(BUILD_DIR) -j

rebuild: clean build

run_tests: build
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

# Per-target build+run: make AimTrain / make PlanetarySim
define EXECUTABLE_template
.PHONY: $(1)
$(1): build
	@$(CMAKE) --build $(BUILD_DIR) --target $(1)
	@$(BUILD_DIR)/$(2)/$(1)
endef

$(eval $(call EXECUTABLE_template,AimTrain,aim_train))
$(eval $(call EXECUTABLE_template,PlanetarySim,planetary_simulation))
