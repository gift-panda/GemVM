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

Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;

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

ObjBoundNative* newBoundNative(NativeFn* fn) {
    ObjBoundNative* bound = ALLOCATE_OBJ(ObjBoundNative,
                                         OBJ_BOUND_NATIVE);
    bound->receiver = NIL_VAL;
    bound->method = fn;
    return bound;
}

ObjClass* newClass(ObjString* name) {
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    initTable(&klass->methods);
    initTable(&klass->staticVars);
    initTable(&klass->staticMethods);
    klass->superclass = NULL;
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
    closure->klass = NULL;
    return closure;
}

uint32_t hashString(const char* key, int length) {
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

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    string->instance = newInstance(vm.stringClass);

    tableSet(&vm.strings, string, NIL_VAL);

    return string;
}

ObjString* newString(char* chars, int length){
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hashString(chars, length);
    string->instance = newInstance(vm.stringClass);

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
    list->instance = newInstance(vm.listClass);
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

ObjThread* newThread(pthread_t *thread, Thread *ctx){
    ObjThread* threadObj = ALLOCATE_OBJ(ObjThread, OBJ_THREAD);
    threadObj->thread = thread;
    threadObj->ctx = ctx;
    threadObj->instance = newInstance(vm.threadClass);
    return threadObj;
}

ObjNamespace* newNamespace(ObjString* name){
    ObjNamespace* ns = ALLOCATE_OBJ(ObjNamespace, OBJ_NAMESPACE);
    ns->namespace = ALLOCATE(Table, 1);
    initTable(ns->namespace);
    ns->name = name;
    return ns;
}

ObjString* takeString(char* chars, int length) {
    // Allocate enough space (worst case: no escapes)
    char* unescaped = ALLOCATE(char, length + 1);
    int write = 0;

    for (int read = 0; read < length; read++) {
        if (chars[read] == '\\' && read + 1 < length) {
            read++;
            switch (chars[read]) {
                case 'n':  unescaped[write++] = '\n'; break;
                case 'r':  unescaped[write++] = '\r'; break;
                case 't':  unescaped[write++] = '\t'; break;
                case '"':  unescaped[write++] = '"';  break;
                case '\'': unescaped[write++] = '\''; break;
                case '\\': unescaped[write++] = '\\'; break;
                case '0':  unescaped[write++] = '\0'; break;
                default:
                    // Unknown escape → preserve both backslash and char
                    unescaped[write++] = '\\';
                    unescaped[write++] = chars[read];
                    break;
            }
        } else {
            unescaped[write++] = chars[read];
        }
    }

    unescaped[write] = '\0';

    uint32_t hash = hashString(unescaped, write);


    ObjString* interned = tableFindString(&vm.strings, unescaped, write, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        FREE_ARRAY(char, unescaped, length + 1);
        return interned;
    }

    FREE_ARRAY(char, chars, length + 1);
    return allocateString(unescaped, write, hash);
}

ObjString* copyString(const char* chars, int length) {
    // Allocate buffer large enough (worst case: no escapes)
    char* unescaped = ALLOCATE(char, length + 1);
    int write = 0;

    for (int read = 0; read < length; read++) {
        if (chars[read] == '\\' && read + 1 < length) {
            read++;
            switch (chars[read]) {
                case 'n':  unescaped[write++] = '\n'; break;
                case 'r':  unescaped[write++] = '\r'; break;
                case 't':  unescaped[write++] = '\t'; break;
                case '"':  unescaped[write++] = '"';  break;
                case '\'': unescaped[write++] = '\''; break;
                case '\\': unescaped[write++] = '\\'; break;
                case '0':  unescaped[write++] = '\0'; break;
                default:
                    unescaped[write++] = '\\';
                    unescaped[write++] = chars[read];
                    break;
            }
        } else {
            unescaped[write++] = chars[read];
        }
    }

    unescaped[write] = '\0';

    uint32_t hash = hashString(unescaped, write);


    ObjString* interned = tableFindString(&vm.strings, unescaped, write, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, unescaped, length + 1);
        return interned;
    }

    return allocateString(unescaped, write, hash);
}


ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->next = NULL;
    upvalue->closed = NIL_VAL;
    return upvalue;
}

ObjImage* newImage(SDL_Texture* texture, int width, int height) {
    ObjImage* image = ALLOCATE_OBJ(ObjImage, OBJ_IMAGE);
    image->texture = texture;
    image->width = width;
    image->height = height;
    image->instance = newInstance(vm.imageClass);
    
    return image;
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
            fflush(stdout);
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
            ObjMultiDispatch* method = AS_MULTI_DISPATCH(value);
            printf("<fn %s>", method->name->chars);
            break;
        }
        case OBJ_NAMESPACE:{
            ObjNamespace* ns = AS_NAMESPACE(value);
            printf("<ns %s>", ns->name->chars);
            break;
        }
    }
    fflush(stdout);
}
