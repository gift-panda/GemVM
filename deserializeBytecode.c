// deserialize.c
#include "vm.h"
#include "table.h"
#include "memory.h"
#include "object.h"
#include <string.h>

ObjFunction* deserialize_function(ObjInstance* func);
static Value deserialize_value(Value v);
static void deserialize_chunk(Chunk* chunk, ObjInstance* cobj);

/* Look up globals["function"] and deserialize it.
 * Returns NULL if the lookup or type is missing (no runtime errors).
 */
ObjFunction* getCompiledBytecode() {
    Value funVal;
    if (!tableGet(&vm.globals, copyString("function", 8), &funVal)) {
        return NULL;
    }

    if (!IS_INSTANCE(funVal)) {
        return NULL;
    }

    return deserialize_function(AS_INSTANCE(funVal));
}

/* ---------- deserialize_value ----------
 * Return the Value usable by the VM.
 * Primitives are returned as-is. Instances named "Function" or "Chunk"
 * are deserialized; other kinds return NIL_VAL (silently).
 */
static Value deserialize_value(Value v) {
    if (IS_NUMBER(v) || IS_BOOL(v) || IS_NIL(v) || IS_STRING(v)) {
        return v;
    }

    if (IS_INSTANCE(v)) {
        ObjInstance* inst = AS_INSTANCE(v);
        ObjString* cname = inst->klass->name;

        if (cname && cname->chars) {
            if (strcmp(cname->chars, "Function") == 0) {
                ObjFunction* f = deserialize_function(inst);
                if (f) return OBJ_VAL(f);
                return NIL_VAL;
            }

            if (strcmp(cname->chars, "Chunk") == 0) {
                Chunk* ch = ALLOCATE(Chunk, 1);
                initChunk(ch);
                deserialize_chunk(ch, inst);
                return OBJ_VAL((Obj*)ch);
            }
        }
    }

    return NIL_VAL;
}

/* ---------- deserialize_chunk ----------
 * Read fields if present; if any field is missing, we return early silently.
 * Uses writeChunk to append bytes so chunk internals are allocated properly.
 */
static void deserialize_chunk(Chunk* chunk, ObjInstance* cobj) {
    Value tmp;

    if (!tableGet(&cobj->fields, copyString("count", 5), &tmp)) return;
    int count = (int)AS_NUMBER(tmp);

    if (!tableGet(&cobj->fields, copyString("code", 4), &tmp)) return;
    ObjList* codeList = AS_LIST(tmp);

    /* Append code bytes */
    for (int i = 0; i < count; i++) {
        Value byteVal = codeList->elements.values[i];
        uint8_t b = (uint8_t)AS_NUMBER(byteVal);
        writeChunk(chunk, b, 0);
    }

    if (!tableGet(&cobj->fields, copyString("lines", 5), &tmp)) return;
    ObjList* linesList = AS_LIST(tmp);

    for (int i = 0; i < count; i++) {
        Value lineVal = linesList->elements.values[i];
        int line = (int)AS_NUMBER(lineVal);
        chunk->lines[i] = line;
    }

    if (!tableGet(&cobj->fields, copyString("constants", 9), &tmp)) return;
    ObjList* constList = AS_LIST(tmp);

    for (int i = 0; i < constList->elements.count; i++) {
        Value cv = constList->elements.values[i];
        Value des = deserialize_value(cv);
        writeValueArray(&chunk->constants, des);
    }
}

/* ---------- deserialize_function ----------
 * Read expected fields if present. If a field is missing we use a safe default
 * or return NULL for a missing essential piece (like not an instance).
 */
ObjFunction* deserialize_function(ObjInstance* func) {
    if (!func) return NULL;

    ObjFunction* f = newFunction();

    Value tmp;

    /* name */
    if (tableGet(&func->fields, copyString("name", 4), &tmp)) {
        if (IS_NIL(tmp)) {
            f->name = NULL;
        } else if (IS_STRING(tmp)) {
            f->name = AS_STRING(tmp);
        } else {
            f->name = NULL;
        }
    } else {
        f->name = NULL;
    }

    /* arity */
    if (tableGet(&func->fields, copyString("arity", 5), &tmp)) {
        f->arity = (uint8_t)AS_NUMBER(tmp);
    } else {
        f->arity = 0;
    }

    /* upvalueCount */
    if (tableGet(&func->fields, copyString("upvalueCount", 12), &tmp)) {
        f->upvalueCount = (uint8_t)AS_NUMBER(tmp);
    } else {
        f->upvalueCount = 0;
    }

    /* chunk */
    if (tableGet(&func->fields, copyString("chunk", 5), &tmp) && IS_INSTANCE(tmp)) {
        ObjInstance* chunkObj = AS_INSTANCE(tmp);
        initChunk(&f->chunk);
        deserialize_chunk(&f->chunk, chunkObj);
    } else {
        /* leave f->chunk initialized but empty */
        initChunk(&f->chunk);
    }

    return f;
}

