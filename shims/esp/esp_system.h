#pragma once
#include <cstdint>

// Minimal esp_system surface for firmware diagnostics. The sim has no heap
// ceiling; report a plausible fixed free-heap figure so status payloads stay
// well-formed and deterministic.
inline uint32_t esp_get_free_heap_size(void) { return 200000; }
