#include "TFT_eSPI.h"
#include "framebuffer.h"
#include "sim_touch.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

// Standard 5x7 GLCD font, printable ASCII 0x20-0x7E. Column-major, bit0 = top
// row. This is the classic Adafruit/TFT_eSPI font 1 glyph set.
static const uint8_t FONT5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x01,0x01},
    {0x3E,0x41,0x41,0x51,0x32}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x7F,0x20,0x18,0x20,0x7F}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x00,0x7F,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20}, {0x41,0x41,0x7F,0x00,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00}, {0x08,0x04,0x08,0x10,0x08},
};

TFT_eSPI::TFT_eSPI(int16_t w, int16_t h) : _w(w), _h(h) {}

void TFT_eSPI::init(uint8_t)  { if (!_buf) sim_framebuffer().init(_w, _h); }
void TFT_eSPI::begin(uint8_t) { init(); }

void TFT_eSPI::setRotation(uint8_t r) {
    _rotation = r & 3;
    int16_t s = _w < _h ? _w : _h, l = _w < _h ? _h : _w;   // short / long edge
    if (_rotation & 1) { _w = l; _h = s; } else { _w = s; _h = l; }
    if (!_buf) sim_framebuffer().init(_w, _h);
}

uint16_t TFT_eSPI::color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void TFT_eSPI::putpx(int32_t x, int32_t y, uint16_t color) {
    if (_buf) {
        if (x < 0 || y < 0 || x >= _bw || y >= _bh) return;
        _buf[y * _bw + x] = color;
    } else if (!_is_sprite) {
        // Rotations 2 and 3 are the 180-degree mounts of 0 and 1: mirror both
        // axes so a flipped sketch renders flipped, as on the real panel.
        if (_rotation >= 2) { x = _w - 1 - x; y = _h - 1 - y; }
        Framebuffer& fb = sim_framebuffer();
        if (x < 0 || y < 0 || x >= fb.w() || y >= fb.h()) return;
        fb.data()[y * fb.w() + x] = color;
    }
    // A sprite without a buffer draws nowhere.
}

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint16_t color) { putpx(x, y, color); }

void TFT_eSPI::fillScreen(uint16_t color) {
    if (_buf)             std::fill(_buf, _buf + (size_t)_bw * _bh, color);
    else if (!_is_sprite) sim_framebuffer().fill(color);
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t c) {
    for (int32_t i = 0; i < w; ++i) putpx(x + i, y, c);
}
void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t c) {
    for (int32_t i = 0; i < h; ++i) putpx(x, y + i, c);
}
void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    for (int32_t row = 0; row < h; ++row) drawFastHLine(x, y + row, w, c);
}
void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c);
}
void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) {
    int32_t dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        putpx(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void TFT_eSPI::drawCircle(int32_t x0, int32_t y0, int32_t r, uint16_t c) {
    int32_t x = -r, y = 0, err = 2 - 2 * r;
    do {
        putpx(x0 - x, y0 + y, c); putpx(x0 - y, y0 - x, c);
        putpx(x0 + x, y0 - y, c); putpx(x0 + y, y0 + x, c);
        r = err;
        if (r <= y) err += ++y * 2 + 1;
        if (r > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
}
void TFT_eSPI::fillCircle(int32_t x0, int32_t y0, int32_t r, uint16_t c) {
    for (int32_t dy = -r; dy <= r; ++dy)
        for (int32_t dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) putpx(x0 + dx, y0 + dy, c);
}
void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    fillRect(x + r, y, w - 2 * r, h, c);
    fillRect(x, y + r, r, h - 2 * r, c);
    fillRect(x + w - r, y + r, r, h - 2 * r, c);
    fillCircle(x + r, y + r, r, c);         fillCircle(x + w - r - 1, y + r, r, c);
    fillCircle(x + r, y + h - r - 1, r, c);  fillCircle(x + w - r - 1, y + h - r - 1, r, c);
}
void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    drawFastHLine(x + r, y, w - 2 * r, c);  drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
    drawFastVLine(x, y + r, h - 2 * r, c);  drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
    drawCircle(x + r, y + r, r, c);         drawCircle(x + w - r - 1, y + r, r, c);
    drawCircle(x + r, y + h - r - 1, r, c);  drawCircle(x + w - r - 1, y + h - r - 1, r, c);
}
void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint16_t c) {
    int32_t minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
    int32_t miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto edge = [](int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    for (int32_t py = miny; py <= maxy; ++py)
        for (int32_t px = minx; px <= maxx; ++px) {
            int32_t w0 = edge(x1, y1, x2, y2, px, py);
            int32_t w1 = edge(x2, y2, x0, y0, px, py);
            int32_t w2 = edge(x0, y0, x1, y1, px, py);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                putpx(px, py, c);
        }
}
void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
    for (int32_t row = 0; row < h; ++row)
        for (int32_t col = 0; col < w; ++col)
            putpx(x + col, y + row, data[row * w + col]);
}

// ---- Text ----
void TFT_eSPI::drawChar(int32_t x, int32_t y, char c, uint16_t fg, uint16_t bg, uint8_t size) {
    unsigned char uc = (unsigned char)c;
    if (uc < 0x20 || uc > 0x7E) uc = '?';
    const uint8_t* g = FONT5x7[uc - 0x20];
    for (int col = 0; col < 6; ++col) {
        uint8_t bits = col < 5 ? g[col] : 0x00;   // 6th column = inter-char gap
        for (int row = 0; row < 8; ++row) {
            bool on = (bits >> row) & 1;
            if (on)            fillRect(x + col * size, y + row * size, size, size, fg);
            else if (_opaque)  fillRect(x + col * size, y + row * size, size, size, bg);
        }
    }
}
void TFT_eSPI::setCursor(int32_t x, int32_t y) { _cx = x; _cy = y; }
void TFT_eSPI::setCursor(int32_t x, int32_t y, uint8_t) { _cx = x; _cy = y; }
void TFT_eSPI::setTextColor(uint16_t c) { _fg = c; _opaque = false; }
void TFT_eSPI::setTextColor(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; _opaque = true; }
void TFT_eSPI::setTextSize(uint8_t s) { _textsize = s < 1 ? 1 : s; }
int16_t TFT_eSPI::textWidth(const char* s) const { return (int16_t)(strlen(s) * 6 * _textsize); }

size_t TFT_eSPI::write(uint8_t c) {
    if (c == '\n') { _cx = 0; _cy += 8 * _textsize; return 1; }
    if (c == '\r') return 1;
    if (_wrap && _cx + 6 * _textsize > _w) { _cx = 0; _cy += 8 * _textsize; }
    drawChar(_cx, _cy, (char)c, _fg, _bg, _textsize);
    _cx += 6 * _textsize;
    return 1;
}
size_t TFT_eSPI::print(const char* s) { size_t n = 0; while (s && *s) n += write((uint8_t)*s++); return n; }
size_t TFT_eSPI::print(char c)        { return write((uint8_t)c); }
size_t TFT_eSPI::print(int v)         { char b[16]; snprintf(b, sizeof(b), "%d", v); return print(b); }
size_t TFT_eSPI::print(unsigned v)    { char b[16]; snprintf(b, sizeof(b), "%u", v); return print(b); }
size_t TFT_eSPI::println(const char* s) { size_t n = print(s); n += write('\n'); return n; }
size_t TFT_eSPI::println(int v)         { char b[16]; snprintf(b, sizeof(b), "%d", v); return println(b); }

int16_t TFT_eSPI::drawString(const char* s, int32_t x, int32_t y) {
    int16_t tw = textWidth(s), th = fontHeight();
    int32_t sx = x, sy = y;
    if (_datum == TC_DATUM || _datum == MC_DATUM || _datum == BC_DATUM) sx = x - tw / 2;
    else if (_datum == TR_DATUM || _datum == MR_DATUM || _datum == BR_DATUM) sx = x - tw;
    if (_datum == ML_DATUM || _datum == MC_DATUM || _datum == MR_DATUM) sy = y - th / 2;
    else if (_datum == BL_DATUM || _datum == BC_DATUM || _datum == BR_DATUM) sy = y - th;
    for (const char* p = s; p && *p; ++p) { drawChar(sx, sy, *p, _fg, _bg, _textsize); sx += 6 * _textsize; }
    return tw;
}
int16_t TFT_eSPI::drawString(const char* s, int32_t x, int32_t y, uint8_t) { return drawString(s, x, y); }
int16_t TFT_eSPI::drawCentreString(const char* s, int32_t x, int32_t y, uint8_t) {
    uint8_t d = _datum; _datum = TC_DATUM; int16_t r = drawString(s, x, y); _datum = d; return r;
}
int16_t TFT_eSPI::drawRightString(const char* s, int32_t x, int32_t y, uint8_t) {
    uint8_t d = _datum; _datum = TR_DATUM; int16_t r = drawString(s, x, y); _datum = d; return r;
}

// ---- Touch ----
uint8_t TFT_eSPI::getTouch(uint16_t* x, uint16_t* y, uint16_t) {
    int tx, ty;
    if (!sim_touch(&tx, &ty)) return 0;
    // Taps are injected in framebuffer coordinates; report them in the
    // sketch's logical coordinates, inverting the display's 180-degree flip.
    if (_rotation >= 2) { tx = _w - 1 - tx; ty = _h - 1 - ty; }
    if (x) *x = (uint16_t)tx;
    if (y) *y = (uint16_t)ty;
    return 1;
}

// ---- Sprite ----
TFT_eSprite::TFT_eSprite(TFT_eSPI* parent) : _parent(parent) { _is_sprite = true; }
void* TFT_eSprite::createSprite(int16_t w, int16_t h) {
    if (w <= 0 || h <= 0) return nullptr;
    _bw = w; _bh = h; _w = w; _h = h;
    _pixels.assign((size_t)w * h, 0);
    _buf = _pixels.data();
    return _buf;
}
void TFT_eSprite::deleteSprite() {
    std::vector<uint16_t>().swap(_pixels);   // release the buffer's memory
    _buf = nullptr;
    _bw = _bh = 0;
}
void TFT_eSprite::pushSprite(int32_t x, int32_t y) {
    if (!_buf) return;
    // Blit through the parent so a sprite parented to another sprite composes
    // into that sprite's buffer, not straight onto the screen.
    if (_parent) _parent->pushImage(x, y, _bw, _bh, _buf);
    else         sim_framebuffer().blit(x, y, _bw, _bh, _buf);
}
