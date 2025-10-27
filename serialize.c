#include "cJSON.h"
#include "object.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "vm.h"        // <- for theVM
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static cJSON* serialize_function(ObjFunction* func);
// -------------------------
// Serialize ObjString
// -------------------------
static cJSON* serialize_string(ObjString* string) {
    if (!string) return cJSON_CreateNull();

    cJSON* jsonStr = cJSON_CreateObject();
    // Use length to get actual string content
    cJSON_AddStringToObject(jsonStr, "chars", strndup(string->chars, string->length));
    return jsonStr;
}

// -------------------------
// Serialize Value (basic)
// -------------------------
static cJSON* serialize_value(Value value) {
    if (IS_STRING(value)) {
        return serialize_string(AS_STRING(value));
    } else if (IS_FUNCTION(value)) {
        return serialize_function(AS_FUNCTION(value)); // function handled separately
    } else if (value.type == VAL_NUMBER) {
        return cJSON_CreateNumber(value.as.number);
    } else if (value.type == VAL_BOOL) {
        return cJSON_CreateBool(value.as.boolean);
    } else if (value.type == VAL_NIL) {
        return cJSON_CreateNull();
    } else {
        return cJSON_CreateString("<unsupported_value>");
    }
}

// -------------------------
// Serialize Chunk
// -------------------------
static cJSON* serialize_chunk(Chunk* chunk) {
    if (!chunk) return cJSON_CreateNull();

    cJSON* jsonChunk = cJSON_CreateObject();
    cJSON_AddNumberToObject(jsonChunk, "count", chunk->count);
    cJSON_AddNumberToObject(jsonChunk, "capacity", chunk->capacity);

    // Serialize code array (uint8_t -> int)
    cJSON* codeArray = cJSON_CreateArray();
    for (int i = 0; i < chunk->count; i++) {
        cJSON_AddItemToArray(codeArray, cJSON_CreateNumber(chunk->code[i]));
    }
    cJSON_AddItemToObject(jsonChunk, "code", codeArray);

    // Serialize lines array
    cJSON* linesArray = cJSON_CreateArray();
    for (int i = 0; i < chunk->count; i++) {
        cJSON_AddItemToArray(linesArray, cJSON_CreateNumber(chunk->lines[i]));
    }
    cJSON_AddItemToObject(jsonChunk, "lines", linesArray);

    // Serialize constants
    cJSON* constArray = cJSON_CreateArray();
    for (int i = 0; i < chunk->constants.count; i++) {
        cJSON* valJson = serialize_value(chunk->constants.values[i]);
        if (valJson) cJSON_AddItemToArray(constArray, valJson);
    }
    cJSON_AddItemToObject(jsonChunk, "constants", constArray);

    return jsonChunk;
}

// -------------------------
// Serialize ObjFunction
// -------------------------
static cJSON* serialize_function(ObjFunction* func) {
    if (!func) return cJSON_CreateNull();

    cJSON* jsonFunc = cJSON_CreateObject();
    cJSON_AddStringToObject(jsonFunc, "type", "ObjFunction");

    if (func->name) {
        cJSON_AddItemToObject(jsonFunc, "name", serialize_string(func->name));
    } else {
        cJSON_AddNullToObject(jsonFunc, "name");
    }

    cJSON_AddNumberToObject(jsonFunc, "arity", func->arity);
    cJSON_AddNumberToObject(jsonFunc, "upvalueCount", func->upvalueCount);
    cJSON_AddItemToObject(jsonFunc, "chunk", serialize_chunk(&func->chunk));

    return jsonFunc;
}

#include <zlib.h>
void serialize(const char* filename, ObjFunction* function) {
    cJSON* root = cJSON_CreateArray();
    Value val = OBJ_VAL(function);

    cJSON* funcJson = serialize_function(AS_FUNCTION(val));
    cJSON_AddItemToArray(root, funcJson);
    
    char* jsonStr = cJSON_Print(root); // minified
    if (jsonStr) {
        if(vm.zip){
            gzFile f = gzopen(filename, "wb"); // open gzip file
            if (f) {
                gzwrite(f, jsonStr, strlen(jsonStr));
                gzclose(f);
            }
            free(jsonStr);
        }
        else{
            FILE* f = fopen(filename, "wb");
            if (f) {
                fwrite(jsonStr, 1, strlen(jsonStr), f);
                fclose(f);
            }
            free(jsonStr);
        }
    }

    cJSON_Delete(root);
}

