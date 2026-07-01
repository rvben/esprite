#include "lvgl_snapshot.h"
#include "lvgl.h"
#include "framebuffer.h"
#include <functional>
#include <string>
#include <map>
#include <utility>
#include <cstdio>

// Refs from the most recent snapshot -> widget center, so tap --ref uses the
// coordinates as of that snapshot (the agent-browser contract) even if the tree
// changes afterwards. Cleared on boot via lvgl_snapshot_reset().
static std::map<std::string, std::pair<int, int>> g_refs;

static const char* type_name(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_label_class))  return "label";
    if (lv_obj_check_type(o, &lv_bar_class))    return "bar";
    if (lv_obj_check_type(o, &lv_arc_class))    return "arc";
    if (lv_obj_check_type(o, &lv_button_class)) return "button";
    if (lv_obj_check_type(o, &lv_image_class))  return "image";
    if (lv_obj_check_type(o, &lv_line_class))   return "line";
    return "obj";
}

// DFS pre-order walk over visible children, assigning sequential refs.
static void walk(lv_obj_t* obj, int& counter,
                 const std::function<void(int, lv_obj_t*)>& visit) {
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* c = lv_obj_get_child(obj, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        visit(counter++, c);
        walk(c, counter, visit);
    }
}

static void json_escape(std::string& out, const char* s) {
    for (const char* p = s; p && *p; ++p) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
            else out += (char)c;
        }
    }
}

// The active LVGL screen for the current target, or null. Guards against a stale
// tree left in LVGL's global state by a previously-booted target: if the active
// display's resolution no longer matches the current framebuffer, this target
// does not own the LVGL screen (e.g. a non-LVGL target booted after an LVGL one).
static lv_obj_t* current_screen() {
    lv_display_t* disp = lv_display_get_default();
    if (!disp) return nullptr;
    if ((int)lv_display_get_horizontal_resolution(disp) != sim_framebuffer().w() ||
        (int)lv_display_get_vertical_resolution(disp)   != sim_framebuffer().h())
        return nullptr;
    return lv_screen_active();
}

std::string lvgl_snapshot_json() {
    g_refs.clear();
    lv_obj_t* scr = current_screen();
    std::string out = "[";
    if (scr) {
        int counter = 0;
        bool first = true;
        walk(scr, counter, [&](int ref, lv_obj_t* o) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            g_refs["e" + std::to_string(ref)] = { (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2 };
            if (!first) out += ",";
            first = false;
            out += "{\"ref\":\"e" + std::to_string(ref) + "\",\"type\":\"" + type_name(o) + "\"";
            out += ",\"x\":" + std::to_string(a.x1) + ",\"y\":" + std::to_string(a.y1);
            out += ",\"w\":" + std::to_string(a.x2 - a.x1 + 1);
            out += ",\"h\":" + std::to_string(a.y2 - a.y1 + 1);
            if (lv_obj_check_type(o, &lv_label_class)) {
                out += ",\"text\":\"";
                json_escape(out, lv_label_get_text(o));
                out += "\"";
            } else if (lv_obj_check_type(o, &lv_bar_class)) {
                out += ",\"value\":" + std::to_string(lv_bar_get_value(o));
            } else if (lv_obj_check_type(o, &lv_arc_class)) {
                out += ",\"value\":" + std::to_string(lv_arc_get_value(o));
            }
            out += "}";
        });
    }
    out += "]";
    return out;
}

bool lvgl_ref_center(const std::string& ref, int* x, int* y) {
    // Resolve against the last snapshot's refs. If none was taken this session
    // (e.g. a one-shot `tap --ref`), snapshot the current tree first.
    if (g_refs.empty()) lvgl_snapshot_json();
    auto it = g_refs.find(ref);
    if (it == g_refs.end()) return false;
    *x = it->second.first;
    *y = it->second.second;
    return true;
}

void lvgl_snapshot_reset() { g_refs.clear(); }

// Reset the ref map on every boot via the common runtime hook, so refs never
// leak across a re-boot regardless of the caller.
extern void sim_on_boot(void (*)());   // core/runtime
namespace { struct BootReg { BootReg() { sim_on_boot(lvgl_snapshot_reset); } } g_boot_reg; }
