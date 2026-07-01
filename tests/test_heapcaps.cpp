#include "doctest.h"
#include "esp_heap_caps.h"
#include <cstring>

TEST_CASE("heap_caps_malloc returns usable memory") {
    void* p = heap_caps_malloc(480 * 20 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    REQUIRE(p != nullptr);
    memset(p, 0xAB, 480 * 20 * 2);
    CHECK(((uint8_t*)p)[0] == 0xAB);
    heap_caps_free(p);
}
