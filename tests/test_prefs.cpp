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
}
