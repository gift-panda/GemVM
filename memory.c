#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "memory.h"
#include "compiler.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 1.3

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;

    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
        if (pthread_self() == vm.main && vm.gcEnabled)
            if (vm.bytesAllocated > vm.nextGC) {
                collectGarbage();
            }
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;

    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj**)realloc(vm.grayStack,
                                      sizeof(Obj*) * vm.grayCapacity);

        if (vm.grayStack == NULL) {
            fprintf(stderr, "Not enough memory for GC.\n");
            exit(1);
        }
    }

    vm.grayStack[vm.grayCount++] = object;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
}

void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    switch (object->type) {
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            markObject((Obj*)klass->name);
            markTable(&klass->methods);
            markTable(&klass->staticMethods);
            markTable(&klass->staticVars);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            markObject((Obj*)instance->klass);
            markTable(&instance->fields);
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* method = (ObjBoundMethod*)object;
            markObject((Obj*)method->name);
            for (int i = 0; i < 10; i++) {
                if (method->method[i] != NULL)
                    markObject((Obj*)method->method[i]);
            }
            break;
        }
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            markArray(&list->elements);
            break;
        }
        case OBJ_MULTI_DISPATCH: {
            ObjMultiDispatch* method = (ObjMultiDispatch*)object;
            markObject((Obj*)method->name);
            for (int i = 0; i < 10; i++) {
                if (method->closures[i] != NULL)
                    markObject((Obj*)method->closures[i]);
            }
            break;
        }
    }
}

static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_CLOSURE: {
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues,
                       closure->upvalueCount);

            FREE(ObjUpvalue, object);
            break;
        }
        case OBJ_CLASS: {
            ObjClass* klass = (ObjClass*)object;
            freeTable(&klass->methods);
            freeTable(&klass->staticMethods);
            freeTable(&klass->staticVars);
            FREE(ObjClass, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* instance = (ObjInstance*)object;
            freeTable(&instance->fields);
            FREE(ObjInstance, object);
            break;
        }
        case OBJ_BOUND_METHOD:
            FREE(ObjBoundMethod, object);
            break;
        case OBJ_LIST: {
            ObjList* list = (ObjList*)object;
            freeValueArray(&list->elements);
            FREE(ObjList, object);
            break;
        }
        case OBJ_MULTI_DISPATCH: {
            FREE(ObjMultiDispatch, object);
            break;
        }
    }
}

void freeObjects() {
    printf("freeing things"); fflush(stdout);
    
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
        printf("%p\n", next);
    }

    free(vm.grayStack);
}

static void markRoots() {
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    markTable(&vm.globals);
    markCompilerRoots();
    markObject((Obj*)vm.initString);
}

static void traceReferences() {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep() {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

size_t getUsedRAM() {
    FILE* file = fopen("/proc/meminfo", "r");
    if (!file) return 0;

    size_t total = 0, available = 0;
    char key[64];
    size_t value;
    char unit[16];

    while (fscanf(file, "%63s %zu %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) total = value;
        else if (strcmp(key, "MemAvailable:") == 0) available = value;
        if (total && available) break;
    }

    fclose(file);
    return (total - available) * 1024;  // kB -> bytes
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytesAllocated;
#endif

    atomic_store(&vm.gcRequest, true);
    pthread_mutex_lock(&vm.lock);

    while (atomic_load(&vm.waiting) < atomic_load(&vm.threads)) {
        pthread_cond_wait(&vm.cond, &vm.lock);
    }

    pthread_mutex_unlock(&vm.lock);

    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    sweep();

    if (getUsedRAM() < 2.0 / 3 * vm.maxRAM) {
        vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    }

    pthread_mutex_lock(&vm.lock);
    atomic_store(&vm.gcRequest, false);
    pthread_cond_broadcast(&vm.cond); // wake all threads
    pthread_mutex_unlock(&vm.lock);

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif

}

