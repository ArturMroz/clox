#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX  (FRAMES_MAX * (UINT8_COUNT))
// #define UINT8_COUNT (UINT8_MAX + 1)

typedef struct {
    ObjClosure *closure;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frame_count;
    Chunk *chunk;
    uint8_t *ip;
    Value stack[STACK_MAX];
    Value *stack_top;

    Table globals;
    Table strings;
    ObjString *init_string;
    ObjUpvalue *open_upvalues;
    Obj *objects;

    size_t bytes_allocated;
    size_t next_gc;
    int gray_count;
    int gray_capacity;
    Obj **gray_stack;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void init_vm();
void free_vm();
void push(Value value);
Value pop();
InterpretResult interpret(const char *source);

#endif