#include "screenshot.h"
#include "framebuffer.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <vector>
#include <cstdint>

bool sim_screenshot_png(const char* path) {
    Framebuffer& fb = sim_framebuffer();
    int w = fb.w(), h = fb.h();
    if (w <= 0 || h <= 0) return false;
    const uint16_t* px = fb.data();
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int i = 0; i < w * h; ++i) {
        uint16_t p = px[i];
        uint8_t r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
        rgb[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        rgb[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        rgb[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
    return stbi_write_png(path, w, h, 3, rgb.data(), w * 3) != 0;
}
