#include "lvgl_snapshot.h"
#include "lvgl.h"
#include <functional>
#include <string>
#include <cstdio>

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

std::string lvgl_snapshot_json() {
    lv_obj_t* scr = lv_screen_active();
    std::string out = "[";
    if (scr) {
        int counter = 0;
        bool first = true;
        walk(scr, counter, [&](int ref, lv_obj_t* o) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
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
    lv_obj_t* scr = lv_screen_active();
    if (!scr) return false;
    int counter = 0;
    bool found = false;
    walk(scr, counter, [&](int r, lv_obj_t* o) {
        if (found) return;
        if (("e" + std::to_string(r)) == ref) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            *x = (a.x1 + a.x2) / 2;
            *y = (a.y1 + a.y2) / 2;
            found = true;
        }
    });
    return found;
}
