#include "cli.h"
#include <cstdlib>

int main(int argc, char** argv) {
#ifdef ESPRITE_RUNNER
    // A per-project runner (built via esprite_add_runner) is native single-target:
    // don't register the embedded qemu boards unless the caller explicitly
    // re-enables them, so --target-less commands resolve to the one onboarded
    // target. overwrite=0 respects an ESPRITE_REGISTER_QEMU_BUILTINS already set.
    setenv("ESPRITE_REGISTER_QEMU_BUILTINS", "0", 0);
#endif
    return esprite_main(argc, argv);
}
