#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Decodes a binary P6 PPM (the format QMP screendump writes by default) into
// an RGB565 pixel buffer sized for sim_framebuffer(). Pure - no I/O - so the
// parser is unit-testable without a QEMU child. Returns false and fills *err
// on malformed input: wrong magic, missing header fields, maxval other than
// 255, dimensions outside 1..4096, or truncated pixel data.
bool ppm_decode_rgb565(const std::string& ppm, int* w, int* h,
                       std::vector<uint16_t>* px, std::string* err);
