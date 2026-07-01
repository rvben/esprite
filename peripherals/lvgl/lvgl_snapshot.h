#pragma once
#include <string>

// Snapshot-ref model for LVGL targets (like agent-browser's page snapshot). Walk
// the active screen's widget tree and return a JSON array of visible elements,
// each with a stable ref, type, absolute coords, and text/value. Agents read the
// UI structurally and act on refs instead of guessing pixels. Returns "[]" when
// there is no active LVGL screen (non-LVGL targets).
std::string lvgl_snapshot_json();

// Resolve a ref (e.g. "e3") to the center of its widget in device coordinates,
// suitable for a touch. Returns false if the ref is not in the current tree.
bool lvgl_ref_center(const std::string& ref, int* x, int* y);
