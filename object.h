#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(val)    (AS_OBJ(val)->type)

#define IS_FUNCTION(val) is_obj_type(val, OBJ_FUNCTION)
#define IS_STRING(val)   is_obj_type(val, OBJ_STRING)

#define AS_FUNCTION(val) ((ObjFunction *)AS_OBJ(val))
#define AS_STRING(val)   ((ObjString *)AS_OBJ(val))
#define AS_CSTRING(val)  (((ObjString *)AS_OBJ(val))->chars)

typedef enum {
    OBJ_FUNCTION,
    OBJ_STRING,
} ObjType;

struct Obj {
    ObjType type;
    struct Obj *next;
};

typedef struct {
    Obj obj;
    int arity;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

struct ObjString {
    Obj obj;
    int len;
    uint32_t hash;
    // TODO currently 2 allocations are needed: 1) ObjString 2) chars array
    //      use 'flexible array members' to store ObjString & chars in a single allocation
    char *chars;
};

ObjFunction *new_function();
ObjString *take_string(char *chars, int length);
ObjString *copy_string(const char *chars, int length);
void print_object(Value value);

static inline bool is_obj_type(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif