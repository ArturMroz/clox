#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct {
    // TODO support numbers & bools as keys
    ObjString *key;
    Value value;
} Entry;

typedef struct {
    int len;
    int cap;
    Entry *entries;
} Table;

void init_table(Table *table);
void free_table(Table *table);
bool table_set(Table *table, ObjString *key, Value value);
bool table_get(Table *table, ObjString *key, Value *value);
bool table_delete(Table *table, ObjString *key);
void table_add_all(Table *from, Table *to);
ObjString *table_find_string(Table *table, const char *chars, int length, uint32_t hash);
void mark_table(Table *table);

#endif