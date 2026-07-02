#pragma once
#include <string>

// Snapshot-ref model for LVGL targets (like agent-browser's page snapshot). Walk
// the active screen's widget tree and return a JSON array of visible elements,
// each with a stable ref, type, absolute coords, and text/value. Agents read the
// UI structurally and act on refs instead of guessing pixels. Returns "[]" when
// there is no active LVGL screen (non-LVGL targets).
std::string lvgl_snapshot_json();

// Resolve a ref (e.g. "e3") to the center of its widget in device coordinates,
// suitable for a touch. Uses the refs from the last lvgl_snapshot_json() (taking
// one if none exists yet), so a ref stays valid until the next snapshot even if
// the tree changes. Returns false if the ref is not in that snapshot.
bool lvgl_ref_center(const std::string& ref, int* x, int* y);

// True if any visible label on the active screen has text equal to (exact) or
// containing (!exact) `needle`. Backs the scenario/run `expect` assertion.
bool lvgl_has_text(const std::string& needle, bool exact);

// Clear the stored refs (call on boot so a new target starts clean).
void lvgl_snapshot_reset();
