#ifndef CLOX_VM_H
#define CLOX_VM_H

#include <stdbool.h>
#include <stdint.h>
#include "value.h"
#include "table.h"
#include "pthread.h"
#include "object.h"
#include <stdatomic.h>

// Forward declarations to break cyclic dependency
typedef struct ObjClosure ObjClosure;
typedef struct ObjClass ObjClass;

// ---------------------
// Call frames and threads
// ---------------------
#define FRAMES_MAX 1000
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
    ObjInstance* receiver;
} CallFrame;

typedef struct Thread {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;

    Table* namespace;

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

    ObjString* initString;
    ObjString* toString;
    Table stringClassMethods;
    Table listClassMethods;
    Table imageClassMethods;
    Table threadClassMethods;

    ObjFunction* fileCompiler;
    ObjFunction* sourceCompiler;

    ObjClass* stringClass;
    ObjClass* listClass;
    ObjClass* imageClass;
    ObjClass* threadClass;
    ObjClass* numberClass;
    ObjClass* boolClass;

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


    const char* path;
    bool isInvokingNative;
    bool gcEnabled;
    bool showBytecode;
    bool noRun;
    bool repl;
    bool hasError;
    bool zip;

} VM;

// ---------------------
// Interpret result
// ---------------------
typedef enum {
    INTERPRET_OK,
    COMPILE_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
    BYTECODE_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpretBootStrapped(const char* source);
InterpretResult interpret(const char* source);
InterpretResult load(const char* source);
InterpretResult callFunction(ObjFunction* function);
void push(Value value);
Value pop();
void printStack();
CallFrame* runtimeErrorCtx(Thread*, ObjClass*, const char* format, ...);

Value spawnNative(Thread* ctx, int argCount, Value* args);
Value joinNative(Thread* ctx, int argCount, Value* args);
Value joinInternal(Value arg);
#endif
