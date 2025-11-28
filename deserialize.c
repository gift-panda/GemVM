#include <gc.h>
#include "object.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "debug.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FunctionType 0
#define StringType   1
#define NilType      2
#define NumType      3
#define BoolType     4

// ---------------------------
// File read helpers
// ---------------------------
static FILE* file;

static uint8_t readByte() {
    uint8_t v;
    fread(&v, sizeof(uint8_t), 1, file);
    return v;
}

static uint16_t readShort() {
    uint16_t v;
    fread(&v, sizeof(uint16_t), 1, file);
    return v;
}

static int readInt() {
    int v;
    fread(&v, sizeof(int), 1, file);
    return v;
}

static double readDouble() {
    double v;
    fread(&v, sizeof(double), 1, file);
    return v;
}

static Value deserialize_value();
static ObjFunction* deserialize_function();

static ObjString* deserialize_string() {
    int length = readInt();
    char* chars = GC_MALLOC(length + 1);

    for (int i = 0; i < length; i++)
        chars[i] = (char)readByte();

    chars[length] = '\0';

    return copyString(chars, length); 
}

// ---------------------------
// Chunk
// ---------------------------
static void deserialize_chunk(Chunk* chunk) {
     initChunk(chunk);
    
    int count = readInt();

    for (int i = 0; i < count; i++)
        writeChunk(chunk, readByte(), 0);

    for (int i = 0; i < count; i++)
        chunk->lines[i] = readInt();

    count = readInt();

    for (int i = 0; i < count; i++)
        writeValueArray(&chunk->constants, deserialize_value());
}

static Value deserialize_value() {
    uint8_t tag = readByte();

    switch (tag) {
        case StringType: {
            ObjString* str = deserialize_string();
            return OBJ_VAL(str);
        }
        case FunctionType: {
            ObjFunction* fn = deserialize_function();
            return OBJ_VAL(fn);
        }
        case NumType:
            return NUMBER_VAL(readDouble());
        case BoolType:
            return BOOL_VAL(readByte());
        case NilType:
            return NIL_VAL;
        default:
            printf("Unknown value tag %d\n", tag);
            return NIL_VAL;
    }
}

static ObjFunction* deserialize_function() {
    ObjFunction* func = newFunction();

    uint8_t next = readByte();
    if (next == NilType) {
        func->name = NULL;
    } else if (next == StringType) {
        func->name = deserialize_string();
    } else {
        printf("Invalid name type: %d\n", next);
        func->name = NULL;
    }

    func->arity = readInt();
    func->upvalueCount = readInt();
    deserialize_chunk(&func->chunk);

    if (vm.showBytecode)
        disassembleChunk(&func->chunk, func->name != NULL ? func->name->chars : "<script>");
        
    return func;
}

ObjFunction* deserialize(const char* filename) {
    file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file for reading");
        return NULL;
    }

    uint32_t magic = 0x474D4F44;
    uint32_t header = readInt();

    if(magic != header){
        printf("Invalid bytecode format.\n");
        return NULL;
    }

    uint8_t type = readByte();
    if (type != FunctionType) {
        printf("Expected FunctionType, got %d\n", type);
        return NULL;
    }
    
    ObjFunction* fn = deserialize_function();
    fclose(file);
    return fn;
}
