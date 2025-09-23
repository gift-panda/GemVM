#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include <stdbool.h>


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

typedef struct{
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;

    ObjUpvalue* openUpvalues;
    bool hasError;
    bool finished;
} Thread;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals;
    Table strings;
    ObjUpvalue* openUpvalues;
    Obj* objects;

    int grayCount;
    int grayCapacity;
    Obj** grayStack;

    size_t bytesAllocated;
    size_t nextGC;
    size_t maxRAM;

    ObjString* initString;
    ObjString* toString;
    Table stringClassMethods;
    Table listClassMethods;
    Table imageClassMethods;

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

    bool isInvokingNative;
    bool gcEnabled;
    bool showBytecode;
    bool noRun;
    bool repl;
    bool hasError;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();
void printStack();
CallFrame* runtimeError(ObjClass*, const char* format, ...);

#endif
