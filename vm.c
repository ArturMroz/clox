#include <stdio.h>

#include "common.h"
#include "vm.h"

VM vm;

void init_vm() {
}

void free_vm() {
}

InterpretResult interpret(Chunk *chunk) {
    vm.chunk = chunk;
    vm.ip = vm.chunk->code;
    return run();
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

    for (;;) {
        uint8_t instruction;
        switch (instruction = *vm.ip++) {
        case OP_RETURN: {
            return INTERPRET_OK;
        }
        case OP_CONSTANT: {
            Value constant = vm.chunk->constants.values[*vm.ip++];
            print_value(constant);
            printf("\n");
            break;
        }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
}