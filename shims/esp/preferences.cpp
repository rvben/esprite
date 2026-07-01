#include "Preferences.h"
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>

static std::string state_dir() {
    const char* d = getenv("ESPRITE_STATE_DIR");
    return d ? d : "/tmp/esprite";
}

bool Preferences::begin(const char* ns, bool readOnly) {
    readonly_ = readOnly;
    std::string dir = state_dir();
    mkdir(dir.c_str(), 0755);
    path_ = dir + "/prefs_" + ns + ".txt";
    kv_.clear();
    load();
    return true;
}

void Preferences::load() {
    FILE* f = fopen(path_.c_str(), "r");
    if (!f) return;
    char key[64];
    long val;
    while (fscanf(f, "%63s %ld", key, &val) == 2) kv_[key] = val;
    fclose(f);
}

bool Preferences::save() {
    FILE* f = fopen(path_.c_str(), "w");
    if (!f) return false;
    for (auto& kv : kv_) fprintf(f, "%s %ld\n", kv.first.c_str(), kv.second);
    fclose(f);
    return true;
}

uint8_t Preferences::getUChar(const char* key, uint8_t def) {
    auto it = kv_.find(key);
    return it == kv_.end() ? def : (uint8_t)it->second;
}
// Real NVS rejects writes on a read-only handle (returns 0 bytes written);
// mutating even in memory would let firmware read back a value the device
// would never have stored.
size_t Preferences::putUChar(const char* key, uint8_t value) {
    if (readonly_) return 0;
    kv_[key] = value;
    return save() ? 1 : 0;
}
uint32_t Preferences::getUInt(const char* key, uint32_t def) {
    auto it = kv_.find(key);
    return it == kv_.end() ? def : (uint32_t)it->second;
}
size_t Preferences::putUInt(const char* key, uint32_t value) {
    if (readonly_) return 0;
    kv_[key] = (long)value;
    return save() ? 4 : 0;
}
bool Preferences::isKey(const char* key) { return kv_.find(key) != kv_.end(); }

void Preferences::end() {}
