#include "doctest.h"
#include "cli.h"
#include "framebuffer.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <set>
#include <vector>

// Integration test for the `run` session (esprite_daemon) on an LVGL target.
// Own executable so LVGL global state is clean (one sim_boot per process).

static std::string run_daemon(const std::string& input) {
    FILE* in = fmemopen((void*)input.data(), input.size(), "r");
    char* buf = nullptr;
    size_t len = 0;
    FILE* out = open_memstream(&buf, &len);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    esprite_daemon(in, out);
    fclose(in);
    fclose(out);
    std::string reply(buf, len);
    free(buf);
    return reply;
}

// Number of distinct RGB565 values inside a framebuffer rect. A widget that was
// actually painted shows several (track, indicator, text); a stale region that
// still holds the previous screen's background shows one.
static size_t distinct_colors(int x, int y, int w, int h) {
    const Framebuffer& fb = sim_framebuffer();
    std::set<uint16_t> colors;
    for (int yy = y; yy < y + h && yy < fb.h(); ++yy)
        for (int xx = x; xx < x + w && xx < fb.w(); ++xx)
            if (xx >= 0 && yy >= 0) colors.insert(fb.data()[yy * fb.w() + xx]);
    return colors.size();
}

TEST_CASE("run session: injected snapshot data is rendered, not just applied to the tree") {
    // Regression: after {"cmd":"snapshot"} the widget tree updated (ui reported
    // bars with the injected values) but the framebuffer kept the previous
    // screen's pixels because too few loop steps ran for an LVGL refresh.
    setenv("ESPRITE_HTTP_PORT", "0", 1);

    // One session, one boot (lv_init runs once per process). The unknown-ref
    // tap and the explicit steps command are side-effect-free (no input lands,
    // no view changes), so the painted-bars assertion below stays valid.
    std::string out = run_daemon(
        "{\"cmd\":\"boot\",\"target\":\"waveshare_amoled_18\"}\n"
        "{\"cmd\":\"snapshot\",\"data\":{\"lim\":1,\"s5\":42,\"s5r\":180,"
        "\"s7\":10,\"s7r\":6000,\"ctx\":55,\"cost\":1.5,\"model\":\"opus\"}}\n"
        "{\"cmd\":\"ui\"}\n"
        "{\"cmd\":\"tap\",\"ref\":\"e999\"}\n"
        "{\"cmd\":\"steps\",\"n\":25}\n"
        "{\"cmd\":\"quit\"}\n");
    unsetenv("ESPRITE_HTTP_PORT");

    // Refs resolve against the session's ui snapshot: a bogus ref is rejected.
    CHECK(out.find("\"kind\":\"ref_not_found\"") != std::string::npos);

    // Pull the ui reply (the line that is a JSON array) and find the bars the
    // firmware reports for the injected 42% / 10% limits.
    size_t ui_start = out.find("\n[");
    REQUIRE(ui_start != std::string::npos);
    size_t ui_end = out.find("\n", ui_start + 1);
    std::string ui_line = out.substr(ui_start + 1, ui_end - ui_start - 1);

    JsonDocument doc;
    REQUIRE(deserializeJson(doc, ui_line) == DeserializationError::Ok);
    std::vector<JsonObject> bars;
    for (JsonObject el : doc.as<JsonArray>())
        if (std::string(el["type"] | "") == "bar") bars.push_back(el);
    REQUIRE(bars.size() >= 2);   // the 5h and 7d limit bars

    // Every bar the tree reports must actually be painted in the framebuffer.
    for (JsonObject bar : bars) {
        int x = bar["x"] | 0, y = bar["y"] | 0, w = bar["w"] | 0, h = bar["h"] | 0;
        CAPTURE(x); CAPTURE(y);
        CHECK(distinct_colors(x, y, w, h) >= 2);
    }
}
