#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <map>

// File-backed NVS shim so saved settings persist across runs. Storage path from
// env ESPRITE_STATE_DIR (default /tmp/esprite), one file per namespace.
class Preferences {
public:
    bool begin(const char* ns, bool readOnly);
    void end();
    uint8_t  getUChar(const char* key, uint8_t defaultValue);
    size_t   putUChar(const char* key, uint8_t value);
    uint32_t getUInt(const char* key, uint32_t defaultValue);
    size_t   putUInt(const char* key, uint32_t value);
    bool     isKey(const char* key);
private:
    std::string path_;
    std::map<std::string, long> kv_;
    bool readonly_ = true;
    void load();
    void save();
};
