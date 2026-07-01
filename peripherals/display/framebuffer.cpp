#include "framebuffer.h"
#include <vector>

static std::vector<uint16_t> g_pixels;

void Framebuffer::init(int w, int h) {
    w_ = w; h_ = h;
    g_pixels.assign((size_t)w * h, 0);
}

uint16_t* Framebuffer::data() { return g_pixels.data(); }
const uint16_t* Framebuffer::data() const { return g_pixels.data(); }

uint16_t Framebuffer::pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
    return g_pixels[(size_t)y * w_ + x];
}

void Framebuffer::fill(uint16_t color) {
    for (auto& p : g_pixels) p = color;
}

void Framebuffer::blit(int x, int y, int bw, int bh, const uint16_t* px) {
    for (int row = 0; row < bh; ++row) {
        int py = y + row;
        if (py < 0 || py >= h_) continue;
        for (int col = 0; col < bw; ++col) {
            int pxc = x + col;
            if (pxc < 0 || pxc >= w_) continue;
            g_pixels[(size_t)py * w_ + pxc] = px[(size_t)row * bw + col];
        }
    }
}

Framebuffer& sim_framebuffer() {
    static Framebuffer fb;
    return fb;
}
