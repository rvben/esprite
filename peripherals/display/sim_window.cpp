#include "sim_window.h"
#include "window_layout.h"
#include "framebuffer.h"
#include "sim_input.h"

#define SDL_MAIN_HANDLED   // this app owns main(); do not let SDL redefine it
#include <SDL.h>
#include "stb_image_write.h"   // implementation lives in screenshot.cpp
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

// GPIO shim (shims/arduino); resolved at final link.
int  sim_gpio_get(int pin);
void sim_gpio_set(int pin, int level);

// Device-bezel presentation: window_layout.h owns every rect (in window
// points); this file only turns those rects into drawable pixels (scaled by
// the HiDPI ratio) and turns mouse events back into layout hit tests. Mirrors
// window_layout.h's own truncation cap so the held-state arrays below size
// correctly.
static const int MAX_BTN = MAX_LAYOUT_BUTTONS;

struct SimWindow {
    SDL_Window*      window   = nullptr;
    SDL_Renderer*    renderer = nullptr;
    SDL_Texture*     texture  = nullptr;
    const BoardDesc* board    = nullptr;
    WindowLayout     layout{};
    int  scale = 1;
    bool vsync = false;
    bool touching = false;
    int  mouse_held = -1;      // physical button index the mouse is holding, or -1
    bool key_held[MAX_BTN]{};  // per-button keyboard hold state (a button can be held by both)
    int  pwr_flash = 0;
    // PWR hold state: short click injects on release; holding past the AXP
    // long-press threshold injects the long edge, then release its edge.
    uint32_t pwr_down_at = 0;
    bool     pwr_long_sent = false;
    bool has_battery = false, has_rotation = false, bat_dragging = false;
    // Overlay state: at most one of these is open at a time (opening one
    // closes the other); Esc closes whichever is open before it ever quits.
    bool help_open = false, panel_open = false;
    // Last known mouse position in window points, tracked from motion events
    // (rather than SDL_GetMouseState) so a harness driving the window via
    // pushed SDL_MOUSEMOTION events exercises the exact same hover path a
    // real user would.
    int  mouse_x = -1, mouse_y = -1;
    std::string capture_path;
};

// ---- 5x7 font (A-Z, 0-9, % ( ) : . ` ?) for chrome text; bit 4 = leftmost ----
struct Glyph { char c; uint8_t rows[7]; };
static const Glyph FONT[] = {
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3',{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W',{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},
    // Added for the device-bezel chrome: tooltips ("LABEL (KEY)"), the help
    // card ("PWR: HOLD 1.5S...", "` HARDWARE CONTROLS", "? HELP").
    {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {':',{0x00,0x00,0x04,0x00,0x04,0x00,0x00}}, {'.',{0x00,0x00,0x00,0x00,0x00,0x00,0x04}},
    {'`',{0x08,0x04,0x00,0x00,0x00,0x00,0x00}}, {'?',{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
};
static const uint8_t* glyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (const Glyph& g : FONT) if (g.c == c) return g.rows;
    return nullptr;
}
static int text_w(const char* s, int px) { return (int)strlen(s) * 6 * px; }
static void draw_text(SDL_Renderer* r, int x, int y, const char* s, int px) {
    int cx = x;
    for (const char* p = s; *p; ++p) {
        const uint8_t* g = glyph(*p);
        if (g)
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g[row] & (1 << (4 - col))) {
                        SDL_Rect rr{cx + col * px, y + row * px, px, px};
                        SDL_RenderFillRect(r, &rr);
                    }
        cx += 6 * px;
    }
}

static void draw_labeled(SDL_Renderer* r, const SDL_Rect& rect, const char* label,
                         bool on, int px) {
    if (on) SDL_SetRenderDrawColor(r, 92, 122, 92, 255);
    else    SDL_SetRenderDrawColor(r, 58, 60, 66, 255);
    SDL_RenderFillRect(r, &rect);
    SDL_SetRenderDrawColor(r, 140, 142, 150, 255);
    SDL_RenderDrawRect(r, &rect);
    int tw = text_w(label, px), th = 7 * px;
    SDL_SetRenderDrawColor(r, 232, 232, 238, 255);
    draw_text(r, rect.x + (rect.w - tw) / 2, rect.y + (rect.h - th) / 2, label, px);
}

// A short, readable name for a button's keyboard shortcut; the raw char is
// often not printable chrome text (space, tab).
static std::string key_name(char k) {
    if (!k) return "";
    if (k == ' ')  return "SPACE";
    if (k == '\t') return "TAB";
    char buf[2] = {(char)toupper((unsigned char)k), 0};
    return std::string(buf);
}

// ---- Layout points -> drawable pixels. Everything in window_layout.h lives
// in window points; the renderer only ever multiplies by dpr right here, at
// the moment of drawing or hit-testing against a raw event coordinate. ----
static SDL_Rect to_px(const WinRect& r, float dpr) {
    int x0 = (int)std::lround(r.x * dpr);
    int y0 = (int)std::lround(r.y * dpr);
    int x1 = (int)std::lround((r.x + r.w) * dpr);
    int y1 = (int)std::lround((r.y + r.h) * dpr);
    return SDL_Rect{x0, y0, x1 - x0, y1 - y0};
}
static int scale_len(int v, float dpr) { return std::max(1, (int)std::lround(v * dpr)); }

static void fill_circle(SDL_Renderer* r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = (int)std::lround(std::sqrt((double)radius * radius - (double)dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// Fills `rect` with `fill`, then rounds its four corners to `radius` by
// painting each corner box `bg` and restoring an inward circle of `fill` -
// the "10 px corner notch" the plan calls for, without new SDL geometry APIs.
static void draw_rounded_rect(SDL_Renderer* r, const SDL_Rect& rect, int radius,
                               SDL_Color fill, SDL_Color bg) {
    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(r, &rect);
    if (radius <= 0 || radius * 2 > rect.w || radius * 2 > rect.h) return;
    SDL_Rect boxes[4] = {
        {rect.x, rect.y, radius, radius},
        {rect.x + rect.w - radius, rect.y, radius, radius},
        {rect.x, rect.y + rect.h - radius, radius, radius},
        {rect.x + rect.w - radius, rect.y + rect.h - radius, radius, radius},
    };
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    for (auto& b : boxes) SDL_RenderFillRect(r, &b);
    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
    int cxs[4] = {rect.x + radius, rect.x + rect.w - radius, rect.x + radius, rect.x + rect.w - radius};
    int cys[4] = {rect.y + radius, rect.y + radius, rect.y + rect.h - radius, rect.y + rect.h - radius};
    for (int i = 0; i < 4; ++i) fill_circle(r, cxs[i], cys[i], radius);
}

static void draw_outline(SDL_Renderer* r, const SDL_Rect& rect, int thickness, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int t = 0; t < thickness; ++t) {
        SDL_Rect rr{rect.x + t, rect.y + t, rect.w - 2 * t, rect.h - 2 * t};
        if (rr.w <= 0 || rr.h <= 0) break;
        SDL_RenderDrawRect(r, &rr);
    }
}

// ---- Design-value palette (docs/plans/2026-07-02-window-bezel.md) ----
static const SDL_Color kBezelFill    = {26, 27, 30, 255};    // #1a1b1e
static const SDL_Color kBezelOutline = {58, 60, 66, 255};    // #3a3c42
static const SDL_Color kGutter       = {0, 0, 0, 255};       // #000
static const SDL_Color kNubIdle      = {44, 46, 51, 255};    // #2c2e33
static const SDL_Color kNubHover     = {74, 76, 82, 255};    // #4a4c52
static const SDL_Color kNubActive    = {92, 122, 92, 255};   // #5c7a5c
static const SDL_Color kNubOutline   = {106, 109, 117, 255}; // #6a6d75 (brighter than idle fill, so nubs read as controls without hover)
static const SDL_Color kTooltipBg    = {16, 17, 20, 255};    // #101114
static const SDL_Color kTooltipText  = {232, 232, 238, 255}; // #e8e8ee

static void set_touch(SimWindow* win, int mx, int my) {
    const WinRect& s = win->layout.screen;
    int fx = win->scale > 0 ? (mx - s.x) / win->scale : 0;
    int fy = win->scale > 0 ? (my - s.y) / win->scale : 0;
    if (fx < 0) fx = 0; else if (fx >= win->board->width)  fx = win->board->width - 1;
    if (fy < 0) fy = 0; else if (fy >= win->board->height) fy = win->board->height - 1;
    sim_input().touch_x = fx;
    sim_input().touch_y = fy;
}
static void set_battery_from_x(SimWindow* win, int mx) {
    PanelLayout p = layout_panel(win->layout, win->has_battery, win->has_rotation);
    int span = p.bat_bar.w - 4;
    int pct = span > 0 ? (mx - (p.bat_bar.x + 2)) * 100 / span : 0;
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    sim_input().battery_pct = pct;
}

// Matches the AXP PMU's long-press threshold: holding PWR this long emits the
// long edge (2); the eventual release then emits the release edge (3). A
// shorter hold emits one short-press event (1) on release, like the hardware.
static const uint32_t PWR_LONG_MS = 1500;

static bool pwr_is_held(SimWindow* win) {
    for (int i = 0; i < win->layout.nub_count; ++i)
        if (win->board->buttons[i].action == ACT_PWR &&
            (win->mouse_held == i || win->key_held[i]))
            return true;
    return false;
}

static void press_action(SimWindow* win, const SimButton* b) {
    switch (b->action) {
    case ACT_PRIMARY:   sim_input().button[0] = true; break;
    case ACT_SECONDARY: sim_input().button[1] = true; break;
    case ACT_GPIO:      sim_gpio_set(b->gpio, 1); break;
    case ACT_PWR:
        win->pwr_down_at = SDL_GetTicks();
        win->pwr_long_sent = false;
        win->pwr_flash = 10;
        break;
    }
}
static void release_action(SimWindow* win, const SimButton* b) {
    switch (b->action) {
    case ACT_PRIMARY:   sim_input().button[0] = false; break;
    case ACT_SECONDARY: sim_input().button[1] = false; break;
    case ACT_GPIO:      sim_gpio_set(b->gpio, 0); break;
    case ACT_PWR:
        sim_input().pwr_events.push_back(win->pwr_long_sent ? 3 : 1);
        break;
    }
}
static bool is_active(SimWindow* win, const SimButton* b) {
    switch (b->action) {
    case ACT_PRIMARY:   return sim_input().button[0];
    case ACT_SECONDARY: return sim_input().button[1];
    case ACT_GPIO:      return sim_gpio_get(b->gpio) != 0;
    case ACT_PWR:       return win->pwr_flash > 0 || pwr_is_held(win);
    }
    return false;
}

SimWindow* sim_window_open(const char* title, const BoardDesc* board, int scale) {
    if (!board) return nullptr;
    if (scale < 1) scale = 1;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sim_window: SDL_Init failed: %s\n", SDL_GetError());
        return nullptr;
    }
    SimWindow* win = new SimWindow();
    win->board = board;
    win->scale = scale;
    win->layout = window_layout(board, scale);
    win->has_battery  = board->has_battery;
    win->has_rotation = board->has_rotation;

    win->window = SDL_CreateWindow(title ? title : "esprite",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win->layout.window.w, win->layout.window.h, SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win->window) {
        fprintf(stderr, "sim_window: SDL_CreateWindow failed: %s\n", SDL_GetError());
        sim_window_close(win); return nullptr;
    }
    win->renderer = SDL_CreateRenderer(win->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win->renderer) win->renderer = SDL_CreateRenderer(win->window, -1, 0);
    if (!win->renderer) {
        fprintf(stderr, "sim_window: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        sim_window_close(win); return nullptr;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(win->renderer, &info) == 0)
        win->vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;

    win->texture = SDL_CreateTexture(win->renderer, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, board->width, board->height);
    if (!win->texture) {
        fprintf(stderr, "sim_window: SDL_CreateTexture failed: %s\n", SDL_GetError());
        sim_window_close(win); return nullptr;
    }

    fprintf(stderr, "sim_window: %s", win->layout.nub_count ? "buttons (hover for tooltips)" : "no buttons");
    if (win->has_battery)  fprintf(stderr, ", battery/charge/USB");
    if (win->has_rotation) fprintf(stderr, ", rotate");
    fprintf(stderr, "  (mouse on screen = touch, ? or h = help, ` = hardware controls, Esc quits)\n");
    return win;
}

bool sim_window_tick(SimWindow* win) {
    if (!win) return false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            return false;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                if (win_rect_contains(win->layout.more_nub, mx, my)) {
                    // The "..." nub always toggles the panel, whichever
                    // overlay (if any) is currently open.
                    win->panel_open = !win->panel_open;
                    win->help_open = false;
                } else if (win->help_open) {
                    // Any click dismisses the help overlay instead of also
                    // acting as touch/nub input on the dimmed device below.
                    win->help_open = false;
                } else if (win->panel_open) {
                    WinRect card = layout_panel_card(win->layout);
                    if (win_rect_contains(card, mx, my)) {
                        PanelLayout p = layout_panel(win->layout, win->has_battery, win->has_rotation);
                        if (win->has_battery && win_rect_contains(p.bat_bar, mx, my)) {
                            win->bat_dragging = true;
                            set_battery_from_x(win, mx);
                        } else if (win->has_battery && win_rect_contains(p.chg_btn, mx, my)) {
                            sim_input().charging = !sim_input().charging;
                        } else if (win->has_battery && win_rect_contains(p.usb_btn, mx, my)) {
                            sim_input().vbus = !sim_input().vbus;
                        } else if (win->has_rotation && win_rect_contains(p.rot_btn, mx, my)) {
                            sim_input().quadrant = (sim_input().quadrant + 1) % 4;
                        }
                    } else {
                        win->panel_open = false;   // click outside the panel closes it
                    }
                } else if (win_rect_contains(win->layout.screen, mx, my)) {
                    win->touching = true;
                    sim_input().touch_pressed = true;
                    set_touch(win, mx, my);
                } else {
                    for (int i = 0; i < win->layout.nub_count; ++i)
                        if (win_rect_contains(win->layout.nubs[i].hit, mx, my)) {
                            press_action(win, &win->board->buttons[i]);  // idempotent for held state
                            win->mouse_held = i;
                            break;
                        }
                }
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (win->touching) { win->touching = false; sim_input().touch_pressed = false; }
                win->bat_dragging = false;
                if (win->mouse_held >= 0) {
                    int i = win->mouse_held;
                    win->mouse_held = -1;
                    // Don't clear a button the keyboard is still holding.
                    if (!win->key_held[i]) release_action(win, &win->board->buttons[i]);
                }
            }
            break;
        case SDL_MOUSEMOTION:
            // Mouse coordinates arrive in window points (not drawable
            // pixels) even on a HiDPI display, matching layout coordinates
            // directly - no dpr division needed for hit-testing.
            win->mouse_x = e.motion.x;
            win->mouse_y = e.motion.y;
            if (win->touching) {
                set_touch(win, e.motion.x, e.motion.y);
            } else if (win->bat_dragging) {
                set_battery_from_x(win, e.motion.x);
            }
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                if (win->help_open)  { win->help_open = false; break; }
                if (win->panel_open) { win->panel_open = false; break; }
                return false;
            }
            if (!e.key.repeat && e.key.keysym.sym == SDLK_BACKQUOTE) {
                win->panel_open = !win->panel_open;
                win->help_open = false;
                break;
            }
            // '?' is shift+/ on a US layout; SDL reports the unshifted
            // keycode, so bind the physical / key as well as 'h'.
            if (!e.key.repeat && (e.key.keysym.sym == SDLK_SLASH || e.key.keysym.sym == SDLK_h)) {
                win->help_open = !win->help_open;
                win->panel_open = false;
                break;
            }
            if (!e.key.repeat)
                for (int i = 0; i < win->layout.nub_count; ++i)
                    if (win->board->buttons[i].key &&
                        e.key.keysym.sym == (SDL_Keycode)(unsigned char)win->board->buttons[i].key) {
                        win->key_held[i] = true;
                        press_action(win, &win->board->buttons[i]);  // idempotent for held state
                    }
            break;
        case SDL_KEYUP:
            for (int i = 0; i < win->layout.nub_count; ++i)
                if (win->board->buttons[i].key &&
                    e.key.keysym.sym == (SDL_Keycode)(unsigned char)win->board->buttons[i].key) {
                    win->key_held[i] = false;
                    // Don't clear a button the mouse is still holding.
                    if (win->mouse_held != i) release_action(win, &win->board->buttons[i]);
                }
            break;
        default: break;
        }
    }

    SDL_Renderer* r = win->renderer;
    const WindowLayout& l = win->layout;

    int win_w = 0, win_h = 0, draw_w = 0, draw_h = 0;
    SDL_GetWindowSize(win->window, &win_w, &win_h);
    SDL_GetRendererOutputSize(r, &draw_w, &draw_h);
    float dpr = win_w > 0 ? (float)draw_w / (float)win_w : 1.0f;

    if (win->pwr_flash > 0) --win->pwr_flash;
    // A PWR hold crossing the long-press threshold emits the long edge once.
    if (pwr_is_held(win) && !win->pwr_long_sent &&
        SDL_GetTicks() - win->pwr_down_at >= PWR_LONG_MS) {
        sim_input().pwr_events.push_back(2);
        win->pwr_long_sent = true;
    }

    int hover_nub = -1;
    for (int i = 0; i < l.nub_count; ++i)
        if (win_rect_contains(l.nubs[i].hit, win->mouse_x, win->mouse_y)) { hover_nub = i; break; }
    bool hover_more = win_rect_contains(l.more_nub, win->mouse_x, win->mouse_y);

    // Draw order: bezel + outline + corners, screen gutter, framebuffer,
    // nubs, the "..." nub, the hovered nub's tooltip, then any open overlay.
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    SDL_Rect win_px = to_px(l.window, dpr);
    draw_rounded_rect(r, win_px, scale_len(10, dpr), kBezelFill, kGutter);
    draw_outline(r, win_px, scale_len(1, dpr), kBezelOutline);

    WinRect gutter_pts{l.screen.x - 2, l.screen.y - 2, l.screen.w + 4, l.screen.h + 4};
    SDL_Rect gutter_px = to_px(gutter_pts, dpr);
    SDL_SetRenderDrawColor(r, kGutter.r, kGutter.g, kGutter.b, kGutter.a);
    SDL_RenderFillRect(r, &gutter_px);

    SDL_UpdateTexture(win->texture, nullptr, sim_framebuffer().data(), win->board->width * 2);
    SDL_Rect screen_px = to_px(l.screen, dpr);
    SDL_RenderCopy(r, win->texture, nullptr, &screen_px);

    for (int i = 0; i < l.nub_count; ++i) {
        bool act = is_active(win, &win->board->buttons[i]);
        bool hov = (hover_nub == i);
        SDL_Color fill = act ? kNubActive : (hov ? kNubHover : kNubIdle);
        SDL_Rect body_px = to_px(l.nubs[i].body, dpr);
        draw_rounded_rect(r, body_px, scale_len(3, dpr), fill, kBezelFill);
        draw_outline(r, body_px, scale_len(1, dpr), kNubOutline);
    }

    SDL_Rect more_px = to_px(l.more_nub, dpr);
    SDL_Color more_fill = win->panel_open ? kNubActive : (hover_more ? kNubHover : kNubIdle);
    draw_rounded_rect(r, more_px, scale_len(3, dpr), more_fill, kBezelFill);
    draw_outline(r, more_px, scale_len(1, dpr), kNubOutline);
    {
        SDL_SetRenderDrawColor(r, kTooltipText.r, kTooltipText.g, kTooltipText.b, kTooltipText.a);
        int dotr = std::max(1, scale_len(1, dpr));
        int cy = more_px.y + more_px.h / 2;
        for (int k = 1; k <= 3; ++k) {
            int cx = more_px.x + more_px.w * k / 4;
            SDL_Rect d{cx - dotr, cy - dotr, dotr * 2, dotr * 2};
            SDL_RenderFillRect(r, &d);
        }
    }

    if (hover_nub >= 0) {
        const NubLayout& n = l.nubs[hover_nub];
        const SimButton& b = win->board->buttons[hover_nub];
        std::string text = std::string(b.label) + " (" + key_name(b.key) + ")";
        int px = scale_len(2, dpr);
        int pad = scale_len(6, dpr);
        int gap = scale_len(4, dpr);
        int tw = text_w(text.c_str(), px) + 2 * pad;
        int th = 7 * px + 2 * pad;
        SDL_Rect body_px = to_px(n.body, dpr);
        int tx = 0, ty = 0;
        switch (n.edge) {
        case EDGE_RIGHT:  tx = body_px.x - tw - gap; ty = body_px.y + body_px.h / 2 - th / 2; break;
        case EDGE_LEFT:   tx = body_px.x + body_px.w + gap; ty = body_px.y + body_px.h / 2 - th / 2; break;
        case EDGE_TOP:    tx = body_px.x + body_px.w / 2 - tw / 2; ty = body_px.y + body_px.h + gap; break;
        case EDGE_BOTTOM: tx = body_px.x + body_px.w / 2 - tw / 2; ty = body_px.y - th - gap; break;
        }
        if (tx < win_px.x) tx = win_px.x;
        if (tx + tw > win_px.x + win_px.w) tx = win_px.x + win_px.w - tw;
        if (ty < win_px.y) ty = win_px.y;
        if (ty + th > win_px.y + win_px.h) ty = win_px.y + win_px.h - th;
        SDL_Rect bubble{tx, ty, tw, th};
        SDL_SetRenderDrawColor(r, kTooltipBg.r, kTooltipBg.g, kTooltipBg.b, kTooltipBg.a);
        SDL_RenderFillRect(r, &bubble);
        SDL_SetRenderDrawColor(r, kTooltipText.r, kTooltipText.g, kTooltipText.b, kTooltipText.a);
        draw_text(r, tx + pad, ty + pad, text.c_str(), px);
    }

    if (win->help_open) {
        // Build the help text first so the card can be sized to its content
        // (window_layout.h owns card geometry but has no notion of button
        // labels, key names, or line counts - only this renderer does).
        std::vector<std::string> lines;
        bool has_pwr = false;
        for (int i = 0; i < l.nub_count; ++i) {
            const SimButton& b = win->board->buttons[i];
            lines.push_back(std::string(b.label) + " (" + key_name(b.key) + ")");
            if (b.action == ACT_PWR) has_pwr = true;
        }
        if (has_pwr) lines.push_back("PWR: HOLD 1.5S FOR LONG PRESS");
        lines.push_back("` HARDWARE CONTROLS");
        lines.push_back("? HELP");
        lines.push_back("ESC QUIT");

        // Content extent in window points (nominal px=2, the scale every
        // line renders at) - layout_help_card adds HELP_CARD_PAD around this
        // and centers/clamps the result to the window.
        int content_w = 0;
        for (const std::string& line : lines) content_w = std::max(content_w, text_w(line.c_str(), 2));
        int line_h_pts = 7 * 2 + 6;
        int content_h = (int)lines.size() * line_h_pts;

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 153);   // 60% dim over everything
        SDL_RenderFillRect(r, &win_px);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        SDL_Rect card = to_px(layout_help_card(l, content_w, content_h), dpr);
        SDL_SetRenderDrawColor(r, kBezelFill.r, kBezelFill.g, kBezelFill.b, kBezelFill.a);
        SDL_RenderFillRect(r, &card);
        SDL_SetRenderDrawColor(r, kBezelOutline.r, kBezelOutline.g, kBezelOutline.b, kBezelOutline.a);
        SDL_RenderDrawRect(r, &card);

        int px = scale_len(2, dpr);
        int pad = scale_len(HELP_CARD_PAD, dpr);
        int line_h = 7 * px + scale_len(6, dpr);
        int ty = card.y + pad;
        SDL_SetRenderDrawColor(r, kTooltipText.r, kTooltipText.g, kTooltipText.b, kTooltipText.a);
        for (const std::string& line : lines) {
            draw_text(r, card.x + pad, ty, line.c_str(), px);
            ty += line_h;
        }
    } else if (win->panel_open) {
        SDL_Rect card = to_px(layout_panel_card(l), dpr);
        SDL_SetRenderDrawColor(r, kBezelFill.r, kBezelFill.g, kBezelFill.b, kBezelFill.a);
        SDL_RenderFillRect(r, &card);
        SDL_SetRenderDrawColor(r, kBezelOutline.r, kBezelOutline.g, kBezelOutline.b, kBezelOutline.a);
        SDL_RenderDrawRect(r, &card);

        PanelLayout p = layout_panel(l, win->has_battery, win->has_rotation);
        int px = scale_len(2, dpr);
        if (win->has_battery) {
            int pct = sim_input().battery_pct;
            if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
            SDL_Rect bar = to_px(p.bat_bar, dpr);
            SDL_SetRenderDrawColor(r, 120, 122, 130, 255);
            SDL_RenderDrawRect(r, &bar);
            int inset = scale_len(2, dpr);
            SDL_Rect fill{bar.x + inset, bar.y + inset, (bar.w - 2 * inset) * pct / 100, bar.h - 2 * inset};
            if (pct <= 10)      SDL_SetRenderDrawColor(r, 200, 60, 50, 255);
            else if (pct <= 20) SDL_SetRenderDrawColor(r, 210, 150, 40, 255);
            else                SDL_SetRenderDrawColor(r, 90, 170, 90, 255);
            SDL_RenderFillRect(r, &fill);
            char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
            SDL_SetRenderDrawColor(r, 220, 220, 228, 255);
            // Drawn inside PANEL_PCT_GAP (8 pt lead-in + text_w("100%", 2)),
            // the room layout_panel reserves between bat_bar and chg_btn so
            // this text has somewhere to live instead of being painted over.
            draw_text(r, bar.x + bar.w + scale_len(8, dpr), bar.y + scale_len(4, dpr), buf, px);
            draw_labeled(r, to_px(p.chg_btn, dpr), "CHG", sim_input().charging, px);
            draw_labeled(r, to_px(p.usb_btn, dpr), "USB", sim_input().vbus, px);
        }
        if (win->has_rotation) {
            char rl[4]; snprintf(rl, sizeof(rl), "R%d", sim_input().quadrant);
            draw_labeled(r, to_px(p.rot_btn, dpr), rl, false, px);
        }
    }

    if (!win->capture_path.empty()) {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(r, &ow, &oh);
        std::vector<uint8_t> rgb((size_t)ow * oh * 3);
        if (ow > 0 && oh > 0 &&
            SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGB24, rgb.data(), ow * 3) == 0)
            stbi_write_png(win->capture_path.c_str(), ow, oh, 3, rgb.data(), ow * 3);
        win->capture_path.clear();
    }

    SDL_RenderPresent(r);
    if (!win->vsync) SDL_Delay(15);
    return true;
}

void sim_window_request_capture(SimWindow* win, const char* path) {
    if (win && path) win->capture_path = path;
}

void sim_window_close(SimWindow* win) {
    if (!win) return;
    if (win->texture)  SDL_DestroyTexture(win->texture);
    if (win->renderer) SDL_DestroyRenderer(win->renderer);
    if (win->window)   SDL_DestroyWindow(win->window);
    delete win;
    SDL_Quit();
}
