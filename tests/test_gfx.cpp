#include "doctest.h"
#include "Arduino_GFX_Library.h"
#include "framebuffer.h"

TEST_CASE("Arduino_GFX primitives render into the framebuffer") {
    Arduino_GFX gfx(320, 240);
    REQUIRE(gfx.begin());
    CHECK(sim_framebuffer().w() == 320);
    CHECK(sim_framebuffer().h() == 240);

    gfx.fillScreen(BLUE);
    CHECK(sim_framebuffer().pixel(0, 0) == BLUE);

    gfx.fillRect(10, 10, 20, 20, RED);
    CHECK(sim_framebuffer().pixel(15, 15) == RED);
    CHECK(sim_framebuffer().pixel(5, 5) == BLUE);      // outside the rect

    gfx.fillCircle(160, 120, 10, GREEN);
    CHECK(sim_framebuffer().pixel(160, 120) == GREEN);
    CHECK(sim_framebuffer().pixel(160, 140) == BLUE);  // outside the circle
}
