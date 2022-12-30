#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(val)      (AS_OBJ(val)->type)

#define IS_FUNCTION(val)   is_obj_type(val, OBJ_FUNCTION)
#define IS_NATIVE(value)   is_obj_type(value, OBJ_NATIVE)
#define IS_CLOSURE(value)  is_obj_type(value, OBJ_CLOSURE)
#define IS_CLASS(value)    isObjType(value, OBJ_CLASS)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_STRING(val)     is_obj_type(val, OBJ_STRING)

#define AS_FUNCTION(val)   ((ObjFunction *)AS_OBJ(val))
#define AS_NATIVE(value)   (((ObjNative *)AS_OBJ(value))->function)
#define AS_CLOSURE(value)  ((ObjClosure *)AS_OBJ(value))
#define AS_CLASS(value)    ((ObjClass *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_STRING(val)     ((ObjString *)AS_OBJ(val))
#define AS_CSTRING(val)    (((ObjString *)AS_OBJ(val))->chars)

typedef enum {
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_STRING,
    OBJ_UPVALUE,
} ObjType;

struct Obj {
    ObjType type;
    bool is_marked;
    struct Obj *next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalue_count;
    Chunk chunk;
    ObjString *name;
} ObjFunction;

typedef Value (*NativeFn)(int arg_count, Value *args);

typedef struct {
    Obj obj;
    Value *location;
    struct ObjUpvalue *next;
    Value closed;
} ObjUpvalue;

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

typedef struct {
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalue_count;
} ObjClosure;

typedef struct {
    Obj obj;
    ObjString *name;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass *klass;
    Table fields;
} ObjInstance;

struct ObjString {
    Obj obj;
    int len;
    uint32_t hash;
    // TODO currently 2 allocations are needed: 1) ObjString 2) chars array
    //      use 'flexible array members' to store ObjString & chars in a single allocation
    char *chars;
};

ObjFunction *new_function();
ObjNative *new_native(NativeFn function);
ObjClosure *new_closure(ObjFunction *function);
ObjClass *new_class(ObjString *name);
ObjInstance *new_instance(ObjClass *klass);
ObjString *take_string(char *chars, int length);
ObjString *copy_string(const char *chars, int length);
ObjUpvalue *new_upvalue(Value *slot);

void print_object(Value value);

static inline bool is_obj_type(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif