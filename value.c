#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

void init_value_array(ValueArray *array) {
    array->values = NULL;
    array->cap    = 0;
    array->len    = 0;
}

void write_value_array(ValueArray *array, Value value) {
    if (array->cap < array->len + 1) {
        int old_cap   = array->cap;
        array->cap    = GROW_CAPACITY(old_cap);
        array->values = GROW_ARRAY(Value, array->values, old_cap, array->cap);
    }

    array->values[array->len] = value;
    array->len++;
}

void free_value_array(ValueArray *array) {
    FREE_ARRAY(Value, array->values, array->cap);
    init_value_array(array);
}

void print_value(Value value) {
    switch (value.type) {
    case VAL_BOOL:
        printf(AS_BOOL(value) ? "true" : "false");
        break;
    case VAL_NIL:
        printf("nil");
        break;
    case VAL_NUMBER:
        printf("%g", AS_NUMBER(value));
        break;
    case VAL_OBJ:
        print_object(value);
        break;
    }
}

bool values_equal(Value a, Value b) {
    if (a.type != b.type) return false;

    switch (a.type) {
    case VAL_BOOL:
        return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
        return true; // nil == nil
    case VAL_NUMBER:
        return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
        ObjString *astr = AS_STRING(a);
        ObjString *bstr = AS_STRING(b);
        return astr->len == bstr->len && memcmp(astr->chars, bstr->chars, astr->len) == 0;
    }

    default:
        return false; // unreachable
    }
}