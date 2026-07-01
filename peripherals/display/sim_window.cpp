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

// A right-side bezel holds clickable buttons mirroring the device's physical
// side buttons, so they can be pushed with the mouse like a hardware simulator.
static const int CHROME_W = 104;

// kind: 0 = PRIMARY (held), 1 = SECONDARY (held), 2 = PWR (edge pulse).
struct Btn { SDL_Rect r; const char* label; int kind; };

struct SimWindow {
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture  = nullptr;
    int  w = 0, h = 0, scale = 1;
    bool vsync = false;
    bool touching = false;
    int  held_button = -1;     // 0/1 while a held button is pressed by the mouse
    int  pwr_flash = 0;        // frames left to highlight PWR after a pulse
    Btn  btns[3];
    std::string capture_path;  // pending full-window capture
};

// ---- tiny 5x7 font for the button labels (only the needed glyphs) ----
struct Glyph { char c; uint8_t rows[7]; };
static const Glyph FONT[] = {
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
};
static const uint8_t* glyph(char c) {
    for (const Glyph& g : FONT) if (g.c == c) return g.rows;
    return nullptr;
}
static int text_w(const char* s, int px) { return (int)strlen(s) * 6 * px; }
static void draw_text(SDL_Renderer* r, int x, int y, const char* s, int px) {
    int cx = x;
    for (const char* p = s; *p; ++p) {
        const uint8_t* g = glyph(*p);
        if (g) {
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g[row] & (1 << (4 - col))) {
                        SDL_Rect rr{cx + col * px, y + row * px, px, px};
                        SDL_RenderFillRect(r, &rr);
                    }
        }
        cx += 6 * px;
    }
}

static bool in_rect(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void draw_button(SDL_Renderer* r, const Btn& b, bool pressed) {
    if (pressed) SDL_SetRenderDrawColor(r, 92, 122, 92, 255);
    else         SDL_SetRenderDrawColor(r, 58, 60, 66, 255);
    SDL_RenderFillRect(r, &b.r);
    SDL_SetRenderDrawColor(r, 140, 142, 150, 255);
    SDL_RenderDrawRect(r, &b.r);
    const int px = 3;
    int tw = text_w(b.label, px), th = 7 * px;
    SDL_SetRenderDrawColor(r, 232, 232, 238, 255);
    draw_text(r, b.r.x + (b.r.w - tw) / 2, b.r.y + (b.r.h - th) / 2, b.label, px);
}

static void set_touch_from_window(SimWindow* win, int wx, int wy) {
    float lx = 0.0f, ly = 0.0f;
    SDL_RenderWindowToLogical(win->renderer, wx, wy, &lx, &ly);
    int x = (int)lx, y = (int)ly;
    if (x < 0) x = 0; else if (x >= win->w) x = win->w - 1;
    if (y < 0) y = 0; else if (y >= win->h) y = win->h - 1;
    sim_input().touch_x = x;
    sim_input().touch_y = y;
}

SimWindow* sim_window_open(const char* title, int w, int h, int scale) {
    if (scale < 1) scale = 1;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sim_window: SDL_Init failed: %s\n", SDL_GetError());
        return nullptr;
    }
    SimWindow* win = new SimWindow();
    win->w = w; win->h = h; win->scale = scale;

    win->window = SDL_CreateWindow(title ? title : "esp32-sim",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (w + CHROME_W) * scale, h * scale, SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win->window) {
        fprintf(stderr, "sim_window: SDL_CreateWindow failed: %s\n", SDL_GetError());
        sim_window_close(win);
        return nullptr;
    }

    win->renderer = SDL_CreateRenderer(win->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win->renderer) win->renderer = SDL_CreateRenderer(win->window, -1, 0);
    if (!win->renderer) {
        fprintf(stderr, "sim_window: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        sim_window_close(win);
        return nullptr;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(win->renderer, &info) == 0)
        win->vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    SDL_RenderSetLogicalSize(win->renderer, w + CHROME_W, h);

    win->texture = SDL_CreateTexture(win->renderer, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!win->texture) {
        fprintf(stderr, "sim_window: SDL_CreateTexture failed: %s\n", SDL_GetError());
        sim_window_close(win);
        return nullptr;
    }

    // Lay out three side buttons in the bezel, matching the physical device.
    const char* labels[3] = {"BOOT", "PWR", "KEY"};
    const int   kinds[3]  = {0, 2, 1};
    const int   bw = CHROME_W - 24, bh = 46, gap = 20;
    const int   bx = w + 12;
    const int   by = (h - (3 * bh + 2 * gap)) / 2;
    for (int i = 0; i < 3; ++i) {
        win->btns[i].r = SDL_Rect{bx, by + i * (bh + gap), bw, bh};
        win->btns[i].label = labels[i];
        win->btns[i].kind = kinds[i];
    }
    fprintf(stderr, "sim_window: side buttons - BOOT=PRIMARY, PWR, KEY=SECONDARY "
                    "(also keys: space/p/tab, Esc quits)\n");
    return win;
}

static void press_kind(SimWindow* win, int kind) {
    if (kind == 0)      { sim_input().button[0] = true; win->held_button = 0; }
    else if (kind == 1) { sim_input().button[1] = true; win->held_button = 1; }
    else                { sim_input().pwr_events.push_back(1); win->pwr_flash = 10; }
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
                if (lx < win->w) {
                    win->touching = true;
                    sim_input().touch_pressed = true;
                    set_touch_from_window(win, e.button.x, e.button.y);
                } else {
                    for (const Btn& b : win->btns)
                        if (in_rect(b.r, (int)lx, (int)ly)) { press_kind(win, b.kind); break; }
                }
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (win->touching) { win->touching = false; sim_input().touch_pressed = false; }
                if (win->held_button >= 0) {
                    sim_input().button[win->held_button] = false;
                    win->held_button = -1;
                }
            }
            break;
        case SDL_MOUSEMOTION:
            if (win->touching) set_touch_from_window(win, e.motion.x, e.motion.y);
            break;
        case SDL_KEYDOWN:
            if (!e.key.repeat) {
                switch (e.key.keysym.sym) {
                case SDLK_SPACE:  sim_input().button[0] = true; break;
                case SDLK_TAB:    sim_input().button[1] = true; break;
                case SDLK_p:      sim_input().pwr_events.push_back(1); win->pwr_flash = 10; break;
                case SDLK_ESCAPE: return false;
                default: break;
                }
            }
            break;
        case SDL_KEYUP:
            switch (e.key.keysym.sym) {
            case SDLK_SPACE: sim_input().button[0] = false; break;
            case SDLK_TAB:   sim_input().button[1] = false; break;
            default: break;
            }
            break;
        default: break;
        }
    }

    // Device screen (RGB565, native-endian; pitch = width * 2 bytes).
    SDL_UpdateTexture(win->texture, nullptr, sim_framebuffer().data(), win->w * 2);
    SDL_SetRenderDrawColor(win->renderer, 24, 25, 28, 255);   // bezel background
    SDL_RenderClear(win->renderer);
    SDL_Rect dev{0, 0, win->w, win->h};
    SDL_RenderCopy(win->renderer, win->texture, nullptr, &dev);

    if (win->pwr_flash > 0) --win->pwr_flash;
    for (const Btn& b : win->btns) {
        bool pressed = (b.kind == 0 && sim_input().button[0])
                    || (b.kind == 1 && sim_input().button[1])
                    || (b.kind == 2 && win->pwr_flash > 0);
        draw_button(win->renderer, b, pressed);
    }

    if (!win->capture_path.empty()) {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(win->renderer, &ow, &oh);
        std::vector<uint8_t> rgb((size_t)ow * oh * 3);
        if (ow > 0 && oh > 0 &&
            SDL_RenderReadPixels(win->renderer, nullptr, SDL_PIXELFORMAT_RGB24,
                                 rgb.data(), ow * 3) == 0) {
            stbi_write_png(win->capture_path.c_str(), ow, oh, 3, rgb.data(), ow * 3);
        }
        win->capture_path.clear();
    }

    SDL_RenderPresent(win->renderer);
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
