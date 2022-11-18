#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) (type *)allocate_object(sizeof(type), objectType)

static Obj *allocate_object(size_t size, ObjType type) {
    Obj *object = (Obj *)reallocate(NULL, 0, size);

    object->type = type;
    object->next = vm.objects;
    vm.objects   = object;

    return object;
}

static ObjString *allocate_string(char *chars, int length, uint32_t hash) {
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);

    string->len   = length;
    string->chars = chars;
    string->hash  = hash;

    return string;
}

static uint32_t hash_string(const char *key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString *take_string(char *chars, int length) {
    uint32_t hash = hash_string(chars, length);
    return allocate_string(chars, length, hash);
}

ObjString *copy_string(const char *chars, int length) {
    char *heap_chars = ALLOCATE(char, length + 1);
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';

    uint32_t hash = hash_string(chars, length);

    return allocate_string(heap_chars, length, hash);
}

void print_object(Value value) {
    switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    }
}