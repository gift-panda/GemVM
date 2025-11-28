#include "object.h"
#include "value.h"
#include "vm.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>     // open(), O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND
#include <unistd.h>    // write(), read(), close()
#include <string.h>    // strchr(), strlen()
#include <errno.h>     // errno
#include <stdio.h>     // perror()
#endif

#include <gc.h>

Value writeByteNative(Thread* ctx, int argCount, Value* args);
Value writeNative(Thread* ctx, int argCount, Value* args);
Value openNative(Thread* ctx, int argCount, Value* args);
Value readNative(Thread* ctx, int argCount, Value* args);
Value writeDoubleNative(Thread* ctx, int argCount, Value* args);
