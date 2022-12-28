#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <stdio.h>
#endif

void *reallocate(void *pointer, size_t old_size, size_t new_size) {
    if (new_size > old_size) {
#ifdef DEBUG_STRESS_GC
        collect_garbage();
#endif
    }

    if (new_size == 0) {
        free(pointer);
        return NULL;
    }

    void *result = realloc(pointer, new_size);

    if (result == NULL) exit(1);

    return result;
}

void mark_object(Obj *object) {
    if (object == NULL) return;
    if (object->is_marked) return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *)object);
    print_value(OBJ_VAL(object));
    printf("\n");
#endif

    object->is_marked = true;

    if (vm.gray_capacity < vm.gray_count + 1) {
        vm.gray_capacity = GROW_CAPACITY(vm.gray_capacity);
        vm.gray_stack    = (Obj **)realloc(vm.gray_stack, sizeof(Obj *) * vm.gray_capacity);

        // if there's no memory left for GC to even run, we just abort
        // this is very rare as gray_stack is small
        if (vm.gray_stack == NULL) exit(1);
    }

    vm.gray_stack[vm.gray_count++] = object;
}

void mark_value(Value value) {
    if (IS_OBJ(value)) mark_object(AS_OBJ(value));
}

static void mark_array(ValueArray *array) {
    for (int i = 0; i < array->len; i++) {
        mark_value(array->values[i]);
    }
}

static void free_object(Obj *object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *)object, object->type);
#endif

    switch (object->type) {
    case OBJ_FUNCTION: {
        ObjFunction *function = (ObjFunction *)object;
        free_chunk(&function->chunk);
        FREE(ObjFunction, object);
        break;
    }
    case OBJ_NATIVE:
        FREE(ObjNative, object);
        break;
    case OBJ_CLOSURE: {
        ObjClosure *closure = (ObjClosure *)object;
        FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalue_count);
        FREE(ObjClosure, object);
        break;
    }
    case OBJ_UPVALUE:
        FREE(ObjUpvalue, object);
        break;
    case OBJ_STRING: {
        ObjString *str = (ObjString *)object;
        FREE_ARRAY(char, str->chars, str->len + 1);
        FREE(ObjString, object);
        break;
    }
    }
}

void free_objects() {
    Obj *object = vm.objects;
    while (object != NULL) {
        Obj *next = object->next;
        free_object(object);
        object = next;
    }

    free(vm.gray_stack);
}

static void mark_roots() {
    for (Value *slot = vm.stack; slot < vm.stack_top; slot++) {
        mark_value(*slot);
    }

    for (int i = 0; i < vm.frame_count; i++) {
        mark_object((Obj *)vm.frames[i].closure);
    }

    for (ObjUpvalue *upvalue = vm.open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
        mark_object((Obj *)upvalue);
    }

    mark_table(&vm.globals);
    mark_compiler_roots();
}

static void blacken_object(Obj *object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void *)object);
    print_value(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
    case OBJ_CLOSURE: {
        ObjClosure *closure = (ObjClosure *)object;
        mark_object((Obj *)closure->function);
        for (int i = 0; i < closure->upvalue_count; i++) {
            mark_object((Obj *)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction *function = (ObjFunction *)object;
        mark_object((Obj *)function->name);
        mark_array(&function->chunk.constants);
        break;
    }
    case OBJ_UPVALUE:
        mark_value(((ObjUpvalue *)object)->closed);
        break;
    case OBJ_NATIVE:
    case OBJ_STRING:
        // TODO strings and native fs don't need processing so can skip adding these to the gray
        // stack and just darken them from white straight to black
        break;
    }
}

static void trace_references() {
    while (vm.gray_count > 0) {
        Obj *object = vm.gray_stack[--vm.gray_count];
        blacken_object(object);
    }
}

void collect_garbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif

    mark_roots();

    trace_references();

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
#endif
}
