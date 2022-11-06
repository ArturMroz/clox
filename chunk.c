#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk *chunk)
{
    chunk->code = NULL;
    chunk->len = 0;
    chunk->cap = 0;
    initValueArray(&chunk->constants);
}

void writeChunk(Chunk *chunk, uint8_t byte)
{
    if (chunk->cap < chunk->len + 1)
    {
        int oldCap = chunk->cap;
        chunk->cap = GROW_CAPACITY(oldCap);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCap, chunk->cap);
    }

    chunk->code[chunk->len] = byte;
    chunk->len++;
}

void freeChunk(Chunk *chunk)
{
    FREE_ARRAY(uint8_t, chunk->code, chunk->cap);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

int addConstant(Chunk *chunk, Value value)
{
    writeValueArray(&chunk->constants, value);
    return chunk->constants.len - 1;
}