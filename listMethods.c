#include "memory.h"
#include "value.h"
#include "vm.h"

static Value listAppendNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("list.append() takes a value to append.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    writeValueArray(&list->elements, args[0]);
    return OBJ_VAL(list);
}

static Value listLengthNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("list.length() takes no arguments.");
        return NIL_VAL;
    }
    ObjList* list = AS_LIST(args[-1]);
    return NUMBER_VAL(list->elements.count);
}

static Value listGetNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        runtimeError("list.get(index) takes a single integer argument.");
        return NIL_VAL;
    }

    if (!IS_LIST(args[-1])) {
        runtimeError("list.get() must be called on a list.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);

    if (index < 0 || index >= list->elements.count) {
        runtimeError("list.get() index out of bounds.");
        return NIL_VAL;
    }

    return list->elements.values[index];
}

static Value listSetNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_NUMBER(args[0])) {
        runtimeError("list.set(index, value) takes an index and a value.");
        return NIL_VAL;
    }

    if (!IS_LIST(args[-1])) {
        runtimeError("list.set() must be called on a list.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);

    if (index < 0 || index >= list->elements.count) {
        runtimeError("list.set() index out of bounds.");
        return NIL_VAL;
    }

    list->elements.values[index] = args[1];
    return args[1];
}

static Value listPopNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("list.pop() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_LIST(args[-1])) {
        runtimeError("list.pop() must be called on a list.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);

    if (list->elements.count == 0) {
        runtimeError("list.pop() on empty list.");
        return NIL_VAL;
    }

    list->elements.count--;
    return list->elements.values[list->elements.count];
}

static Value listInsertNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_NUMBER(args[0])) {
        runtimeError("list.insert(index, value) takes an index and a value.");
        return NIL_VAL;
    }

    if (!IS_LIST(args[-1])) {
        runtimeError("list.insert() must be called on a list.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);

    if (index < 0 || index > list->elements.count) {
        runtimeError("list.insert() index out of bounds.");
        return NIL_VAL;
    }

    // Resize if necessary
    if (list->elements.count + 1 > list->elements.capacity) {
        int oldCapacity = list->elements.capacity;
        int newCapacity = GROW_CAPACITY(oldCapacity);
        list->elements.values = GROW_ARRAY(Value, list->elements.values, oldCapacity, newCapacity);
        list->elements.capacity = newCapacity;
    }

    // Shift elements right
    for (int i = list->elements.count; i > index; i--) {
        list->elements.values[i] = list->elements.values[i - 1];
    }

    list->elements.values[index] = args[1];
    list->elements.count++;
    return args[1];
}

static Value listClearNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("list.clear() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_LIST(args[-1])) {
        runtimeError("list.clear() must be called on a list.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    list->elements.count = 0;
    return NIL_VAL;
}

static Value listContainsNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("list.contains(value) takes exactly one argument.");
        return NIL_VAL;
    }

    if (!IS_LIST(args[-1])) {
        runtimeError("list.contains() must be called on a list.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    Value target = args[0];

    for (int i = 0; i < list->elements.count; i++) {
        if (valuesEqual(list->elements.values[i], target)) {
            return BOOL_VAL(true);
        }
    }

    return BOOL_VAL(false);
}

static Value listRemoveNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[0])) {
        runtimeError("list.remove(index) takes exactly one integer argument.");
        return NIL_VAL;
    }

    if (!IS_LIST(args[-1])) {
        runtimeError("list.remove() must be called on a list.");
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);

    if (index < 0 || index >= list->elements.count) {
        runtimeError("list.remove() index out of bounds.");
        return NIL_VAL;
    }

    Value removed = list->elements.values[index];

    for (int i = index; i < list->elements.count - 1; i++) {
        list->elements.values[i] = list->elements.values[i + 1];
    }

    list->elements.count--;
    return removed;
}