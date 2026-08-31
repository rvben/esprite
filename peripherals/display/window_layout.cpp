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

// Where the idx-th of n auto-stacked nubs starts along its edge.
//
// Autos share the middle 60% of the edge while that band gives each one a full
// hit rect (NUB_LONG + 2 * HIT_INFLATE) of pitch, so a roomy edge keeps the
// nubs clustered around its centre. Below that they fall back to exactly that
// pitch, centred on the edge. The outermost hit inflation then hangs into the
// bezel, where no other target sits, so n of them need (n - 1) * pitch +
// NUB_LONG points of edge rather than n * pitch. An edge shorter than that
// cannot separate n targets at any spacing; the stack spreads evenly over the
// whole edge and adjacent targets do overlap.
int auto_start(int idx, int n, int edge_origin, int edge_length) {
    const int pitch = NUB_LONG + 2 * HIT_INFLATE;
    float slice = 0.6f * (float)edge_length / (float)n;
    if (slice >= (float)pitch) {
        float range_start = edge_origin + 0.2f * edge_length;
        float range_len   = 0.6f * (float)edge_length;
        int center = (int)(range_start + slice * (idx + 0.5f));
        return clampi(center - NUB_LONG / 2, (int)range_start,
                      (int)(range_start + range_len) - NUB_LONG);
    }
    int span = (n - 1) * pitch + NUB_LONG;
    if (span <= edge_length)
        return edge_origin + (edge_length - span) / 2 + idx * pitch;
    float slot = (float)edge_length / (float)n;
    int center = (int)(edge_origin + slot * (idx + 0.5f));
    return clampi(center - NUB_LONG / 2, edge_origin,
                  edge_origin + edge_length - NUB_LONG);
}

// One panel control at x, vertically centered on the card's own height and
// clipped to the card's padded interior. layout_panel_card shrinks the card on
// a window too small for PANEL_CARD_W/H, and sim_window only hit-tests the
// controls after the click is already inside the card, so a control drawn past
// the card edge is both wrong to look at and impossible to press. A control
// left with no room comes back as a zero rect, the same way this module
// represents a control the board does not have; win_rect_contains never
// matches one and the renderer skips it.
WinRect place_control(const WinRect& card, int x, int w, int h) {
    int right    = card.x + card.w - PANEL_PAD;
    int interior = card.h - 2 * PANEL_PAD;
    if (w > right - x)   w = right - x;
    if (h > interior)    h = interior;
    if (w <= 0 || h <= 0) return WinRect{0, 0, 0, 0};
    return WinRect{x, card.y + (card.h - h) / 2, w, h};
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
    // never overlaps the screen regardless of which edges have buttons. Only
    // placed when the panel it opens would actually hold a control - a board
    // with neither battery nor rotation has nothing for "..." to show.
    if (board->has_battery || board->has_rotation)
        l.more_nub = WinRect{l.window.w - NUB_LONG, l.window.h - NUB_THICK,
                              NUB_LONG, NUB_THICK};

    // How many auto-stack (pos < 0) buttons share each edge, so each one
    // knows its slice of the band.
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
            // What must not overlap is the hit rect, which is longer than the
            // body by HIT_INFLATE at each end, so a stack pitched at NUB_LONG
            // hands adjacent buttons overlapping click targets even while the
            // bodies look correctly spaced. auto_start pitches by the hit rect.
            start = auto_start(idx, n, edge_origin, edge_length);
            // The "..." nub owns the bottom-right corner and sim_window
            // hit-tests it before the buttons, so a bottom stack reaching into
            // it would lose those clicks to the panel. Slide the whole stack
            // left by the overrun rather than clamping the last nub onto its
            // neighbour, which would trade the corner for an overlap. The floor
            // is the window edge, not the screen's: a stack this dense may need
            // the bezel strip left of the screen, which is the nubs' own band
            // and carries nothing else.
            if (edge == EDGE_BOTTOM && l.more_nub.w > 0) {
                int overrun = auto_start(n - 1, n, edge_origin, edge_length)
                            + NUB_LONG + HIT_INFLATE - l.more_nub.x;
                if (overrun > 0) start = clampi(start - overrun, HIT_INFLATE, start);
            }
        } else {
            float pos = clampf(b.pos, 0.0f, 1.0f);
            int center = (int)(edge_origin + pos * edge_length);
            start = clampi(center - NUB_LONG / 2, edge_origin,
                           edge_origin + edge_length - NUB_LONG);
        }

        // Inset by HIT_INFLATE from the absolute window edge: the hit rect
        // (body inflated by HIT_INFLATE on every side) must stay inside the
        // window, or its outward slice sits beyond any coordinate the mouse
        // can reach and is dead click area.
        WinRect body;
        if (vertical) {
            body.y = start;
            body.h = NUB_LONG;
            body.w = NUB_THICK;
            body.x = (edge == EDGE_RIGHT) ? l.window.w - NUB_THICK - HIT_INFLATE : HIT_INFLATE;
        } else {
            body.x = start;
            body.w = NUB_LONG;
            body.h = NUB_THICK;
            body.y = (edge == EDGE_BOTTOM) ? l.window.h - NUB_THICK - HIT_INFLATE : HIT_INFLATE;
        }

        l.nubs[i] = NubLayout{body, inflate(body, HIT_INFLATE), i, edge};
    }

    return l;
}

WinRect layout_help_card(const WindowLayout& l, int content_w, int content_h) {
    int w = content_w + 2 * HELP_CARD_PAD;
    int h = content_h + 2 * HELP_CARD_PAD;
    // Clamp to fit fully inside the window (floor 0), same approach as
    // layout_panel_card: content taller/wider than the window itself must
    // never push the card off-window.
    if (w > l.window.w) w = l.window.w;
    if (h > l.window.h) h = l.window.h;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    int x = l.window.x + (l.window.w - w) / 2;
    int y = l.window.y + (l.window.h - h) / 2;
    return WinRect{x, y, w, h};
}

WinRect layout_panel_card(const WindowLayout& l) {
    // Size first: a window narrower or shorter than the fixed card (e.g. a
    // tiny synthetic board) would overflow past the right/bottom edge if the
    // card kept its full size, so shrink to fit before anchoring position.
    // Floors at 0 - the card simply has no room to draw on a degenerate window.
    int w = PANEL_CARD_W;
    int h = PANEL_CARD_H;
    int max_w = l.window.w - 2 * PANEL_MARGIN;
    int max_h = l.window.h - 2 * PANEL_MARGIN;
    if (w > max_w) w = max_w;
    if (h > max_h) h = max_h;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    // Anchored PANEL_MARGIN off the bottom-right corner, but never past the
    // window's own top-left: a narrow board's window can be only a few
    // pixels wider than the (fixed-size) card, and PANEL_MARGIN would
    // otherwise push it off the left/top edge (e.g. sample_gfx at scale 1).
    int x = l.window.x + l.window.w - PANEL_MARGIN - w;
    int y = l.window.y + l.window.h - PANEL_MARGIN - h;
    if (x < l.window.x) x = l.window.x;
    if (y < l.window.y) y = l.window.y;
    return WinRect{x, y, w, h};
}

PanelLayout layout_panel(const WindowLayout& l, bool battery, bool rotation) {
    WinRect card = layout_panel_card(l);
    PanelLayout p{};
    // x advances by each control's declared width, not its clipped width, so
    // a control that had to narrow does not shift the ones after it.
    int x = card.x + PANEL_PAD;
    if (battery) {
        p.bat_bar = place_control(card, x, BAT_BAR_W, BAT_BAR_H);
        // Top-aligned to the bar rather than centered on the card: the readout
        // reads as a label on the bar, so it tracks the bar's own position.
        if (p.bat_bar.w > 0) {
            p.bat_pct = place_control(card, p.bat_bar.x + p.bat_bar.w + PANEL_PCT_LEAD,
                                      PANEL_PCT_W, PANEL_PCT_H);
            if (p.bat_pct.w < PANEL_PCT_W) p.bat_pct = WinRect{0, 0, 0, 0};  // "100%" would be cut
            else                           p.bat_pct.y = p.bat_bar.y + 4;
        }
        x += BAT_BAR_W + PANEL_PCT_GAP;
        p.chg_btn = place_control(card, x, PANEL_BTN_W, PANEL_BTN_H);
        x += PANEL_BTN_W + PANEL_GAP;
        p.usb_btn = place_control(card, x, PANEL_BTN_W, PANEL_BTN_H);
        x += PANEL_BTN_W + PANEL_GAP;
    }
    if (rotation) {
        p.rot_btn = place_control(card, x, PANEL_BTN_W, PANEL_BTN_H);
    }
    return p;
}
