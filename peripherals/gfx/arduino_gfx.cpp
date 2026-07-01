#include "Arduino_GFX_Library.h"
#include "framebuffer.h"

bool Arduino_GFX::begin(int32_t) {
    sim_framebuffer().init(w_, h_);
    return true;
}

void Arduino_GFX::fillScreen(uint16_t color) { sim_framebuffer().fill(color); }

void Arduino_GFX::drawPixel(int16_t x, int16_t y, uint16_t color) {
    uint16_t px = color;
    sim_framebuffer().blit(x, y, 1, 1, &px);
}

void Arduino_GFX::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t row = 0; row < h; ++row)
        drawFastHLine(x, y + row, w, color);
}

void Arduino_GFX::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void Arduino_GFX::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    for (int16_t i = 0; i < w; ++i) drawPixel(x + i, y, color);
}

void Arduino_GFX::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    for (int16_t i = 0; i < h; ++i) drawPixel(x, y + i, color);
}

void Arduino_GFX::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    for (int16_t dy = -r; dy <= r; ++dy) {
        for (int16_t dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) drawPixel(x0 + dx, y0 + dy, color);
        }
    }
}

void Arduino_GFX::draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t* bmp,
                                     int16_t w, int16_t h) {
    sim_framebuffer().blit(x, y, w, h, bmp);
}
