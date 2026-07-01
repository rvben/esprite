// A CYD control-panel demo written against the real TFT_eSPI API - the library
// almost every off-the-shelf CYD sketch uses. It runs unmodified against
// esprite's TFT_eSPI shim: tft.init/setRotation/fillScreen, fillRoundRect
// buttons, drawString with datums, and tft.getTouch() reading the touch bus.
// Tapping a button toggles it.
#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

struct Button {
    int         x, y, w, h;
    const char* label;
    uint16_t    color;
    bool        on;
};

static Button buttons[3] = {
    {  20,  70, 130, 60, "LED",   TFT_BLUE,  false },
    { 170,  70, 130, 60, "FAN",   TFT_GREEN, false },
    {  20, 160, 280, 55, "RESET", TFT_MAROON, false },
};

static void draw_button(const Button& b) {
    tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.on ? b.color : TFT_DARKGREY);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 1);
    tft.setTextDatum(TL_DATUM);
}

void setup() {
    Serial.begin(115200);
    Serial.println("cyd_tft: booting");
    tft.init();
    tft.setRotation(1);                 // 320x240 landscape
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_YELLOW);
    tft.setTextSize(3);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("CYD Control", tft.width() / 2, 14, 1);
    tft.setTextDatum(TL_DATUM);

    for (const Button& b : buttons) draw_button(b);
    Serial.println("cyd_tft: ready");
}

void loop() {
    static bool was_touched = false;
    uint16_t x, y;
    bool now = tft.getTouch(&x, &y);
    if (now && !was_touched) {          // act on the press edge, not while held
        for (Button& b : buttons)
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
                b.on = !b.on;
                draw_button(b);
            }
    }
    was_touched = now;
    delay(10);
}
