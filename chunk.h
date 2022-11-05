#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"

typedef enum
{
    OP_RETURN
} OpCode;

typedef struct
{
    int len;
    int cap;
    uint8_t *code;
} Chunk;

void initChunk(Chunk *chunk);
void writeChunk(Chunk *chunk, uint8_t byte);
void freeChunk(Chunk *chunk);

#endif