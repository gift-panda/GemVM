#include "memory.h"
#include "value.h"
#include "vm.h"

static Value listIteratorNative(Thread* ctx, int argCount, Value* args) {
    Value classVal;
    ObjString* className = copyString("ListIterator", 12);
    if (!tableGet(&vm.globals, className, &classVal)) {
        runtimeErrorCtx(ctx, vm.lookUpErrorClass, "ListIterator class not found.");
        return NIL_VAL;
    }

    ObjClass* iteratorClass = AS_CLASS(classVal);
    ObjInstance* instance = newInstance(iteratorClass);

    Value listVal = args[-1];
    ObjString* listField = copyString("list", 4);
    tableSet(&instance->fields, listField, listVal);

    ObjString* indexField = copyString("index", 5);
    tableSet(&instance->fields, indexField, NUMBER_VAL(0));

    return OBJ_VAL(instance);
}

static Value listAppendNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method append for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    writeValueArray(&list->elements, args[0]);
    return OBJ_VAL(list);
}

static Value listPeekNative(Thread* ctx, int argCount, Value* args){
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method append for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    return list->elements.values[list->elements.count - 1];
}

static Value listLengthNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method length for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    return NUMBER_VAL(list->elements.count);
}

static Value listGetNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method get for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "get: expected (Number) but got (%s).",
                     getValueTypeName(args[0]));
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);
    if (index < 0 || index >= list->elements.count) {
        runtimeErrorCtx(ctx, vm.indexErrorClass,
                     "get: index %d out of range (0–%d).",
                     index, list->elements.count - 1);
        return NIL_VAL;
    }

    return list->elements.values[index];
}

static Value listSetNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 2) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method set for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "set: expected (Number, Any) but got (%s, %s).",
                     getValueTypeName(args[0]), getValueTypeName(args[1]));
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);
    if (index < 0 || index >= list->elements.count) {
        runtimeErrorCtx(ctx, vm.indexErrorClass,
                     "set: index %d out of range (0–%d).",
                     index, list->elements.count - 1);
        return NIL_VAL;
    }

    list->elements.values[index] = args[1];
    return args[1];
}

static Value listPopNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method pop for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    if (list->elements.count == 0) {
        runtimeErrorCtx(ctx, vm.indexErrorClass, "pop: empty list.");
        return NIL_VAL;
    }

    list->elements.count--;
    return list->elements.values[list->elements.count];
}

static Value listInsertNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 2) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method insert for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "insert: expected (Number, Any) but got (%s, %s).",
                     getValueTypeName(args[0]), getValueTypeName(args[1]));
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);
    if (index < 0 || index > list->elements.count) {
        runtimeErrorCtx(ctx, vm.indexErrorClass,
                     "insert: index %d out of range (0–%d).",
                     index, list->elements.count);
        return NIL_VAL;
    }

    if (list->elements.count + 1 > list->elements.capacity) {
        int oldCap = list->elements.capacity;
        int newCap = GROW_CAPACITY(oldCap);
        list->elements.values =
            GROW_ARRAY(Value, list->elements.values, oldCap, newCap);
        list->elements.capacity = newCap;
    }

    for (int i = list->elements.count; i > index; i--) {
        list->elements.values[i] = list->elements.values[i - 1];
    }

    list->elements.values[index] = args[1];
    list->elements.count++;
    return args[1];
}

static Value listClearNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method clear for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    list->elements.count = 0;
    return NIL_VAL;
}

static Value listContainsNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method contains for arity %d.", argCount);
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

static Value listRemoveNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method remove for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "remove: expected (Number) but got (%s).",
                     getValueTypeName(args[0]));
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    int index = (int)AS_NUMBER(args[0]);
    if (index < 0 || index >= list->elements.count) {
        runtimeErrorCtx(ctx, vm.indexErrorClass,
                     "remove: index %d out of range (0–%d).",
                     index, list->elements.count - 1);
        return NIL_VAL;
    }

    Value removed = list->elements.values[index];
    for (int i = index; i < list->elements.count - 1; i++) {
        list->elements.values[i] = list->elements.values[i + 1];
    }

    list->elements.count--;
    return removed;
}

static bool compareWithComparator(Thread* ctx, Value comparator, Value a, Value b) {
    Value args[3] = {comparator, a, b };
    Value result = spawnNative(NULL, 3, args);
    
    result = joinInternal(result);
    
    if (!IS_BOOL(result) && !IS_NUMBER(result)) {
        runtimeErrorCtx(ctx, vm.typeErrorClass,
                     "Comparator must return a boolean, got %s.",
                     getValueTypeName(result));
        return false;
    }

    return AS_BOOL(result);
}

static void quickSort(Thread* ctx, Value* arr, int left, int right, Value comparator, bool useComparator) {
    if (left >= right) return;

    Value pivot = arr[right];
    int i = left - 1;

    for (int j = left; j < right; j++) {
        bool lessOrEqual;
        if (useComparator) {
            lessOrEqual = compareWithComparator(ctx, comparator, arr[j], pivot);
        } else {
            lessOrEqual = AS_NUMBER(arr[j]) <= AS_NUMBER(pivot);
        }

        if (lessOrEqual) {
            i++;
            Value tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
        }
    }

    Value tmp = arr[i + 1];
    arr[i + 1] = arr[right];
    arr[right] = tmp;

    int pi = i + 1;
    quickSort(ctx, arr, left, pi - 1, comparator, useComparator);
    quickSort(ctx, arr, pi + 1, right, comparator, useComparator);
}

static Value listSortNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0 && argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "sort() expects 0 or 1 argument, got %d.", argCount);
        return NIL_VAL;
    }

    ObjList* list = AS_LIST(args[-1]);
    if (list->elements.count <= 1) return OBJ_VAL(list);

    Value comparator = NIL_VAL;
    bool useComparator = false;

    if (argCount == 1) {
        if (!IS_MULTI_DISPATCH(args[0]) && !IS_CLOSURE(args[0])) {
            runtimeErrorCtx(ctx, vm.typeErrorClass,
                         "sort(comparator): comparator must be a function.");
            return NIL_VAL;
        }

        if(IS_MULTI_DISPATCH(args[0])){
            ObjMultiDispatch* dispatch = AS_MULTI_DISPATCH(args[0]);
            if (dispatch->closures[1] != NULL) {
                runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                            "Comparator must have arity 2.");
                return NIL_VAL;
    
            }
        }
        else if(IS_CLOSURE(args[0])){
            ObjClosure* closure = AS_CLOSURE(args[0]);
            if(closure->function->arity != 2){
                runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                            "Comparator must have arity 2.");
                return NIL_VAL;
            }
        }

        comparator = args[0];
        useComparator = true;
    }

    // Type check elements if no comparator
    if (!useComparator) {
        for (int i = 0; i < list->elements.count; i++) {
            if (!IS_NUMBER(list->elements.values[i])) {
                runtimeErrorCtx(ctx, vm.typeErrorClass,
                             "sort: unsupported element type (%s).",
                             getValueTypeName(list->elements.values[i]));
                return NIL_VAL;
            }
        }
    }

    quickSort(ctx, list->elements.values, 0, list->elements.count - 1, comparator, useComparator);
    return OBJ_VAL(list);
}

