#include "target.h"

// Tier-1 QEMU prototype target: boots a real ESP32-C3 flash image under
// Espressif's QEMU fork (backends/qemu/qemu_backend.cpp) instead of running
// Arduino code in this process. No setup()/loop() - a qemu backend never
// calls them - and no board peripherals (buttons, battery, IMU) since
// nothing native-side ever drives them; the CLI gate in cli.cpp restricts
// this target to serial/logs/serve only.
static const BoardDesc kBoard = {"QEMU ESP32-C3 (tier 1: serial only)",
                                 0, 0, false, false, false, nullptr, 0};
static const QemuMachineSpec kQemu = {"esp32c3", "riscv32"};
static const SimTarget kTarget = {
    "qemu_esp32c3",
    "Real flash image on Espressif QEMU (serial/logs/serve; image via ESPRITE_QEMU_IMAGE)",
    nullptr, nullptr, &kBoard, BACKEND_QEMU, &kQemu};

namespace {
struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg;
}
