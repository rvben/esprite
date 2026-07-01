BUILD ?= build
TARGET ?= clawdmeter

.PHONY: configure build test screenshot goldens clean

configure:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Debug

build: configure
	cmake --build $(BUILD) -j

test: configure
	cmake --build $(BUILD) --target sim_tests -j
	ctest --test-dir $(BUILD) --output-on-failure

screenshot: build
	$(BUILD)/esp32sim screenshot --target $(TARGET) $(TARGET).png

goldens: build
	$(BUILD)/esp32sim scenario scenarios/$(TARGET).json

clean:
	rm -rf $(BUILD)
