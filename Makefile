BUILD   ?= build
RELEASE ?= build-release
PREFIX  ?= /usr/local
TARGET  ?= waveshare_amoled_18

# Optional path to the agentgauge firmware source; the CMake default is the
# ../agentgauge sibling checkout's firmware/src. agentgauge's own Makefile
# points this at its checkout when building esprite as a test harness.
CMAKE_ARGS := -DCMAKE_BUILD_TYPE=Debug
ifdef AGENTGAUGE_SRC
CMAKE_ARGS += -DAGENTGAUGE_SRC=$(AGENTGAUGE_SRC)
endif

.PHONY: configure build test screenshot scenario goldens release install dist clean qemu-fetch qemu-fixtures qemu-test qemu-goldens

configure:
	cmake -S . -B $(BUILD) $(CMAKE_ARGS)

build: configure
	cmake --build $(BUILD) -j

# Optimized binary in $(RELEASE)/esprite.
release:
	cmake -S . -B $(RELEASE) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(RELEASE) -j

# Install the release binary to $(PREFIX)/bin (override PREFIX=~/.local).
install: release
	cmake --install $(RELEASE) --prefix $(PREFIX)

# Distributable tarball in dist/: esprite-<version>-<os>-<arch>.tar.gz with the
# optimized binary, LICENSE, and README. Version comes from the binary itself.
dist: release
	@set -e; \
	VERSION=$$($(RELEASE)/esprite --version | sed 's/.*"version":"\([^"]*\)".*/\1/'); \
	OS=$$(uname -s | tr '[:upper:]' '[:lower:]'); ARCH=$$(uname -m); \
	NAME=esprite-$$VERSION-$$OS-$$ARCH; \
	rm -rf dist/$$NAME; mkdir -p dist/$$NAME; \
	cp $(RELEASE)/esprite LICENSE README.md dist/$$NAME/; \
	tar -czf dist/$$NAME.tar.gz -C dist $$NAME; \
	rm -rf dist/$$NAME; \
	echo "dist/$$NAME.tar.gz"

# Unit tests (sim_tests) and target integration tests (sim_itests).
test: build
	ctest --test-dir $(BUILD) --output-on-failure --timeout 90

# One-shot screenshot of a target: make screenshot TARGET=sample_gfx
screenshot: build
	$(BUILD)/esprite screenshot --target $(TARGET) $(TARGET).png
	@echo "wrote $(TARGET).png"

# Run a target's JSON scenario (emits PNGs into the current directory).
scenario: build
	$(BUILD)/esprite scenario scenarios/$(TARGET).json

# Regenerate golden screenshots for a target into goldens/.
goldens: build
	mkdir -p goldens
	cd goldens && ../$(BUILD)/esprite scenario ../scenarios/$(TARGET).json

clean:
	rm -rf $(BUILD) $(RELEASE)

# Download prebuilt Espressif QEMU (qemu-system-riscv32, qemu-system-xtensa)
# into .qemu/; writes .qemu/env.sh with ESPRITE_QEMU_RISCV32/XTENSA paths.
qemu-fetch:
	bash tools/qemu/fetch-qemu.sh

# Build QEMU test flash images (needs docker + arduino-cli): tests/fixtures/qemu/
qemu-fixtures:
	bash tools/qemu/build-fixtures.sh
	bash tools/qemu/build-lvgl-fixture.sh

# Regenerate the emulator golden screenshots from the fixture scenarios
# (needs qemu-fetch + qemu-fixtures). Eyeball the PNGs before committing:
# the gated tests compare byte-exact.
qemu-goldens: build
	mkdir -p tests/goldens/qemu
	cd tests/goldens/qemu && . ../../../.qemu/env.sh && \
	  ESPRITE_QEMU_IMAGE=$(CURDIR)/tests/fixtures/qemu/rgb_c3.bin \
	  ../../../$(BUILD)/esprite scenario ../../../scenarios/qemu_esp32c3_rgb.json
	cd tests/goldens/qemu && . ../../../.qemu/env.sh && \
	  ESPRITE_QEMU_IMAGE=$(CURDIR)/tests/fixtures/qemu/lvgl_c3.bin \
	  ../../../$(BUILD)/esprite scenario ../../../scenarios/qemu_esp32c3_rgb_lvgl.json

# Run the gated emulator integration tests (needs qemu-fetch + qemu-fixtures
# to have been run first; sources .qemu/env.sh for ESPRITE_QEMU_RISCV32/XTENSA).
# --repeat until-pass:2 tolerates one transient retry: the xtensa case runs
# wall-clock and can miss its pump deadline under host load. A persistent
# failure (fails both attempts) still fails the gate.
qemu-test: build
	. .qemu/env.sh && ctest --test-dir $(BUILD) -R sim_itests_qemu --output-on-failure --repeat until-pass:2
