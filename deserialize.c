#include "cJSON.h"
#include "object.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static ObjString* deserialize_string(cJSON* jsonStr);
static Value deserialize_value(cJSON* jsonVal);
static void deserialize_chunk(cJSON* jsonChunk, Chunk* chunk);
static ObjFunction* deserialize_function(cJSON* jsonFunc);

// -------------------------
// Deserialize ObjString
// -------------------------
static ObjString* deserialize_string(cJSON* jsonStr) {
    if (!jsonStr || cJSON_IsNull(jsonStr)) return NULL;

    cJSON* charsItem = cJSON_GetObjectItem(jsonStr, "chars");
    if (!cJSON_IsString(charsItem)) return NULL;
    return copyString(charsItem->valuestring, strlen(charsItem->valuestring));
}

// -------------------------
// Deserialize Value
// -------------------------
static Value deserialize_value(cJSON* jsonVal) {
    if (!jsonVal || cJSON_IsNull(jsonVal)) return NIL_VAL;

    if (cJSON_IsObject(jsonVal)) {
        cJSON* typeItem = cJSON_GetObjectItem(jsonVal, "type");
        if (typeItem && cJSON_IsString(typeItem)) {
            if (strcmp(typeItem->valuestring, "ObjFunction") == 0) {
                return OBJ_VAL(deserialize_function(jsonVal));
            }
        }

        // Otherwise treat as string if it has "chars"
        if (cJSON_GetObjectItem(jsonVal, "chars")) {
            return OBJ_VAL(deserialize_string(jsonVal));
        }

        // fallback
        return NIL_VAL;
    } else if (cJSON_IsNumber(jsonVal)) {
        return NUMBER_VAL(jsonVal->valuedouble);
    } else if (cJSON_IsBool(jsonVal)) {
        return BOOL_VAL(cJSON_IsTrue(jsonVal));
    } else {
        return NIL_VAL;
    }
}

// -------------------------
// Deserialize Chunk
// -------------------------
static void deserialize_chunk(cJSON* jsonChunk, Chunk* chunk) {
    if (!jsonChunk || !chunk) return;

    cJSON* codeItem = cJSON_GetObjectItem(jsonChunk, "code");
    cJSON* linesItem = cJSON_GetObjectItem(jsonChunk, "lines");
    cJSON* constantsItem = cJSON_GetObjectItem(jsonChunk, "constants");

    initChunk(chunk);

    if (cJSON_IsArray(codeItem)) {
        int count = cJSON_GetArraySize(codeItem);
        for (int i = 0; i < count; i++) {
            cJSON* codeVal = cJSON_GetArrayItem(codeItem, i);
            writeChunk(chunk, (uint8_t)codeVal->valuedouble, 0);
        }
    }

    if (cJSON_IsArray(linesItem)) {
        int count = cJSON_GetArraySize(linesItem);
        for (int i = 0; i < count; i++) {
            cJSON* lineVal = cJSON_GetArrayItem(linesItem, i);
            chunk->lines[i] = (int)lineVal->valuedouble;
        }
    }

    if (cJSON_IsArray(constantsItem)) {
        int count = cJSON_GetArraySize(constantsItem);
        for (int i = 0; i < count; i++) {
            Value val = deserialize_value(cJSON_GetArrayItem(constantsItem, i));
            writeValueArray(&chunk->constants, val);
        }
    }
}

// -------------------------
// Deserialize ObjFunction
// -------------------------
static ObjFunction* deserialize_function(cJSON* jsonFunc) {
    if (!jsonFunc) return NULL;

    ObjFunction* func = newFunction();

    cJSON* nameItem = cJSON_GetObjectItem(jsonFunc, "name");
    if (nameItem && !cJSON_IsNull(nameItem)) func->name = deserialize_string(nameItem);

    cJSON* arityItem = cJSON_GetObjectItem(jsonFunc, "arity");
    if (arityItem && cJSON_IsNumber(arityItem)) func->arity = arityItem->valuedouble;

    cJSON* upvalueCountItem = cJSON_GetObjectItem(jsonFunc, "upvalueCount");
    if (upvalueCountItem && cJSON_IsNumber(upvalueCountItem))
        func->upvalueCount = upvalueCountItem->valuedouble;

    cJSON* chunkItem = cJSON_GetObjectItem(jsonFunc, "chunk");
    if (chunkItem) deserialize_chunk(chunkItem, &func->chunk);

    return func;
}

// -------------------------
// Deserialize from file
// -------------------------

#include <zlib.h>

ObjFunction* deserialize(const char* filename) {
    gzFile f = gzopen(filename, "rb");
    if (!f) return NULL;

    fseek(stdin, 0, SEEK_END);
    size_t bufsize = 65536; 
    char* data = malloc(bufsize);
    if (!data) {
        gzclose(f);
        return NULL;
    }

    size_t total = 0;
    int bytes;
    while ((bytes = gzread(f, data + total, bufsize - total)) > 0) {
        total += bytes;
        if (total == bufsize) {
            bufsize *= 2;
            data = realloc(data, bufsize);
        }
    }
    data[total] = '\0';
    gzclose(f);

    cJSON* root = cJSON_Parse(data);
    free(data);

    if (!root || !cJSON_IsArray(root) || cJSON_GetArraySize(root) == 0) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON* funcJson = cJSON_GetArrayItem(root, 0);
    ObjFunction* func = deserialize_function(funcJson);

    cJSON_Delete(root);
    return func;
}

