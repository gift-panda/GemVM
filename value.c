#include <stdio.h>

#include "memory.h"
#include "value.h"

#include <string.h>

#include "object.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values,
                                   oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value) {
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL: printf("nil"); break;
        case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ: printObject(value); break;


    }
}

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ:    
            if (AS_OBJ(a)->type == OBJ_STRING && AS_OBJ(b)->type == OBJ_STRING)
                return (
                    AS_STRING(a)->length == AS_STRING(b)->length &&
                    memcmp(AS_STRING(a)->chars, AS_STRING(b)->chars, AS_STRING(a)->length) == 0
                );
            return AS_OBJ(a) == AS_OBJ(b);
            
        default:         return false; // Unreachable.
    }
}

char* getValueTypeName(Value value){
    switch (value.type) {
    case VAL_BOOL:   return "Boolean";
    case VAL_NIL:    return "Nil";
    case VAL_NUMBER: return "Number";
    case VAL_OBJ:
        switch (OBJ_TYPE(value)) {
            case OBJ_STRING:   return "String";
            case OBJ_FUNCTION: return "Function";
            case OBJ_NATIVE:   return "Native";
            case OBJ_CLOSURE:  return "Closure";
            case OBJ_UPVALUE:  return "Upvalue";
            case OBJ_CLASS:    return "Class";
            case OBJ_INSTANCE: return "Instance";
            case OBJ_BOUND_METHOD: return "BoundMethod";
            case OBJ_MULTI_DISPATCH: return "MultiDispatch";
            case OBJ_LIST:     return "List";
            case OBJ_ERROR:    return "Error";
            default:           return "Object";
        }
    }
    return "Unknown";
}
