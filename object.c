//
// Created by meow on 6/16/25.
//

#include "object.h"
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"

#include <sys/types.h>

#include "value.h"
#include "vm.h"


#define ALLOCATE_OBJ(type, objectType) \
(type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;

    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

    return object;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjString* name) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod,
                                         OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    for (int i = 0; i < 10; i++) {
        bound->method[i] = NULL;
    }
    bound->name = name;
    return bound;
}

ObjClass* newClass(ObjString* name) {
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    initTable(&klass->methods);
    initTable(&klass->staticVars);
    initTable(&klass->staticMethods);
    return klass;
}

ObjClosure* newClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*,
                                   function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();

    initTable(&string->methods);
    tableAddAll(&vm.stringClassMethods, &string->methods);

    return string;
}

ObjFunction* newFunction() {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    function->upvalueCount = 0;
    return function;
}

ObjMultiDispatch* newMultiDispatch(ObjString* name) {
    ObjMultiDispatch* dispatch = ALLOCATE_OBJ(ObjMultiDispatch, OBJ_MULTI_DISPATCH);
    dispatch->name = name;
    for (int i = 0; i < 10; i++) {
        dispatch->closures[i] = NULL;
    }
    return dispatch;
}


ObjList* newList() {
    ObjList* list = ALLOCATE_OBJ(ObjList, OBJ_LIST);
    initValueArray(&list->elements);

    tableAddAll(&vm.listClassMethods, &list->methods);

    return list;
}


ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

ObjNative* newNative(NativeFn function) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjError* newError(ObjString* message) {
    ObjError* error = ALLOCATE_OBJ(ObjError, OBJ_ERROR);
    error->message = message;
    return error;
}

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 0x811C9DC5u; // same offset as FNV-1a for compatibility
    const uint32_t prime = 0x01000193u;

    int i = 0;
    // Fast path: process 4 bytes at a time
    while (i + 4 <= length) {
        uint32_t chunk;
        memcpy(&chunk, key + i, 4);  // safely read 4 bytes
        hash ^= chunk;
        hash *= prime;
        i += 4;
    }

    // Tail: process remaining bytes
    while (i < length) {
        hash ^= (uint8_t)key[i++];
        hash *= prime;
    }

    return hash;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                       hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                        hash);
    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->next = NULL;
    upvalue->closed = NIL_VAL;
    return upvalue;
}

static void printFunction(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_INSTANCE:
            printf("%s instance",
                   AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_BOUND_METHOD:
            printf("<md ");
            printObject(OBJ_VAL(AS_BOUND_METHOD(value)->name));
            printf(">");
            break;
        case OBJ_LIST: {
            //printf("list");
            ObjList* list = AS_LIST(value);
            printf("[");
            for (int i = 0; i < list->elements.count; i++) {
                printValue(list->elements.values[i]);
                if (i != list->elements.count - 1) printf(", ");
            }
            printf("]");
            break;
        }
        case OBJ_MULTI_DISPATCH: {
            //printf("list");
            ObjMultiDispatch* method = AS_MULTI_DISPATCH(value);
            printf("<fn %s>", method->name->chars);
            break;
        }
        case OBJ_ERROR: {
#ifdef DEBUG_TRACE_EXECUTION
            printf("<error>");
            break;
#endif
            ObjError* error = AS_ERROR(value);
            printf("%s", error->message->chars);
            break;

        }
    }
    fflush(stdout);
}
