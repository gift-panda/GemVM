#include "common.h"
#include "vm.h"

#include <ctype.h>
#include <stdio.h>

#include "compiler.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "memory.h"
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <setjmp.h>

#include "debug.h"
#include "stringMethods.c"
#include "listMethods.c"
#include "windowMethods.h"
#include "Math.c"
#include <pthread.h>
#include <gc.h>

extern jmp_buf repl_env;

VM vm;
void printStackCtx(Thread *ctx);
void printStack();

ObjClass* errorClass;
bool replError;
pthread_t *threads;
int threadCount = 0;

static Value sleepNative(Thread* ctx, int argCount, Value* args){
    int ms = AS_NUMBER(args[0]);
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
    return NIL_VAL;
}

#include <stdatomic.h>
static Value syncNative(Thread* ctx, int argCount, Value* args){
    __atomic_thread_fence(memory_order_seq_cst);
    return NIL_VAL;
}

static void resetStackCtx(Thread *ctx);
void pushCtx(Thread *ctx, Value);
Value popCtx(Thread *ctx);
static bool callCtx(Thread *ctx, ObjClosure* closure, int argCount);
static bool callValueCtx(Thread *ctx, Value callee, int argCount);
static void* runCtx(void*);

Value spawnNative(Thread*, int argCount, Value* args) {
    pthread_t *tid = malloc(sizeof(pthread_t)); 
    if (!tid) {
        perror("malloc");
        exit(1);
    }

    Thread *ctx = GC_MALLOC(sizeof(Thread));
    memset(ctx, 0, sizeof(Thread));
    resetStackCtx(ctx);
    ctx->namespace = NULL;

    for (int i = 0; i < argCount; i++)
        pushCtx(ctx, args[i]);

    callValueCtx(ctx, args[0], argCount - 1);

    int res = pthread_create(tid, NULL, runCtx, ctx);
    if (res != 0) {
        printf("pthread_create failed: %s\n", strerror(res));
        exit(1);
    }

    return OBJ_VAL(newThread(tid, ctx));
}

Value spawnNamespace(ObjClosure* closure, ObjNamespace* namespace) {
    pthread_t *tid = malloc(sizeof(pthread_t)); 
    if (!tid) {
        perror("malloc");
        exit(1);
    }

    Thread *ctx = GC_MALLOC(sizeof(Thread));
    memset(ctx, 0, sizeof(Thread));
    resetStackCtx(ctx);
    ctx->namespace = namespace->namespace;

    pushCtx(ctx, OBJ_VAL(closure));
    callValueCtx(ctx, OBJ_VAL(closure), 0);

    int res = pthread_create(tid, NULL, runCtx, ctx);
    if (res != 0) {
        printf("pthread_create failed: %s\n", strerror(res));
        exit(1);
    }

    return OBJ_VAL(newThread(tid, ctx));
}

Value joinNative(Thread* ctx, int argCount, Value* args) {
    ObjThread* threadObj = AS_THREAD(args[-1]);

    pthread_join(*threadObj->thread, NULL);
    free(threadObj->thread);  // ✅ free after join
    threadObj->thread = NULL;

    return popCtx(threadObj->ctx);
}

Value joinInternal(Value arg) {
    ObjThread* threadObj = AS_THREAD(arg);

    pthread_join(*threadObj->thread, NULL);
    free(threadObj->thread);
    threadObj->thread = NULL;

    return popCtx(threadObj->ctx);
}

static Value clockNative(Thread* ctx, int argCount, Value* args) {
    if(argCount != 0){
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "clock() does not accept any argument.");
        return NIL_VAL;
    }
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value readNative(Thread* ctx, int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "Argument must be a string (file path).");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        runtimeErrorCtx(ctx, vm.accessErrorClass, "Could not open file \"%s\".", path);
        return NIL_VAL;
    }

    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char* buffer = (char*)GC_MALLOC(size + 1);
    if (buffer == NULL) {
        fclose(file);
        runtimeErrorCtx(ctx, vm.accessErrorClass, "Not enough memory to read file.");
        return NIL_VAL;
    }

    size_t bytesRead = fread(buffer, 1, size, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return OBJ_VAL(copyString(buffer, (int)bytesRead));
}

static Value inputNative(Thread* ctx, int argCount, Value* args) {
    if (argCount > 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "input() takes no or one arguments.");
        return NIL_VAL;
    }

    if (argCount == 1) {
        if (!IS_STRING(args[0])) {
            runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "input() prompt must be a string.");
            return NIL_VAL;
        }
        printf("%s", AS_CSTRING(args[0]));
    }

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        }
        return OBJ_VAL(copyString(buffer, len));
    }
    return NIL_VAL;
}

void defineStringMethods() {
    tableSet(&vm.stringClassMethods, copyString("length", 6), OBJ_VAL(newNative(stringLengthNative)));
    tableSet(&vm.stringClassMethods, copyString("charAt", 6), OBJ_VAL(newNative(stringCharAtNative)));
    tableSet(&vm.stringClassMethods, copyString("toUpperCase", 11), OBJ_VAL(newNative(stringToUpperCaseNative)));
    tableSet(&vm.stringClassMethods, copyString("toLowerCase", 11), OBJ_VAL(newNative(stringToLowerCaseNative)));
    tableSet(&vm.stringClassMethods, copyString("substring", 9), OBJ_VAL(newNative(stringSubstringNative)));
    tableSet(&vm.stringClassMethods, copyString("indexOf", 7), OBJ_VAL(newNative(stringIndexOfNative)));
    tableSet(&vm.stringClassMethods, copyString("asNum", 5), OBJ_VAL(newNative(stringParseNumberNative)));
    tableSet(&vm.stringClassMethods, copyString("asBool", 6), OBJ_VAL(newNative(stringParseBooleanNative)));
    tableSet(&vm.stringClassMethods, copyString("charCode", 8), OBJ_VAL(newNative(stringCharCodeNative)));
    tableSet(&vm.stringClassMethods, copyString("parse", 5), OBJ_VAL(newNative(str_parse)));
    tableSet(&vm.stringClassMethods, copyString("split", 5), OBJ_VAL(newNative(stringSplitNative)));
    tableSet(&vm.stringClassMethods, copyString("trim", 4), OBJ_VAL(newNative(stringTrimNative)));
    tableSet(&vm.stringClassMethods, copyString("startsWith", 10), OBJ_VAL(newNative(stringStartsWithNative)));
    tableSet(&vm.stringClassMethods, copyString("endsWith", 8), OBJ_VAL(newNative(stringEndsWithNative)));
    tableSet(&vm.stringClassMethods, copyString("isDigit", 7), OBJ_VAL(newNative(str_isDigit)));
    tableSet(&vm.stringClassMethods, copyString("iterator", 8), OBJ_VAL(newNative(stringIteratorNative)));
}

void defineListMethods() {
    tableSet(&vm.listClassMethods, copyString("append", 6), OBJ_VAL(newNative(listAppendNative)));
    tableSet(&vm.listClassMethods, copyString("length", 6), OBJ_VAL(newNative(listLengthNative)));
    tableSet(&vm.listClassMethods, copyString("get", 3), OBJ_VAL(newNative(listGetNative)));
    tableSet(&vm.listClassMethods, copyString("set", 3), OBJ_VAL(newNative(listSetNative)));
    tableSet(&vm.listClassMethods, copyString("pop", 3), OBJ_VAL(newNative(listPopNative)));
    tableSet(&vm.listClassMethods, copyString("insert", 6), OBJ_VAL(newNative(listInsertNative)));
    tableSet(&vm.listClassMethods, copyString("clear", 5), OBJ_VAL(newNative(listClearNative)));
    tableSet(&vm.listClassMethods, copyString("contains", 8), OBJ_VAL(newNative(listContainsNative)));
    tableSet(&vm.listClassMethods, copyString("remove", 6), OBJ_VAL(newNative(listRemoveNative)));
    tableSet(&vm.listClassMethods, copyString("sort", 4), OBJ_VAL(newNative(listSortNative)));
    tableSet(&vm.listClassMethods, copyString("iterator", 8), OBJ_VAL(newNative(listIteratorNative)));


    tableSet(&vm.imageClassMethods, copyString("getWidth", 8), OBJ_VAL(newNative(Image_getWidth)));
    tableSet(&vm.imageClassMethods, copyString("getHeight", 9), OBJ_VAL(newNative(Image_getHeight)));
    
}

void defineThreadMethods() {
    tableSet(&vm.threadClassMethods, copyString("join", 4), OBJ_VAL(newNative(joinNative)));
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static void resetStackCtx(Thread *ctx) {
    ctx->stackTop = ctx->stack;
    ctx->frameCount = 0;
    ctx->openUpvalues = NULL;
}

CallFrame* runtimeErrorCtx(Thread *ctx, ObjClass* errorClass, const char* format, ...) {
    char msgbuf[512];      // message only
    char tracebuf[1024];   // stack trace
    char linebuf[256];
    size_t msgOffset = 0;
    size_t traceOffset = 0;

    va_list args;
    va_start(args, format);

    // Add the error type first
    msgOffset += snprintf(msgbuf + msgOffset, sizeof(msgbuf) - msgOffset, "%s: ", errorClass->name->chars);
    msgOffset += vsnprintf(msgbuf + msgOffset, sizeof(msgbuf) - msgOffset, format, args);
    va_end(args);
    //msgOffset += snprintf(msgbuf + msgOffset, sizeof(msgbuf) - msgOffset, "\n");


    // Step 2: Build the stack trace string separately
    for (int i = ctx->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &ctx->frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        int line = function->chunk.lines[instruction];

        int len = snprintf(linebuf, sizeof(linebuf), "[line %d] in ", line);
        if (traceOffset + len < sizeof(tracebuf)) {
            memcpy(tracebuf + traceOffset, linebuf, len);
            traceOffset += len;
        }
        if (function->name == NULL) {
            len = snprintf(linebuf, sizeof(linebuf), "script");
        } else {
            len = snprintf(linebuf, sizeof(linebuf), "%s()\n", function->name->chars);
        }
        if (traceOffset + len < sizeof(tracebuf)) {
            memcpy(tracebuf + traceOffset, linebuf, len);
            traceOffset += len;
        }
    }

    // Step 3: Create the error instance and set msg + stackTrace
    ObjInstance* errorInstance = newInstance(errorClass);
    ObjString* msgString = copyString(msgbuf, msgOffset);
    ObjString* traceString = copyString(tracebuf, traceOffset);

    tableSet(&errorInstance->fields, copyString("msg", 3), OBJ_VAL(msgString));
    tableSet(&errorInstance->fields, copyString("stackTrace", 10), OBJ_VAL(traceString));

    // Step 4: Unwind the call stack looking for try block
    while (ctx->frameCount > 0) {
        CallFrame* frame = &ctx->frames[ctx->frameCount - 1];

        if (frame->hasTry[frame->tryTop] != -1) {
            ctx->stackTop = ++frame->saveStack[frame->tryTop];
            frame->ip = frame->saveIP[frame->tryTop] + frame->hasTry[frame->tryTop] - 1;
            pushCtx(ctx, OBJ_VAL(errorInstance));
            ctx->hasError = true;
            return frame;
        }

        ctx->frameCount--;
        ctx->stackTop = frame->slots;
    }
    // Step 5: No try/catch found — print msg and stack trace, then exit
    fwrite(msgbuf, 1, msgOffset, stderr);
    fputc('\n', stderr); // <- Add a newline after the message
    fwrite(tracebuf, 1, traceOffset, stderr);
    pthread_exit(NULL);
}

CallFrame* throwRuntimeErrorCtx(Thread *ctx, ObjInstance* errorInstance) {
    char tracebuf[1024];
    char linebuf[256];
    size_t offset = 0;

    // Build the stack trace string
    for (int i = ctx->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &ctx->frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        int line = function->chunk.lines[instruction];

        int len = snprintf(linebuf, sizeof(linebuf), "[line %d] in ", line);
        if (offset + len < sizeof(tracebuf)) {
            memcpy(tracebuf + offset, linebuf, len);
            offset += len;
        }

        if (function->name == NULL) {
            len = snprintf(linebuf, sizeof(linebuf), "script\n");
        } else {
            len = snprintf(linebuf, sizeof(linebuf), "%s()\n", function->name->chars);
        }

        if (offset + len < sizeof(tracebuf)) {
            memcpy(tracebuf + offset, linebuf, len);
            offset += len;
        }
    }

    // Create and set the 'stackTrace' field
    ObjString* traceString = copyString(tracebuf, offset);
    Value traceValue = OBJ_VAL(traceString);
    tableSet(&errorInstance->fields, copyString("stackTrace", 10), traceValue);

    // Unwind the call stack looking for a try block
    while (ctx->frameCount > 0) {
        CallFrame* frame = &ctx->frames[ctx->frameCount - 1];

        if (frame->hasTry[frame->tryTop] != -1) {
            ctx->stackTop = ++frame->saveStack[frame->tryTop];
            frame->ip = frame->saveIP[frame->tryTop] + frame->hasTry[frame->tryTop] - 1;
            pushCtx(ctx, OBJ_VAL(errorInstance));
            ctx->hasError = true;
            return frame;
        }

        ctx->frameCount--;
        ctx->stackTop = frame->slots;
    }

    // No try block found — print message and stack trace, then exit
    Value msgVal;
    if (tableGet(&errorInstance->fields, copyString("msg", 3), &msgVal) && IS_STRING(msgVal)) {
        fwrite(AS_CSTRING(msgVal), 1, AS_STRING(msgVal)->length, stderr);
        fputc('\n', stderr);
    }

    fwrite(tracebuf, 1, offset, stderr);
    pthread_exit(NULL);
}

CallFrame* VMErrorCtx(Thread *ctx, const char* format, ...) {
    char msgbuf[1024];
    char linebuf[256];
    size_t offset = 0;

    va_list args;
    va_start(args, format);
    offset += vsnprintf(msgbuf + offset, sizeof(msgbuf) - offset, format, args);
    va_end(args);

    offset += snprintf(msgbuf + offset, sizeof(msgbuf) - offset, "\n");

    for (int i = ctx->frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &ctx->frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        int line = function->chunk.lines[instruction];

        int len = snprintf(linebuf, sizeof(linebuf), "[line %d] in ", line);
        if (offset + len < sizeof(msgbuf)) {
            memcpy(msgbuf + offset, linebuf, len);
            offset += len;
        }

        if (function->name == NULL) {
            len = snprintf(linebuf, sizeof(linebuf), "script\n");
        } else {
            len = snprintf(linebuf, sizeof(linebuf), "%s()\n", function->name->chars);
        }

        if (offset + len < sizeof(msgbuf)) {
            memcpy(msgbuf + offset, linebuf, len);
            offset += len;
        }
    }

    fwrite(msgbuf, 1, offset, stderr);
    pthread_exit(NULL);
}

static void defineNative(const char* name, NativeFn function) {
    tableSet(&vm.globals, copyString(name, (int)strlen(name)), OBJ_VAL(newNative(function)));
}

void initVM() {
    initTable(&vm.strings);
    initTable(&vm.globals);
    initTable(&vm.stringClassMethods);
    initTable(&vm.listClassMethods);
    initTable(&vm.imageClassMethods);
    initTable(&vm.threadClassMethods);

    vm.initString = copyString("init", 4);
    vm.toString = copyString("toString", 8);
    vm.errorString = copyString("Error", 5);
    vm.indexErrorString = copyString("IndexOutOfBoundsError", 21);
    vm.typeErrorString = copyString("TypeError", 9);
    vm.nameErrorString = copyString("NameError", 9);
    vm.accessErrorString = copyString("AccessError", 11);
    vm.illegalArgumentsErrorString = copyString("IllegalArgumentError", 20);
    vm.lookUpErrorString = copyString("LookUpError", 11);
    vm.formatErrorString = copyString("FormatError", 11);

    defineNative("clock", clockNative);
    defineNative("input", inputNative);
    defineNative("sleep", sleepNative);
    defineNative("read",  readNative);
    defineNative("spawn", spawnNative);
    defineNative("join", joinNative);
    defineNative("sync", syncNative);
    defineStringMethods();
    defineListMethods();
    defineThreadMethods();

    vm.repl = 0;
    vm.path = "";
}

void pushCtx(Thread *ctx, Value value) {
    *ctx->stackTop = value;
    ctx->stackTop++;
}

Value popCtx(Thread *ctx) {
    ctx->stackTop--;
    return *ctx->stackTop;
}

static Value peekCtx(Thread *ctx, int distance) {
    return ctx->stackTop[-1 - distance];
}

static bool callCtx(Thread *ctx, ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "Expected %d arguments but got %d.",
            closure->function->arity, argCount);
        return false;
    }

    if (ctx->frameCount == FRAMES_MAX) {
        VMErrorCtx(ctx, "Stack overflow.");
        return false;
    }

    CallFrame* frame = &ctx->frames[ctx->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = ctx->stackTop - argCount - 1;
    frame->tryTop = 0;
    frame->hasTry[frame->tryTop] = -1;
    frame->klass = closure->klass;

    return true;
}

void printDispatcher(ObjMultiDispatch* dispatcher) {
    printf("Dispatcher at %p\n", (void*)dispatcher);
    printf("Closures by arity:\n");

    for (int i = 0; i < 10; i++) {
        ObjClosure* closure = dispatcher->closures[i];
        if (closure == NULL) continue;

        printf("  Arity %d -> <closure at %p>\n", i, (void*)closure);
    }
}

static bool callBoundedNativeCtx(Thread *ctx, Value callee, int argCount) {
    NativeFn native = AS_NATIVE(callee);
    Value result = native(ctx, argCount, ctx->stackTop - argCount);
    if(ctx->hasError){
        ctx->hasError = false;
        result = popCtx(ctx);
        pushCtx(ctx, result);
    }
    ctx->stackTop = ctx->stackTop - argCount;
    pushCtx(ctx, result);
    return true;
}


void printTable(Table* table) {
    printf("Table at %p:\n", (void*)table);
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        printf("  [%d] ", i);
        printf("%s → ", entry->key->chars);
        printValue(entry->value);
        printf("\n");
    }
}

static bool callValueCtx(Thread *ctx, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return callCtx(ctx, AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(ctx, argCount, ctx->stackTop - argCount);
                if(ctx->hasError){
                    ctx->hasError = false;
                    result = popCtx(ctx);
                    pushCtx(ctx, result);
                }
                ctx->stackTop = ctx->stackTop - argCount - 1;
                pushCtx(ctx, result);
                return true;
            }
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                Value initializer;

                if (tableGet(&klass->methods, vm.initString, &initializer)) {

                    ctx->stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                    if (AS_BOUND_METHOD(initializer)->method[argCount] == NULL) {
                        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "No matching initializer found with %d arguments.", argCount);
                        return false;
                    }
                    return callCtx(ctx, AS_BOUND_METHOD(initializer)->method[argCount], argCount);
                }
                else if(argCount == 0){
                    ctx->stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                    return true;
                }

                return false;
            }
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                ObjClosure* closure = bound->method[argCount];
                if (closure == NULL) {
                    runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "No method with arity %d found.", argCount);
                    return false;
                }

                ctx->stackTop[-argCount - 1] = bound->receiver;
                return callCtx(ctx, bound->method[argCount], argCount);
            }
            case OBJ_MULTI_DISPATCH:{
                ObjMultiDispatch* md = AS_MULTI_DISPATCH(callee);

                ObjClosure* closure = md->closures[argCount];
                if (closure == NULL) {
                    runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "No method for arity %d.", argCount);
                    return false;
                }
                return callCtx(ctx, closure, argCount);
            }
            default:
                break;
        }
    }
    runtimeErrorCtx(ctx, vm.typeErrorClass, "Can only call functions and classes.");
    return false;
}

static ObjUpvalue* captureUpvalueCtx(Thread *ctx, Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = ctx->openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);

    createdUpvalue->next = upvalue;
    if (prevUpvalue == NULL) {
        ctx->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvaluesCtx(Thread *ctx, Value* last) {
    while (ctx->openUpvalues != NULL && ctx->openUpvalues->location >= last) {
        ObjUpvalue* upvalue = ctx->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        ctx->openUpvalues = upvalue->next;
    }
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value)) || (IS_NUMBER(value) && AS_NUMBER(value) == 0.0);
}

static void concatenateCtx(Thread *ctx) {
    ObjString* b = AS_STRING(popCtx(ctx));
    ObjString* a = AS_STRING(popCtx(ctx));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = newString(chars, length);
    pushCtx(ctx, OBJ_VAL(result));
}

void multiDispatchAdd(ObjMultiDispatch* dispatch, ObjClosure* closure) {
    int arity = closure->function->arity;
    dispatch->closures[arity] = closure;
}

void multiBoundAdd(ObjBoundMethod* dispatch, ObjClosure* closure) {
    int arity = closure->function->arity;
    dispatch->method[arity] = closure;
}

static void defineMethodCtx(Thread *ctx, ObjString* name) {
    Value method = peekCtx(ctx, 0);
    ObjClass* klass = AS_CLASS(peekCtx(ctx, 1));
    AS_CLOSURE(method)->klass = klass;

    Value dispatcher;
    if (tableGet(&klass->methods, name, &dispatcher)) {
        ObjBoundMethod* md = AS_BOUND_METHOD(dispatcher);
        multiBoundAdd(md, AS_CLOSURE(method));
    }
    else {
        ObjBoundMethod* md = newBoundMethod(dispatcher, AS_CLOSURE(method)->function->name);
        multiBoundAdd(md, AS_CLOSURE(method));
        tableSet(&klass->methods, name, OBJ_VAL(md));
    }
    popCtx(ctx);
}

static bool bindMethodCtx(Thread *ctx, ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        return false;
    }

    ObjBoundMethod* bound = AS_BOUND_METHOD(method);
    bound->receiver = peekCtx(ctx, 0);

    popCtx(ctx);
    pushCtx(ctx, OBJ_VAL(bound));

    return true;
}

static bool invokeFromClassCtx(Thread *ctx, ObjClass* klass, ObjString* name, int argCount) {
    Value method;

    if (tableGet(&klass->methods, name, &method)) {
        AS_BOUND_METHOD(method)->receiver = peekCtx(ctx, argCount);
        bool yes = callValueCtx(ctx, OBJ_VAL(AS_BOUND_METHOD(method)), argCount);
        return yes;
    }

    if (tableGet(&klass->staticMethods, name, &method)) {
        if (IS_NATIVE(method)) {
            bool res = callBoundedNativeCtx(ctx, method, argCount);
            Value result = popCtx(ctx);
            popCtx(ctx);
            pushCtx(ctx, result);
            return res;
        }
        bool res = callValueCtx(ctx, method, argCount);
        Value result = popCtx(ctx);
        popCtx(ctx);
        pushCtx(ctx, result);
        return res;
    }

    runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined method '%s' on instance of class '%s'.",
                 name->chars, klass->name->chars);
    return false;
}

void printStackCtx(Thread *ctx) {
    printf("          ");
    printf("Stack at %p\n", (void*)ctx->stack);
    for (Value* slot = ctx->stack; slot < ctx->stackTop; slot++) {
        printf("[ ");
        printValue(*slot);
        printf(" ]");
    }
    printf("\n");
}

static bool isPrivate(ObjString* name) {
    return name->length > 0 && name->chars[0] == '#';
}

static bool invokeCtx(Thread *ctx, ObjString* name, int argCount, CallFrame* frame) {
    Value receiver = peekCtx(ctx, argCount);

    if (IS_STRING(receiver)) {
        Value value;
        if (tableGet(&vm.stringClassMethods, name, &value)) {
            bool res = callBoundedNativeCtx(ctx, value, argCount);
            Value result = popCtx(ctx);
            popCtx(ctx);
            pushCtx(ctx, result);
            return res;
        }
        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined method '%s' on string.", name->chars);
        return false;
    }


    if (IS_IMAGE(receiver)) {
        Value value;
        if (tableGet(&vm.imageClassMethods, name, &value)) {
            bool res = callBoundedNativeCtx(ctx, value, argCount);
            Value result = popCtx(ctx);
            popCtx(ctx);
            pushCtx(ctx, result);
            return res;
        }
        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined method '%s' on image.", name->chars);
        return false;
    }

    if (IS_LIST(receiver)) {
        Value value;
        if (tableGet(&vm.listClassMethods, name, &value)) {
            bool res = callBoundedNativeCtx(ctx, value, argCount);
            Value result = popCtx(ctx);
            popCtx(ctx);
            pushCtx(ctx, result);
            return res;
        }
        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined method '%s' on list.", name->chars);
        return false;
    }

    if (IS_NAMESPACE(receiver)){
        ObjNamespace* ns = AS_NAMESPACE(receiver);
        Value value;
                
        if (tableGet(ns->namespace, name, &value)) {
            bool res = callValueCtx(ctx, value, argCount);
            return res;
        }
                
        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined function '%s' in namespace '%s'.", name->chars, ns->name->chars);
        return false;
    }

    if (IS_THREAD(receiver)) {
        Value value;
        if (tableGet(&vm.threadClassMethods, name, &value)) {
            bool res = callBoundedNativeCtx(ctx, value, argCount);
            Value result = popCtx(ctx);
            popCtx(ctx);
            pushCtx(ctx, result);
            return res;
        }
        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined method '%s' on thread.", name->chars);
        return false;
    }

    if (IS_CLASS(receiver)) {
        Value value;
        ObjClass* klass = AS_CLASS(receiver);

        if (isPrivate(name) && klass != frame->klass){
            runtimeErrorCtx(ctx, vm.accessErrorClass, "Cannot access private field from a different class.");
            return false;
        }

        if (tableGet(&klass->staticMethods, name, &value)) {
            if (IS_NATIVE(value)) {
                bool res = callBoundedNativeCtx(ctx, value, argCount);
                Value result = popCtx(ctx);
                popCtx(ctx);
                pushCtx(ctx, result);
                return res;
            }

            bool res = callValueCtx(ctx, value, argCount);
            return res;
        }

        klass = klass->superclass;
        if (klass != NULL && tableGet(&klass->staticMethods, name, &value)) {
            bool res = callValueCtx(ctx, value, argCount);
            return res;
        }
        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined static method '%s' on class '%s'.",
                     name->chars, klass->name->chars);
        return false;
    }


    if (!IS_INSTANCE(receiver)) {
        runtimeErrorCtx(ctx, vm.typeErrorClass, "Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    if (isPrivate(name) && instance->klass != frame->klass) 
        runtimeErrorCtx(ctx, vm.accessErrorClass, "Cannot access private field of a different class.");

    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        ctx->stackTop[-argCount - 1] = value;
        return callValueCtx(ctx, value, argCount);
    }

    return invokeFromClassCtx(ctx, instance->klass, name, argCount); 
} 

static int resolveMultiDispatchCtx(Thread *ctx, Value* result, ObjString* name) {
    CallFrame* frame = &ctx->frames[ctx->frameCount - 1];

    for (Value* slot = ctx->stackTop - 1; slot >= frame->slots; slot--) {
        if (IS_OBJ(*slot) && IS_MULTI_DISPATCH(*slot)) {
            ObjMultiDispatch* md = AS_MULTI_DISPATCH(*slot);

            if (md->name == name) {
                *result = *slot;
                return 2;
            }
        }
    }

    return tableGet(&vm.globals, name, result);
}

bool hasAncestor(ObjInstance* instance, ObjClass* ancestor) {
    ObjClass* current = instance->klass;
    while (current != NULL) {
        if (current == ancestor) return true;
        current = current->superclass;
    }
    return false;
}

Value readConst(CallFrame* frame){
    frame->ip += 1;
    return frame->closure->function->chunk.constants.values[/*(frame->ip[-3] << 16 | frame->ip[-2] << 8 | */frame->ip[-1]];
}


#include <gc/gc.h>
static void* runCtx(void *context) {
    if (GC_thread_is_registered() == 0) {
        if (GC_register_my_thread(NULL) != 0) {
            fprintf(stderr, "Failed to register GC thread\n");
            pthread_exit(NULL);
        }
    }

    
    Thread* ctx = (Thread*)context;
    ctx->finished = false;
    register CallFrame* frame = &ctx->frames[ctx->frameCount - 1];

    #define READ_BYTE() (*frame->ip++)

    #define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

    #define READ_CONSTANT() \
        readConst(frame)
    #define BINARY_OP(valueType, op) \
        do { \
            bool fl = false; \
            int peeker = 1; \
            if (!IS_NUMBER(peekCtx(ctx, 0)) || !IS_NUMBER(peekCtx(ctx, peeker))) { \
                runtimeErrorCtx(ctx, vm.typeErrorClass, "Operands must be numbers."); \
                fl = true; \
                break; \
            } \
            double b = AS_NUMBER(popCtx(ctx)); \
            double a = AS_NUMBER(popCtx(ctx)); \
            pushCtx(ctx, valueType(a op b)); \
            if (fl) break;\
        } while (false)\

    #define READ_STRING() AS_STRING(READ_CONSTANT())

        for (;;) {
    #ifdef DEBUG_TRACE_EXECUTION
            printf("          ");
            for (Value* slot = ctx->stack; slot < ctx->stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            disassembleInstruction(&frame->closure->function->chunk,
                    (int)(frame->ip - frame->closure->function->chunk.code));
    #endif
        uint8_t instruction;
        instruction = READ_BYTE();
        switch (instruction) {
            case OP_RETURN: {
                Value result = popCtx(ctx);
                closeUpvaluesCtx(ctx, frame->slots);
                ctx->frameCount--;
                if (ctx->frameCount == 0) {
                    popCtx(ctx);
                    pushCtx(ctx, result);
                    //GC_unregister_my_thread();
                    return nullptr;
                }

                ctx->stackTop = frame->slots;
                pushCtx(ctx, result);
                frame = &ctx->frames[ctx->frameCount - 1];

                if (IS_INSTANCE(result)) {
                    ObjInstance* instance = AS_INSTANCE(result);
                    if (hasAncestor(instance, vm.errorClass)) {
                        Value msgVal;
                        if (tableGet(&instance->fields, copyString("msg", 3), &msgVal) && IS_STRING(msgVal)) {
                            ObjString* msgStr = AS_STRING(msgVal);
                            ObjString* className = instance->klass->name;

                            char formatted[512];
                            int len = snprintf(formatted, sizeof(formatted), "%s: %s", className->chars, msgStr->chars);

                            ObjString* fullMsg = copyString(formatted, len);
                            tableSet(&instance->fields, copyString("msg", 3), OBJ_VAL(fullMsg));
                        }
                    }
                }
                break;
            }
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                pushCtx(ctx, constant);
                break;
            }
            case OP_NEGATE:
                if (!IS_NUMBER(peekCtx(ctx, 0))) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Operand must be a number.");
                    break;
                }
                pushCtx(ctx, NUMBER_VAL(-AS_NUMBER(popCtx(ctx))));
                break;

            case OP_ADD: {
                if (IS_INSTANCE(peekCtx(ctx, 0)) && IS_INSTANCE(peekCtx(ctx, 1))) {
                    ObjInstance* a = AS_INSTANCE(peekCtx(ctx, 0));
                    ObjInstance* b = AS_INSTANCE(peekCtx(ctx, 1));

                    if (a->klass != b->klass) {
                        runtimeErrorCtx(ctx, vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("+", 1), &method)) {
                        int argCount = 1;
                        invokeCtx(ctx, copyString("+", 1), argCount, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "No overload of '+' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                if (IS_STRING(peekCtx(ctx, 0)) && IS_STRING(peekCtx(ctx, 1))) {
                    concatenateCtx(ctx);
                } else if (IS_NUMBER(peekCtx(ctx, 0)) && IS_NUMBER(peekCtx(ctx, 1))) {
                    double b = AS_NUMBER(peekCtx(ctx, 0));
                    double a = AS_NUMBER(peekCtx(ctx, 1));
                    popCtx(ctx);
                    popCtx(ctx);
                    pushCtx(ctx, NUMBER_VAL(a + b));
                }
                else if (IS_STRING(peekCtx(ctx, 0)) && IS_NUMBER(peekCtx(ctx, 1))) {
                    ObjString* b = AS_STRING(peekCtx(ctx, 0));
                    double num = AS_NUMBER(peekCtx(ctx, 1));

                    char buffer[32];
                    int len = snprintf(buffer, sizeof(buffer), "%.14g", num);

                    // Allocate and intern as ObjString
                    ObjString* a = newString(buffer, len);

                    int length = a->length + b->length;
                    char* chars = ALLOCATE(char, length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';

                    ObjString* result = newString(chars, length);
                    popCtx(ctx);
                    popCtx(ctx);
                    pushCtx(ctx, OBJ_VAL(result));
                }
                else if (IS_NUMBER(peekCtx(ctx, 0)) && IS_STRING(peekCtx(ctx, 1))) {
                    double num = AS_NUMBER(peekCtx(ctx, 0));

                    char buffer[32];
                    int len = snprintf(buffer, sizeof(buffer), "%.14g", num);

                    // Allocate and intern as ObjString
                    ObjString* b = copyString(buffer, len);
                    ObjString* a = AS_STRING(peekCtx(ctx, 1));

                    int length = a->length + b->length;
                    char* chars = ALLOCATE(char, length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';

                    ObjString* result = newString(chars, length);
                    popCtx(ctx);popCtx(ctx);
                    pushCtx(ctx, OBJ_VAL(result));
                }
                else {
                    runtimeErrorCtx(ctx, vm.typeErrorClass,
                        "Operands must be two numbers or two strings.");
                    break;
                }
                break;
            }
            case OP_SUBTRACT:
                if (IS_INSTANCE(peekCtx(ctx, 0)) && IS_INSTANCE(peekCtx(ctx, 1))) {
                    ObjInstance* a = AS_INSTANCE(peekCtx(ctx, 0));
                    ObjInstance* b = AS_INSTANCE(peekCtx(ctx, 1));

                    if (a->klass != b->klass) {
                        runtimeErrorCtx(ctx, vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("-", 1), &method)) {
                        int argCount = 1;
                        invokeCtx(ctx, copyString("-", 1), argCount, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "No overload of '-' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: {
                if (IS_INSTANCE(peekCtx(ctx, 0)) && IS_INSTANCE(peekCtx(ctx, 1))) {
                    ObjInstance* a = AS_INSTANCE(peekCtx(ctx, 0));
                    ObjInstance* b = AS_INSTANCE(peekCtx(ctx, 1));

                    if (a->klass != b->klass) {
                        runtimeErrorCtx(ctx, vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("*", 1), &method)) {
                        int argCount = 1;
                        invokeCtx(ctx, copyString("*", 1), argCount, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "No overload of '*' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                BINARY_OP(NUMBER_VAL, *); break;
            }
            case OP_DIVIDE:
                if (IS_INSTANCE(peekCtx(ctx, 0)) && IS_INSTANCE(peekCtx(ctx, 1))) {
                    ObjInstance* a = AS_INSTANCE(peekCtx(ctx, 0));
                    ObjInstance* b = AS_INSTANCE(peekCtx(ctx, 1));

                    if (a->klass != b->klass) {
                        runtimeErrorCtx(ctx, vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("/", 1), &method)) {
                        int argCount = 1;
                        invokeCtx(ctx, copyString("/", 1), argCount, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "No overload of '/' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                BINARY_OP(NUMBER_VAL, /); break;
            case OP_MOD: {
                if (IS_INSTANCE(peekCtx(ctx, 0)) && IS_INSTANCE(peekCtx(ctx, 1))) {
                    ObjInstance* a = AS_INSTANCE(peekCtx(ctx, 0));
                    ObjInstance* b = AS_INSTANCE(peekCtx(ctx, 1));

                    if (a->klass != b->klass) {
                        runtimeErrorCtx(ctx, vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("-", 1), &method)) {
                        int argCount = 1;
                        invokeCtx(ctx, copyString("-", 1), argCount, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "No overload of '-' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }

                bool fl = false;
                if (!IS_NUMBER(peekCtx(ctx, 0)) || !IS_NUMBER(peekCtx(ctx, 1))) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Operands must be numbers.");
                    fl = true;
                    break;
                }
                double b = AS_NUMBER(popCtx(ctx));
                double a = AS_NUMBER(popCtx(ctx));
                pushCtx(ctx, NUMBER_VAL(fmod(a, b)));
                break;
            }
            case OP_INS: {
                if (IS_INSTANCE(peekCtx(ctx, 0)) && IS_INSTANCE(peekCtx(ctx, 1))) {
                    ObjInstance* a = AS_INSTANCE(peekCtx(ctx, 0));
                    ObjInstance* b = AS_INSTANCE(peekCtx(ctx, 1));

                    if (a->klass != b->klass) {
                        runtimeErrorCtx(ctx, vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("-", 1), &method)) {
                        int argCount = 1;
                        invokeCtx(ctx, copyString("-", 1), argCount, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "No overload of '\' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }

                bool fl = false;
                if (!IS_NUMBER(peekCtx(ctx, 0)) || !IS_NUMBER(peekCtx(ctx, 1))) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Operands must be numbers.");
                    fl = true;
                    break;
                }
                double b = AS_NUMBER(popCtx(ctx));
                double a = AS_NUMBER(popCtx(ctx));
                pushCtx(ctx, NUMBER_VAL((int)(a / b)));
                break;
            }
            case OP_NIL: pushCtx(ctx, NIL_VAL); break;
            case OP_TRUE: pushCtx(ctx, BOOL_VAL(true)); break;
            case OP_FALSE: pushCtx(ctx, BOOL_VAL(false)); break;
            case OP_NOT:
                pushCtx(ctx, BOOL_VAL(isFalsey(popCtx(ctx))));
                break;
            case OP_EQUAL: {
                Value b = popCtx(ctx);
                Value a = popCtx(ctx);
                pushCtx(ctx, BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_PRINT: {
                Value printVal = peekCtx(ctx, 0);
                if (IS_INSTANCE(printVal)) {
                    ObjInstance* instance = AS_INSTANCE(printVal);
                    Value method;
                    if (tableGet(&instance->klass->methods, vm.toString, &method)) {
                        frame->ip--;
                        invokeCtx(ctx, vm.toString, 0, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                }
                popCtx(ctx);
                printValue(printVal);
                break;
            }
            case OP_PRINTLN: {
                Value printVal = peekCtx(ctx, 0);
                if (IS_INSTANCE(printVal)) {
                    ObjInstance* instance = AS_INSTANCE(printVal);
                    Value method;
                    if (tableGet(&instance->klass->methods, vm.toString, &method)) {
                        frame->ip--;
                        invokeCtx(ctx, vm.toString, 0, frame);
                        frame = &ctx->frames[ctx->frameCount - 1];
                        break;
                    }
                }
                popCtx(ctx);
                printValue(printVal);
                printf("\n");
                break;
            }
            case OP_PRINTLN_BLANK: {
                printf("\n");
                break;
            }
            case OP_POP: popCtx(ctx); break;
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                if(ctx->namespace != NULL)
                    tableSet(ctx->namespace, name, peekCtx(ctx, 0));
                else 
                    tableSet(&vm.globals, name, peekCtx(ctx, 0));
                popCtx(ctx);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;

                if (!(ctx->namespace && tableGet(ctx->namespace, name, &value))) {
                    if (!tableGet(&vm.globals, name, &value)) {
                        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined variable '%s'.", name->chars);
                        break;
                    }
                }

                pushCtx(ctx, value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value = peekCtx(ctx, 0);

                if (ctx->namespace != NULL) {
                    if (tableSet(ctx->namespace, name, value)) {
                        tableDelete(ctx->namespace, name);
                        if (tableSet(&vm.globals, name, value)) {
                            tableDelete(&vm.globals, name);
                            runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined variable '%s'.", name->chars);
                            break;
                        }
                    }
                } else {
                    if (tableSet(&vm.globals, name, value)) {
                        tableDelete(&vm.globals, name);
                        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined variable '%s'.", name->chars);
                        break;
                    }
                }

                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                pushCtx(ctx, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peekCtx(ctx, 0);
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peekCtx(ctx, 0))) frame->ip += offset;
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();

                if (!callValueCtx(ctx, peekCtx(ctx, argCount), argCount)) {
                    break;
                }
                frame = &ctx->frames[ctx->frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                pushCtx(ctx, OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] =
                            captureUpvalueCtx(ctx, frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                pushCtx(ctx, *frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peekCtx(ctx, 0);
                break;
            }
            case OP_CLOSE_UPVALUE:
                closeUpvaluesCtx(ctx, ctx->stackTop - 1);
                popCtx(ctx);
                break;
            case OP_CLASS:
                ObjString* name = READ_STRING();
                ObjClass* klass = newClass(name);
                if (name == vm.errorString) vm.errorClass = klass;
                if (name == vm.errorString) vm.errorClass = klass;
                if (name == vm.indexErrorString) vm.indexErrorClass = klass;
                if (name == vm.typeErrorString) vm.typeErrorClass = klass;
                if (name == vm.nameErrorString) vm.nameErrorClass = klass;
                if (name == vm.accessErrorString) vm.accessErrorClass = klass;
                if (name == vm.illegalArgumentsErrorString) vm.illegalArgumentsErrorClass = klass;
                if (name == vm.lookUpErrorString) vm.lookUpErrorClass = klass;
                if (name == vm.formatErrorString) vm.formatErrorClass = klass;

                if (name == copyString("Window", 6)) {
                    tableSet(&klass->staticMethods, copyString("init", 4),       OBJ_VAL(newNative(window_init)));
                    tableSet(&klass->staticMethods, copyString("clear", 5),      OBJ_VAL(newNative(window_clear)));
                    tableSet(&klass->staticMethods, copyString("drawRect", 8),   OBJ_VAL(newNative(window_drawRect)));
                    tableSet(&klass->staticMethods, copyString("update", 6),     OBJ_VAL(newNative(window_update)));
                    tableSet(&klass->staticMethods, copyString("pollEvent", 9),  OBJ_VAL(newNative(window_pollEvent)));
                    tableSet(&klass->staticMethods, copyString("getMousePos", 11),   OBJ_VAL(newNative(window_getMousePosition)));
                    tableSet(&klass->staticMethods, copyString("drawCircle", 10),OBJ_VAL(newNative(window_drawCircle)));
                    tableSet(&klass->staticMethods, copyString("drawImage", 9),   OBJ_VAL(newNative(window_drawImage)));
                    tableSet(&klass->staticMethods, copyString("loadImage", 9),   OBJ_VAL(newNative(window_loadImage)));
                    tableSet(&klass->staticMethods, copyString("exit", 4),        OBJ_VAL(newNative(window_exit)));
                    tableSet(&klass->staticMethods, copyString("drawLine", 8),        OBJ_VAL(newNative(window_drawLine)));
                    tableSet(&klass->staticMethods, copyString("drawTrig", 8),        OBJ_VAL(newNative(window_drawTriangle)));
                    tableSet(&klass->staticMethods, copyString("drawText", 8),    OBJ_VAL(newNative(window_drawText)));
                }

                if (name == copyString("Math", 4)) {
                    Table* staticMethods = &klass->staticMethods;
                    tableSet(staticMethods, copyString("abs", 3), OBJ_VAL(newNative(math_abs)));
                    tableSet(staticMethods, copyString("min", 3), OBJ_VAL(newNative(math_min)));
                    tableSet(staticMethods, copyString("max", 3), OBJ_VAL(newNative(math_max)));
                    tableSet(staticMethods, copyString("clamp", 5), OBJ_VAL(newNative(math_clamp)));
                    tableSet(staticMethods, copyString("sign", 4), OBJ_VAL(newNative(math_sign)));

                    tableSet(staticMethods, copyString("pow", 3), OBJ_VAL(newNative(math_pow)));
                    tableSet(staticMethods, copyString("sqrt", 4), OBJ_VAL(newNative(math_sqrt)));
                    tableSet(staticMethods, copyString("cbrt", 4), OBJ_VAL(newNative(math_cbrt)));

                    tableSet(staticMethods, copyString("exp", 3), OBJ_VAL(newNative(math_exp)));
                    tableSet(staticMethods, copyString("log", 3), OBJ_VAL(newNative(math_log)));
                    tableSet(staticMethods, copyString("log10", 5), OBJ_VAL(newNative(math_log10)));

                    tableSet(staticMethods, copyString("sin", 3), OBJ_VAL(newNative(math_sin)));
                    tableSet(staticMethods, copyString("cos", 3), OBJ_VAL(newNative(math_cos)));
                    tableSet(staticMethods, copyString("tan", 3), OBJ_VAL(newNative(math_tan)));
                    tableSet(staticMethods, copyString("asin", 4), OBJ_VAL(newNative(math_asin)));
                    tableSet(staticMethods, copyString("acos", 4), OBJ_VAL(newNative(math_acos)));
                    tableSet(staticMethods, copyString("atan", 4), OBJ_VAL(newNative(math_atan)));
                    tableSet(staticMethods, copyString("atan2", 5), OBJ_VAL(newNative(math_atan2)));

                    tableSet(staticMethods, copyString("floor", 5), OBJ_VAL(newNative(math_floor)));
                    tableSet(staticMethods, copyString("ceil", 4), OBJ_VAL(newNative(math_ceil)));
                    tableSet(staticMethods, copyString("round", 5), OBJ_VAL(newNative(math_round)));
                    tableSet(staticMethods, copyString("trunc", 5), OBJ_VAL(newNative(math_trunc)));

                    tableSet(staticMethods, copyString("mod", 3), OBJ_VAL(newNative(math_mod)));
                    tableSet(staticMethods, copyString("lerp", 4), OBJ_VAL(newNative(math_lerp)));
                }


                pushCtx(ctx, OBJ_VAL(klass));
                break;
            case OP_GET_PROPERTY: {
                if (IS_STRING(peekCtx(ctx, 0)) || IS_LIST(peekCtx(ctx, 0))){
                    runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                }
                if (IS_NAMESPACE(peekCtx(ctx, 0))){
                    ObjNamespace* ns = AS_NAMESPACE(peekCtx(ctx, 0));
                    ObjString* name = READ_STRING();
                    Value value;

                    if (!tableGet(ns->namespace, name, &value)) {
                        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined variable '%s' in namespace '%s'.", name->chars, ns->name->chars);
                        break;
                    }

                    popCtx(ctx);
                    pushCtx(ctx, value);
                    break;
                }
                if (IS_INSTANCE(peekCtx(ctx, 0))) {
                    ObjInstance* instance = AS_INSTANCE(peekCtx(ctx, 0));
                    ObjString* name = READ_STRING();

                    if (isPrivate(name) && instance->klass != frame->klass){
                        runtimeErrorCtx(ctx, vm.accessErrorClass, "Cannot access private field from a different class.");
                        break;
                    }
                    
                    Value value;
                    if (tableGet(&instance->fields, name, &value)) {
                        popCtx(ctx);
                        pushCtx(ctx, value);
                        break;
                    }

                    if (bindMethodCtx(ctx, instance->klass, name)) {
                        break;
                    }

                    runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                }

                if (IS_CLASS(peekCtx(ctx, 0))) {
                    ObjClass* klass = AS_CLASS(peekCtx(ctx, 0));
                    ObjString* name = READ_STRING();

                    if (isPrivate(name) && klass != frame->klass) runtimeErrorCtx(ctx, vm.accessErrorClass, "Cannot access private field from a different class.");

                    Value value;
                    if (tableGet(&klass->staticVars, name, &value)) {
                        popCtx(ctx);
                        pushCtx(ctx, value);
                        break;
                    }
                    if (tableGet(&klass->staticMethods, name, &value)) {
                        popCtx(ctx);
                        pushCtx(ctx, value);
                        break;
                    }

                    klass = klass->superclass;
                    if (klass != NULL) {
                        Value value;
                        if (tableGet(&klass->staticVars, name, &value)) {
                            popCtx(ctx);
                            pushCtx(ctx, value);
                            break;
                        }
                        if (tableGet(&klass->staticMethods, name, &value)) {
                            popCtx(ctx);
                            pushCtx(ctx, value);
                            break;
                        }
                    }
                    runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                    break;
                }

                runtimeErrorCtx(ctx, vm.typeErrorClass, "Only instances and classes have fields, got %s.", getValueTypeName(peekCtx(ctx, 0)));
                break;
            }
            case OP_SET_PROPERTY: {
                if (IS_STRING(peekCtx(ctx, 1)) || IS_LIST(peekCtx(ctx, 1))){
                    runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                }
                if (IS_NAMESPACE(peekCtx(ctx, 1))){
                    ObjNamespace* ns = AS_NAMESPACE(peekCtx(ctx, 1));

                    ObjString* name = READ_STRING();
                    Value value = peekCtx(ctx, 0);

                    if (tableSet(ns->namespace, name, value)) {
                        tableDelete(ns->namespace, name);
                        runtimeErrorCtx(ctx, vm.nameErrorClass, "Undefined variable '%s' in namespace '%s'.", name->chars, ns->name->chars);
                        break;
                    }
                    popCtx(ctx);
                    popCtx(ctx);
                    pushCtx(ctx, value);
                    break;
                }
                if (IS_CLASS(peekCtx(ctx, 1))) {
                    ObjClass* klass = AS_CLASS(peekCtx(ctx, 1));
                    ObjString* name = READ_STRING();

                    if (klass->superclass != NULL && tableGet(&klass->superclass->staticVars, name, NULL)) {
                        tableSet(&klass->superclass->staticVars, name, peekCtx(ctx, 0));
                        Value value = popCtx(ctx);
                        popCtx(ctx);
                        pushCtx(ctx, value);
                        break;
                    }
                    tableSet(&klass->staticVars, name, peekCtx(ctx, 0));
                    Value value = popCtx(ctx);
                    popCtx(ctx);
                    pushCtx(ctx, value);
                    break;
                }

                if (!IS_INSTANCE(peekCtx(ctx, 1))) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Only instances have fields.");
                    break;
                }

                ObjInstance* instance = AS_INSTANCE(peekCtx(ctx, 1));
                tableSet(&instance->fields, READ_STRING(), peekCtx(ctx, 0));
                Value value = popCtx(ctx);
                popCtx(ctx);
                pushCtx(ctx, value);
                break;
            }
            case OP_METHOD:
                defineMethodCtx(ctx, READ_STRING());
                break;
            case OP_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invokeCtx(ctx, method, argCount, frame)) {
                    break;
                }
                //popCtx(ctx);
                frame = &ctx->frames[ctx->frameCount - 1];
                break;
            }
            case OP_INHERIT: {
                if (!IS_CLASS(peekCtx(ctx, 1))) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Superclass must be a class.");
                    break;
                }

                ObjClass* superclass = AS_CLASS(peekCtx(ctx, 1));
                ObjClass* subclass = AS_CLASS(peekCtx(ctx, 0));
                //tableAddAll(&AS_CLASS(superclass)->methods,
                //            &subclass->methods);

                subclass->superclass = superclass;
                for (int i = 0; i < superclass->methods.capacity; i++) {
                    Entry* entry = &superclass->methods.entries[i];
                    if (entry == NULL) continue;
                    if (entry->key == NULL) continue;

                    Value temp = OBJ_VAL(subclass);
                    ObjBoundMethod* md = newBoundMethod(temp, entry->key);

                    for (int j = 0; j < 10; j++) {
                        md->method[j] = AS_BOUND_METHOD(entry->value)->method[j];
                    }

                    tableSet(&subclass->methods, entry->key, OBJ_VAL(md));
                }

                popCtx(ctx); // Subclass.
                break;
            }
            case OP_GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = AS_CLASS(popCtx(ctx));

                if (!bindMethodCtx(ctx, superclass, name)) {
                    break;
                }
                break;
            }
            case OP_SUPER_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(popCtx(ctx));
                if (!invokeFromClassCtx(ctx, superclass, method, argCount)) {
                    break;
                }
                frame = &ctx->frames[ctx->frameCount - 1];
                break;
            }
            case OP_GET_INDEX: {
                Value index = popCtx(ctx);
                Value list = popCtx(ctx);

                if (!IS_LIST(list)) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Can only index into lists.");
                    break;
                }

                if (!IS_NUMBER(index)) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "List index must be a number.");
                    break;
                }

                ObjList* objList = AS_LIST(list);
                int i = (int)AS_NUMBER(index);

                if (i < 0 || i >= objList->elements.count) {
                    runtimeErrorCtx(ctx, vm.indexErrorClass, "List index out of bounds.");
                    break;
                }

                pushCtx(ctx, objList->elements.values[i]);
                break;
            }

            case OP_SET_INDEX: {
                Value value = popCtx(ctx);
                Value index = popCtx(ctx);
                Value list = popCtx(ctx);

                if (!IS_LIST(list)) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Can only index into lists.");
                    break;
                }

                if (!IS_NUMBER(index)) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "List index must be a number.");
                    break;
                }

                ObjList* objList = AS_LIST(list);
                int i = (int)AS_NUMBER(index);

                if (i < 0 || i >= objList->elements.count) {
                    runtimeErrorCtx(ctx, vm.indexErrorClass, "List index out of bounds.");
                    break;
                }

                objList->elements.values[i] = value;
                pushCtx(ctx, value);
                break;
            }
            case OP_LIST: {
                int count = READ_BYTE();
                ObjList* list = newList();
                pushCtx(ctx, OBJ_VAL(list));

                for (int i = count; i >= 1; i--) {
                    writeValueArray(&list->elements, peekCtx(ctx, i));
                }
                Value listValue = popCtx(ctx);
                for (int i = 0; i < count; i++) popCtx(ctx);

                pushCtx(ctx, listValue);
                break;
            }
            case OP_DISPATCH: {
                Value closureVal = peekCtx(ctx, 0);
                if (!IS_CLOSURE(closureVal)) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Expected function to dispatch.");
                    break;
                }

                ObjClosure* closure = AS_CLOSURE(closureVal);
                ObjString* name = closure->function->name;

                Value dispatchVal;
                int scope = resolveMultiDispatchCtx(ctx, &dispatchVal, name);
                if (scope) {
                    ObjMultiDispatch* dispatch = AS_MULTI_DISPATCH(dispatchVal);
                    multiDispatchAdd(dispatch, closure);
                    popCtx(ctx); 
                    pushCtx(ctx, OBJ_VAL(dispatch));
                } else {
                    ObjMultiDispatch* dispatch = newMultiDispatch(name);
                    multiDispatchAdd(dispatch, closure);
                    popCtx(ctx); 
                    pushCtx(ctx, OBJ_VAL(dispatch));
                }

                break;
            }
            case OP_TRY: {
                if (frame->tryTop == 10) VMErrorCtx(ctx, "Max try depth reached");
                frame->tryTop++;
                frame->saveStack[frame->tryTop] = ctx->stackTop;
                frame->saveIP[frame->tryTop] = frame->ip;
                frame->hasTry[frame->tryTop] = READ_BYTE();

                break;
            }
            case OP_END_TRY: {
                if (frame->tryTop == 0) VMErrorCtx(ctx, "No try block to end");
                frame->hasTry[frame->tryTop] = -1;
                if (frame->tryTop >= 0) {
                    frame->tryTop--;
                }
                break;
            }
            case OP_STATIC_VAR: {
                ObjString* name = READ_STRING();
                Value value = popCtx(ctx);
                ObjClass* klass = AS_CLASS(peekCtx(ctx, 0));
                tableSet(&klass->staticVars, name, value);
                break;
            }
            case OP_STATIC_METHOD: {
                Value method = peekCtx(ctx, 0);
                ObjClass* klass = AS_CLASS(peekCtx(ctx, 1));
                ObjString* name = READ_STRING();

                AS_CLOSURE(method)->klass = klass;

                Value dispatcher;
                if (tableGet(&klass->staticMethods, name, &dispatcher)) {
                    ObjMultiDispatch* md = AS_MULTI_DISPATCH(dispatcher);
                    multiDispatchAdd(md, AS_CLOSURE(method));
                }
                else {
                    ObjMultiDispatch* md = newMultiDispatch(name);
                    multiDispatchAdd(md, AS_CLOSURE(method));
                    tableSet(&klass->staticMethods, name, OBJ_VAL(md));
                }
                popCtx(ctx);
                break;
            }
            case OP_CONSTANT_LONG: {
                    uint32_t b1 = READ_BYTE();
                    uint32_t b2 = READ_BYTE();
                    uint32_t b3 = READ_BYTE();
                    uint32_t index = (b1 << 16) | (b2 << 8) | b3;
                    pushCtx(ctx, frame->closure->function->chunk.constants.values[index]);
                    break;
            }

            case OP_THROW: {
                Value error = popCtx(ctx);
                if (!IS_INSTANCE(error)) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Can only throw instances of error classes.");
                    return nullptr;
                }

                ObjInstance* instance = AS_INSTANCE(error);
                if (!hasAncestor(instance, vm.errorClass)) {
                    runtimeErrorCtx(ctx, vm.typeErrorClass, "Can only throw instances of error.");
                    return nullptr;
                }

                throwRuntimeErrorCtx(ctx, instance); // a function you'll define
                break;
            }
            case OP_NAMESPACE:{
                ObjClosure* closure = AS_CLOSURE(peekCtx(ctx, 0));
                ObjString* name = closure->function->name;

                ObjNamespace* namespace = newNamespace(name);

                joinInternal(spawnNamespace(closure, namespace));
                popCtx(ctx);
                pushCtx(ctx, OBJ_VAL(namespace));
            }
        }
    }
    ctx->finished = true;

#undef READ_CONSTANT
#undef READ_BYTE
#undef READ_STRING
#undef READ_SHORT
}

#include "serialize.h"
#include "deserialize.h"

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (vm.noRun) {
        size_t len = strlen(vm.path);
        char* filename = GC_MALLOC(len + 2);     // +1 for 'c', +1 for '\0'
        strcpy(filename, vm.path);             // copy original filename
        filename[len] = 'c';                   // append 'c'
        filename[len + 1] = '\0';

        serialize(filename, function);
        return COMPILE_OK;
    }
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    Value input[1] = {OBJ_VAL(newClosure(function))};

    Value result = spawnNative(NULL, 1, input);
    result = joinInternal(result);
    
    return INTERPRET_OK;
}

InterpretResult load(const char* source) {
        ObjFunction* function = deserialize(source);
        Value input[1] = {OBJ_VAL(newClosure(function))};

        Value result = spawnNative(NULL, 1, input);
        result = joinInternal(result);
        
        return INTERPRET_OK;
    
}
