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
        runtimeError(vm.illegalArgumentsErrorClass, "charCode() takes no arguments.");
        return NIL_VAL;
    }

    ObjString* self = AS_STRING(args[-1]); // assuming it's a bound method
    if (self->length == 0) return NUMBER_VAL(-1);

    return NUMBER_VAL((uint8_t)self->chars[0]);
}

static Value stringSplitNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError(vm.illegalArgumentsErrorClass, "split(delimiter) expects one string argument.");
        return NIL_VAL;
    }

    ObjString* source = AS_STRING(args[-1]); // receiver: the original string
    ObjString* delimiter = AS_STRING(args[0]);

    const char* str = source->chars;
    const char* delim = delimiter->chars;
    int strLen = source->length;
    int delimLen = delimiter->length;

    ObjList* result = newList();
    push(OBJ_VAL(result));
    // ✅ SPECIAL CASE: empty delimiter → split into characters
    if (delimLen == 0) {
        for (int i = 0; i < strLen; i++) {
            ObjString* ch = copyString(str + i, 1);
            writeValueArray(&result->elements, OBJ_VAL(ch));
        }
        return OBJ_VAL(result);
    }

    int start = 0;

    // ✅ Normal splitting
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
    pop();

    return OBJ_VAL(result);
}

#include <ctype.h>

static Value stringTrimNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "trim() takes no arguments.");
        return NIL_VAL;
    }

    ObjString* self = AS_STRING(args[-1]); // receiver
    const char* start = self->chars;
    const char* end = self->chars + self->length - 1;

    // Trim leading whitespace
    while (start <= end && isspace((unsigned char)*start)) {
        start++;
    }

    // Trim trailing whitespace
    while (end >= start && isspace((unsigned char)*end)) {
        end--;
    }

    int newLength = (int)(end - start + 1);
    return OBJ_VAL(copyString(start, newLength));
}

static Value stringLengthNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "string.length() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError(vm.typeErrorClass, "string.length() must be called on a string.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    return NUMBER_VAL(string->length);
}

static Value stringStartsWithNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError(vm.illegalArgumentsErrorClass, "startsWith() expects one string argument.");
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

static Value stringEndsWithNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError(vm.illegalArgumentsErrorClass, "endsWith() expects one string argument.");
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


static Value stringCharAtNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(vm.typeErrorClass, "string.charAt() takes a integer.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError(vm.typeErrorClass, "string.charAt() must be called on a string.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm.typeErrorClass, "string.charAt() index must be  an integer.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    int index = AS_NUMBER(args[0]);
    if (index < 0 || index >= string->length) {
        runtimeError(vm.indexErrorClass, "string.charAt() index out of bounds.");
        return NIL_VAL;
    }
    if (index%1 != 0) {
        runtimeError(vm.typeErrorClass, "string.charAt() index must be an integer.");
        return NIL_VAL;
    }
    return OBJ_VAL(copyString(&string->chars[index], 1));
}

static Value stringToUpperCaseNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass,"string.toUpperCase() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError(vm.typeErrorClass, "string.toUpperCase() must be called on a string.");
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
        runtimeError(vm.illegalArgumentsErrorClass, "string.toLowerCase() takes no arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[-1])) {
        runtimeError(vm.illegalArgumentsErrorClass, "string.toLowerCase() must be called on a string.");
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
        runtimeError(vm.illegalArgumentsErrorClass, "string.substring() takes 2 arguments: start and end indices.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError(vm.illegalArgumentsErrorClass, "string.substring() takes two number indices.");
        return NIL_VAL;
    }

    ObjString* string = AS_STRING(args[-1]);
    int start = (int)AS_NUMBER(args[0]);
    int end = (int)AS_NUMBER(args[1]);

    if (start < 0 || end > string->length || start > end) {
        runtimeError(vm.indexErrorClass, "string.substring() indices out of bounds.");
        return NIL_VAL;
    }

    return OBJ_VAL(copyString(&string->chars[start], end - start));
}

static Value stringIndexOfNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(vm.illegalArgumentsErrorClass, "str.indexOf() takes 1 argument.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm.illegalArgumentsErrorClass, "Parameter must be of type string.");
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
        runtimeError(vm.illegalArgumentsErrorClass, "str.asNum() takes no arguments.");
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    char* end;
    double value = strtod(str->chars, &end);
    if (end == str->chars || *end != '\0') {
        runtimeError(vm.formatErrorClass, "Invalid number value %s", str->chars);
        return NIL_VAL;
    }
    return NUMBER_VAL(value);
}

static Value stringParseBooleanNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "str.parseBoolean() takes no arguments.");
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[-1]);
    if (strcasecmp(str->chars, "true") == 0 || strcmp(str->chars, "1") == 0) {
        return BOOL_VAL(true);
    } else if (strcasecmp(str->chars, "false") == 0 || strcmp(str->chars, "0") == 0) {
        return BOOL_VAL(false);
    } else {
        runtimeError(vm.formatErrorClass, "Invalid boolean value %s", str->chars );
        return NIL_VAL;
    }
}

static Value str_isDigit(int argCount, Value* args) {
    if (argCount != 0) runtimeError(vm.illegalArgumentsErrorClass, "str.parse() takes no aruments.");
    ObjString* str = AS_STRING(args[-1]);

    char* end;
    double num = strtod(str->chars, &end);
    if (end != str->chars && *end == '\0') {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value str_parse(int argCount, Value* args) {
    if (argCount != 0) runtimeError(vm.illegalArgumentsErrorClass, "str.parse() takes no aruments.");
    ObjString* str = AS_STRING(args[-1]);

    char* end;
    double num = strtod(str->chars, &end);
    if (end != str->chars && *end == '\0') {
        return NUMBER_VAL(num);
    }

    if (strcmp(str->chars, "true") == 0 || strcmp(str->chars, "1") == 0) {
        return BOOL_VAL(true);
    } else if (strcmp(str->chars, "false") == 0 || strcmp(str->chars, "0") == 0) {
        return BOOL_VAL(false);
    }

    return OBJ_VAL(str);
}
