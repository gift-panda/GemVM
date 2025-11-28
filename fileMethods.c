#include "fileMethods.h"

Value readNative(Thread* ctx, int argCount, Value* args) {
    ObjDescriptor* d = AS_DESCRIPTOR(args[0]);

    // Disallow binary mode
    for (int i = 0; i < d->mode->length; i++) {
        if (d->mode->chars[i] == 'b') {
            runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                            "read() cannot be used on binary files. Use readByte().");
            return NIL_VAL;
        }
    }

    off_t originalPos = lseek(d->fd, 0, SEEK_CUR);
    if (originalPos < 0) {
        perror("lseek");
        return NIL_VAL;
    }
    off_t size = lseek(d->fd, 0, SEEK_END);
    if (size < 0) {
        perror("lseek");
        return NIL_VAL;
    }
    if (lseek(d->fd, 0, SEEK_SET) < 0) {
        perror("lseek");
        return NIL_VAL;
    }
    char* buffer = (char*)GC_MALLOC(size + 1);
    if (!buffer) {
        runtimeErrorCtx(ctx, vm.accessErrorClass, "Not enough memory to read file.");
        return NIL_VAL;
    }
    ssize_t bytesRead = read(d->fd, buffer, size);

    buffer[bytesRead] = '\0';
    lseek(d->fd, originalPos, SEEK_SET);
    return OBJ_VAL(newString(buffer, (int)bytesRead));
}

Value openNative(Thread* ctx, int argCount, Value* args) {
    // Validate args:
    if (!IS_STRING(args[0])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "open() first argument must be a string filename.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "open() second argument must be a mode string like 'r', 'w', 'a', 'r+'");
        return NIL_VAL;
    }

    ObjString* name = AS_STRING(args[0]);
    ObjString* mode = AS_STRING(args[1]);

    const char* m = mode->chars;

    // Validate Python-style mode
    // Allowed starting chars: r w a
    if (m[0] != 'r' && m[0] != 'w' && m[0] != 'a') {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "open(): invalid mode '%s'. Use r, w, a, r+, w+, a+.", m);
        return NIL_VAL;
    }

    // Validate that any additional chars must be: + or b
    for (int i = 1; i < mode->length; i++) {
        if (m[i] != '+' && m[i] != 'b') {
            runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass, "open(): invalid mode '%s'. Only + or b allowed after initial char.", m);
            return NIL_VAL;
        }
    }

    // Interpret mode -> POSIX flags
    int flags = 0;
    bool plus = strchr(m, '+') != NULL;
    bool append = (m[0] == 'a');
    bool writeOnly = (m[0] == 'w');

    if (m[0] == 'r' && !plus)        flags = O_RDONLY;
    else if (m[0] == 'r' && plus)   flags = O_RDWR;
    else if (m[0] == 'w' && !plus)  flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (m[0] == 'w' && plus)   flags = O_RDWR   | O_CREAT | O_TRUNC;
    else if (append && !plus)       flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (append && plus)        flags = O_RDWR   | O_CREAT | O_APPEND;

    // Create descriptor object
    ObjDescriptor* d = ALLOCATE_OBJ(ObjDescriptor, OBJ_DESCRIPTOR);
    d->name = name;
    d->mode = mode;

    d->fd = open(name->chars, flags, 0644);

    if (d->fd < 0) {
        return BOOL_VAL(false);
    }

    return OBJ_VAL(d);

}

Value writeNative(Thread* ctx, int argCount, Value* args) {
    ObjDescriptor* d = AS_DESCRIPTOR(args[0]);
    Value v = args[1];

    bool isBinary = false;
    for (int i = 0; i < d->mode->length; i++) {
        if (d->mode->chars[i] == 'b') {
            isBinary = true;
            break;
        }
    }


    const char* ptr = NULL;
    char buffer[256];
    size_t len = 0;

    if (!isBinary) {
        // -----------------------------
        // TEXT MODE
        // -----------------------------
        if (IS_STRING(v)) {
            ObjString* s = AS_STRING(v);
            ptr = s->chars;
            len = s->length;
        }
        else if (IS_NUMBER(v)) {
            len = snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(v));
            ptr = buffer;
        }
        else if (IS_BOOL(v)) {
            ptr = AS_BOOL(v) ? "1" : "0";
            len = 1;
        }
        else {
            runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                "write() expects string, number, or bool");
            return NUMBER_VAL(-1);
        }
    }
    else {
        // -----------------------------
        // BINARY MODE
        // -----------------------------
        if (IS_STRING(v)) {
            ObjString* s = AS_STRING(v);
            ptr = s->chars;
            len = s->length;
        }
        else if (IS_NUMBER(v)) {
            double num = AS_NUMBER(v);
            memcpy(buffer, &num, sizeof(double));
            ptr = buffer;
            len = sizeof(double);
        }
        else if (IS_BOOL(v)) {
            buffer[0] = AS_BOOL(v) ? 1 : 0;
            ptr = buffer;
            len = 1;
        }
        else {
            runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                "write() expects string, number, or bool");
            return NUMBER_VAL(-1);
        }
    }

    ssize_t written = write(d->fd, ptr, len);

    if (written < 0) {
        perror("write");
        return NUMBER_VAL(-1);
    }

    return BOOL_VAL(written >= 0? true : false);
}

Value writeByteNative(Thread* ctx, int argCount, Value* args) {
    ObjDescriptor* d = AS_DESCRIPTOR(args[0]);
    Value v = args[1];

    unsigned char byte;

    if (IS_BOOL(v)) {
        byte = AS_BOOL(v) ? 1 : 0;
    }
    else if (IS_NUMBER(v)) {
        double num = AS_NUMBER(v);

        // Convert double → byte the same way C does for uint8_t
        byte = (unsigned char)num;
    }
    else {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                        "writeByte() expects number or bool");
        return NUMBER_VAL(-1);
    }

    ssize_t written = write(d->fd, &byte, 1);

    if (written < 0) {
        perror("write");
        return NUMBER_VAL(-1);
    }

    return NUMBER_VAL(1);  // exactly 1 byte written
}

Value writeDoubleNative(Thread* ctx, int argCount, Value* args) {
    ObjDescriptor* d = AS_DESCRIPTOR(args[0]);
    Value v = args[1];

    if (!IS_NUMBER(v)) {
        runtimeErrorCtx(ctx, vm.illegalArgumentsErrorClass,
                        "writeDouble() expects a number.");
        return NUMBER_VAL(-1);
    }

    double num = AS_NUMBER(v);

    // Write raw IEEE754 bytes exactly like fwrite(&num, 8, 1)
    ssize_t written = write(d->fd, &num, sizeof(double));

    if (written < 0) {
        perror("write");
        return NUMBER_VAL(-1);
    }

    return NUMBER_VAL(written);  // should return 8
}


