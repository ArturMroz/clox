#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

#define IS_STRING(value)  is_obj_type(value, OBJ_STRING)

#define AS_STRING(value)  ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

typedef enum {
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj *next;
};

struct ObjString {
    Obj obj;
    int len;
    uint32_t hash;
    // TODO currently 2 allocations are needed: 1) ObjString 2) chars array
    //      use 'flexible array members' to store ObjString & chars in a single allocation
    char *chars;
};

ObjString *take_string(char *chars, int length);
ObjString *copy_string(const char *chars, int length);
void print_object(Value value);

static inline bool is_obj_type(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif