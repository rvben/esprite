#pragma once
#include "target.h"   // BoardDesc, SimButton

// Optional live device window (SDL2). Presents the sim framebuffer in a native
// window and feeds mouse/keyboard input into the sim bus, so the device is
// interactive. The device's controls come from its BoardDesc, so the window is
// device-accurate: a slim hardware-style bezel carries a clickable nub at each
// button's declared physical position (hover for a tooltip), and an extended
// panel (backtick, or the "..." nub) exposes battery/charge/USB/rotate for the
// capabilities the board actually has. Only compiled when SDL2 is found; the
// CLI guards use of these on HAVE_SDL2.

struct SimWindow;

// Open a live window for `board`, scaled by `scale`. Returns null on failure.
SimWindow* sim_window_open(const char* title, const BoardDesc* board, int scale);

// Present the current framebuffer and pump input events into the sim bus.
// Returns false when the user closes the window (or presses Escape).
bool sim_window_tick(SimWindow* win);

// Request a PNG of the FULL window (screen + bezel + nubs, and any open
// overlay), written on the next tick. No-op if win is null.
void sim_window_request_capture(SimWindow* win, const char* path);

void sim_window_close(SimWindow* win);
