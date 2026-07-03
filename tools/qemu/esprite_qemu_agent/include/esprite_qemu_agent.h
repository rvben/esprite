// esprite's guest-side input agent for QEMU-emulated firmware (tier 2).
//
// esprite injects input over the machine's second UART (wired by esprite to
// a unix-socket chardev). This component runs a small task that serves a
// one-line command protocol on UART1 and exposes the injected state to the
// firmware. The QEMU fork has no host-side input path (no GPIO or touch
// device models), so cooperating firmware reads THESE APIs instead of the
// gpio/touch drivers - measured on the fork's C3 model: gpio_get_level never
// reflects driven or pulled levels, so a transparent gpio route is
// impossible.
//
// Protocol v1 (one command per line, one reply line each):
//   ping                      -> "ok esprite-agent v1"
//   gpio <pin> <0|1>          -> "ok"   (sets the injected level; a falling
//                                        write also counts a button event)
//   pulse <pin> <level> <ms>  -> "ok"   (level for ms, then the opposite;
//                                        replies after the pulse completes,
//                                        counts one button event)
//   touch <x> <y>             -> "ok"   (press or drag-move)
//   release                   -> "ok"
//   tap <x> <y> <ms>          -> "ok"   (press held for ms guest-side, then
//                                        released; replies after release.
//                                        Live-state pollers - LVGL indev
//                                        drivers - need the hold to observe
//                                        the press; a host-paced press and
//                                        release can fall between two polls)
// Malformed input replies "err <reason>". The agent prints
// "esprite-agent v1" once on start.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts the agent task (UART1 line server). Call once after boot.
void esprite_agent_start(void);

// Live injected touch state: true while a touch is held, filling x/y with
// panel coordinates. Poll from the firmware's input path (e.g. an LVGL
// indev read callback).
bool esprite_agent_touch(int* x, int* y);

// Monotonic count of `touch` commands, filling x/y with the most recent
// coordinates. A host tap (press immediately followed by release) can come
// and go between two live polls; the counter never misses it. Poll the
// delta, mirror of esprite_agent_gpio_events.
int esprite_agent_touch_events(int* x, int* y);

// Latest injected level for a pin (0..63), or default_level if that pin was
// never injected. Replaces gpio_get_level for agent-cooperating firmware.
int esprite_agent_gpio_level(int pin, int default_level);

// Monotonic count of completed button events on a pin: one per `pulse` and
// one per falling `gpio <pin> 0` write. Poll the delta instead of edge
// detection on levels - a short pulse can complete while the firmware is
// blocked in a frame draw, and the count never misses it.
int esprite_agent_gpio_events(int pin);

#ifdef __cplusplus
}
#endif
