#include "doctest.h"
#include "TFT_eSPI.h"
#include "framebuffer.h"
#include "sim_input.h"

static uint16_t fb_px(int x, int y) {
    return sim_framebuffer().data()[y * sim_framebuffer().w() + x];
}

TEST_CASE("drawing on a sprite without a buffer is a no-op, not a screen write") {
    // Real TFT_eSPI sprites draw nowhere before createSprite (and after
    // deleteSprite); the shim previously fell through to the main display.
    TFT_eSPI tft;
    tft.init();
    tft.fillScreen(TFT_BLACK);
    TFT_eSprite spr(&tft);
    spr.fillSprite(TFT_RED);
    spr.drawPixel(5, 5, TFT_GREEN);
    CHECK(fb_px(5, 5) == TFT_BLACK);

    spr.createSprite(8, 8);
    spr.deleteSprite();
    spr.fillSprite(TFT_RED);          // after delete: also a no-op
    CHECK(fb_px(5, 5) == TFT_BLACK);
}

TEST_CASE("createSprite rejects non-positive dimensions") {
    TFT_eSPI tft;
    TFT_eSprite spr(&tft);
    CHECK(spr.createSprite(-100, 10) == nullptr);
    CHECK(spr.createSprite(0, 10) == nullptr);
    CHECK(spr.createSprite(10, -1) == nullptr);
}

TEST_CASE("pushSprite blits through its parent, so nested sprites compose") {
    TFT_eSPI tft;
    tft.init();
    tft.fillScreen(TFT_BLACK);
    TFT_eSprite outer(&tft);
    outer.createSprite(20, 20);
    outer.fillSprite(TFT_BLUE);
    TFT_eSprite inner(&outer);
    inner.createSprite(4, 4);
    inner.fillSprite(TFT_RED);

    inner.pushSprite(2, 2);            // into outer's buffer, not the screen
    CHECK(fb_px(3, 3) == TFT_BLACK);

    outer.pushSprite(0, 0);            // outer (with inner composed) to screen
    CHECK(fb_px(3, 3) == TFT_RED);
    CHECK(fb_px(10, 10) == TFT_BLUE);
}

TEST_CASE("rotations 2 and 3 are the 180-degree mirrors of 0 and 1") {
    // Real panels: 0/2 are portrait and 1/3 landscape, each pair mounted 180
    // degrees apart. The shim previously treated 1 and 3 (and 0 and 2) as
    // identical, so a sketch flipping a mounted display rendered unflipped.
    TFT_eSPI tft;
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.drawPixel(5, 3, TFT_RED);
    CHECK(fb_px(5, 3) == TFT_RED);

    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    tft.drawPixel(5, 3, TFT_RED);
    CHECK(fb_px(tft.width() - 6, tft.height() - 4) == TFT_RED);
    CHECK(fb_px(5, 3) == TFT_BLACK);
}

TEST_CASE("getTouch reports logical coordinates under a flipped rotation") {
    TFT_eSPI tft;
    tft.setRotation(3);   // flipped landscape
    sim_input().touch_pressed = true;
    sim_input().touch_x = 10;
    sim_input().touch_y = 20;
    uint16_t x = 0, y = 0;
    REQUIRE(tft.getTouch(&x, &y) == 1);
    // The screen pixel at board (10,20) is logical (w-11, h-21) under 180 flip;
    // a sketch comparing getTouch against what it drew needs that same mapping.
    CHECK(x == tft.width() - 11);
    CHECK(y == tft.height() - 21);
    sim_input().touch_pressed = false;
}
