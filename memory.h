#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(type, count)   (type *)reallocate(NULL, 0, sizeof(type) * (count))
#define FREE(type, pointer)     reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(type, pointer, old_len, new_len) \
    (type *)reallocate(pointer, sizeof(type) * (old_len), sizeof(type) * (new_len))

#define FREE_ARRAY(type, pointer, old_len) \
    reallocate(pointer, sizeof(type) * (old_len), 0)

void *reallocate(void *pointer, size_t old_size, size_t new_size);
void free_objects();

#endif