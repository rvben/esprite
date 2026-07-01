#include "doctest.h"
#include "Preferences.h"
#include <cstdlib>

TEST_CASE("putUChar persists across Preferences instances") {
    setenv("ESPRITE_STATE_DIR", "/tmp/esprite_test_prefs", 1);
    system("rm -rf /tmp/esprite_test_prefs");
    {
        Preferences p; p.begin("clawdmeter", false);
        p.putUChar("brt_idx", 3);
        p.end();
    }
    {
        Preferences p; p.begin("clawdmeter", true);
        CHECK(p.getUChar("brt_idx", 0xFF) == 3);
        CHECK(p.getUChar("missing", 0xFF) == 0xFF);
        p.end();
    }
    unsetenv("ESPRITE_STATE_DIR");
}

TEST_CASE("putX on a read-only handle fails without mutating (NVS semantics)") {
    // Real Preferences opened read-only rejects writes (returns 0); the shim
    // previously mutated the in-memory map and reported success.
    setenv("ESPRITE_STATE_DIR", "/tmp/esprite_test_prefs_ro", 1);
    system("rm -rf /tmp/esprite_test_prefs_ro");
    {
        Preferences p; p.begin("ns", false);
        CHECK(p.putUChar("k", 3) == 1);
        CHECK(p.putUInt("u", 7) == 4);
        p.end();
    }
    {
        Preferences p; p.begin("ns", true);
        CHECK(p.putUChar("k", 9) == 0);       // refused
        CHECK(p.putUInt("u", 9) == 0);        // refused
        CHECK(p.getUChar("k", 0xFF) == 3);    // untouched, even in memory
        CHECK(p.getUInt("u", 0xFF) == 7);
        p.end();
    }
    unsetenv("ESPRITE_STATE_DIR");
}
