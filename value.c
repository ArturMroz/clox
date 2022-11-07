#include <stdio.h>

#include "memory.h"
#include "value.h"

void init_value_array(ValueArray *array)
{
    array->values = NULL;
    array->cap = 0;
    array->len = 0;
}

void write_value_array(ValueArray *array, Value value)
{
    if (array->cap < array->len + 1)
    {
        int old_cap = array->cap;
        array->cap = GROW_CAPACITY(old_cap);
        array->values = GROW_ARRAY(Value, array->values, old_cap, array->cap);
    }

    array->values[array->len] = value;
    array->len++;
}

void free_value_array(ValueArray *array)
{
    FREE_ARRAY(Value, array->values, array->cap);
    init_value_array(array);
}

void print_value(Value value)
{
    printf("%g", value);
}