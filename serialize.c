#include "object.h"
#include "value.h"
#include "chunk.h"
#include "table.h"
#include "vm.h"        // <- for theVM
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "value.h"


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

    uint32_t magic = 0x474D4F44;
    writeInt(magic);
    
    Value val = OBJ_VAL(function);

    serialize_function(AS_FUNCTION(val));
    fclose(file);
}
