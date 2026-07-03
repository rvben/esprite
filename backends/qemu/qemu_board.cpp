#include "qemu_board.h"
#include "target.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>

bool qemu_board_parse(const std::string& json, QemuBoardSpec* spec, std::string* err) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        if (err) *err = "board spec is not valid JSON";
        return false;
    }
    spec->key         = doc["key"] | "";
    spec->name        = doc["name"] | "";
    spec->description = doc["description"] | "";
    spec->machine     = doc["machine"] | "";
    spec->arch        = doc["arch"] | "";
    if (spec->key.empty() || spec->description.empty() || spec->machine.empty()) {
        if (err) *err = "board spec needs non-empty key, description, and machine";
        return false;
    }
    if (spec->arch != "riscv32" && spec->arch != "xtensa") {
        if (err) *err = "board spec arch must be riscv32 or xtensa, got '" + spec->arch + "'";
        return false;
    }
    if (spec->name.empty()) spec->name = spec->key;
    spec->display_w = spec->display_h = 0;
    if (!doc["display"].isNull()) {
        int w = doc["display"]["width"] | 0;
        int h = doc["display"]["height"] | 0;
        // Same defensive bound as the screendump decoder: esp_rgb tops out at
        // 800x600, 4096 guards the framebuffer allocation.
        if (w < 1 || w > 4096 || h < 1 || h > 4096) {
            if (err) *err = "board spec display needs width and height in 1..4096";
            return false;
        }
        spec->display_w = w;
        spec->display_h = h;
    }
    return true;
}

namespace {

// Registered specs live for the whole process: the target registry holds raw
// pointers, exactly like the static SimTarget objects in targets/*/board.cpp.
// Deliberately never freed.
struct OwnedBoard {
    std::string key, name, description, machine, arch;
    BoardDesc board;
    QemuMachineSpec qemu;
    SimTarget target;
};

}  // namespace

const SimTarget* qemu_board_register(const QemuBoardSpec& spec, std::string* err) {
    if (sim_target(spec.key)) {
        if (err) *err = "duplicate target key '" + spec.key + "'";
        return nullptr;
    }
    auto* o = new OwnedBoard;
    o->key = spec.key; o->name = spec.name; o->description = spec.description;
    o->machine = spec.machine; o->arch = spec.arch;
    o->board = {o->name.c_str(), spec.display_w, spec.display_h,
                false, false, false,   // has_rotation, has_battery, has_imu
                nullptr, 0};
    o->qemu = {o->machine.c_str(), o->arch.c_str()};
    o->target = {o->key.c_str(), o->description.c_str(),
                 nullptr, nullptr,    // no in-process entry points
                 &o->board, BACKEND_QEMU, &o->qemu};
    sim_register_target(&o->target);
    return &o->target;
}

bool qemu_board_register_file(const std::string& path, std::string* err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "ESPRITE_QEMU_BOARD file not readable: " + path;
        return false;
    }
    std::string json;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
    fclose(f);
    QemuBoardSpec spec;
    if (!qemu_board_parse(json, &spec, err)) {
        if (err) *err = path + ": " + *err;
        return false;
    }
    return qemu_board_register(spec, err) != nullptr;
}

// Filled by the build: CMake embeds every targets/qemu/*.json into
// qemu_boards_gen.cpp (see the root CMakeLists).
extern const char* const kQemuBoardBuiltins[];
extern const int kQemuBoardBuiltinCount;

bool qemu_boards_install(std::string* err) {
    static bool done = false;
    if (done) return true;
    for (int i = 0; i < kQemuBoardBuiltinCount; i++) {
        QemuBoardSpec spec;
        if (!qemu_board_parse(kQemuBoardBuiltins[i], &spec, err)) return false;
        if (!qemu_board_register(spec, err)) return false;
    }
    if (const char* path = getenv("ESPRITE_QEMU_BOARD")) {
        if (!qemu_board_register_file(path, err)) return false;
    }
    done = true;
    return true;
}
