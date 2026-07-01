#include "sim_window.h"
#include "framebuffer.h"
#include "sim_input.h"

#define SDL_MAIN_HANDLED   // this app owns main(); do not let SDL redefine it
#include <SDL.h>
#include <cstdio>

struct SimWindow {
    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture  = nullptr;
    int  w = 0, h = 0, scale = 1;
    bool touching = false;
    bool vsync = false;
};

// Map a window pixel to a device pixel via the renderer's logical size, so the
// mapping stays correct under HiDPI and any window scaling.
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
        w * scale, h * scale, SDL_WINDOW_ALLOW_HIGHDPI);
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
    SDL_RenderSetLogicalSize(win->renderer, w, h);

    win->texture = SDL_CreateTexture(win->renderer, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!win->texture) {
        fprintf(stderr, "sim_window: SDL_CreateTexture failed: %s\n", SDL_GetError());
        sim_window_close(win);
        return nullptr;
    }
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
                win->touching = true;
                sim_input().touch_pressed = true;
                set_touch_from_window(win, e.button.x, e.button.y);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                win->touching = false;
                sim_input().touch_pressed = false;
            }
            break;
        case SDL_MOUSEMOTION:
            if (win->touching) set_touch_from_window(win, e.motion.x, e.motion.y);
            break;
        case SDL_KEYDOWN:
            if (!e.key.repeat) {
                switch (e.key.keysym.sym) {
                case SDLK_SPACE:  sim_input().button[0] = true; break;   // PRIMARY
                case SDLK_TAB:    sim_input().button[1] = true; break;   // SECONDARY
                case SDLK_p:      sim_input().pwr_events.push_back(1); break; // PWR
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

    // Present the framebuffer (RGB565, native-endian; pitch = width * 2 bytes).
    SDL_UpdateTexture(win->texture, nullptr, sim_framebuffer().data(), win->w * 2);
    SDL_RenderClear(win->renderer);
    SDL_RenderCopy(win->renderer, win->texture, nullptr, nullptr);
    SDL_RenderPresent(win->renderer);
    // When vsync paces the present we are already frame-limited; otherwise cap
    // the loop so it does not spin the CPU at 100%.
    if (!win->vsync) SDL_Delay(15);
    return true;
}

void sim_window_close(SimWindow* win) {
    if (!win) return;
    if (win->texture)  SDL_DestroyTexture(win->texture);
    if (win->renderer) SDL_DestroyRenderer(win->renderer);
    if (win->window)   SDL_DestroyWindow(win->window);
    delete win;
    SDL_Quit();
}
