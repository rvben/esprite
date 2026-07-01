#pragma once
#include <cstdint>

// Virtual display: an RGB565 framebuffer any target's display adapter writes
// into and the screenshot encoder reads from. One active framebuffer per
// process (single active target). Sized by whoever calls init().
class Framebuffer {
public:
    void init(int w, int h);
    int  w() const { return w_; }
    int  h() const { return h_; }
    uint16_t*       data();
    const uint16_t* data() const;
    uint16_t pixel(int x, int y) const;

    void fill(uint16_t color565);
    // Blit a bw x bh RGB565 bitmap at (x, y), clipped to the framebuffer.
    void blit(int x, int y, int bw, int bh, const uint16_t* px);

private:
    int w_ = 0, h_ = 0;
};

Framebuffer& sim_framebuffer();
