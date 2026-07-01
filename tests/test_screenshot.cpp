#include "doctest.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "framebuffer.h"
#include "screenshot.h"

TEST_CASE("screenshot writes a decodable PNG matching the framebuffer") {
    Framebuffer& fb = sim_framebuffer();
    fb.init(480, 480);
    fb.fill(0xF800);                       // red
    REQUIRE(sim_screenshot_png("/tmp/esp32sim_shot.png"));

    int w, h, ch;
    unsigned char* img = stbi_load("/tmp/esp32sim_shot.png", &w, &h, &ch, 3);
    REQUIRE(img != nullptr);
    CHECK(w == 480);
    CHECK(h == 480);
    // RGB565 0xF800 -> R ~248, G 0, B 0.
    CHECK(img[0] >= 240);
    CHECK(img[1] == 0);
    CHECK(img[2] == 0);
    stbi_image_free(img);
}
