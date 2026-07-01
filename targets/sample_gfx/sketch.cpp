// A minimal Arduino sketch that uses only the Arduino core and Arduino_GFX.
// It has no custom HAL and no esprite-specific code: it runs purely against
// the framework's shims and the GFX-to-framebuffer peripheral. This is the
// generality proof - onboarding it needed only a board.cpp registration.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"

static Arduino_GFX gfx(320, 240);

void setup() {
    Serial.begin(115200);
    Serial.println("sample_gfx: booting");
    gfx.begin();

    gfx.fillScreen(gfx_rgb565(20, 20, 30));

    // RGB test bars across the top.
    gfx.fillRect(0,   0, 106, 60, RED);
    gfx.fillRect(106, 0, 107, 60, GREEN);
    gfx.fillRect(213, 0, 107, 60, BLUE);

    // A white double border.
    gfx.drawRect(0, 0, 320, 240, WHITE);
    gfx.drawRect(1, 1, 318, 238, WHITE);

    // A filled circle in the middle.
    gfx.fillCircle(160, 130, 40, ORANGE);

    Serial.println("sample_gfx: ready");
}

void loop() {
    // A progress bar that advances with virtual time, proving loop() runs and
    // the runtime clock ticks.
    uint32_t t = millis();
    int w = (int)((t / 10) % 300);
    gfx.fillRect(10, 210, 300, 14, gfx_rgb565(40, 40, 40));
    gfx.fillRect(10, 210, w, 14, CYAN);
    delay(5);
}
