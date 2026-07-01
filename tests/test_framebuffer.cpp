#include "doctest.h"
#include "framebuffer.h"

TEST_CASE("fill and clipped blit land in the framebuffer") {
    Framebuffer& fb = sim_framebuffer();
    fb.init(480, 480);
    fb.fill(0xF800);                       // red
    CHECK(fb.pixel(0, 0) == 0xF800);
    CHECK(fb.pixel(479, 479) == 0xF800);

    uint16_t patch[4] = {0x001F, 0x001F, 0x001F, 0x001F};  // 2x2 blue
    fb.blit(10, 10, 2, 2, patch);
    CHECK(fb.pixel(10, 10) == 0x001F);
    CHECK(fb.pixel(11, 11) == 0x001F);
    CHECK(fb.pixel(12, 12) == 0xF800);     // outside the patch stays red
}

TEST_CASE("blit clips at the edges without overflow") {
    Framebuffer& fb = sim_framebuffer();
    fb.init(4, 4);
    fb.fill(0);
    uint16_t patch[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    fb.blit(3, 3, 2, 2, patch);            // only (3,3) is in bounds
    CHECK(fb.pixel(3, 3) == 0xFFFF);
    CHECK(fb.pixel(0, 0) == 0);
}
