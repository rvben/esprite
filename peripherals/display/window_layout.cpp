#include "window_layout.h"

bool win_rect_contains(const WinRect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

namespace {

WinRect inflate(const WinRect& r, int by) {
    return WinRect{r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by};
}

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int clampi(int v, int lo, int hi) {
    if (lo > hi) return lo;   // degenerate range: edge shorter than a nub
    return v < lo ? lo : (v > hi ? hi : v);
}

int edge_margin(bool has_buttons) {
    return BEZEL_MARGIN + (has_buttons ? NUB_PROTRUDE : 0);
}

}  // namespace

WindowLayout window_layout(const BoardDesc* board, int scale) {
    WindowLayout l{};
    if (!board) return l;
    if (scale < 1) scale = 1;

    int count = board->button_count < 0 ? 0
              : (board->button_count > MAX_LAYOUT_BUTTONS ? MAX_LAYOUT_BUTTONS
                                                            : board->button_count);

    // Which edges carry at least one nub decides which edges get the extra
    // NUB_PROTRUDE bezel thickness.
    bool has_edge[4] = {false, false, false, false};
    for (int i = 0; i < count; i++) has_edge[board->buttons[i].edge] = true;

    int left   = edge_margin(has_edge[EDGE_LEFT]);
    int right  = edge_margin(has_edge[EDGE_RIGHT]);
    int top    = edge_margin(has_edge[EDGE_TOP]);
    int bottom = edge_margin(has_edge[EDGE_BOTTOM]);

    int sw = board->width  * scale;
    int sh = board->height * scale;

    l.screen = WinRect{left, top, sw, sh};
    l.window = WinRect{0, 0, sw + left + right, sh + top + bottom};
    l.bezel  = l.window;

    // Flush to the bottom-right window corner; the bottom bezel band is
    // always >= BEZEL_MARGIN (26) tall, well clear of NUB_THICK (12), so this
    // never overlaps the screen regardless of which edges have buttons.
    l.more_nub = WinRect{l.window.w - NUB_LONG, l.window.h - NUB_THICK,
                          NUB_LONG, NUB_THICK};

    // How many auto-stack (pos < 0) buttons share each edge, so each one
    // knows its slice of the middle 60%.
    int auto_total[4] = {0, 0, 0, 0};
    for (int i = 0; i < count; i++)
        if (board->buttons[i].pos < 0.0f) auto_total[board->buttons[i].edge]++;
    int auto_index[4] = {0, 0, 0, 0};

    l.nub_count = count;
    for (int i = 0; i < count; i++) {
        const SimButton& b = board->buttons[i];
        SimEdge edge = b.edge;
        bool vertical = (edge == EDGE_LEFT || edge == EDGE_RIGHT);
        int edge_origin = vertical ? l.screen.y : l.screen.x;
        int edge_length = vertical ? l.screen.h : l.screen.w;

        int start;
        if (b.pos < 0.0f) {
            int n   = auto_total[edge];
            int idx = auto_index[edge]++;
            float range_start = edge_origin + 0.2f * edge_length;
            float range_len   = 0.6f * edge_length;
            float slice       = range_len / (float)n;
            int center = (int)(range_start + slice * (idx + 0.5f));
            // Clamped to the middle-60% range itself (not the full edge):
            // autos must stay inside that band and never overlap each other.
            start = clampi(center - NUB_LONG / 2, (int)range_start,
                           (int)(range_start + range_len) - NUB_LONG);
        } else {
            float pos = clampf(b.pos, 0.0f, 1.0f);
            int center = (int)(edge_origin + pos * edge_length);
            start = clampi(center - NUB_LONG / 2, edge_origin,
                           edge_origin + edge_length - NUB_LONG);
        }

        WinRect body;
        if (vertical) {
            body.y = start;
            body.h = NUB_LONG;
            body.w = NUB_THICK;
            body.x = (edge == EDGE_RIGHT) ? l.window.w - NUB_THICK : 0;
        } else {
            body.x = start;
            body.w = NUB_LONG;
            body.h = NUB_THICK;
            body.y = (edge == EDGE_BOTTOM) ? l.window.h - NUB_THICK : 0;
        }

        l.nubs[i] = NubLayout{body, inflate(body, HIT_INFLATE), i, edge};
    }

    return l;
}

WinRect layout_help_card(const WindowLayout& l) {
    return WinRect{l.window.x + HELP_CARD_MARGIN, l.window.y + HELP_CARD_MARGIN,
                    l.window.w - 2 * HELP_CARD_MARGIN, l.window.h - 2 * HELP_CARD_MARGIN};
}

WinRect layout_panel_card(const WindowLayout& l) {
    // Anchored PANEL_MARGIN off the bottom-right corner, but never past the
    // window's own top-left: a narrow board's window can be only a few
    // pixels wider than the (fixed-size) card, and PANEL_MARGIN would
    // otherwise push it off the left/top edge (e.g. sample_gfx at scale 1).
    int x = l.window.x + l.window.w - PANEL_MARGIN - PANEL_CARD_W;
    int y = l.window.y + l.window.h - PANEL_MARGIN - PANEL_CARD_H;
    if (x < l.window.x) x = l.window.x;
    if (y < l.window.y) y = l.window.y;
    return WinRect{x, y, PANEL_CARD_W, PANEL_CARD_H};
}

PanelLayout layout_panel(const WindowLayout& l, bool battery, bool rotation) {
    WinRect card = layout_panel_card(l);
    PanelLayout p{};
    int x = card.x + PANEL_PAD;
    if (battery) {
        p.bat_bar = WinRect{x, card.y + (PANEL_CARD_H - BAT_BAR_H) / 2, BAT_BAR_W, BAT_BAR_H};
        x += BAT_BAR_W + PANEL_GAP;
        p.chg_btn = WinRect{x, card.y + (PANEL_CARD_H - PANEL_BTN_H) / 2, PANEL_BTN_W, PANEL_BTN_H};
        x += PANEL_BTN_W + PANEL_GAP;
        p.usb_btn = WinRect{x, card.y + (PANEL_CARD_H - PANEL_BTN_H) / 2, PANEL_BTN_W, PANEL_BTN_H};
        x += PANEL_BTN_W + PANEL_GAP;
    }
    if (rotation) {
        p.rot_btn = WinRect{x, card.y + (PANEL_CARD_H - PANEL_BTN_H) / 2, PANEL_BTN_W, PANEL_BTN_H};
    }
    return p;
}
