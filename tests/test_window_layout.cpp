#include "doctest.h"
#include "window_layout.h"
#include "target.h"
#include <cstdlib>  // std::abs

namespace {

// Mirrors agentgauge's real button declaration (three explicit EDGE_RIGHT
// positions) without depending on the agentgauge target existing - the brief
// asks for a local fixture so this test runs the same whether or not the
// agentgauge firmware checkout is present.
const SimButton kThreeRight[] = {
    {"PRIMARY",   ACT_PRIMARY,   0, ' ',  EDGE_RIGHT, 0.30f},
    {"PWR",       ACT_PWR,       0, 'p',  EDGE_RIGHT, 0.50f},
    {"SECONDARY", ACT_SECONDARY, 0, '\t', EDGE_RIGHT, 0.70f},
};
const BoardDesc kThreeRightBoard = {
    "fixture-three-right", 480, 480, false, true, false, kThreeRight, 3,
};

const BoardDesc kZeroButtonBoard = {
    "fixture-zero", 320, 240, false, false, false, nullptr, 0,
};

// Two explicit + two auto-stacked buttons on the same edge, to prove autos
// stack independent of explicit siblings and never overlap each other.
// D at 0.8 stays inside the edge without hitting the overflow clamp (that
// case is covered separately below), leaving these four cleanly ordered:
// A, then the two middle-60%-stacked autos, then D.
const SimButton kMixed[] = {
    {"A", ACT_PRIMARY,   0, 'a', EDGE_RIGHT, 0.10f},
    {"B", ACT_SECONDARY, 0, 'b', EDGE_RIGHT, -1.0f},
    {"C", ACT_GPIO,      1, 'c', EDGE_RIGHT, -1.0f},
    {"D", ACT_PWR,       0, 'd', EDGE_RIGHT, 0.80f},
};
const BoardDesc kMixedBoard = {
    "fixture-mixed", 240, 240, false, false, false, kMixed, 4,
};

// Nine buttons (over MAX_LAYOUT_BUTTONS) to prove truncation at 8.
const SimButton kNine[] = {
    {"0", ACT_PRIMARY, 0, '0', EDGE_RIGHT, -1.0f}, {"1", ACT_PRIMARY, 0, '1', EDGE_RIGHT, -1.0f},
    {"2", ACT_PRIMARY, 0, '2', EDGE_RIGHT, -1.0f}, {"3", ACT_PRIMARY, 0, '3', EDGE_RIGHT, -1.0f},
    {"4", ACT_PRIMARY, 0, '4', EDGE_RIGHT, -1.0f}, {"5", ACT_PRIMARY, 0, '5', EDGE_RIGHT, -1.0f},
    {"6", ACT_PRIMARY, 0, '6', EDGE_RIGHT, -1.0f}, {"7", ACT_PRIMARY, 0, '7', EDGE_RIGHT, -1.0f},
    {"8", ACT_PRIMARY, 0, '8', EDGE_RIGHT, -1.0f},
};
const BoardDesc kNineBoard = {
    "fixture-nine", 480, 480, false, false, false, kNine, 9,
};

// One button per edge, to exercise EDGE_LEFT/EDGE_TOP/EDGE_BOTTOM as well as
// EDGE_RIGHT (every other fixture above only uses EDGE_RIGHT).
const SimButton kAllEdges[] = {
    {"R", ACT_PRIMARY,   0, 'r', EDGE_RIGHT,  0.5f},
    {"L", ACT_SECONDARY, 0, 'l', EDGE_LEFT,   0.5f},
    {"T", ACT_GPIO,      1, 't', EDGE_TOP,    0.5f},
    {"B", ACT_PWR,       0, 'b', EDGE_BOTTOM, 0.5f},
};
const BoardDesc kAllEdgesBoard = {
    "fixture-all-edges", 300, 200, false, false, false, kAllEdges, 4,
};

// A board far too small to hold the fixed-size panel card at full size:
// exercises layout_panel_card's min-clamp on both width and height at once.
const BoardDesc kTinyBoard = {
    "fixture-tiny", 20, 20, false, false, false, nullptr, 0,
};

bool rects_overlap(const WinRect& a, const WinRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// inner is fully contained in outer (both edges inclusive of outer's bounds).
bool rect_within(const WinRect& inner, const WinRect& outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

// The nub body sits in the bezel strip for its edge: outside the screen, at
// or before the window edge. This is the "outer NUB_PROTRUDE zone" property,
// expressed generically off the layout's own screen/window rects rather than
// restating the placement formula.
bool body_in_edge_zone(const NubLayout& n, const WindowLayout& l) {
    switch (n.edge) {
    case EDGE_RIGHT:
        return n.body.x >= l.screen.x + l.screen.w && n.body.x + n.body.w <= l.window.x + l.window.w;
    case EDGE_LEFT:
        return n.body.x + n.body.w <= l.screen.x && n.body.x >= l.window.x;
    case EDGE_TOP:
        return n.body.y + n.body.h <= l.screen.y && n.body.y >= l.window.y;
    case EDGE_BOTTOM:
        return n.body.y >= l.screen.y + l.screen.h && n.body.y + n.body.h <= l.window.y + l.window.h;
    }
    return false;
}

}  // namespace

TEST_CASE("win_rect_contains matches half-open rect semantics") {
    WinRect r{10, 10, 5, 5};
    CHECK(win_rect_contains(r, 10, 10));
    CHECK(win_rect_contains(r, 14, 14));
    CHECK_FALSE(win_rect_contains(r, 15, 15));
    CHECK_FALSE(win_rect_contains(r, 9, 10));
}

TEST_CASE("screen rect is the board size at scale, offset by the left/top bezel") {
    WindowLayout l = window_layout(&kThreeRightBoard, 2);
    CHECK(l.screen.w == 480 * 2);
    CHECK(l.screen.h == 480 * 2);
    // kThreeRightBoard only has EDGE_RIGHT buttons, so left and top stay at
    // the plain BEZEL_MARGIN (no NUB_PROTRUDE on edges without buttons).
    CHECK(l.screen.x == l.window.x + BEZEL_MARGIN);
    CHECK(l.screen.y == l.window.y + BEZEL_MARGIN);
}

TEST_CASE("right bezel gets NUB_PROTRUDE when right-edge buttons exist") {
    WindowLayout with_buttons = window_layout(&kThreeRightBoard, 1);
    int right_margin = with_buttons.window.w - (with_buttons.screen.x + with_buttons.screen.w);
    CHECK(right_margin == BEZEL_MARGIN + NUB_PROTRUDE);

    WindowLayout none = window_layout(&kZeroButtonBoard, 1);
    int right_margin_none = none.window.w - (none.screen.x + none.screen.w);
    CHECK(right_margin_none == BEZEL_MARGIN);
}

TEST_CASE("zero-button board has BEZEL_MARGIN on all four sides and no nub padding") {
    WindowLayout l = window_layout(&kZeroButtonBoard, 3);
    CHECK(l.nub_count == 0);
    CHECK(l.screen.x == BEZEL_MARGIN);
    CHECK(l.screen.y == BEZEL_MARGIN);
    CHECK(l.window.w == l.screen.w + 2 * BEZEL_MARGIN);
    CHECK(l.window.h == l.screen.h + 2 * BEZEL_MARGIN);
}

TEST_CASE("explicit pos buttons center at their fraction of screen height, monotonically, no overlap") {
    WindowLayout l = window_layout(&kThreeRightBoard, 1);
    REQUIRE(l.nub_count == 3);

    int last_y = -1;
    for (int i = 0; i < 3; i++) {
        const NubLayout& n = l.nubs[i];
        CHECK(n.edge == EDGE_RIGHT);
        int center_y = n.body.y + n.body.h / 2;
        int expected = l.screen.y + (int)(kThreeRight[i].pos * l.screen.h);
        CHECK(std::abs(center_y - expected) <= 1);  // float rounding at the cast
        CHECK(center_y > last_y);
        last_y = center_y;
    }
    // Disjoint bodies.
    CHECK_FALSE(rects_overlap(l.nubs[0].body, l.nubs[1].body));
    CHECK_FALSE(rects_overlap(l.nubs[1].body, l.nubs[2].body));
}

TEST_CASE("every nub's hit rect stays inside the window, disjoint from the screen, "
          "with its body in the edge's bezel zone") {
    // Spec-derived properties (not a restatement of the placement formula):
    // a hit rect a real mouse could never reach is dead UI, so every nub on
    // every edge, across scales, must satisfy all three at once.
    struct Fixture { const BoardDesc* board; int scale; };
    const Fixture fixtures[] = {
        {&kThreeRightBoard, 1}, {&kThreeRightBoard, 2},
        {&kMixedBoard, 1},
        {&kNineBoard, 1},
        {&kAllEdgesBoard, 1}, {&kAllEdgesBoard, 3},
    };
    for (const Fixture& f : fixtures) {
        WindowLayout l = window_layout(f.board, f.scale);
        REQUIRE(l.nub_count > 0);
        for (int i = 0; i < l.nub_count; i++) {
            const NubLayout& n = l.nubs[i];
            CAPTURE(f.board->name);
            CAPTURE(f.scale);
            CAPTURE(i);
            CHECK(rect_within(n.hit, l.window));
            CHECK_FALSE(rects_overlap(n.hit, l.screen));
            CHECK(body_in_edge_zone(n, l));
        }
    }
}

TEST_CASE("auto-stacked buttons spread evenly in the middle 60% and never overlap") {
    WindowLayout l = window_layout(&kMixedBoard, 1);
    REQUIRE(l.nub_count == 4);
    float range_start = l.screen.y + 0.2f * l.screen.h;
    float range_end   = l.screen.y + 0.8f * l.screen.h;

    const NubLayout& auto_b = l.nubs[1];  // "B", first auto button
    const NubLayout& auto_c = l.nubs[2];  // "C", second auto button
    CHECK(auto_b.body.y >= (int)range_start);
    CHECK(auto_b.body.y + auto_b.body.h <= (int)range_end);
    CHECK(auto_c.body.y >= (int)range_start);
    CHECK(auto_c.body.y + auto_c.body.h <= (int)range_end);
    CHECK(auto_b.body.y < auto_c.body.y);
    CHECK_FALSE(rects_overlap(auto_b.body, auto_c.body));
}

TEST_CASE("mixed explicit and auto buttons on one edge: autos ignore explicit siblings") {
    WindowLayout l = window_layout(&kMixedBoard, 1);
    REQUIRE(l.nub_count == 4);
    // Declaration order: A (explicit), B and C (auto), D (explicit). The
    // autos' placement only depends on how many autos share the edge (2),
    // not on where A or D sit, so all four land in ascending y with none
    // overlapping - autos stacking "independent of explicit ones".
    CHECK(l.nubs[0].body.y < l.nubs[1].body.y);
    CHECK(l.nubs[1].body.y < l.nubs[2].body.y);
    CHECK(l.nubs[2].body.y < l.nubs[3].body.y);
    CHECK_FALSE(rects_overlap(l.nubs[0].body, l.nubs[1].body));
    CHECK_FALSE(rects_overlap(l.nubs[1].body, l.nubs[2].body));
    CHECK_FALSE(rects_overlap(l.nubs[2].body, l.nubs[3].body));
}

TEST_CASE("a nub whose length would overflow its edge clamps inside the edge") {
    const SimButton edge_case[] = {
        {"X", ACT_PRIMARY, 0, 'x', EDGE_RIGHT, 0.0f},
        {"Y", ACT_PRIMARY, 0, 'y', EDGE_RIGHT, 1.0f},
    };
    const BoardDesc board = {"fixture-edge", 100, 100, false, false, false, edge_case, 2};
    WindowLayout l = window_layout(&board, 1);
    for (int i = 0; i < l.nub_count; i++) {
        CHECK(l.nubs[i].body.y >= l.screen.y);
        CHECK(l.nubs[i].body.y + l.nubs[i].body.h <= l.screen.y + l.screen.h);
    }
}

TEST_CASE("more than MAX_LAYOUT_BUTTONS truncates at MAX_LAYOUT_BUTTONS") {
    WindowLayout l = window_layout(&kNineBoard, 1);
    CHECK(MAX_LAYOUT_BUTTONS == 8);
    CHECK(l.nub_count == MAX_LAYOUT_BUTTONS);
}

TEST_CASE("pos values above 1.0 clamp to the far end of the edge") {
    // A pos < 0 is the documented auto-stack sentinel, not an out-of-range
    // value to clamp - only the high side (pos > 1) is a literal clamp case.
    const SimButton above[] = {{"HI", ACT_PRIMARY, 0, 'h', EDGE_RIGHT, 1.5f}};
    const BoardDesc board = {"fixture-clamp", 200, 200, false, false, false, above, 1};
    WindowLayout l = window_layout(&board, 1);
    REQUIRE(l.nub_count == 1);
    // Clamped to pos == 1.0: body sits at the far (bottom) end of the edge.
    CHECK(l.nubs[0].body.y == l.screen.y + l.screen.h - NUB_LONG);
}

TEST_CASE("more_nub sits in the bottom-right bezel corner, outside the screen") {
    WindowLayout l = window_layout(&kThreeRightBoard, 1);
    CHECK(l.more_nub.x == l.window.w - NUB_LONG);
    CHECK(l.more_nub.y == l.window.h - NUB_THICK);
    CHECK(l.more_nub.w == NUB_LONG);
    CHECK(l.more_nub.h == NUB_THICK);
    CHECK_FALSE(rects_overlap(l.more_nub, l.screen));

    WindowLayout none = window_layout(&kZeroButtonBoard, 1);
    CHECK_FALSE(rects_overlap(none.more_nub, none.screen));
}

TEST_CASE("help card is centered and nested inside the window") {
    WindowLayout l = window_layout(&kThreeRightBoard, 1);
    WinRect help = layout_help_card(l);
    CHECK(help.x > l.window.x);
    CHECK(help.y > l.window.y);
    CHECK(help.x + help.w < l.window.x + l.window.w);
    CHECK(help.y + help.h < l.window.y + l.window.h);
    // Centered: equal margin left/right and top/bottom.
    CHECK(help.x - l.window.x == l.window.x + l.window.w - (help.x + help.w));
    CHECK(help.y - l.window.y == l.window.y + l.window.h - (help.y + help.h));
}

TEST_CASE("panel card sits bottom-right, nested inside the window, avoiding the top-left origin") {
    WindowLayout l = window_layout(&kThreeRightBoard, 1);
    WinRect panel = layout_panel_card(l);
    CHECK(panel.x + panel.w <= l.window.x + l.window.w);
    CHECK(panel.y + panel.h <= l.window.y + l.window.h);
    CHECK(panel.x >= l.window.x);
    CHECK(panel.y >= l.window.y);
    // Bottom-right: much closer to the right/bottom edges than to the
    // left/top ones (a fixed-size card in a large window need not cross the
    // window's midpoint to still be "bottom-right").
    int dist_from_right  = l.window.x + l.window.w - (panel.x + panel.w);
    int dist_from_left   = panel.x - l.window.x;
    int dist_from_bottom = l.window.y + l.window.h - (panel.y + panel.h);
    int dist_from_top    = panel.y - l.window.y;
    CHECK(dist_from_right < dist_from_left);
    CHECK(dist_from_bottom < dist_from_top);
    CHECK_FALSE(win_rect_contains(panel, l.window.x, l.window.y));  // avoids origin
}

TEST_CASE("layout_panel includes only the controls the board has") {
    WindowLayout l = window_layout(&kThreeRightBoard, 1);
    WinRect card = layout_panel_card(l);

    PanelLayout both = layout_panel(l, true, true);
    CHECK(both.bat_bar.w == BAT_BAR_W);
    CHECK(both.bat_bar.h == BAT_BAR_H);
    CHECK(both.chg_btn.w == PANEL_BTN_W);
    CHECK(both.usb_btn.w == PANEL_BTN_W);
    CHECK(both.rot_btn.w == PANEL_BTN_W);
    // Left to right, each nested in the card, gapped by PANEL_GAP.
    CHECK(both.bat_bar.x >= card.x);
    CHECK(both.chg_btn.x == both.bat_bar.x + both.bat_bar.w + PANEL_GAP);
    CHECK(both.usb_btn.x == both.chg_btn.x + both.chg_btn.w + PANEL_GAP);
    CHECK(both.rot_btn.x == both.usb_btn.x + both.usb_btn.w + PANEL_GAP);
    CHECK(both.rot_btn.x + both.rot_btn.w <= card.x + card.w);

    PanelLayout neither = layout_panel(l, false, false);
    CHECK(neither.bat_bar.w == 0);
    CHECK(neither.chg_btn.w == 0);
    CHECK(neither.usb_btn.w == 0);
    CHECK(neither.rot_btn.w == 0);

    PanelLayout rotation_only = layout_panel(l, false, true);
    CHECK(rotation_only.bat_bar.w == 0);
    CHECK(rotation_only.rot_btn.w == PANEL_BTN_W);
    CHECK(rotation_only.rot_btn.x == card.x + PANEL_PAD);  // no battery to its left
}

TEST_CASE("sample_gfx (real zero-button target) gets a bare-screen layout") {
    const SimTarget* t = sim_target("sample_gfx");
    REQUIRE(t != nullptr);
    WindowLayout l = window_layout(t->board, 2);
    CHECK(l.nub_count == 0);
    CHECK(l.screen.w == t->board->width * 2);
    CHECK(l.screen.h == t->board->height * 2);
    CHECK(l.screen.x == BEZEL_MARGIN);
    CHECK(l.screen.y == BEZEL_MARGIN);
}

TEST_CASE("panel card stays nested inside the window even for a narrow real board") {
    // sample_gfx at scale 1 (320x240, no buttons) makes window.w (372) only
    // 6px wider than PANEL_CARD_W + PANEL_MARGIN (378): a regression case
    // where the naive bottom-right anchor would push the card off-window.
    const SimTarget* t = sim_target("sample_gfx");
    REQUIRE(t != nullptr);
    WindowLayout l = window_layout(t->board, 1);
    WinRect panel = layout_panel_card(l);
    CHECK(panel.x >= l.window.x);
    CHECK(panel.y >= l.window.y);
    CHECK(panel.x + panel.w <= l.window.x + l.window.w);
    CHECK(panel.y + panel.h <= l.window.y + l.window.h);
}

TEST_CASE("panel card min-clamps its size to fit a synthetic tiny board, fully nested") {
    // kTinyBoard's whole window is smaller than PANEL_CARD_W/H, so position
    // clamping alone (the sample_gfx fix above) isn't enough - the card
    // itself must shrink, on both axes at once, to stay nested.
    WindowLayout l = window_layout(&kTinyBoard, 1);
    WinRect panel = layout_panel_card(l);
    CHECK(panel.x >= l.window.x);
    CHECK(panel.y >= l.window.y);
    CHECK(panel.x + panel.w <= l.window.x + l.window.w);
    CHECK(panel.y + panel.h <= l.window.y + l.window.h);
    CHECK(panel.w < PANEL_CARD_W);   // proves the min-clamp actually engaged
    CHECK(panel.h < PANEL_CARD_H);
}
