#include <stdio.h>

#include "memory.h"
#include "value.h"

void initValueArray(ValueArray *array)
{
    array->values = NULL;
    array->cap = 0;
    array->len = 0;
}

void writeValueArray(ValueArray *array, Value value)
{
    if (array->cap < array->len + 1)
    {
        int oldcap = array->cap;
        array->cap = GROW_CAPACITY(oldcap);
        array->values = GROW_ARRAY(Value, array->values, oldcap, array->cap);
    }

    array->values[array->len] = value;
    array->len++;
}

void freeValueArray(ValueArray *array)
{
    FREE_ARRAY(Value, array->values, array->cap);
    initValueArray(array);
}

void printValue(Value value)
{
    printf("%g", value);
}