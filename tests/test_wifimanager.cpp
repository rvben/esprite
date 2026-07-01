#include "doctest.h"
#include "WiFiManager.h"
#include <cstdlib>

TEST_CASE("autoConnect opens the captive portal only when enabled and opted in") {
    WiFiManager wm;
    static bool portal_opened;
    portal_opened = false;
    wm.setAPCallback([](WiFiManager*) { portal_opened = true; });

    // Default: no env opt-in, connects instantly.
    unsetenv("ESPRITE_WIFI_PORTAL");
    CHECK(wm.autoConnect("ap"));
    CHECK_FALSE(portal_opened);

    // Opted in: the AP callback runs and autoConnect reports not-connected,
    // so the firmware renders its captive-portal hint.
    setenv("ESPRITE_WIFI_PORTAL", "1", 1);
    CHECK_FALSE(wm.autoConnect("ap"));
    CHECK(portal_opened);

    // The trial-connect path disables the portal; env alone must not open it.
    portal_opened = false;
    wm.setEnableConfigPortal(false);
    CHECK(wm.autoConnect("ap"));
    CHECK_FALSE(portal_opened);
    unsetenv("ESPRITE_WIFI_PORTAL");
}
