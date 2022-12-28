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
    uint32_t index   = key->hash % capacity;
    Entry *tombstone = NULL;

    for (;;) {
        Entry *entry = &entries[index];

        if (entry->key == key) {
            // found the key
            return entry;
        } else if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // empty entry
                return tombstone != NULL ? tombstone : entry;
            } else {
                // found a tombstone
                if (tombstone == NULL) tombstone = entry;
            }
        }

        // keep probing until we find an empty bucket
        index++;
        index %= capacity;
    }
}

bool table_get(Table *table, ObjString *key, Value *value) {
    if (table->len == 0) return false;

    Entry *entry = find_entry(table->entries, table->cap, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

static void adjust_capacity(Table *table, int capacity) {
    Entry *new_entries = ALLOCATE(Entry, capacity);

    // rebuild the whole hash table
    for (int i = 0; i < capacity; i++) {
        new_entries[i].key   = NULL;
        new_entries[i].value = NIL_VAL;
    }

    // we don't want to include tombstones in total num of items
    // so we're clearing the len and recounting when copying items over below
    table->len = 0;

    for (int i = 0; i < table->cap; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key == NULL) continue;

        Entry *dest = find_entry(new_entries, capacity, entry->key);
        dest->key   = entry->key;
        dest->value = entry->value;
        table->len++;
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

    if (is_new_key && IS_NIL(entry->value)) {
        table->len++;
    }

    entry->key   = key;
    entry->value = value;

    return is_new_key;
}

bool table_delete(Table *table, ObjString *key) {
    if (table->len == 0) return false;

    Entry *entry = find_entry(table->entries, table->cap, key);
    if (entry->key == NULL) return false;

    // place a tombstone in the entry
    entry->key   = NULL;
    entry->value = BOOL_VAL(true);

    return true;
}

void table_add_all(Table *from, Table *to) {
    for (int i = 0; i < from->cap; i++) {
        Entry *entry = &from->entries[i];
        if (entry->key != NULL) {
            table_set(to, entry->key, entry->value);
        }
    }
}

ObjString *table_find_string(Table *table, const char *chars, int length, uint32_t hash) {
    if (table->len == 0) return NULL;

    uint32_t index = hash % table->cap;
    for (;;) {
        Entry *entry = &table->entries[index];
        if (entry->key == NULL) {
            // stop if we find an empty non-tombstone entry
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->len == length &&
                   entry->key->hash == hash &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            // we found it
            return entry->key;
        }

        index = (index + 1) % table->cap;
    }
}

void mark_table(Table *table) {
    for (int i = 0; i < table->cap; i++) {
        Entry *entry = &table->entries[i];
        mark_object((Obj *)entry->key);
        mark_value(entry->value);
    }
}