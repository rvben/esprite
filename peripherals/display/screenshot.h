#pragma once

// Encode the active framebuffer (RGB565) to a PNG (RGB888). Returns false if the
// framebuffer is unsized or the write fails.
bool sim_screenshot_png(const char* path);
