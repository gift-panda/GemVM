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

static Value stringCharCodeNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("charCode() takes no arguments.");
        return NIL_VAL;
    }

    ObjString* self = AS_STRING(args[-1]); // assuming it's a bound method
    if (self->length == 0) return NUMBER_VAL(-1);

    return NUMBER_VAL((uint8_t)self->chars[0]);
}

static Value stringLengthNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("string.length() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError("string.length() must be called on a string.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    return NUMBER_VAL(string->length);
}

static Value stringCharAtNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("string.charAt() takes a integer.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError("string.charAt() must be called on a string.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError("string.charAt() index must be  an integer.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    int index = AS_NUMBER(args[0]);
    if (index < 0 || index >= string->length) {
        runtimeError("string.charAt() index out of bounds.");
        return NIL_VAL;
    }
    if (index%1 != 0) {
        runtimeError("string.charAt() index must be an integer.");
        return NIL_VAL;
    }
    return OBJ_VAL(copyString(&string->chars[index], 1));
}

static Value stringToUpperCaseNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("string.toUpperCase() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError("string.toUpperCase() must be called on a string.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    char* upper = ALLOCATE(char, string->length);
    for (int i = 0; i < string->length; i++) {
        upper[i] = toupper(string->chars[i]);
    }
    return OBJ_VAL(copyString(upper, string->length));
}

static Value stringToLowerCaseNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("string.toLowerCase() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError("string.toLowerCase() must be called on a string.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    char* lower = ALLOCATE(char, string->length);
    for (int i = 0; i < string->length; i++) {
        lower[i] = tolower(string->chars[i]);
    }
    return OBJ_VAL(copyString(lower, string->length));
}

static Value stringSubstringNative(int argCount, Value* args) {
    if (argCount != 2) {
        runtimeError("string.substring(start, end) takes two arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1]) || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("string.substring() must be called on a string and use integer arguments.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    int start = (int)AS_NUMBER(args[0]);
    int end = (int)AS_NUMBER(args[1]);

    if (start < 0 || end > string->length || start > end) {
        runtimeError("string.substring() indices out of bounds.");
        return NIL_VAL;
    }

    return OBJ_VAL(copyString(&string->chars[start], end - start));
}

static Value stringIndexOfNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("string.indexOf(substr) takes one argument.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1]) || !IS_STRING(args[0])) {
        runtimeError("string.indexOf() must be called on a string with a string argument.");
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

static Value stringParseNumberNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("string.parseNumber() takes no arguments.");
        return NIL_VAL;
    }
    if (!IS_STRING(args[-1])) {
        runtimeError("string.parseNumber() must be called on a string.");
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    char* end;
    double value = strtod(str->chars, &end);
    if (end == str->chars || *end != '\0') {
        runtimeError("Invalid number format: '%s'", str->chars);
        return NIL_VAL;
    }
    return NUMBER_VAL(value);
}

static Value stringParseBooleanNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("string.parseBoolean() takes no arguments.");
        return NIL_VAL;
    }
    if (!IS_STRING(args[-1])) {
        runtimeError("string.parseBoolean() must be called on a string.");
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    if (strcasecmp(str->chars, "true") == 0 || strcmp(str->chars, "1") == 0) {
        return BOOL_VAL(true);
    } else if (strcasecmp(str->chars, "false") == 0 || strcmp(str->chars, "0") == 0) {
        return BOOL_VAL(false);
    } else {
        runtimeError("Invalid boolean format: '%s'", str->chars);
        return NIL_VAL;
    }
}

