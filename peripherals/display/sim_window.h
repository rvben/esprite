#pragma once
#include "target.h"   // SimButton

// Optional live device window (SDL2). Presents the sim framebuffer in a native
// window and feeds mouse/keyboard input into the sim bus, so the device is
// interactive. The physical buttons are described by the target (BoardDesc), so
// the bezel is device-accurate: a target with no buttons gets just the screen.
// Only compiled when SDL2 is found; the CLI guards use of these on HAVE_SDL2.

struct SimWindow;

// Open a window of w x h device pixels, scaled by `scale`, with `button_count`
// side buttons drawn in a bezel (0 = screen only). Returns null on failure.
SimWindow* sim_window_open(const char* title, int w, int h, int scale,
                           const SimButton* buttons, int button_count);

// Present the current framebuffer and pump input events into the sim bus.
// Returns false when the user closes the window (or presses Escape).
bool sim_window_tick(SimWindow* win);

// Request a PNG of the FULL window (device screen + button bezel), written on
// the next tick. No-op if win is null.
void sim_window_request_capture(SimWindow* win, const char* path);

void sim_window_close(SimWindow* win);
