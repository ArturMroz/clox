#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
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

ObjFunction *new_function() {
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity       = 0;
    function->name        = NULL;
    init_chunk(&function->chunk);

    return function;
}

static ObjString *allocate_string(char *chars, int length, uint32_t hash) {
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING);

    string->len   = length;
    string->chars = chars;
    string->hash  = hash;

    table_set(&vm.strings, string, NIL_VAL);

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
    uint32_t hash       = hash_string(chars, length);
    ObjString *interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocate_string(chars, length, hash);
}

ObjString *copy_string(const char *chars, int length) {
    uint32_t hash       = hash_string(chars, length);
    ObjString *interned = table_find_string(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char *heap_chars = ALLOCATE(char, length + 1);
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';

    return allocate_string(heap_chars, length, hash);
}

static void print_function(ObjFunction *fn) {
    if (fn->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", fn->name->chars);
}

void print_object(Value value) {
    switch (OBJ_TYPE(value)) {
    case OBJ_FUNCTION:
        print_function(AS_FUNCTION(value));
        break;
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    }
}