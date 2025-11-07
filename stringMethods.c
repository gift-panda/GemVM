//
// Created by meow on 6/27/25.
//


#include <ctype.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include <stdlib.h>

static Value stringIteratorNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method iterator for arity %d.", argCount);
        return NIL_VAL;
    }
    
    Value classVal;
    ObjString* className = copyString("StringIterator", 14);
    if (!tableGet(&vm.globals, className, &classVal)) {
        runtimeErrorCtx(ctx, vm.lookUpErrorClass, "StringIterator class not found.");
        return NIL_VAL;
    }

    ObjClass* iteratorClass = AS_CLASS(classVal);
    ObjInstance* instance = newInstance(iteratorClass);

    Value listVal = args[-1];
    ObjString* listField = copyString("str", 3);
    tableSet(&instance->fields, listField, listVal);

    ObjString* indexField = copyString("index", 5);
    tableSet(&instance->fields, indexField, NUMBER_VAL(0));

    return OBJ_VAL(instance);
}

static Value stringCharCodeNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method charCode for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* self = AS_STRING(args[-1]); // assuming it's a bound method
    if (self->length == 0) return NUMBER_VAL(-1);

    return NUMBER_VAL((uint8_t)self->chars[0]);
}

static Value stringSplitNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method split for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "split: expected (String) but got (%s).",
                     getValueTypeName(args[0]));
        return NIL_VAL;
    }

    ObjString* source = AS_STRING(args[-1]);
    ObjString* delimiter = AS_STRING(args[0]);

    const char* str = source->chars;
    const char* delim = delimiter->chars;
    int strLen = source->length;
    int delimLen = delimiter->length;

    ObjList* result = newList();

    if (delimLen == 0) {
        for (int i = 0; i < strLen; i++) {
            ObjString* ch = copyString(str + i, 1);
            writeValueArray(&result->elements, OBJ_VAL(ch));
        }
        return OBJ_VAL(result);
    }

    int start = 0;
    for (int i = 0; i <= strLen - delimLen;) {
        if (memcmp(str + i, delim, delimLen) == 0) {
            int segmentLen = i - start;
            ObjString* segment = copyString(str + start, segmentLen);
            writeValueArray(&result->elements, OBJ_VAL(segment));
            i += delimLen;
            start = i;
        } else {
            i++;
        }
    }

    if (start <= strLen) {
        ObjString* segment = copyString(str + start, strLen - start);
        writeValueArray(&result->elements, OBJ_VAL(segment));
    }

    return OBJ_VAL(result);
}

#include <ctype.h>

static Value stringTrimNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method trim for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* self = AS_STRING(args[-1]);
    const char* start = self->chars;
    const char* end = self->chars + self->length - 1;

    while (start <= end && isspace((unsigned char)*start)) start++;
    while (end >= start && isspace((unsigned char)*end)) end--;

    int newLength = (int)(end - start + 1);
    return OBJ_VAL(copyString(start, newLength));
}

static Value stringLengthNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method length for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    return NUMBER_VAL(string->length);
}

static Value stringStartsWithNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method startsWith for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "startsWith: expected (String) but got (%s).",
                     getValueTypeName(args[0]));
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    ObjString* prefix = AS_STRING(args[0]);

    if (prefix->length > str->length) return BOOL_VAL(false);

    for (int i = 0; i < prefix->length; i++) {
        if (str->chars[i] != prefix->chars[i]) return BOOL_VAL(false);
    }

    return BOOL_VAL(true);
}

static Value stringEndsWithNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method endsWith for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "endsWith: expected (String) but got (%s).",
                     getValueTypeName(args[0]));
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    ObjString* suffix = AS_STRING(args[0]);

    if (suffix->length > str->length) return BOOL_VAL(false);

    int offset = str->length - suffix->length;
    for (int i = 0; i < suffix->length; i++) {
        if (str->chars[offset + i] != suffix->chars[i]) return BOOL_VAL(false);
    }

    return BOOL_VAL(true);
}

static Value stringCharAtNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method charAt for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "charAt: expected (Number) but got (%s).",
                     getValueTypeName(args[0]));
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    double num = AS_NUMBER(args[0]);
    int index = (int)num;

    return OBJ_VAL(copyString(&string->chars[index], 1));
}

static Value stringToUpperCaseNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method toUpperCase for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    char* upper = ALLOCATE(char, string->length);
    for (int i = 0; i < string->length; i++) {
        upper[i] = toupper((unsigned char)string->chars[i]);
    }

    Value result = OBJ_VAL(copyString(upper, string->length));
    FREE_ARRAY(char, upper, string->length);
    return result;
}

static Value stringToLowerCaseNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method toLowerCase for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    char* lower = ALLOCATE(char, string->length);

    for (int i = 0; i < string->length; i++) {
        lower[i] = tolower((unsigned char)string->chars[i]);
    }

    Value result = OBJ_VAL(copyString(lower, string->length));
    FREE_ARRAY(char, lower, string->length);
    return result;
}

static Value stringSubstringNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 2) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "No method substring for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "substring: expected (Number, Number) but got (%s, %s).",
                     getValueTypeName(args[0]), getValueTypeName(args[1]));
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    int start = (int)AS_NUMBER(args[0]);
    int end = (int)AS_NUMBER(args[1]);

    if (start < 0 || end > string->length || start > end) {
        runtimeErrorCtx(ctx, vm.indexErrorClass, "substring: indices out of bounds.");
        return NIL_VAL;
    }

    return OBJ_VAL(copyString(&string->chars[start], end - start));
}

static Value stringIndexOfNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "no method indexOf for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "indexOf: expected (String) but got (%s).", getValueTypeName(args[0]));
        return NIL_VAL;
    }

    ObjString* haystack = AS_STRING(args[-1]);
    ObjString* needle = AS_STRING(args[0]);

    for (int i = 0; i <= haystack->length - needle->length; i++) {
        if (memcmp(&haystack->chars[i], needle->chars, needle->length) == 0) {
            return NUMBER_VAL(i);
        }
    }

    return NUMBER_VAL(-1);
}


static Value stringParseNumberNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "no method asNum for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    char* end;
    double value = strtod(str->chars, &end);

    if (end == str->chars || *end != '\0') {
        runtimeErrorCtx(ctx, vm.formatErrorClass,
                     "asNum: invalid numeric value (%s).", str->chars);
        return NIL_VAL;
    }

    return NUMBER_VAL(value);
}

static Value stringParseBooleanNative(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "no method parseBoolean for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    if (strcasecmp(str->chars, "true") == 0 || strcmp(str->chars, "1") == 0)
        return BOOL_VAL(true);
    if (strcasecmp(str->chars, "false") == 0 || strcmp(str->chars, "0") == 0)
        return BOOL_VAL(false);

    runtimeErrorCtx(ctx, vm.formatErrorClass,
                 "parseBoolean: invalid boolean value (%s).", str->chars);
    return NIL_VAL;
}

static Value str_isDigit(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "no method isDigit for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    char* end;
    double num = strtod(str->chars, &end);
    return BOOL_VAL(end != str->chars && *end == '\0');
}

static Value str_parse(Thread* ctx, int argCount, Value* args) {
    if (argCount != 0) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                     "no method parse for arity %d.", argCount);
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    char* end;
    double num = strtod(str->chars, &end);

    if (end != str->chars && *end == '\0')
        return NUMBER_VAL(num);

    if (strcasecmp(str->chars, "true") == 0 || strcmp(str->chars, "1") == 0)
        return BOOL_VAL(true);
    if (strcasecmp(str->chars, "false") == 0 || strcmp(str->chars, "0") == 0)
        return BOOL_VAL(false);

    return OBJ_VAL(str);
}
