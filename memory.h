#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(type, pointer, oldLen, newLen) \
    (type *)reallocate(pointer, sizeof(type) * (oldLen), sizeof(type) * (newLen))

#define FREE_ARRAY(type, pointer, oldLen) \
    reallocate(pointer, sizeof(type) * (oldLen), 0)

void *reallocate(void *pointer, size_t oldSize, size_t newSize);

#endif