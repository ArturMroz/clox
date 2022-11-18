#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void init_table(Table *table) {
    table->len     = 0;
    table->cap     = 0;
    table->entries = NULL;
}

void free_table(Table *table) {
    FREE_ARRAY(Entry, table->entries, table->cap);
    init_table(table);
}

static Entry *find_entry(Entry *entries, int capacity, ObjString *key) {
    uint32_t index = key->hash % capacity;

    for (;;) {
        Entry *entry = &entries[index];
        if (entry->key == key || entry->key == NULL) {
            return entry;
        }

        // keep probing until we find an empty bucket
        index++;
        index %= capacity;
    }
}

static void adjust_capacity(Table *table, int capacity) {
    Entry *new_entries = ALLOCATE(Entry, capacity);

    // rebuild the whole hash table
    for (int i = 0; i < capacity; i++) {
        new_entries[i].key   = NULL;
        new_entries[i].value = NIL_VAL;
    }

    for (int i = 0; i < table->cap; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key == NULL) continue;

        Entry *dest = find_entry(new_entries, capacity, entry->key);
        dest->key   = entry->key;
        dest->value = entry->value;
    }

    FREE_ARRAY(Entry, table->entries, table->cap);
    table->entries = new_entries;
    table->cap     = capacity;
}

bool table_set(Table *table, ObjString *key, Value value) {
    if (table->len + 1 > table->cap * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->cap);
        adjust_capacity(table, capacity);
    }

    Entry *entry    = find_entry(table->entries, table->cap, key);
    bool is_new_key = entry->key == NULL;

    entry->key   = key;
    entry->value = value;

    if (is_new_key) table->len++;

    return is_new_key;
}

void table_add_all(Table *from, Table *to) {
    for (int i = 0; i < from->cap; i++) {
        Entry *entry = &from->entries[i];
        if (entry->key != NULL) {
            table_set(to, entry->key, entry->value);
        }
    }
}