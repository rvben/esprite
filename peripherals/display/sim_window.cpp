#include "sim_window.h"
#include "framebuffer.h"
#include "sim_input.h"

#define SDL_MAIN_HANDLED   // this app owns main(); do not let SDL redefine it
#include <SDL.h>
#include "stb_image_write.h"   // implementation lives in screenshot.cpp
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

// GPIO shim (shims/arduino); resolved at final link.
int  sim_gpio_get(int pin);
void sim_gpio_set(int pin, int level);

static const int MAX_BTN  = 8;
static const int CHROME_W = 104;   // right bezel width when there are buttons
static const int STRIP_H  = 56;    // bottom control strip height when needed

struct WinBtn { SDL_Rect r; const SimButton* def; };

struct SimWindow {
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture  = nullptr;
    int  w = 0, h = 0, scale = 1, chrome = 0, strip = 0;
    bool vsync = false;
    bool touching = false;
    int  mouse_held = -1;      // physical button index the mouse is holding, or -1
    bool key_held[MAX_BTN]{};  // per-button keyboard hold state (a button can be held by both)
    int  pwr_flash = 0;
    int  nbtn = 0;
    WinBtn btns[MAX_BTN];
    // Hardware control strip.
    bool has_battery = false, has_rotation = false, bat_dragging = false;
    SDL_Rect bat_bar{}, chg_btn{}, usb_btn{}, rot_btn{};
    std::string capture_path;
};

// ---- 5x7 font (A-Z, 0-9, %) for labels; bit 4 = leftmost ----
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
static bool in_rect(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
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

static void set_touch(SimWindow* win, int wx, int wy) {
    float lx = 0, ly = 0;
    SDL_RenderWindowToLogical(win->renderer, wx, wy, &lx, &ly);
    int x = (int)lx, y = (int)ly;
    if (x < 0) x = 0; else if (x >= win->w) x = win->w - 1;
    if (y < 0) y = 0; else if (y >= win->h) y = win->h - 1;
    sim_input().touch_x = x;
    sim_input().touch_y = y;
}
static void set_battery_from_x(SimWindow* win, int mx) {
    int span = win->bat_bar.w - 4;
    int pct = span > 0 ? (mx - (win->bat_bar.x + 2)) * 100 / span : 0;
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    sim_input().battery_pct = pct;
}

static void press_action(SimWindow* win, const SimButton* b) {
    switch (b->action) {
    case ACT_PRIMARY:   sim_input().button[0] = true; break;
    case ACT_SECONDARY: sim_input().button[1] = true; break;
    case ACT_GPIO:      sim_gpio_set(b->gpio, 1); break;
    case ACT_PWR:       sim_input().pwr_events.push_back(1); win->pwr_flash = 10; break;
    }
}
static void release_action(const SimButton* b) {
    switch (b->action) {
    case ACT_PRIMARY:   sim_input().button[0] = false; break;
    case ACT_SECONDARY: sim_input().button[1] = false; break;
    case ACT_GPIO:      sim_gpio_set(b->gpio, 0); break;
    case ACT_PWR:       break;
    }
}
static bool is_active(SimWindow* win, const SimButton* b) {
    switch (b->action) {
    case ACT_PRIMARY:   return sim_input().button[0];
    case ACT_SECONDARY: return sim_input().button[1];
    case ACT_GPIO:      return sim_gpio_get(b->gpio) != 0;
    case ACT_PWR:       return win->pwr_flash > 0;
    }
    return false;
}

SimWindow* sim_window_open(const char* title, const BoardDesc* board, int scale) {
    if (!board) return nullptr;
    if (scale < 1) scale = 1;
    const int w = board->width, h = board->height;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sim_window: SDL_Init failed: %s\n", SDL_GetError());
        return nullptr;
    }
    SimWindow* win = new SimWindow();
    win->w = w; win->h = h; win->scale = scale;
    win->nbtn = board->button_count < 0 ? 0
              : (board->button_count > MAX_BTN ? MAX_BTN : board->button_count);
    win->chrome = win->nbtn > 0 ? CHROME_W : 0;
    win->has_battery  = board->has_battery;
    win->has_rotation = board->has_rotation;
    win->strip = (win->has_battery || win->has_rotation) ? STRIP_H : 0;

    win->window = SDL_CreateWindow(title ? title : "esp32-sim",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (w + win->chrome) * scale, (h + win->strip) * scale, SDL_WINDOW_ALLOW_HIGHDPI);
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
    SDL_RenderSetLogicalSize(win->renderer, w + win->chrome, h + win->strip);

    win->texture = SDL_CreateTexture(win->renderer, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!win->texture) {
        fprintf(stderr, "sim_window: SDL_CreateTexture failed: %s\n", SDL_GetError());
        sim_window_close(win); return nullptr;
    }

    // Buttons in the right bezel.
    if (win->nbtn > 0) {
        const int gap = 16, margin = 24;
        int bh = (h - 2 * margin - (win->nbtn - 1) * gap) / win->nbtn;
        if (bh > 48) bh = 48;
        int bw = win->chrome - 24, bx = w + 12;
        int total = win->nbtn * bh + (win->nbtn - 1) * gap;
        int by = (h - total) / 2;
        for (int i = 0; i < win->nbtn; ++i) {
            win->btns[i].def = &board->buttons[i];
            win->btns[i].r = SDL_Rect{bx, by + i * (bh + gap), bw, bh};
        }
    }

    // Hardware controls in the bottom strip, right-aligned toggles.
    if (win->strip > 0) {
        int y0 = h, ty = y0 + (STRIP_H - 34) / 2, rx = w - 12 - 56;
        if (win->has_rotation) { win->rot_btn = {rx, ty, 56, 34}; rx -= 64; }
        if (win->has_battery) {
            win->usb_btn = {rx, ty, 56, 34}; rx -= 64;
            win->chg_btn = {rx, ty, 56, 34};
            // Size the bar to fit before the toggles, leaving room for "100%".
            int bar_x = 12, text_room = text_w("100%", 2) + 16;
            int bar_w = win->chg_btn.x - text_room - bar_x;
            if (bar_w > 150) bar_w = 150;
            if (bar_w < 40)  bar_w = 40;
            win->bat_bar = {bar_x, y0 + (STRIP_H - 22) / 2, bar_w, 22};
        }
    }

    fprintf(stderr, "sim_window: %s", win->nbtn ? "buttons" : "no buttons");
    if (win->has_battery)  fprintf(stderr, ", battery/charge/USB");
    if (win->has_rotation) fprintf(stderr, ", rotate");
    fprintf(stderr, "  (mouse on screen = touch, Esc quits)\n");
    return win;
}

static void handle_strip_click(SimWindow* win, int mx, int my) {
    if (win->has_battery) {
        if (in_rect(win->bat_bar, mx, my)) { win->bat_dragging = true; set_battery_from_x(win, mx); return; }
        if (in_rect(win->chg_btn, mx, my)) { sim_input().charging = !sim_input().charging; return; }
        if (in_rect(win->usb_btn, mx, my)) { sim_input().vbus = !sim_input().vbus; return; }
    }
    if (win->has_rotation && in_rect(win->rot_btn, mx, my))
        sim_input().quadrant = (sim_input().quadrant + 1) % 4;
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
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(win->renderer, e.button.x, e.button.y, &lx, &ly);
                if (ly >= win->h) {
                    handle_strip_click(win, (int)lx, (int)ly);
                } else if (lx < win->w) {
                    win->touching = true;
                    sim_input().touch_pressed = true;
                    set_touch(win, e.button.x, e.button.y);
                } else {
                    for (int i = 0; i < win->nbtn; ++i)
                        if (in_rect(win->btns[i].r, (int)lx, (int)ly)) {
                            press_action(win, win->btns[i].def);  // idempotent for held state
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
                    if (!win->key_held[i]) release_action(win->btns[i].def);
                }
            }
            break;
        case SDL_MOUSEMOTION:
            if (win->touching) {
                set_touch(win, e.motion.x, e.motion.y);
            } else if (win->bat_dragging) {
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(win->renderer, e.motion.x, e.motion.y, &lx, &ly);
                set_battery_from_x(win, (int)lx);
            }
            break;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_ESCAPE) return false;
            if (!e.key.repeat)
                for (int i = 0; i < win->nbtn; ++i)
                    if (win->btns[i].def->key &&
                        e.key.keysym.sym == (SDL_Keycode)(unsigned char)win->btns[i].def->key) {
                        win->key_held[i] = true;
                        press_action(win, win->btns[i].def);  // idempotent for held state
                    }
            break;
        case SDL_KEYUP:
            for (int i = 0; i < win->nbtn; ++i)
                if (win->btns[i].def->key &&
                    e.key.keysym.sym == (SDL_Keycode)(unsigned char)win->btns[i].def->key) {
                    win->key_held[i] = false;
                    // Don't clear a button the mouse is still holding.
                    if (win->mouse_held != i) release_action(win->btns[i].def);
                }
            break;
        default: break;
        }
    }

    SDL_Renderer* r = win->renderer;
    SDL_UpdateTexture(win->texture, nullptr, sim_framebuffer().data(), win->w * 2);
    SDL_SetRenderDrawColor(r, 24, 25, 28, 255);
    SDL_RenderClear(r);
    SDL_Rect dev{0, 0, win->w, win->h};
    SDL_RenderCopy(r, win->texture, nullptr, &dev);

    if (win->pwr_flash > 0) --win->pwr_flash;
    for (int i = 0; i < win->nbtn; ++i)
        draw_labeled(r, win->btns[i].r, win->btns[i].def->label, is_active(win, win->btns[i].def), 3);

    if (win->strip > 0) {
        SDL_Rect strip{0, win->h, win->w + win->chrome, win->strip};
        SDL_SetRenderDrawColor(r, 18, 19, 22, 255);
        SDL_RenderFillRect(r, &strip);
        if (win->has_battery) {
            int pct = sim_input().battery_pct;
            if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
            SDL_SetRenderDrawColor(r, 120, 122, 130, 255);
            SDL_RenderDrawRect(r, &win->bat_bar);
            SDL_Rect fill{win->bat_bar.x + 2, win->bat_bar.y + 2,
                          (win->bat_bar.w - 4) * pct / 100, win->bat_bar.h - 4};
            if (pct <= 10)      SDL_SetRenderDrawColor(r, 200, 60, 50, 255);
            else if (pct <= 20) SDL_SetRenderDrawColor(r, 210, 150, 40, 255);
            else                SDL_SetRenderDrawColor(r, 90, 170, 90, 255);
            SDL_RenderFillRect(r, &fill);
            char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
            SDL_SetRenderDrawColor(r, 220, 220, 228, 255);
            draw_text(r, win->bat_bar.x + win->bat_bar.w + 10, win->bat_bar.y + 4, buf, 2);
            draw_labeled(r, win->chg_btn, "CHG", sim_input().charging, 2);
            draw_labeled(r, win->usb_btn, "USB", sim_input().vbus, 2);
        }
        if (win->has_rotation) {
            char rl[4]; snprintf(rl, sizeof(rl), "R%d", sim_input().quadrant);
            draw_labeled(r, win->rot_btn, rl, false, 2);
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
