#pragma once
// Host stub for the ESP32 Update (OTA flash) library. The sim never flashes, so
// every step "succeeds": firmware that compiles the OTA upload path links and
// runs its bookkeeping (progress, MD5, finalize) without touching real flash.
#include <cstdint>
#include <cstddef>

#ifndef UPDATE_SIZE_UNKNOWN
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#endif

class UpdateClass {
public:
    bool   begin(size_t = UPDATE_SIZE_UNKNOWN) { progress_ = 0; started_ = true; return true; }
    bool   setMD5(const char*) { return true; }
    void   abort() { started_ = false; }
    size_t write(uint8_t*, size_t len) { progress_ += len; return len; }
    size_t progress() { return progress_; }
    bool   end(bool = false) { return started_; }
    bool   hasError() { return false; }
    const char* errorString() { return ""; }
private:
    size_t progress_ = 0;
    bool   started_  = false;
};

inline UpdateClass Update;
