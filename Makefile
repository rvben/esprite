BUILD   ?= build
RELEASE ?= build-release
PREFIX  ?= /usr/local
TARGET  ?= waveshare_amoled_216_c6

.PHONY: configure build test screenshot scenario goldens release install clean

configure:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Debug

build: configure
	cmake --build $(BUILD) -j

# Optimized binary in $(RELEASE)/esprite.
release:
	cmake -S . -B $(RELEASE) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(RELEASE) -j

# Install the release binary to $(PREFIX)/bin (override PREFIX=~/.local).
install: release
	cmake --install $(RELEASE) --prefix $(PREFIX)

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
