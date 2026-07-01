// A CYD (Cheap Yellow Display, ESP32-2432S028R: 320x240 ILI9341 + XPT2046
// resistive touch) touch-paint demo. It uses only the framework's Arduino_GFX
// display shim and the sim touch bus - no CYD-specific sim code - which shows
// esprite hosting a very different board than the Waveshare/Clawdmeter targets.
//
// Tap a colour swatch in the top bar to pick a brush, then tap the canvas to
// paint. The classic CYD hello-world, driven entirely by injected taps.
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "sim_touch.h"

static Arduino_GFX gfx(320, 240);

static const uint16_t PALETTE[6] = { RED, ORANGE, YELLOW, GREEN, CYAN, MAGENTA };
static const int      SWATCH_W   = 320 / 6;   // 6 swatches across the top bar
static const int      BAR_H      = 34;
static uint16_t       brush      = CYAN;

static void draw_palette() {
    for (int i = 0; i < 6; ++i)
        gfx.fillRect(i * SWATCH_W, 0, SWATCH_W, BAR_H, PALETTE[i]);
    gfx.drawRect(0, 0, 320, BAR_H, WHITE);   // frame the palette bar
}

void setup() {
    Serial.begin(115200);
    Serial.println("cyd: booting");
    gfx.begin();
    gfx.fillScreen(gfx_rgb565(16, 16, 20));
    draw_palette();
    Serial.println("cyd: ready");
}

void loop() {
    int x, y;
    if (sim_touch(&x, &y)) {
        if (y < BAR_H) {                       // tapped the palette: pick a brush
            int i = x / SWATCH_W;
            if (i < 0) i = 0;
            if (i > 5) i = 5;
            brush = PALETTE[i];
        } else {                               // tapped the canvas: paint a dot
            gfx.fillCircle(x, y, 8, brush);
        }
    }
    delay(5);
}
