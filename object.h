#ifndef clox_object_h
#define clox_object_h
#include <SDL_render.h>

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
#define IS_STRING(value)       isObjType(value, OBJ_STRING)

#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))

#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define AS_NATIVE(value) (((ObjNative*)AS_OBJ(value))->function)

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

#define IS_IMAGE(value)    isObjType(value, OBJ_IMAGE)
#define AS_IMAGE(value)    ((ObjImage*)AS_OBJ(value))

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
} ObjType;

struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj* next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;


typedef struct ObjClass{
    Obj obj;
    ObjString* name;
    Table methods;
    Table staticVars;
    Table staticMethods;
    struct ObjClass* superclass;
}ObjClass;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
    ObjClass* klass;
} ObjClosure;

typedef struct {
    Obj obj;
    ObjClass* klass;
    Table fields;
} ObjInstance;

typedef struct {
    Obj obj;
    ObjString* name;
    Value receiver;
    ObjClosure* method[10];
} ObjBoundMethod;

typedef struct {
    Obj obj;
    ValueArray elements;
} ObjList;

typedef struct {
    Obj obj;
    ObjString* name;
    ObjClosure* closures[10]; // Indexed by arity
} ObjMultiDispatch;

typedef struct {
    Obj obj;
    SDL_Texture* texture;
    int width;
    int height;
} ObjImage;


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
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
