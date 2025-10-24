#ifndef CLOX_VM_H
#define CLOX_VM_H

#include <stdbool.h>
#include <stdint.h>
#include "value.h"
#include "table.h"
#include "pthread.h"

// Forward declarations to break cyclic dependency
typedef struct ObjClosure ObjClosure;
typedef struct ObjClass ObjClass;

// ---------------------
// Call frames and threads
// ---------------------
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;

    int hasTry[10];
    int tryTop;
    uint8_t *saveIP[10];
    Value* saveStack[10];
    ObjClass* klass;
} CallFrame;

typedef struct Thread {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;

    struct ObjUpvalue* openUpvalues; // forward-declared elsewhere
    bool hasError;
    bool finished;
} Thread;

// ---------------------
// VM
// ---------------------
typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals;
    Table strings;

    struct ObjUpvalue* openUpvalues;
    struct Obj* objects;

    int grayCount;
    int grayCapacity;
    struct Obj** grayStack;

    size_t bytesAllocated;
    size_t nextGC;
    size_t maxRAM;

    ObjString* initString;
    ObjString* toString;
    Table stringClassMethods;
    Table listClassMethods;
    Table imageClassMethods;
    Table threadClassMethods;

    ObjString* errorString;
    ObjClass* errorClass;
    ObjString* indexErrorString;
    ObjClass* indexErrorClass;
    ObjString* typeErrorString;
    ObjClass* typeErrorClass;
    ObjString* nameErrorString;
    ObjClass* nameErrorClass;
    ObjString* accessErrorString;
    ObjClass* accessErrorClass;
    ObjString* illegalArgumentsErrorString;
    ObjClass* illegalArgumentsErrorClass;
    ObjString* lookUpErrorString;
    ObjClass* lookUpErrorClass;
    ObjString* formatErrorString;
    ObjClass* formatErrorClass;

    pthread_t main;

    const char* path;
    bool isInvokingNative;
    bool gcEnabled;
    bool showBytecode;
    bool noRun;
    bool repl;
    bool hasError;
} VM;

// ---------------------
// Interpret result
// ---------------------
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
InterpretResult load(const char* source);
void push(Value value);
Value pop();
void printStack();
CallFrame* runtimeError(ObjClass*, const char* format, ...);

#endif
