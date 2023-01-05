#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm;

void push(Value value) {
    *vm.stack_top = value;
    vm.stack_top++;
}

Value pop() {
    vm.stack_top--;
    return *vm.stack_top;
}

static Value peek(int distance) {
    return vm.stack_top[-1 - distance];
}

static void define_native(const char *name, NativeFn function) {
    // we push & pop these vars to make sure GC doesn't free them out from under us
    push(OBJ_VAL(copy_string(name, (int)strlen(name))));
    push(OBJ_VAL(new_native(function)));

    table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);

    pop();
    pop();
}

static Value clock_native(int argCount, Value *args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void reset_stack() {
    vm.stack_top     = vm.stack;
    vm.frame_count   = 0;
    vm.open_upvalues = NULL;
}

void init_vm() {
    reset_stack();
    vm.objects = NULL;

    vm.gray_count    = 0;
    vm.gray_capacity = 0;
    vm.gray_stack    = NULL;

    vm.bytes_allocated = 0;
    vm.next_gc         = 1024 * 1024;

    init_table(&vm.strings);
    init_table(&vm.globals);

    vm.init_string = NULL; // zero it out first to please GC
    vm.init_string = copy_string("init", 4);

    define_native("clock", clock_native);
}

void free_vm() {
    // the process will free everything on exit, but it feels undignified to
    // require the operating system to clean up our mess
    free_table(&vm.strings);
    free_table(&vm.globals);
    vm.init_string = NULL;
    free_objects();
}

static void runtime_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputs("\n", stderr);

    CallFrame *frame = &vm.frames[vm.frame_count - 1];
    ObjFunction *fn  = frame->closure->function;
    size_t ins       = frame->ip - fn->chunk.code - 1;
    int line         = fn->chunk.lines[ins];

    fprintf(stderr, "[line %d] in script\n", line);

    // dump stack trace
    for (int i = vm.frame_count - 1; i >= 0; i--) {
        CallFrame *frame = &vm.frames[i];
        ObjFunction *fn  = frame->closure->function;
        size_t ins       = frame->ip - fn->chunk.code - 1;

        fprintf(stderr, "[line %d] in ", fn->chunk.lines[ins]);
        if (fn->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", fn->name->chars);
        }
    }

    reset_stack();
}

static bool call(ObjClosure *closure, int arg_count) {
    if (arg_count != closure->function->arity) {
        runtime_error("Expected %d arguments but got %d.", closure->function->arity, arg_count);
        return false;
    }

    if (vm.frame_count == FRAMES_MAX) {
        runtime_error("Stack overflow.");
        return false;
    }

    CallFrame *frame = &vm.frames[vm.frame_count++];
    frame->closure   = closure;
    frame->ip        = closure->function->chunk.code;
    frame->slots     = vm.stack_top - arg_count - 1;

    return true;
}

static bool call_value(Value callee, int arg_count) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bound        = AS_BOUND_METHOD(callee);
            vm.stack_top[-arg_count - 1] = bound->receiver;
            return call(bound->method, arg_count);
        }
        case OBJ_CLASS: {
            ObjClass *klass              = AS_CLASS(callee);
            vm.stack_top[-arg_count - 1] = OBJ_VAL(new_instance(klass));
            Value initializer;
            if (table_get(&klass->methods, vm.init_string, &initializer)) {
                return call(AS_CLOSURE(initializer), arg_count);
            } else if (arg_count != 0) {
                runtime_error("Expected 0 arguments but got %d.", arg_count);
                return false;
            }
            return true;
        }
        case OBJ_CLOSURE:
            return call(AS_CLOSURE(callee), arg_count);
        case OBJ_NATIVE: {
            NativeFn native = AS_NATIVE(callee);
            // TODO validate arity & types before calling native
            Value result = native(arg_count, vm.stack_top - arg_count);
            vm.stack_top -= arg_count + 1;
            push(result);
            return true;
        }
        default:
            break; // non-callable object type
        }
    }

    runtime_error("Can only call functions and classes.");
    return false;
}

static bool bind_method(ObjClass *klass, ObjString *name) {
    Value method;
    if (!table_get(&klass->methods, name, &method)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod *bound = new_bound_method(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

static ObjUpvalue *capture_upvalue(Value *local) {
    ObjUpvalue *prev_upvalue = NULL;
    ObjUpvalue *upvalue      = vm.open_upvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prev_upvalue = upvalue;
        upvalue      = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue *created_upvalue = new_upvalue(local);
    created_upvalue->next       = upvalue;

    if (prev_upvalue == NULL) {
        vm.open_upvalues = created_upvalue;
    } else {
        prev_upvalue->next = created_upvalue;
    }

    return created_upvalue;
}

static void close_upvalues(Value *last) {
    while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last) {
        ObjUpvalue *upvalue = vm.open_upvalues;
        upvalue->closed     = *upvalue->location;
        upvalue->location   = &upvalue->closed;
        vm.open_upvalues    = upvalue->next;
    }
}

static void defineMethod(ObjString *name) {
    Value method    = peek(0);
    ObjClass *klass = AS_CLASS(peek(1));
    table_set(&klass->methods, name, method);
    pop();
}

static bool is_falsey(Value val) {
    return IS_NIL(val) || (IS_BOOL(val) && !AS_BOOL(val));
}

static void concatenate() {
    ObjString *b = AS_STRING(peek(0));
    ObjString *a = AS_STRING(peek(1));

    int len     = a->len + b->len;
    char *chars = ALLOCATE(char, len + 1);

    memcpy(chars, a->chars, a->len);
    memcpy(chars + a->len, b->chars, b->len);
    chars[len] = '\0';

    ObjString *result = take_string(chars, len);
    pop();
    pop();
    push(OBJ_VAL(result));
}

static InterpretResult run() {
    CallFrame *frame = &vm.frames[vm.frame_count - 1];

    // TODO currently we access ip thru a pointer - it would be much more efficient if we stored it
    // in a local var marked as 'register', so the compiler considers putting it in a native CPU register;
    // this would require more code & care when we store local ip back into correct CallFrame tho

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op)                          \
    do {                                                  \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtime_error("Operands must be numbers.");   \
            return INTERPRET_RUNTIME_ERROR;               \
        }                                                 \
        double b = AS_NUMBER(pop());                      \
        double a = AS_NUMBER(pop());                      \
        push(valueType(a op b));                          \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value *slot = vm.stack; slot < vm.stack_top; slot++) {
            printf("[ ");
            print_value(*slot);
            printf(" ]");
        }
        printf("\n");
        disassemble_instruction(
            &frame->closure->function->chunk,
            (int)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction;

        switch (instruction = READ_BYTE()) {
        case OP_CONSTANT: {
            Value constant = READ_CONSTANT();
            push(constant);
            print_value(constant);
            printf("\n");
            break;
        }

        case OP_NIL:
            push(NIL_VAL);
            break;
        case OP_TRUE:
            push(BOOL_VAL(true));
            break;
        case OP_FALSE:
            push(BOOL_VAL(false));
            break;

        case OP_POP:
            pop();
            break;

        case OP_GET_LOCAL: {
            uint8_t slot = READ_BYTE();
            push(frame->slots[slot]);
            break;
        }
        case OP_SET_LOCAL: {
            uint8_t slot       = READ_BYTE();
            frame->slots[slot] = peek(0);
            break;
        }
        case OP_GET_GLOBAL: {
            ObjString *name = READ_STRING();
            Value value;
            if (!table_get(&vm.globals, name, &value)) {
                runtime_error("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }

            push(value);
            break;
        }
        case OP_DEFINE_GLOBAL: {
            ObjString *name = READ_STRING();
            table_set(&vm.globals, name, peek(0));
            // pop value only after it has been added to hashtable, in case GC is triggered meanwhile
            pop();
            break;
        }
        case OP_SET_GLOBAL: {
            ObjString *name = READ_STRING();
            if (table_set(&vm.globals, name, peek(0))) {
                table_delete(&vm.globals, name); // delete zombie value
                runtime_error("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_GET_UPVALUE: {
            uint8_t slot = READ_BYTE();
            push(*frame->closure->upvalues[slot]->location);
            break;
        }
        case OP_SET_UPVALUE: {
            uint8_t slot                              = READ_BYTE();
            *frame->closure->upvalues[slot]->location = peek(0);
            break;
        }
        case OP_GET_PROPERTY: {
            if (!IS_INSTANCE(peek(0))) {
                runtime_error("Only instances can have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(0));
            ObjString *name       = READ_STRING();

            Value value;
            if (table_get(&instance->fields, name, &value)) {
                pop(); // instance
                push(value);
                break;
            }

            if (!bind_method(instance->klass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SET_PROPERTY: {
            if (!IS_INSTANCE(peek(1))) {
                runtime_error("Only instances can have fields.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(1));
            table_set(&instance->fields, READ_STRING(), peek(0));
            Value value = pop();
            pop();
            push(value);
            break;
        }

        case OP_EQUAL: {
            Value b = pop();
            Value a = pop();
            push(BOOL_VAL(values_equal(a, b)));
            break;
        }
        case OP_GREATER:
            BINARY_OP(BOOL_VAL, >);
            break;
        case OP_LESS:
            BINARY_OP(BOOL_VAL, <);
            break;

        case OP_ADD: {
            if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                concatenate();
            } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL(a + b));
            } else {
                runtime_error("Operands must be two numbers or two strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SUBTRACT:
            BINARY_OP(NUMBER_VAL, -);
            break;
        case OP_MULTIPLY:
            BINARY_OP(NUMBER_VAL, *);
            break;
        case OP_DIVIDE:
            BINARY_OP(NUMBER_VAL, /);
            break;

        case OP_NOT:
            push(BOOL_VAL(is_falsey(pop())));
            break;
        case OP_NEGATE:
            if (!IS_NUMBER(peek(0))) {
                runtime_error("Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            push(NUMBER_VAL(-AS_NUMBER(pop())));
            // negating variable without push & pop (probably faster, haven't measured):
            // *(vm.stack_top - 1) = -(*(vm.stack_top - 1));
            break;

        case OP_PRINT: {
            print_value(pop());
            printf("\n");
            break;
        }

        case OP_JUMP: {
            uint16_t offset = READ_SHORT();
            frame->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = READ_SHORT();
            if (is_falsey(peek(0))) frame->ip += offset;
            break;
        }
        case OP_LOOP: {
            uint16_t offset = READ_SHORT();
            frame->ip -= offset;
            break;
        }
        case OP_CALL: {
            int arg_count = READ_BYTE();
            if (!call_value(peek(arg_count), arg_count)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frame_count - 1];
            break;
        }
        case OP_CLOSURE: {
            ObjFunction *fn     = AS_FUNCTION(READ_CONSTANT());
            ObjClosure *closure = new_closure(fn);
            push(OBJ_VAL(closure));

            for (int i = 0; i < closure->upvalue_count; i++) {
                uint8_t is_local = READ_BYTE();
                uint8_t index    = READ_BYTE();
                if (is_local) {
                    closure->upvalues[i] = capture_upvalue(frame->slots + index);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
            break;
        }
        case OP_CLOSE_UPVALUE:
            close_upvalues(vm.stack_top - 1);
            pop();
            break;

        case OP_RETURN: {
            Value result = pop();

            close_upvalues(frame->slots);

            vm.frame_count--;
            if (vm.frame_count == 0) {
                // we finished executing top-level code, kthxbye
                pop();
                return INTERPRET_OK;
            }

            vm.stack_top = frame->slots;
            push(result);
            frame = &vm.frames[vm.frame_count - 1];
            break;
        }
        case OP_CLASS:
            push(OBJ_VAL(new_class(READ_STRING())));
            break;
        case OP_METHOD:
            defineMethod(READ_STRING());
            break;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char *source) {
    ObjFunction *function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    // top level function, kinda like 'main' if you squint
    push(OBJ_VAL(function));
    ObjClosure *closure = new_closure(function);

    // a bit of push & pop dance to keep GC aware of heap allocations
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}
