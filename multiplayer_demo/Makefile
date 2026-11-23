CMAKE := cmake
CTEST := ctest

BUILD_TYPE ?= Release
PROFILING ?= OFF
BACKEND ?= vulkan
TARGET ?= MultiplayerDemo

# Configuration validation
ifeq ($(PROFILING),ON)
BUILD_SUFFIX_PROF := -prof
else ifeq ($(PROFILING),OFF)
BUILD_SUFFIX_PROF :=
else
$(error PROFILING must be ON or OFF, got '$(PROFILING)')
endif

ifeq ($(BACKEND),opengl)
BUILD_SUFFIX_BACKEND :=
else ifeq ($(BACKEND),vulkan)
BUILD_SUFFIX_BACKEND := -vulkan
else
$(error BACKEND must be opengl or vulkan, got '$(BACKEND)')
endif

BUILD_DIR := build$(BUILD_SUFFIX_BACKEND)$(BUILD_SUFFIX_PROF)

# Local development: synchronize your engine source into the submodule directory
ENGINE_SRC ?= ~/Coding/ComponEng

.PHONY: all configure build rebuild clean run_tests sync_engine

all: build

configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCOMPONENG_PROFILING=$(PROFILING) \
		-DRENDERER_BACKEND=$(BACKEND)

build: configure
	@$(CMAKE) --build $(BUILD_DIR) -j

rebuild: clean build

clean:
	@rm -rf $(BUILD_DIR)

clean_all:
	@rm -rf build build-* 

run_tests: build
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

# Build and run a specific executable:
# make run TARGET=ForestDemo
run: configure
	@$(CMAKE) --build $(BUILD_DIR) --target $(TARGET) -j
	@$(BUILD_DIR)/$(TARGET)

# Synchronize local engine source into the submodule directory
sync_engine:
	@rsync -a --delete --itemize-changes $(DRY) \
		--exclude='.git' \
		--filter=':- .gitignore' \
		$(ENGINE_SRC)/ engine/

