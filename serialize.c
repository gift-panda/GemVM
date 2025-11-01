#include "cJSON.h"
#include "object.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "vm.h"        // <- for theVM
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


FILE* file;

#define FunctionType 0
#define StringType 1
#define NilType 2
#define NumType 3
#define BoolType 4
#define ChunkType 5

static void serialize_function(ObjFunction* func);

void writeByte(uint8_t value) {
    fwrite(&value, sizeof(uint8_t), 1, file);
}

void writeShort(uint16_t value) {
    fwrite(&value, sizeof(uint16_t), 1, file);
}

void writeInt(int value) {
    fwrite(&value, sizeof(int), 1, file);
}

void writeDouble(double value){
    fwrite(&value, sizeof(double), 1, file);
}

static void serialize_string(ObjString* string) {
    writeByte(StringType);
    writeInt(string->length);

    for (int i = 0; i < string->length; i++) {
        writeByte(string->chars[i]);
    }
}


static void serialize_value(Value value) {
    if (IS_STRING(value)) {
        serialize_string(AS_STRING(value));
    } else if (IS_FUNCTION(value)) {
        serialize_function(AS_FUNCTION(value));
    } else if (value.type == VAL_NUMBER) {
        writeByte(NumType);
        writeDouble(value.as.number);
    } else if (value.type == VAL_BOOL) {
        writeByte(BoolType);
        writeByte((uint8_t)value.as.boolean);
    } else if (value.type == VAL_NIL) {
        writeByte(NilType);
    } else {
        printf("<unsupported_value>\n");
    }
}

static void serialize_chunk(Chunk* chunk) {
    writeInt(chunk->count);

    for (int i = 0; i < chunk->count; i++) {
        writeByte(chunk->code[i]);
    }

    for (int i = 0; i < chunk->count; i++) {
        writeInt(chunk->lines[i]);
    }

    writeInt(chunk->constants.count);
    for (int i = 0; i < chunk->constants.count; i++) {
        serialize_value(chunk->constants.values[i]);
    }
}

static void serialize_function(ObjFunction* func) {
    writeByte(FunctionType);

    if(func->name == NULL)
        writeByte(NilType);
    else
        serialize_string(func->name);
    
    writeInt(func->arity);
    writeInt(func->upvalueCount);

    serialize_chunk(&func->chunk);
}

void serialize(const char* filename, ObjFunction* function) {
    file = fopen(filename, "wb");
    Value val = OBJ_VAL(function);

    serialize_function(AS_FUNCTION(val));
    fclose(file);
}

#include "cJSON.h"

static cJSON* serialize_function_json(ObjFunction* func);

static cJSON* serialize_string_json(ObjString* string) {
    if (!string) return cJSON_CreateNull();

    cJSON* jsonStr = cJSON_CreateObject();
    // Use length to get actual string content
    cJSON_AddStringToObject(jsonStr, "chars", strndup(string->chars, string->length));
    return jsonStr;
}

static cJSON* serialize_value_json(Value value) {
    if (IS_STRING(value)) {
        return serialize_string_json(AS_STRING(value));
    } else if (IS_FUNCTION(value)) {
        return serialize_function_json(AS_FUNCTION(value)); // function handled separately
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
static cJSON* serialize_chunk_json(Chunk* chunk) {
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
        cJSON* valJson = serialize_value_json(chunk->constants.values[i]);
        if (valJson) cJSON_AddItemToArray(constArray, valJson);
    }
    cJSON_AddItemToObject(jsonChunk, "constants", constArray);

    return jsonChunk;
}

// -------------------------
// Serialize ObjFunction
// -------------------------
static cJSON* serialize_function_json(ObjFunction* func) {
    if (!func) return cJSON_CreateNull();

    cJSON* jsonFunc = cJSON_CreateObject();
    cJSON_AddStringToObject(jsonFunc, "type", "ObjFunction");

    if (func->name) {
        cJSON_AddItemToObject(jsonFunc, "name", serialize_string_json(func->name));
    } else {
        cJSON_AddNullToObject(jsonFunc, "name");
    }

    cJSON_AddNumberToObject(jsonFunc, "arity", func->arity);
    cJSON_AddNumberToObject(jsonFunc, "upvalueCount", func->upvalueCount);
    cJSON_AddItemToObject(jsonFunc, "chunk", serialize_chunk_json(&func->chunk));

    return jsonFunc;
}

#include <zlib.h>
void serialize_json(const char* filename, ObjFunction* function) {
    cJSON* root = cJSON_CreateArray();
    Value val = OBJ_VAL(function);

    cJSON* funcJson = serialize_function_json(AS_FUNCTION(val));
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


