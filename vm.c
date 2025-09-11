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

#include "debug.h"
#include "stringMethods.c"
#include "listMethods.c"
#include "windowMethods.h"
#include  "Math.c"


VM vm;
void printStack();
ObjClass* errorClass;
bool replError;

static Value sleepNative(int argCount, Value* args){
    int ms = AS_NUMBER(args[0]);
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}


static Value clockNative(int argCount, Value* args) {
    if(argCount != 0){
        runtimeError(vm.illegalArgumentsErrorClass, "clock() does not accept any argument.");
        return NIL_VAL;
    }
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value readNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError(vm.illegalArgumentsErrorClass, "Argument must be a string (file path).");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        runtimeError(vm.accessErrorClass, "Could not open file \"%s\".", path);
        return NIL_VAL;
    }

    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(size + 1);
    if (buffer == NULL) {
        fclose(file);
        runtimeError(vm.accessErrorClass, "Not enough memory to read file.");
        return NIL_VAL;
    }

    size_t bytesRead = fread(buffer, 1, size, file);
    buffer[bytesRead] = '\0';

    fclose(file);

    return OBJ_VAL(copyString(buffer, (int)bytesRead));
}

static Value inputNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError(vm.illegalArgumentsErrorClass, "input() takes no or one arguments.");
        return NIL_VAL;
    }

    if (argCount == 1) {
        if (!IS_STRING(args[0])) {
            runtimeError(vm.illegalArgumentsErrorClass, "input() prompt must be a string.");
            return NIL_VAL;
        }
        printf("%s\n", AS_CSTRING(args[0]));
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

    tableSet(&vm.imageClassMethods, copyString("getWidth", 8), OBJ_VAL(newNative(Image_getWidth)));
    tableSet(&vm.imageClassMethods, copyString("getHeight", 9), OBJ_VAL(newNative(Image_getHeight)));
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

CallFrame* runtimeError(ObjClass* errorClass, const char* format, ...) {
    char msgbuf[512];      // message only
    char tracebuf[1024];   // stack trace
    char linebuf[256];
    size_t msgOffset = 0;
    size_t traceOffset = 0;

    va_list args;
    va_start(args, format);

    // Add the error type first
    msgOffset += snprintf(msgbuf + msgOffset, sizeof(msgbuf) - msgOffset, "%s: ", errorClass->name->chars);

    // Then the actual error message
    msgOffset += vsnprintf(msgbuf + msgOffset, sizeof(msgbuf) - msgOffset, format, args);
    va_end(args);

    // Add newline
    msgOffset += snprintf(msgbuf + msgOffset, sizeof(msgbuf) - msgOffset, "\n");


    // Step 2: Build the stack trace string separately
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
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
    while (vm.frameCount > 0) {
        CallFrame* frame = &vm.frames[vm.frameCount - 1];

        if (frame->hasTry[frame->tryTop] != -1) {
            vm.stackTop = ++frame->saveStack[frame->tryTop];
            frame->ip = frame->saveIP[frame->tryTop] + frame->hasTry[frame->tryTop] - 1;
            push(OBJ_VAL(errorInstance));
            vm.hasError = true;
            return frame;
        }

        vm.frameCount--;
        vm.stackTop = frame->slots;
    }

    // Step 5: No try/catch found — print msg and stack trace, then exit
    fwrite(msgbuf, 1, msgOffset, stderr);
    fwrite(tracebuf, 1, traceOffset, stderr);
    if(!vm.repl)
        exit(1);
    replError = true;
}

CallFrame* throwRuntimeError(ObjInstance* errorInstance) {
    char tracebuf[1024];
    char linebuf[256];
    size_t offset = 0;

    // Build the stack trace string
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
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
    while (vm.frameCount > 0) {
        CallFrame* frame = &vm.frames[vm.frameCount - 1];

        if (frame->hasTry[frame->tryTop] != -1) {
            vm.stackTop = ++frame->saveStack[frame->tryTop];
            frame->ip = frame->saveIP[frame->tryTop] + frame->hasTry[frame->tryTop] - 1;
            push(OBJ_VAL(errorInstance));
            vm.hasError = true;
            return frame;
        }

        vm.frameCount--;
        vm.stackTop = frame->slots;
    }

    // No try block found — print message and stack trace, then exit
    Value msgVal;
    if (tableGet(&errorInstance->fields, copyString("msg", 3), &msgVal) && IS_STRING(msgVal)) {
        fwrite(AS_CSTRING(msgVal), 1, AS_STRING(msgVal)->length, stderr);
        fputc('\n', stderr); // <- Add a newline after the message
    }

    fwrite(tracebuf, 1, offset, stderr);

    if(!vm.repl)
        exit(1);
    replError = true;
}


CallFrame* VMError(const char* format, ...) {
    char msgbuf[1024];
    char linebuf[256];
    size_t offset = 0;

    // Step 1: Format the error message
    va_list args;
    va_start(args, format);
    offset += vsnprintf(msgbuf + offset, sizeof(msgbuf) - offset, format, args);
    va_end(args);

    offset += snprintf(msgbuf + offset, sizeof(msgbuf) - offset, "\n");

    // Step 2: Add stack trace to msgbuf
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
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
    exit(1);
}

static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.objects = NULL;
    initTable(&vm.strings);
    initTable(&vm.globals);
    initTable(&vm.stringClassMethods);
    initTable(&vm.listClassMethods);
    initTable(&vm.imageClassMethods);

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024; // Default fallback: 1 MB

    // Platform-specific memory size query (optional tuning)
#ifdef _WIN32
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
        SIZE_T totalPhys = statex.ullTotalPhys;
        vm.nextGC = totalPhys / 64; // Use 1/64 of RAM
    }
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages != -1 && page_size != -1) {
        vm.nextGC = (size_t)pages * (size_t)page_size / 64;
    }
#endif

    vm.initString = NULL;
    vm.initString = copyString("init", 4);

    vm.toString = NULL;
    vm.toString = copyString("toString", 8);

    vm.errorString = NULL;
    vm.errorString = copyString("Error", 5);

    vm.indexErrorString = NULL;
    vm.indexErrorString = copyString("IndexOutOfBoundsError", 21);
    // use this for index errors on lists/strings

    vm.typeErrorString = NULL;
    vm.typeErrorString = copyString("TypeError", 9);
    // for invalid operand types, etc.

    vm.nameErrorString = NULL;
    vm.nameErrorString = copyString("NameError", 9);
    // for undefined variable or property

    vm.accessErrorString = NULL;
    vm.accessErrorString = copyString("AccessError", 11);
    // for visibility violations (private/protected)

    vm.illegalArgumentsErrorString = NULL;
    vm.illegalArgumentsErrorString = copyString("IllegalArgumentError", 20);
    // for invalid arguments (e.g. wrong type, value, or count)

    vm.lookUpErrorString = NULL;
    vm.lookUpErrorString = copyString("LookUpError", 11);

    vm.formatErrorString = NULL;
    vm.formatErrorString = copyString("FormatError", 11);
    // for unresolved superclass methods, bad super/binding

    defineNative("clock", clockNative);
    defineNative("input", inputNative);
    defineNative("sleep", sleepNative);
    defineNative("read",  readNative);
    defineStringMethods();
    defineListMethods();

    vm.repl = 0;
}

void freeVM() {
    freeTable(&vm.strings);
    freeTable(&vm.globals);
    vm.initString = NULL;
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError(vm.illegalArgumentsErrorClass, "Expected %d arguments but got %d.",
            closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        VMError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
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
    fflush(stdout);
}


static bool callBoundedNative(Value callee, int argCount) {
    NativeFn native = AS_NATIVE(callee);
    Value result = native(argCount, vm.stackTop - argCount);
    if(vm.hasError){
        vm.hasError = false;
        result = pop();
        push(result);
    }
    vm.stackTop = vm.stackTop - argCount;
    push(result);
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


static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                if(vm.hasError){
                    vm.hasError = false;
                    result = pop();
                    push(result);
                }
                vm.stackTop = vm.stackTop - argCount - 1;
                push(result);
                return true;
            }
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                Value initializer;
                
                if (tableGet(&klass->methods, vm.initString, &initializer)) {
                    
                    vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                    if (AS_BOUND_METHOD(initializer)->method[argCount] == NULL) {
                        runtimeError(vm.illegalArgumentsErrorClass, "No matching initializer found with %d arguments.", argCount);
                        return false;
                    }
                    return call(AS_BOUND_METHOD(initializer)->method[argCount], argCount);
                }
                else if(argCount == 0){
                    vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                    return true;
                }
                
                return false;
            }
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                ObjClosure* closure = bound->method[argCount];
                if (closure == NULL) {
                    runtimeError(vm.illegalArgumentsErrorClass, "No method with arity %d found.", argCount);
                    return false;
                }

                vm.stackTop[-argCount - 1] = bound->receiver;
                return call(bound->method[argCount], argCount);
            }
            case OBJ_MULTI_DISPATCH:{
                ObjMultiDispatch* md = AS_MULTI_DISPATCH(callee);

                ObjClosure* closure = md->closures[argCount];
                if (closure == NULL) {
                    runtimeError(vm.illegalArgumentsErrorClass, "No method for arity %d.", argCount);
                    return false;
                }
                return call(closure, argCount);
            }
            default:
                break;
        }
    }
    runtimeError(vm.typeErrorClass, "Can only call functions and classes.");
    return false;
}

static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
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
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value)) || (IS_NUMBER(value) && AS_NUMBER(value) == 0.0);
}

static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

void multiDispatchAdd(ObjMultiDispatch* dispatch, ObjClosure* closure) {
    int arity = closure->function->arity;
    dispatch->closures[arity] = closure;
}

void multiBoundAdd(ObjBoundMethod* dispatch, ObjClosure* closure) {
    int arity = closure->function->arity;
    dispatch->method[arity] = closure;
}

static void defineMethod(ObjString* name) {
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
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
    pop();
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        return false;
    }

    ObjBoundMethod* bound = AS_BOUND_METHOD(method);
    bound->receiver = peek(0);

    pop();
    push(OBJ_VAL(bound));

    return true;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name,
                            int argCount) {
    Value method;

    if (tableGet(&klass->methods, name, &method)) {
        AS_BOUND_METHOD(method)->receiver = peek(argCount);
        bool yes = callValue(OBJ_VAL(AS_BOUND_METHOD(method)), argCount);
        return yes;
    }

    if (tableGet(&klass->staticMethods, name, &method)) {
        if (IS_NATIVE(method)) {
            bool res = callBoundedNative(method, argCount);
            Value result = pop();
            pop();
            push(result);
            return res;
        }
        bool res = callValue(method, argCount);
        Value result = pop();
        pop();
        push(result);
        return res;
    }

    runtimeError(vm.nameErrorClass, "Undefined method '%s' on instance of class '%s'.",
                 name->chars, klass->name->chars);
    return false;
}


void printStack() {
    printf("          ");
    printf("Stack at %p\n", (void*)vm.stack);
    fflush(stdout);
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        printf("[ ");
        printValue(*slot);
        printf(" ]");
        fflush(stdout);
    }
    printf("\n");
}

static bool isPrivate(ObjString* name) {
    // Make sure it’s non empty, then check the first char.
    return name->length > 0 && name->chars[0] == '#';
}

static bool invoke(ObjString* name, int argCount, CallFrame* frame) {
    Value receiver = peek(argCount);

    if (IS_STRING(receiver)) {
        Value value;
        if (tableGet(&vm.stringClassMethods, name, &value)) {
            vm.isInvokingNative = true;
            bool res = callBoundedNative(value, argCount);
            Value result = pop();
            pop();
            push(result);
            return res;
        }
        runtimeError(vm.nameErrorClass, "Undefined method '%s' on string.", name->chars);
        return false;
    }


    if (IS_IMAGE(receiver)) {
        Value value;
        if (tableGet(&vm.imageClassMethods, name, &value)) {
            vm.isInvokingNative = true;
            bool res = callBoundedNative(value, argCount);
            Value result = pop();
            pop();
            push(result);
            return res;
        }
        runtimeError(vm.nameErrorClass, "Undefined method '%s' on image.", name->chars);
        return false;
    }

    if (IS_LIST(receiver)) {
        Value value;
        if (tableGet(&vm.listClassMethods, name, &value)) {
            vm.isInvokingNative = true;
            bool res = callBoundedNative(value, argCount);
            Value result = pop();
            pop();
            push(result);
            return res;
        }
        runtimeError(vm.nameErrorClass, "Undefined method '%s' on list.", name->chars);
        return false;
    }

    if (IS_CLASS(receiver)) {
        Value value;
        ObjClass* klass = AS_CLASS(receiver);

        if (isPrivate(name) && klass != frame->klass) frame = runtimeError(vm.accessErrorClass, "Cannot access private field from a different class.");

        if (tableGet(&klass->staticMethods, name, &value)) {
            if (IS_NATIVE(value)) {
                bool res = callBoundedNative(value, argCount);
                Value result = pop();
                pop();
                push(result);
                return res;
            }
            
            bool res = callValue(value, argCount);

            //Idk why i added this? but look out ig? more bugs?
            
            //Value result = pop();
            //pop();
            //push(result);
            return res;
        }

        klass = klass->superclass;
        if (klass != NULL && tableGet(&klass->staticMethods, name, &value)) {
            bool res = callValue(value, argCount);
            //Value result = pop();
            //pop();
            //push(result);
            return res;
        }
        runtimeError(vm.nameErrorClass, "Undefined static method '%s' on class '%s'.",
                     name->chars, klass->name->chars);
        return false;
    }


    if (!IS_INSTANCE(receiver)) {
        runtimeError(vm.typeErrorClass, "Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    if (isPrivate(name) && instance->klass != frame->klass) frame = runtimeError(vm.accessErrorClass, "Cannot access private field of a different class.");

    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

static int resolveMultiDispatch(Value* result, ObjString* name) {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

    // Search locals from top of current frame down
    for (Value* slot = vm.stackTop - 1; slot >= frame->slots; slot--) {
        if (IS_OBJ(*slot) && IS_MULTI_DISPATCH(*slot)) {
            ObjMultiDispatch* md = AS_MULTI_DISPATCH(*slot);

            if (md->name == name) {
                *result = *slot;
                return 2;
            }
        }
    }

    // Then try global table
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

static InterpretResult run() {
    register CallFrame* frame = &vm.frames[vm.frameCount - 1];

    #define READ_BYTE() (*frame->ip++)

    #define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

    #define READ_CONSTANT() \
        (frame->closure->function->chunk.constants.values[READ_BYTE()])
    #define BINARY_OP(valueType, op) \
    do { \
        bool fl = false; \
        int peeker = 1; \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(peeker))) { \
            frame = runtimeError(vm.typeErrorClass, "Operands must be numbers."); \
            fl = true; \
            break; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
        if (fl) break;\
    } while (false)\

    #define READ_STRING() AS_STRING(READ_CONSTANT())

        for (;;) {
            if(replError){
                replError = false;
                return INTERPRET_RUNTIME_ERROR;
            }
    #ifdef DEBUG_TRACE_EXECUTION
            printf("          ");
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            disassembleInstruction(&frame->closure->function->chunk,
                    (int)(frame->ip - frame->closure->function->chunk.code));
    #endif
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_RETURN: {
                Value result = pop();
                closeUpvalues(frame->slots);
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];

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
                push(constant);
                break;
            }
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    frame = runtimeError(vm.typeErrorClass, "Operand must be a number.");
                    break;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;

            case OP_ADD: {
                if (IS_INSTANCE(peek(0)) && IS_INSTANCE(peek(1))) {
                    ObjInstance* a = AS_INSTANCE(peek(0));
                    ObjInstance* b = AS_INSTANCE(peek(1));

                    if (a->klass != b->klass) {
                        frame = runtimeError(vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("+", 1), &method)) {
                        int argCount = 1;
                        invoke(copyString("+", 1), argCount, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                    frame = runtimeError(vm.typeErrorClass, "No overload of '+' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(peek(0));
                    double a = AS_NUMBER(peek(1));
                    pop();
                    pop();
                    push(NUMBER_VAL(a + b));
                }
                else if (IS_STRING(peek(0)) && IS_NUMBER(peek(1))) {
                    ObjString* b = AS_STRING(peek(0));
                    double num = AS_NUMBER(peek(1));

                    char buffer[32];
                    int len = snprintf(buffer, sizeof(buffer), "%.14g", num);

                    // Allocate and intern as ObjString
                    ObjString* a = copyString(buffer, len);

                    int length = a->length + b->length;
                    char* chars = ALLOCATE(char, length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';

                    ObjString* result = takeString(chars, length);
                    pop();
                    pop();
                    push(OBJ_VAL(result));
                }
                else if (IS_NUMBER(peek(0)) && IS_STRING(peek(1))) {
                    double num = AS_NUMBER(peek(0));

                    char buffer[32];
                    int len = snprintf(buffer, sizeof(buffer), "%.14g", num);

                    // Allocate and intern as ObjString
                    ObjString* b = copyString(buffer, len);
                    ObjString* a = AS_STRING(peek(1));

                    int length = a->length + b->length;
                    char* chars = ALLOCATE(char, length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';

                    ObjString* result = takeString(chars, length);
                    pop();pop();
                    push(OBJ_VAL(result));
                }
                else {
                    frame = runtimeError(vm.typeErrorClass,
                        "Operands must be two numbers or two strings.");
                    break;
                }
                break;
            }
            case OP_SUBTRACT:
                if (IS_INSTANCE(peek(0)) && IS_INSTANCE(peek(1))) {
                    ObjInstance* a = AS_INSTANCE(peek(0));
                    ObjInstance* b = AS_INSTANCE(peek(1));

                    if (a->klass != b->klass) {
                        frame = runtimeError(vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("-", 1), &method)) {
                        int argCount = 1;
                        invoke(copyString("-", 1), argCount, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                    frame = runtimeError(vm.typeErrorClass, "No overload of '-' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: {
                if (IS_INSTANCE(peek(0)) && IS_INSTANCE(peek(1))) {
                    ObjInstance* a = AS_INSTANCE(peek(0));
                    ObjInstance* b = AS_INSTANCE(peek(1));

                    if (a->klass != b->klass) {
                        frame = runtimeError(vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("*", 1), &method)) {
                        int argCount = 1;
                        invoke(copyString("*", 1), argCount, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                    frame = runtimeError(vm.typeErrorClass, "No overload of '*' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                BINARY_OP(NUMBER_VAL, *); break;
            }
            case OP_DIVIDE:
                if (IS_INSTANCE(peek(0)) && IS_INSTANCE(peek(1))) {
                    ObjInstance* a = AS_INSTANCE(peek(0));
                    ObjInstance* b = AS_INSTANCE(peek(1));

                    if (a->klass != b->klass) {
                        frame = runtimeError(vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("/", 1), &method)) {
                        int argCount = 1;
                        invoke(copyString("/", 1), argCount, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                    frame = runtimeError(vm.typeErrorClass, "No overload of '/' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }
                BINARY_OP(NUMBER_VAL, /); break;
            case OP_MOD: {
                if (IS_INSTANCE(peek(0)) && IS_INSTANCE(peek(1))) {
                    ObjInstance* a = AS_INSTANCE(peek(0));
                    ObjInstance* b = AS_INSTANCE(peek(1));

                    if (a->klass != b->klass) {
                        frame = runtimeError(vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("-", 1), &method)) {
                        int argCount = 1;
                        invoke(copyString("-", 1), argCount, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                    frame = runtimeError(vm.typeErrorClass, "No overload of '-' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }

                bool fl = false;
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                    frame = runtimeError(vm.typeErrorClass, "Operands must be numbers.");
                    fl = true;
                    break;
                }
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL(fmod(a, b)));
                break;
            }
            case OP_INS: {
                if (IS_INSTANCE(peek(0)) && IS_INSTANCE(peek(1))) {
                    ObjInstance* a = AS_INSTANCE(peek(0));
                    ObjInstance* b = AS_INSTANCE(peek(1));

                    if (a->klass != b->klass) {
                        frame = runtimeError(vm.typeErrorClass, "Cannot perform operation for instances of different classes.");
                        break;
                    }

                    Value method;
                    if (tableGet(&a->klass->methods, copyString("-", 1), &method)) {
                        int argCount = 1;
                        invoke(copyString("-", 1), argCount, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                    frame = runtimeError(vm.typeErrorClass, "No overload of '\' for instances of class '%s'.", a->klass->name->chars);
                    break;
                }

                bool fl = false;
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                    frame = runtimeError(vm.typeErrorClass, "Operands must be numbers.");
                    fl = true;
                    break;
                }
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(NUMBER_VAL((int)(a / b)));
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_PRINT: {
                Value printVal = peek(0);
                if (IS_INSTANCE(printVal)) {
                    ObjInstance* instance = AS_INSTANCE(printVal);
                    Value method;
                    if (tableGet(&instance->klass->methods, vm.toString, &method)) {
                        frame->ip--;
                        invoke(vm.toString, 0, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                }
                pop();
                printValue(printVal);
                break;
            }
            case OP_PRINTLN: {
                Value printVal = peek(0);
                if (IS_INSTANCE(printVal)) {
                    ObjInstance* instance = AS_INSTANCE(printVal);
                    Value method;
                    if (tableGet(&instance->klass->methods, vm.toString, &method)) {
                        frame->ip--;
                        invoke(vm.toString, 0, frame);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                }
                pop();
                printValue(printVal);
                printf("\n");
                break;
            }
            case OP_PRINTLN_BLANK: {
                printf("\n");
                break;
            }
            case OP_POP: pop(); break;
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    frame = runtimeError(vm.nameErrorClass, "Undefined variable '%s'.", name->chars);
                    fflush(stdout);
                    break;
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    frame = runtimeError(vm.nameErrorClass, "Undefined variable '%s'.", name->chars);
                    break;
                }
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
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

                if (!callValue(peek(argCount), argCount)) {
                    break;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] =
                            captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
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


                push(OBJ_VAL(klass));
                break;
            case OP_GET_PROPERTY: {
                if (IS_STRING(peek(0)) || IS_LIST(peek(0))){
                    frame = runtimeError(vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                }
                if (IS_INSTANCE(peek(0))) {
                    ObjInstance* instance = AS_INSTANCE(peek(0));
                    ObjString* name = READ_STRING();

                    if (isPrivate(name) && instance->klass != frame->klass) frame = runtimeError(vm.accessErrorClass, "Cannot access private field from a different class.");

                    Value value;
                    if (tableGet(&instance->fields, name, &value)) {
                        pop();
                        push(value);
                        break;
                    }

                    if (bindMethod(instance->klass, name)) {
                        break;
                    }

                    frame = runtimeError(vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                }

                if (IS_CLASS(peek(0))) {
                    ObjClass* klass = AS_CLASS(peek(0));
                    ObjString* name = READ_STRING();

                    if (isPrivate(name) && klass != frame->klass) frame = runtimeError(vm.accessErrorClass, "Cannot access private field from a different class.");

                    Value value;
                    if (tableGet(&klass->staticVars, name, &value)) {
                        pop();
                        push(value);
                        break;
                    }
                    if (tableGet(&klass->staticMethods, name, &value)) {
                        pop();
                        push(value);
                        break;
                    }

                    klass = klass->superclass;
                    if (klass != NULL) {
                        Value value;
                        if (tableGet(&klass->staticVars, name, &value)) {
                            pop();
                            push(value);
                            break;
                        }
                        if (tableGet(&klass->staticMethods, name, &value)) {
                            pop();
                            push(value);
                            break;
                        }
                    }
                    frame = runtimeError(vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                    break;
                }

                frame = runtimeError(vm.typeErrorClass, "Only instances and classes have fields.");
                break;
            }
            case OP_SET_PROPERTY: {
                if (IS_STRING(peek(1)) || IS_LIST(peek(1))){
                    frame = runtimeError(vm.nameErrorClass, "Undefined property '%s'.", name->chars);
                }

                if (IS_CLASS(peek(1))) {
                    ObjClass* klass = AS_CLASS(peek(1));
                    ObjString* name = READ_STRING();

                    if (klass->superclass != NULL && tableGet(&klass->superclass->staticVars, name, NULL)) {
                        tableSet(&klass->superclass->staticVars, name, peek(0));
                        Value value = pop();
                        pop();
                        push(value);
                        break;
                    }
                    tableSet(&klass->staticVars, name, peek(0));
                    Value value = pop();
                    pop();
                    push(value);
                    break;
                }

                if (!IS_INSTANCE(peek(1))) {
                    frame = runtimeError(vm.typeErrorClass, "Only instances have fields.");
                    break;
                }

                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop();
                push(value);
                break;
            }
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
            case OP_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount, frame)) {
                    break;
                }
                //pop();
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_INHERIT: {
                if (!IS_CLASS(peek(1))) {
                    frame = runtimeError(vm.typeErrorClass, "Superclass must be a class.");
                    break;
                }

                ObjClass* superclass = AS_CLASS(peek(1));
                ObjClass* subclass = AS_CLASS(peek(0));
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

                pop(); // Subclass.
                break;
            }
            case OP_GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = AS_CLASS(pop());

                if (!bindMethod(superclass, name)) {
                    break;
                }
                break;
            }
            case OP_SUPER_INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                ObjClass* superclass = AS_CLASS(pop());
                if (!invokeFromClass(superclass, method, argCount)) {
                    break;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_GET_INDEX: {
                Value index = pop();
                Value list = pop();

                if (!IS_LIST(list)) {
                    frame = runtimeError(vm.typeErrorClass, "Can only index into lists.");
                    break;
                }

                if (!IS_NUMBER(index)) {
                    frame = runtimeError(vm.typeErrorClass, "List index must be a number.");
                    break;
                }

                ObjList* objList = AS_LIST(list);
                int i = (int)AS_NUMBER(index);

                if (i < 0 || i >= objList->elements.count) {
                    frame = runtimeError(vm.indexErrorClass, "List index out of bounds.");
                    break;
                }

                push(objList->elements.values[i]);
                break;
            }

            case OP_SET_INDEX: {
                Value value = pop();
                Value index = pop();
                Value list = pop();

                if (!IS_LIST(list)) {
                    frame = runtimeError(vm.typeErrorClass, "Can only index into lists.");
                    break;
                }

                if (!IS_NUMBER(index)) {
                    frame = runtimeError(vm.typeErrorClass, "List index must be a number.");
                    break;
                }

                ObjList* objList = AS_LIST(list);
                int i = (int)AS_NUMBER(index);

                if (i < 0 || i >= objList->elements.count) {
                    frame = runtimeError(vm.indexErrorClass, "List index out of bounds.");
                    break;
                }

                objList->elements.values[i] = value;
                push(value);
                break;
            }
            case OP_LIST: {
                int count = READ_BYTE();
                ObjList* list = newList();
                push(OBJ_VAL(list));

                for (int i = count; i >= 1; i--) {
                    writeValueArray(&list->elements, peek(i));
                }
                Value listValue = pop();
                for (int i = 0; i < count; i++) pop();

                push(listValue);
                break;
            }
            case OP_DISPATCH: {
                Value closureVal = peek(0);
                if (!IS_CLOSURE(closureVal)) {
                    frame = runtimeError(vm.typeErrorClass, "Expected function to dispatch.");
                    break;
                }

                ObjClosure* closure = AS_CLOSURE(closureVal);
                ObjString* name = closure->function->name;

                Value dispatchVal;
                int scope = resolveMultiDispatch(&dispatchVal, name);
                if (scope) {
                    // Reuse existing MultiDispatch
                    ObjMultiDispatch* dispatch = AS_MULTI_DISPATCH(dispatchVal);
                    multiDispatchAdd(dispatch, closure);
                    pop(); // remove closure
                    //if (scope == 1)
                        push(OBJ_VAL(dispatch)); // push dispatch instead
                } else {
                        // Create new one
                    ObjMultiDispatch* dispatch = newMultiDispatch(name);
                    multiDispatchAdd(dispatch, closure);
                    pop(); // remove closure
                    push(OBJ_VAL(dispatch)); // push new dispatch
                }

                break;
            }
            case OP_TRY: {
                if (frame->tryTop == 10) VMError("Max try depth reached");
                frame->tryTop++;
                frame->saveStack[frame->tryTop] = vm.stackTop;
                frame->saveIP[frame->tryTop] = frame->ip;
                frame->hasTry[frame->tryTop] = READ_BYTE();

                break;
            }
            case OP_END_TRY: {
                if (frame->tryTop == 0) VMError("No try block to end");
                frame->hasTry[frame->tryTop] = -1;
                if (frame->tryTop >= 0) {
                    frame->tryTop--;
                }
                break;
            }
            case OP_STATIC_VAR: {
                ObjString* name = READ_STRING();
                Value value = pop();
                ObjClass* klass = AS_CLASS(peek(0));
                tableSet(&klass->staticVars, name, value);
                break;
            }
            case OP_STATIC_METHOD: {
                Value method = peek(0);
                ObjClass* klass = AS_CLASS(peek(1));
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
                pop();
                break;
            }
            case OP_CONSTANT_LONG: {
                    uint32_t b1 = READ_BYTE();
                    uint32_t b2 = READ_BYTE();
                    uint32_t b3 = READ_BYTE();
                    uint32_t index = (b1 << 16) | (b2 << 8) | b3;
                    push(frame->closure->function->chunk.constants.values[index]);

                    break;
            }

            case OP_THROW: {
                Value error = pop();
                if (!IS_INSTANCE(error)) {
                    runtimeError(vm.typeErrorClass, "Can only throw instances of error classes.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(error);
                if (!hasAncestor(instance, vm.errorClass)) {
                    runtimeError(vm.typeErrorClass, "Can only throw instances of error.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                throwRuntimeError(instance); // a function you'll define
                break;
            }

        }
    }

#undef READ_CONSTANT
#undef READ_BYTE
#undef READ_STRING
#undef READ_SHORT
}

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    if (vm.noRun)
        return 0;

    return run();
}

