#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include <SDL_render.h>
#include <pthread.h>
#include <stdbool.h>
#include "value.h"
#include "table.h"
#include "chunk.h"

// Forward declarations to break cyclic dependency
typedef struct Thread Thread;
typedef struct ObjClosure ObjClosure;
typedef struct ObjClass ObjClass;

// ---------------------
// Object type macros
// ---------------------
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
#define IS_STRING(value)       isObjType(value, OBJ_STRING)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define IS_LIST(value)         isObjType(value, OBJ_LIST)
#define AS_LIST(value)         ((ObjList*)AS_OBJ(value))
#define IS_ERROR(value)        isObjType(value, OBJ_ERROR)
#define AS_ERROR(value)        ((ObjError*)AS_OBJ(value))
#define IS_MULTI_DISPATCH(value) isObjType(value, OBJ_MULTI_DISPATCH)
#define AS_MULTI_DISPATCH(value) ((ObjMultiDispatch*)AS_OBJ(value))
#define IS_IMAGE(value)        isObjType(value, OBJ_IMAGE)
#define AS_IMAGE(value)        ((ObjImage*)AS_OBJ(value))
#define IS_THREAD(value)       isObjType(value, OBJ_THREAD)
#define AS_THREAD(value)       ((ObjThread*)AS_OBJ(value))

// ---------------------
// Object types
// ---------------------
typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_LIST,
    OBJ_MULTI_DISPATCH,
    OBJ_ERROR,
    OBJ_IMAGE,
    OBJ_THREAD,
} ObjType;

struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj* next;
};

// ---------------------
// Concrete objects
// ---------------------
typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    struct ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

typedef struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
} ObjString;

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct ObjClass {
    Obj obj;
    ObjString* name;
    Table methods;
    Table staticVars;
    Table staticMethods;
    struct ObjClass* superclass;
} ObjClass;

typedef struct ObjClosure {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
    ObjClass* klass;
} ObjClosure;

typedef struct ObjInstance {
    Obj obj;
    ObjClass* klass;
    Table fields;
} ObjInstance;

typedef struct ObjBoundMethod {
    Obj obj;
    ObjString* name;
    Value receiver;
    ObjClosure* method[10];
} ObjBoundMethod;

typedef struct ObjList {
    Obj obj;
    ValueArray elements;
} ObjList;

typedef struct ObjMultiDispatch {
    Obj obj;
    ObjString* name;
    ObjClosure* closures[10];
} ObjMultiDispatch;

typedef struct ObjImage {
    Obj obj;
    SDL_Texture* texture;
    int width;
    int height;
} ObjImage;

typedef struct ObjThread {
    Obj obj;
    pthread_t *thread;
    Thread *ctx; // pointer is ok with forward declaration
} ObjThread;

// ---------------------
// Functions
// ---------------------
ObjBoundMethod* newBoundMethod(Value receiver, ObjString*);
ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjClass* klass);
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot);
ObjList* newList();
ObjMultiDispatch* newMultiDispatch(ObjString*);
ObjImage* newImage(SDL_Texture* texture, int width, int height);
ObjThread* newThread(pthread_t *thread, Thread *ctx);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
