#pragma once

// Optional live device window (SDL2). Presents the sim framebuffer in a native
// window and feeds mouse/keyboard input into the sim_input bus, so the device is
// interactive like a hardware simulator. Only compiled when SDL2 is found; the
// CLI guards use of these on HAVE_SDL2. The interface is opaque so translation
// units that do not have SDL2 headers can still include it.

struct SimWindow;

// Open a window of w x h device pixels, scaled by `scale`. Returns null if SDL
// or the window/renderer could not be created (for example, no display).
SimWindow* sim_window_open(const char* title, int w, int h, int scale);

// Present the current framebuffer and pump input events into sim_input().
// Returns false when the user closes the window (or presses Escape).
bool sim_window_tick(SimWindow* win);

// Request a PNG of the FULL window (device screen + button bezel), written on
// the next tick. This captures what the user sees, unlike the device-only
// screenshot. No-op if win is null.
void sim_window_request_capture(SimWindow* win, const char* path);

void sim_window_close(SimWindow* win);
