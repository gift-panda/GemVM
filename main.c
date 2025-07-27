#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "GemError.h"
#include "GemMath.h"
#include "vm.h"

static void repl() {
    char line[1024];
    for (;;) {
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        interpret(line);
    }
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

char* getErrorText() {
    char* buf = malloc(Error_gem_len + 1);
    if (!buf) return NULL;

    memcpy(buf, Error_gem, Error_gem_len);
    buf[Error_gem_len] = '\0';
    return buf;
}

char* getMathText() {
    char* buf = malloc(Math_gem_len + 1);
    if (!buf) return NULL;

    memcpy(buf, Math_gem, Math_gem_len);
    buf[Math_gem_len] = '\0';
    return buf;
}

int main(int argc, const char* argv[]) {
    int runRepl = 1;
    const char* scriptPath = NULL;

    // Runtime flags
    int showBytecode = 0;
    int enableGC = 1;
    int run = 1;

    initVM();

    // Parse flags
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            printf("Usage: clox [options] [script]\n");
            printf("Options:\n");
            printf("  -h, --help       Show this help message.\n");
            printf("  -v, --version    Show version info.\n");
            printf("  -s, --show           Show the bytecode generated.\n");
            printf("  -r, --raw            Turns off the garbage collector.\n");
            printf("  -c, --compile        Does not run the code, only checks for valid compilation.\n");
            return 0;
        } else if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            printf("gem version 1.6.7\n");
            return 0;
        } else if (strcmp(arg, "--show") == 0 || strcmp(arg, "-s") == 0) {
            showBytecode = 1;
        } else if (strcmp(arg, "--raw") == 0 || strcmp(arg, "-r") == 0) {
            enableGC = 0;
        } else if (strcmp(arg, "--compile") == 0 || strcmp(arg, "-c") == 0) {
            run = 0;
        } else if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            return 64;
        } else {
            scriptPath = arg;
            runRepl = 0;
        }
    }

    interpret(getErrorText());
    interpret(getMathText());

    if (enableGC) {
        vm.gcEnabled = true;
    }

    if (runRepl) {
        repl();
    } else {
        char* source = readFile(scriptPath);
        if (!run) {
            vm.noRun = true;
        }
        if (showBytecode) {
           vm.showBytecode = true; // Set a VM flag, then respect it in your compiler
        }
        runFile(scriptPath);
        free(source);
    }

    return 0;
}

