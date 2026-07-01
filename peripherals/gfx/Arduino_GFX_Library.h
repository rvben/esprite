#pragma once
#include <cstdint>

// Minimal Arduino_GFX-compatible surface that renders into the sim framebuffer.
// Lets standard display-library apps run with zero app-specific sim code. Covers
// the common drawing primitives; extend as targets need more.

// Common Arduino_GFX RGB565 color names.
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFC00

static inline uint16_t gfx_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

class Arduino_GFX {
public:
    Arduino_GFX(int16_t w, int16_t h) : w_(w), h_(h) {}

    bool begin(int32_t /*speed*/ = 0);
    int16_t width() const { return w_; }
    int16_t height() const { return h_; }

    void fillScreen(uint16_t color);
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
    void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t* bmp, int16_t w, int16_t h);
    void setRotation(uint8_t) {}

private:
    int16_t w_, h_;
};
