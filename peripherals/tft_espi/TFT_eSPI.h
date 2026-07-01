#pragma once
// Host shim for the TFT_eSPI library, the dominant graphics library on the CYD
// (Cheap Yellow Display) and many other ESP32 TFT boards. Covers the common
// drawing, text, touch, and sprite surface so real off-the-shelf TFT_eSPI
// sketches run against the sim framebuffer. Not a full port - extend as needed.
#include <cstdint>
#include <cstddef>
#include <vector>

// ---- Colours (RGB565), matching TFT_eSPI ----
#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xD69A
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFDA0
#define TFT_GREENYELLOW 0xB7E0
#define TFT_PINK        0xFE19
#define TFT_BROWN       0x9A60
#define TFT_GOLD        0xFEA0
#define TFT_SILVER      0xC618
#define TFT_SKYBLUE     0x867D
#define TFT_VIOLET      0x915C

// ---- Text datums (drawString alignment) ----
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

class TFT_eSPI {
public:
    TFT_eSPI(int16_t w = 240, int16_t h = 320);
    virtual ~TFT_eSPI() {}

    void    init(uint8_t = 0);
    void    begin(uint8_t = 0);
    void    setRotation(uint8_t r);
    uint8_t getRotation() const { return _rotation; }
    int16_t width()  const { return _w; }
    int16_t height() const { return _h; }

    // Drawing primitives.
    void fillScreen(uint16_t color);
    virtual void drawPixel(int32_t x, int32_t y, uint16_t color);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color);
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
    void drawCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color);
    void fillCircle(int32_t x0, int32_t y0, int32_t r, uint16_t color);
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data);
    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b);

    // Text.
    void setCursor(int32_t x, int32_t y);
    void setCursor(int32_t x, int32_t y, uint8_t font);
    void setTextColor(uint16_t c);
    void setTextColor(uint16_t fg, uint16_t bg);
    void setTextSize(uint8_t s);
    void setTextFont(uint8_t) {}
    void setTextDatum(uint8_t d) { _datum = d; }
    void setTextWrap(bool w, bool = false) { _wrap = w; }
    int16_t textWidth(const char* s) const;
    int16_t fontHeight() const { return 8 * _textsize; }
    size_t  write(uint8_t c);
    size_t  print(const char* s);
    size_t  print(char c);
    size_t  print(int v);
    size_t  print(unsigned v);
    size_t  println(const char* s = "");
    size_t  println(int v);
    int16_t drawString(const char* s, int32_t x, int32_t y);
    int16_t drawString(const char* s, int32_t x, int32_t y, uint8_t font);
    int16_t drawCentreString(const char* s, int32_t x, int32_t y, uint8_t font);
    int16_t drawRightString(const char* s, int32_t x, int32_t y, uint8_t font);

    // Resistive touch (XPT2046 on the CYD). Reads the sim touch bus; returns
    // non-zero and fills x,y (screen coords) while a tap is held.
    uint8_t getTouch(uint16_t* x, uint16_t* y, uint16_t threshold = 600);
    void    setTouch(uint16_t*) {}
    void    calibrateTouch(uint16_t*, uint32_t, uint32_t, uint8_t) {}

protected:
    // A draw target: the shared framebuffer (default) or a sprite's own buffer.
    uint16_t* _buf = nullptr;      // null = draw to sim_framebuffer()
    int16_t   _bw = 0, _bh = 0;    // sprite buffer dimensions
    int16_t   _w, _h;              // logical width/height (after rotation)
    uint8_t   _rotation = 0;
    int32_t   _cx = 0, _cy = 0;    // text cursor
    uint16_t  _fg = TFT_WHITE, _bg = TFT_BLACK;
    bool      _opaque = false;     // draw a text background
    uint8_t   _textsize = 1;
    uint8_t   _datum = TL_DATUM;
    bool      _wrap = true;

    void putpx(int32_t x, int32_t y, uint16_t color);   // to the active target
    void drawChar(int32_t x, int32_t y, char c, uint16_t fg, uint16_t bg, uint8_t size);
};

// Off-screen sprite. Draws into its own RGB565 buffer; pushSprite blits it to
// the parent display. Inherits every primitive - they target the sprite buffer.
class TFT_eSprite : public TFT_eSPI {
public:
    explicit TFT_eSprite(TFT_eSPI* parent);
    void* createSprite(int16_t w, int16_t h);
    void  deleteSprite();
    void  pushSprite(int32_t x, int32_t y);
    void  fillSprite(uint16_t color) { fillScreen(color); }

private:
    TFT_eSPI*             _parent;
    std::vector<uint16_t> _pixels;
};
