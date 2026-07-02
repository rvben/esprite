#pragma once
#include "target.h"   // BoardDesc, SimButton, SimEdge

// Pure (SDL-free) layout math for the device-bezel window: given a board and
// an integer pixel scale, this computes every rect the window needs to draw
// and hit-test. No SDL types, no rendering here - peripherals/sim_window
// (Task 3) turns these rects into pixels and back into hit tests.
//
// Coordinates are window points: origin (0,0) at the window's top-left,
// x growing right, y growing down. All WinRects below (including the overlay
// rects returned by layout_help_card/layout_panel_card/layout_panel) live in
// that one space - never relative to a card or a nub.

// Layout constants; named here so Task 3 draws exactly what this module
// measured instead of re-declaring the same numbers.
inline constexpr int BEZEL_MARGIN       = 26;  // bezel thickness, every side, no buttons
inline constexpr int NUB_PROTRUDE       = 14;  // extra thickness on edges that carry nubs
inline constexpr int NUB_THICK          = 12;  // nub body thickness (perpendicular to its edge)
inline constexpr int NUB_LONG           = 36;  // nub body length (along its edge)
inline constexpr int HIT_INFLATE        = 6;   // hit rect = body inflated this much per side
inline constexpr int MAX_LAYOUT_BUTTONS = 8;   // mirrors sim_window.cpp's MAX_BTN

// Extended panel (opened by tapping more_nub): fixed card size plus the
// controls it can hold. The card is always sized for the full combination
// (bar + chg + usb + rot) so its size never depends on which controls a given
// board actually has - only layout_panel's contents vary.
inline constexpr int PANEL_MARGIN  = 12;  // gap from the window's right/bottom edge
inline constexpr int PANEL_PAD     = 12;  // interior padding on every side of the card
inline constexpr int PANEL_GAP     = 8;   // gap between adjacent controls
inline constexpr int BAT_BAR_W     = 150;
inline constexpr int BAT_BAR_H     = 22;
inline constexpr int PANEL_BTN_W   = 56;
inline constexpr int PANEL_BTN_H   = 34;
inline constexpr int PANEL_CARD_W  = PANEL_PAD + BAT_BAR_W + PANEL_GAP + PANEL_BTN_W
                                    + PANEL_GAP + PANEL_BTN_W + PANEL_GAP + PANEL_BTN_W
                                    + PANEL_PAD;
inline constexpr int PANEL_CARD_H  = PANEL_PAD + PANEL_BTN_H + PANEL_PAD;

// Help card: a centered overlay, inset from the window by a fixed margin on
// every side (no board-specific sizing - it is prose, not controls).
inline constexpr int HELP_CARD_MARGIN = 40;

struct WinRect { int x, y, w, h; };
bool win_rect_contains(const WinRect& r, int x, int y);

struct NubLayout {
    WinRect body;     // drawn nub rect (output coords)
    WinRect hit;      // click target (body inflated by HIT_INFLATE)
    int     button;   // index into BoardDesc::buttons
    SimEdge edge;
};

struct WindowLayout {
    WinRect window;   // total client size at scale, origin 0,0
    WinRect screen;   // framebuffer dest rect
    WinRect bezel;    // outer bezel rect (== window)
    WinRect more_nub; // the "..." extended-panel opener, bottom-right bezel
    NubLayout nubs[MAX_LAYOUT_BUTTONS];
    int      nub_count;
};

// Computes everything from the board + scale. Buttons with pos in [0,1] sit
// at that fraction along their edge - the screen's matching dimension: height
// for EDGE_LEFT/EDGE_RIGHT, width for EDGE_TOP/EDGE_BOTTOM - clamped so the
// NUB_LONG body never overflows the edge. Buttons with pos < 0 auto-stack
// evenly within the middle 60% of their edge, in declaration order among that
// edge's own auto buttons, independent of any explicit-pos buttons sharing
// the edge; overlapping explicit positions are the caller's choice, but
// autos never overlap each other. Only the first MAX_LAYOUT_BUTTONS buttons
// are laid out (mirrors sim_window.cpp's MAX_BTN truncation). An edge with no
// buttons on it carries no NUB_PROTRUDE padding, so a board with zero buttons
// anywhere gets BEZEL_MARGIN on all four sides and no more_nub padding either
// (more_nub always fits inside the plain bezel).
WindowLayout window_layout(const BoardDesc* board, int scale);

// Overlay geometry, in the same window-absolute coordinates as WindowLayout:
WinRect layout_help_card(const WindowLayout& l);
WinRect layout_panel_card(const WindowLayout& l);          // bottom-right

struct PanelLayout { WinRect bat_bar, chg_btn, usb_btn, rot_btn; };
// Positions the controls the board actually has inside the fixed-size panel
// card, left to right with PANEL_GAP between them: battery contributes
// bat_bar + chg_btn + usb_btn, rotation contributes rot_btn. A control the
// board lacks comes back as a zero rect ({0,0,0,0}) and reserves no space.
PanelLayout layout_panel(const WindowLayout& l, bool battery, bool rotation);
