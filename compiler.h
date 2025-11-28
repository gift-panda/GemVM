#ifndef clox_compiler_h
#define clox_compiler_h
#include "chunk.h"
#include "object.h"


ObjFunction* compile(const char* source);
Value preprocessorNative(Thread* ctx, int argCount, Value* args);
void markCompilerRoots();

#endif
