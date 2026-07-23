#include "target.h"
#include <vector>

static std::vector<const SimTarget*>& registry() {
    static std::vector<const SimTarget*> r;
    return r;
}

void sim_register_target(const SimTarget* t) { registry().push_back(t); }

const SimTarget* sim_target(const std::string& key) {
    for (auto* t : registry()) if (key == t->key) return t;
    return nullptr;
}

int sim_target_count() { return (int)registry().size(); }
const SimTarget* sim_target_at(int i) {
    return (i >= 0 && i < (int)registry().size()) ? registry()[i] : nullptr;
}

int sim_button_gpio_level(const SimButton& b, bool pressed) {
    // Matches the `button` command's polarity (cli/actions.cpp): pressed drives
    // active_low ? 0 : 1; released is the complement.
    if (b.active_low) return pressed ? 0 : 1;
    return pressed ? 1 : 0;
}
