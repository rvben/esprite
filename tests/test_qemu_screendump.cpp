#include "doctest.h"
#include "screendump.h"
#include <string>
#include <vector>

// Builds a P6 PPM in memory: header then w*h RGB24 pixels, all set to (r,g,b).
static std::string make_ppm(int w, int h, unsigned char r, unsigned char g, unsigned char b,
                            const char* header_extra = "") {
    std::string s = "P6\n" + std::string(header_extra) +
                    std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    for (int i = 0; i < w * h; i++) { s += (char)r; s += (char)g; s += (char)b; }
    return s;
}

TEST_CASE("ppm_decode_rgb565 decodes a solid red image") {
    int w = 0, h = 0;
    std::vector<uint16_t> px;
    std::string err;
    CHECK(ppm_decode_rgb565(make_ppm(4, 2, 255, 0, 0), &w, &h, &px, &err));
    CHECK(w == 4);
    CHECK(h == 2);
    REQUIRE(px.size() == 8);
    CHECK(px[0] == 0xF800);
    CHECK(px[7] == 0xF800);
}

TEST_CASE("ppm_decode_rgb565 converts mixed channels") {
    int w, h;
    std::vector<uint16_t> px;
    std::string err;
    // (255,255,255) -> 0xFFFF; (0,255,0) -> 0x07E0; (0,0,255) -> 0x001F
    CHECK(ppm_decode_rgb565(make_ppm(1, 1, 255, 255, 255), &w, &h, &px, &err));
    CHECK(px[0] == 0xFFFF);
    CHECK(ppm_decode_rgb565(make_ppm(1, 1, 0, 255, 0), &w, &h, &px, &err));
    CHECK(px[0] == 0x07E0);
    CHECK(ppm_decode_rgb565(make_ppm(1, 1, 0, 0, 255), &w, &h, &px, &err));
    CHECK(px[0] == 0x001F);
}

TEST_CASE("ppm_decode_rgb565 accepts header comments") {
    int w, h;
    std::vector<uint16_t> px;
    std::string err;
    CHECK(ppm_decode_rgb565(make_ppm(2, 2, 9, 9, 9, "# made by qemu\n"), &w, &h, &px, &err));
    CHECK(w == 2);
}

TEST_CASE("ppm_decode_rgb565 rejects malformed input") {
    int w, h;
    std::vector<uint16_t> px;
    std::string err;
    CHECK_FALSE(ppm_decode_rgb565("P5\n1 1\n255\nxxx", &w, &h, &px, &err));   // wrong magic
    CHECK(!err.empty());
    CHECK_FALSE(ppm_decode_rgb565("P6\n1 1\n65535\n" + std::string(6, 'x'), &w, &h, &px, &err));  // 16-bit maxval
    std::string truncated = make_ppm(4, 4, 1, 2, 3);
    truncated.resize(truncated.size() - 5);
    CHECK_FALSE(ppm_decode_rgb565(truncated, &w, &h, &px, &err));             // short pixel data
    CHECK_FALSE(ppm_decode_rgb565("P6\n99999 99999\n255\n", &w, &h, &px, &err));  // absurd dims
    CHECK_FALSE(ppm_decode_rgb565("", &w, &h, &px, &err));                    // empty
}
