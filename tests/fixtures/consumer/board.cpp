#include "target.h"

void consumer_setup();
void consumer_loop();

static const BoardDesc kBoard = { "Consumer 120x80", 120, 80,
                                  false, false, false, nullptr, 0 };
static const SimTarget kTarget = { "consumer", "consumer fixture",
                                   consumer_setup, consumer_loop, &kBoard };
namespace { struct Reg { Reg() { sim_register_target(&kTarget); } } g_reg; }
