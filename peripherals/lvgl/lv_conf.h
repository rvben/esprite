#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// Sim LVGL config. Mirrors the depth/features onboarded targets need and enables
// snapshot (host has RAM). Anything not set here takes LVGL's internal default,
// which already enables the common widgets and the software renderer.

#define LV_COLOR_DEPTH 16
#define LV_USE_LOG 0

// Enable snapshot so on-device screenshot code paths also work under the sim.
#define LV_USE_SNAPSHOT 1
#define LV_USE_CANVAS 1

// A default font must be compiled in; targets set their own per-widget fonts.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif // LV_CONF_H
