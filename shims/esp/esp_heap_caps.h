#pragma once
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_8BIT     (1u << 0)
#define MALLOC_CAP_INTERNAL (1u << 1)
#define MALLOC_CAP_SPIRAM   (1u << 2)
#define MALLOC_CAP_DMA      (1u << 3)
#define MALLOC_CAP_DEFAULT  (0u)

inline void* heap_caps_malloc(size_t n, uint32_t /*caps*/) { return malloc(n); }
inline void  heap_caps_free(void* p) { free(p); }
inline void* heap_caps_realloc(void* p, size_t n, uint32_t /*caps*/) { return realloc(p, n); }
inline size_t heap_caps_get_free_size(uint32_t /*caps*/) { return 4u * 1024 * 1024; }
