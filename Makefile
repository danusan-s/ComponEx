CMAKE := cmake
CTEST := ctest
BUILD_TYPE ?= Release

# PROFILING=OFF builds the engine without the GPU/CPU timers and timing panes
# (COMPONENG_PROFILING in engine/CMakeLists.txt), for benchmarking. It gets its
# own build directory, so both configurations stay built and switching between
# them does not recompile everything:  make ForestDemo PROFILING=OFF
PROFILING ?= ON
ifeq ($(PROFILING),OFF)
BUILD_SUFFIX_PROF := -noprof
else ifeq ($(PROFILING),ON)
BUILD_SUFFIX_PROF :=
else
$(error PROFILING must be ON or OFF, got '$(PROFILING)')
endif

# BACKEND picks the renderer backend (RENDERER_BACKEND in engine/CMakeLists.txt).
# Vulkan is the default; it needs the Vulkan headers and glslang. Each backend has
# its own build directory (build-vulkan, build; with PROFILING=OFF the -noprof
# variants):
#   make ForestDemo                  # Vulkan
#   make ForestDemo BACKEND=opengl   # OpenGL
BACKEND ?= vulkan
ifeq ($(BACKEND),opengl)
BUILD_SUFFIX_BACKEND :=
else ifeq ($(BACKEND),vulkan)
BUILD_SUFFIX_BACKEND := -vulkan
else
$(error BACKEND must be opengl or vulkan, got '$(BACKEND)')
endif

BUILD_DIR := build$(BUILD_SUFFIX_BACKEND)$(BUILD_SUFFIX_PROF)

.PHONY: all clean configure build rebuild run_tests

all: build
	@:

clean:
	@rm -rf $(BUILD_DIR)

configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCOMPONENG_PROFILING=$(PROFILING) -DRENDERER_BACKEND=$(BACKEND)

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
$(eval $(call EXECUTABLE_template,MultiplayerDemo,multiplayer_demo))
