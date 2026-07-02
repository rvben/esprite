#include "doctest.h"
#include "sim_ble.h"
#include "runtime.h"
#include <string>

// The generic BLE line bus: a host (the CLI, standing in for the Claude
// desktop app) exchanges newline-delimited lines with a device-side consumer
// (a firmware's BLE HAL adapter). Link state, pairing, and HID capture live
// here; everything protocol-shaped stays in the firmware.

TEST_CASE("the bus is unavailable until a device attaches") {
    sim_ble_reset();
    CHECK_FALSE(sim_ble_available());
    sim_ble_attach();
    CHECK(sim_ble_available());
    CHECK(sim_ble_link_state() == SIM_BLE_ADVERTISING);
}

TEST_CASE("bonded connect goes straight to a secure link") {
    sim_ble_reset();
    sim_ble_attach();
    sim_ble_host_connect(0);
    CHECK(sim_ble_link_state() == SIM_BLE_CONNECTED);
    CHECK(sim_ble_secure());
    CHECK(sim_ble_passkey() == 0);
}

TEST_CASE("pairing displays the passkey until the host confirms") {
    sim_ble_reset();
    sim_ble_attach();
    sim_ble_host_connect(123456);
    CHECK(sim_ble_link_state() == SIM_BLE_CONNECTED);
    CHECK_FALSE(sim_ble_secure());
    CHECK(sim_ble_passkey() == 123456);
    sim_ble_host_confirm_pairing();
    CHECK(sim_ble_secure());
    CHECK(sim_ble_passkey() == 0);
}

TEST_CASE("lines flow both ways and drain in order") {
    sim_ble_reset();
    sim_ble_attach();
    sim_ble_host_connect(0);

    sim_ble_host_send("{\"a\":1}");
    sim_ble_host_send("{\"b\":2}");
    CHECK(sim_ble_device_next_line() == "{\"a\":1}");
    CHECK(sim_ble_device_next_line() == "{\"b\":2}");
    CHECK(sim_ble_device_next_line() == "");   // drained

    sim_ble_device_send("{\"ack\":\"x\"}");
    auto out = sim_ble_host_drain();
    REQUIRE(out.size() == 1);
    CHECK(out[0] == "{\"ack\":\"x\"}");
    CHECK(sim_ble_host_drain().empty());
}

TEST_CASE("disconnect drops the link back to advertising and clears security") {
    sim_ble_reset();
    sim_ble_attach();
    sim_ble_host_connect(0);
    sim_ble_host_disconnect();
    CHECK(sim_ble_link_state() == SIM_BLE_ADVERTISING);
    CHECK_FALSE(sim_ble_secure());
}

TEST_CASE("HID reports from the device are captured for the host") {
    sim_ble_reset();
    sim_ble_attach();
    sim_ble_host_connect(0);
    sim_ble_device_hid_press(0x2C, 0);
    sim_ble_device_hid_release();
    auto ev = sim_ble_hid_drain();
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].press);
    CHECK(ev[0].key == 0x2C);
    CHECK_FALSE(ev[1].press);
}

TEST_CASE("a boot resets the bus like a device power cycle") {
    sim_ble_reset();
    sim_ble_attach();
    sim_ble_host_connect(0);
    sim_ble_host_send("{\"stale\":1}");
    sim_boot("cyd");   // any target; the boot hook must clear BLE state
    CHECK_FALSE(sim_ble_available());
    CHECK(sim_ble_device_next_line() == "");
}
