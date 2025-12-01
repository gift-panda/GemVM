#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "GemError.h"
#include "GemIterator.h"
#include "GemFile.h"
#include "vm.h"

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <setjmp.h>
#include <pthread.h>
#include <gc.h>
#define TB_IMPL
#include "termbox2.h"
#include "serialize.h"
#include "deserialize.h"

#define MAX_LINES 1000
#define MAX_COL_LEN 512

/*
#include "GemWindow.h"
char* getWindowText() {
    char* buf = malloc(Window_gem_len + 1);
    if (!buf) return NULL;

    memcpy(buf, Window_gem, Window_gem_len);
    buf[Window_gem_len] = '\0';
    return buf;
}

#include "GemMath.h"
char* getMathText() {
    char* buf = malloc(Math_gem_len + 1);
    if (!buf) return NULL;

    memcpy(buf, Math_gem, Math_gem_len);
    buf[Math_gem_len] = '\0';
    return buf;
}*/

const char *tmp_filename = "tmp_code.txt";

typedef enum { LINE_INPUT, LINE_OUTPUT } LineType;

char history[MAX_LINES][MAX_COL_LEN]; // all lines: input and output
LineType line_types[MAX_LINES];
int history_count = 0;

int cur_line = 0;
int cur_col = 0;

int saved_stdout_fd = -1;
int saved_stderr_fd = -1;

int current_nesting = 0;
bool new_prompt_line[1024] = {true};

jmp_buf repl_env;

void cleanup() {
    remove(tmp_filename);
}

void handle_sigint(int sig) {
    if (saved_stdout_fd != -1) {
        dup2(saved_stdout_fd, STDOUT_FILENO);
        close(saved_stdout_fd);
        saved_stdout_fd = -1;
    }
    if (saved_stderr_fd != -1) {
        dup2(saved_stderr_fd, STDERR_FILENO);
        close(saved_stderr_fd);
        saved_stderr_fd = -1;
    }

    cleanup();
    tb_shutdown();
    exit(0);
}

int get_nesting_level_line(const char *line) {
    int balance = 0;
    for (const char *p = line; *p; p++) {
        if (*p == '{') balance++;
        else if (*p == '}') balance--;
    }
    return balance;
}


// Calculate block nesting for a given code buffer
int get_total_nesting_up_to(int line_index) {
    int nesting = 0;
    for (int i = 0; i <= line_index; i++) {
        if (line_types[i] == LINE_INPUT) {
            nesting += get_nesting_level_line(history[i]);
            if (nesting < 0) nesting = 0; // avoid negative nesting
        }
    }
    return nesting;
}


int is_block_complete(const char *code) {
    return get_total_nesting_up_to(cur_line) == 0 && strlen(code) > 0;
}

// Append input line with type LINE_INPUT
void append_input_line(const char *line) {
    if (history_count == MAX_LINES) {
        memmove(history, history + 1, sizeof(history[0]) * (MAX_LINES - 1));
        memmove(line_types, line_types + 1, sizeof(LineType) * (MAX_LINES - 1));
        history_count--;
    }
    strncpy(history[history_count], line, MAX_COL_LEN - 1);
    history[history_count][MAX_COL_LEN - 1] = 0;
    line_types[history_count] = LINE_INPUT;
    history_count++;
}

// Append output line with type LINE_OUTPUT
void append_output_line(const char *line) {
    if (history_count == MAX_LINES) {
        memmove(history, history + 1, sizeof(history[0]) * (MAX_LINES - 1));
        memmove(line_types, line_types + 1, sizeof(LineType) * (MAX_LINES - 1));
        history_count--;
    }
    strncpy(history[history_count], line, MAX_COL_LEN - 1);
    history[history_count][MAX_COL_LEN - 1] = 0;
    line_types[history_count] = LINE_OUTPUT;
    history_count++;
}

void run_backend(const char *code) {
    int saved_stdout_fd = dup(STDOUT_FILENO);
    int saved_stderr_fd = dup(STDERR_FILENO);

    if (saved_stdout_fd == -1 || saved_stderr_fd == -1) {
        append_output_line("[ERROR: failed to save stdout/stderr]");
        return ;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        append_output_line("[ERROR: pipe failed]");
        close(saved_stdout_fd);
        close(saved_stderr_fd);
        return ;
    }

    if (dup2(pipefd[1], STDOUT_FILENO) == -1 ||
        dup2(pipefd[1], STDERR_FILENO) == -1) {
        append_output_line("[ERROR: dup2 failed]");
        close(pipefd[0]);
        close(pipefd[1]);
        close(saved_stdout_fd);
        close(saved_stderr_fd);
        return ;
        }

    interpret("");

    if (setjmp(repl_env)){
    }
    else
        interpret(code);


    fflush(stdout);
    fflush(stderr);

    dup2(saved_stdout_fd, STDOUT_FILENO);
    dup2(saved_stderr_fd, STDERR_FILENO);
    close(saved_stdout_fd);
    close(saved_stderr_fd);
    close(pipefd[1]); // Done writing

    char buffer[1024];
    char line[512];
    int n;
    int line_pos = 0;
    int line_count = 0;

    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buffer[i] == '\n' || line_pos >= (int)(sizeof(line) - 2)) {
                line[line_pos] = 0;
                append_output_line(line);
                line_count++;
                line_pos = 0;
            } else {
                line[line_pos++] = buffer[i];
            }
        }
    }

    if (line_pos > 0) {
        line[line_pos] = 0;
        append_output_line(line);
        line_count++;
    }

    close(pipefd[0]);

    new_prompt_line[cur_line + line_count + 1] = true;
}

// Return prompt based on whether line is input, and if continuation line or not
const char *get_prompt_for_line(int line_index) {
    if (line_types[line_index] != LINE_INPUT) {
        return "   ";
    }

    return new_prompt_line[line_index] ? ">>> " : "...";
}

// Calculate nesting level for a specific line's code content only
int line_nesting_level(int line_index) {
    // Count number of '{' and '}' in line
    int balance = 0;
    const char *line = history[line_index];
    for (; *line; line++) {
        if (*line == '{') balance++;
        else if (*line == '}') balance--;
    }
    return balance;
}
int cumulative_nesting_up_to(int line_index) {
    int balance = 0;
    for (int i = 0; i < line_index; i++) {
        if (line_types[i] == LINE_INPUT) {
            balance += line_nesting_level(i);
            if (balance < 0) balance = 0; // avoid negative nesting
        }
    }
    return balance;
}

void draw() {
    tb_clear();
    int w = tb_width();
    int h = tb_height();

    int start = history_count > h ? history_count - h : 0;

    for (int i = start; i < history_count; i++) {
        const char *prompt = get_prompt_for_line(i);
        int y = i - start;

        uint16_t prompt_fg = TB_GREEN;
        uint16_t prompt_bg = TB_DEFAULT;
        tb_print(0, y, prompt_fg, prompt_bg, prompt);

        // For input continuation lines, indent with tabs according to nesting
        if (line_types[i] == LINE_INPUT) {
            int nesting = 0;
            if (strcmp(prompt, "...") == 0) {
                nesting = cumulative_nesting_up_to(i);
            }
            const char *line_text = history[i];
            int prompt_len = strlen(prompt);
            tb_print(prompt_len + nesting, y, TB_WHITE, TB_DEFAULT, line_text);
        } else {
            const char *line_text = history[i];
            tb_print(2, y, TB_WHITE, TB_DEFAULT, line_text);
        }
    }

    const char *prompt = get_prompt_for_line(cur_line);
    int nesting = 0;
    if (strcmp(prompt, "...") == 0) {
        nesting = cumulative_nesting_up_to(cur_line);
    }
    int cursor_x = (int)strlen(prompt) + nesting + cur_col;
    int cursor_y = history_count > h ? h : cur_line - start;
    tb_set_cursor(cursor_x, cursor_y);

    tb_present();
}

int repl() {
    signal(SIGINT, handle_sigint);

    if (tb_init() < 0) {
        fprintf(stderr, "Failed to initialize termbox\n");
        return 1;
    }
    tb_set_input_mode(TB_INPUT_ESC);

    append_input_line("");
    cur_line = 0;
    cur_col = 0;

    char code_buffer[8192] = {0};
    int code_len = 0;

    while (1) {
        draw();

        struct tb_event ev;
        tb_poll_event(&ev);

        if (ev.type == TB_EVENT_KEY) {
            if (ev.key == TB_KEY_CTRL_C) {
                break;
            }
            else if (ev.key == TB_KEY_ENTER) {
                // Append current input line to code_buffer
                int line_len = strlen(history[cur_line]);
                if (code_len + line_len + 2 >= (int)sizeof(code_buffer)) {
                    append_output_line("[ERROR: code buffer overflow]");
                    code_len = 0;
                    code_buffer[0] = 0;
                } else {
                    memcpy(code_buffer + code_len, history[cur_line], line_len);
                    code_len += line_len;
                    code_buffer[code_len++] = '\n';
                    code_buffer[code_len] = 0;
                }

                if (is_block_complete(code_buffer)) {
                    run_backend(code_buffer);
                    code_len = 0;
                    code_buffer[0] = 0;

                    append_input_line("");
                    cur_line = history_count - 1;
                    cur_col = 0;
                } else {
                    new_prompt_line[cur_line + 1] = false;
                    current_nesting = get_total_nesting_up_to(cur_line);
                    char indent_line[MAX_COL_LEN] = {0};
                    int tab_count = current_nesting;
                    int pos = 0;
                    for (int i = 0; i < tab_count && pos < MAX_COL_LEN - 1; i++) {
                        for (int j =0; j < 4; j++)
                            indent_line[pos++] = ' ';
                    }
                    indent_line[pos] = 0;
                    append_input_line(indent_line);
                    cur_line = history_count - 1;
                    cur_col = pos;
                }
            }
            else if ((ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2)) {
                char *line = history[cur_line];
                const char *prompt = get_prompt_for_line(cur_line);

                if (cur_col > 0) {
                    int delete_start = cur_col - 1;

                    // Check for multiple consecutive spaces before cursor
                    while (delete_start > cur_col - 4 && line[delete_start - 1] == ' ' && line[delete_start] == ' ') {
                        delete_start--;
                    }

                    int delete_count = cur_col - delete_start;

                    memmove(&line[delete_start], &line[cur_col], strlen(line) - cur_col + 1);
                    cur_col = delete_start;
                } else {
                    if (strcmp(prompt, "...") == 0 && cur_line > 0) {
                        int prev_line = cur_line - 1;
                        while (prev_line >= 0 && line_types[prev_line] != LINE_INPUT) prev_line--;
                        if (prev_line >= 0) {
                            int prev_len = (int)strlen(history[prev_line]);
                            int cur_len = (int)strlen(history[cur_line]);
                            char *cur_content = history[cur_line];
                            while (*cur_content == '\t') cur_content++;

                            if (prev_len + (int)strlen(cur_content) >= MAX_COL_LEN - 1) {
                                append_output_line("[ERROR: line too long on merge]");
                            } else {
                                strcat(history[prev_line], cur_content);
                                cur_line = prev_line;
                                cur_col = prev_len;
                                // Remove current line from history
                                for (int i = cur_line + 1; i < history_count - 1; i++) {
                                    strcpy(history[i], history[i+1]);
                                    line_types[i] = line_types[i+1];
                                }
                                history_count--;
                            }
                        }
                    }
                }
            }
            else if (ev.key == TB_KEY_ARROW_LEFT) {
                if (cur_col > 0) cur_col--;
            }
            else if (ev.key == TB_KEY_ARROW_RIGHT) {
                if (cur_col < (int)strlen(history[cur_line])) cur_col++;
            }
            else if (ev.key == TB_KEY_ARROW_UP) {
                // Move to previous input line
                int i = cur_line - 1;
                while (i >= 0 && line_types[i] != LINE_INPUT) i--;
                if (i >= 0) {
                    cur_line = i;
                    cur_col = (int)strlen(history[cur_line]);
                }
            }
            else if (ev.key == TB_KEY_ARROW_DOWN) {
                int i = cur_line + 1;
                while (i < history_count && line_types[i] != LINE_INPUT) i++;
                if (i < history_count) {
                    cur_line = i;
                    cur_col = (int)strlen(history[cur_line]);
                }
            }
            else if (ev.key == TB_KEY_TAB) {
                char *line = history[cur_line];
                int len = (int)strlen(line);

                const int tab_spaces = 4;
                if (len + tab_spaces < MAX_COL_LEN - 1) {
                    memmove(&line[cur_col + tab_spaces], &line[cur_col], len - cur_col + 1);

                    for (int i = 0; i < tab_spaces; i++) {
                        line[cur_col + i] = ' ';
                    }

                    cur_col += tab_spaces;
                }
            }
            else if (ev.ch >= 32 && ev.ch < 127) {
                char *line = history[cur_line];
                int len = (int)strlen(line);
                if (len < MAX_COL_LEN - 1) {
                    memmove(&line[cur_col + 1], &line[cur_col], len - cur_col + 1);
                    line[cur_col] = (char)ev.ch;
                    cur_col++;
                }
            }
        }
    }

    tb_shutdown();
    cleanup();
    return 0;
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool has_extension(const char *filename, const char *ext) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return false;  // no dot or dot is first char
    return strcmp(dot + 1, ext) == 0;
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

static void runFileBootStrapped(const char* path) {
    char* source = readFile(path);
    InterpretResult result = interpretBootStrapped(source);
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
//03371003678
char* getIteratorText() {
    char* buf = malloc(Iterator_gem_len + 1);
    if (!buf) return NULL;

    memcpy(buf, Iterator_gem, Iterator_gem_len);
    buf[Iterator_gem_len] = '\0';
    return buf;
}

char* getFileText() {
    char* buf = malloc(File_gem_len + 1);
    if (!buf) return NULL;

    memcpy(buf, File_gem, File_gem_len);
    buf[File_gem_len] = '\0';
    return buf;
}

void no_warn_proc(const char* msg, GC_word arg) {
    // Do nothing
}

#include "deserializeMemory.h"

// This macro embeds a binary file into the executable.
// Usage: INCBIN(symbolName, "filename.bin");

#ifndef INCBIN_H
#define INCBIN_H

#define INCBIN(NAME, FILENAME)                     \
    __asm__ (                                      \
        ".pushsection .rodata\n"                   \
        ".global " #NAME "_start\n"                \
        #NAME "_start:\n"                          \
        ".incbin \"" FILENAME "\"\n"               \
        ".global " #NAME "_end\n"                  \
        #NAME "_end:\n"                            \
        ".popsection\n"                            \
    );                                              \
    extern const unsigned char NAME##_start[];      \
    extern const unsigned char NAME##_end[];


#endif


INCBIN(FileCompiler, "/home/meow/boot/compiler.gemc")
INCBIN(SourceCompiler, "/home/meow/boot/compilerFromSource.gemc");

ObjFunction* loadFileCompiler() {
    size_t size = (size_t)(FileCompiler_end - FileCompiler_start);
    return deserialize_from_memory(FileCompiler_start, size);
}

ObjFunction* loadSourceCompiler() {
    size_t size = (size_t)(SourceCompiler_end - SourceCompiler_start);
    return deserialize_from_memory(SourceCompiler_start, size);
}

int main(int argc, const char* argv[]) {
    GC_set_warn_proc(no_warn_proc);
    GC_INIT();
    GC_allow_register_threads();
    int runRepl = 1;
    const char* scriptPath = NULL;

    // Runtime flags
    int showBytecode = 0;
    int enableGC = 1;
    int run = 1;

    initVM();

    // Parse flags
    int i = 1;
    for (; i < argc; i++) {
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
        }
        else if (strcmp(arg, "--zip") == 0 || strcmp(arg, "-z") == 0) {
            vm.zip = true;
        } else if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            return 64;
        } else {
            scriptPath = arg;
            runRepl = 0;
            break;
        }
    }

    ObjList* gem_argv = newList();
    for(; i < argc; i++){
        writeValueArray(&gem_argv->elements, OBJ_VAL(copyString(argv[i], strlen(argv[i]))));
    }
    tableSet(&vm.globals, copyString("argv", 4), OBJ_VAL(gem_argv));


    interpret(getErrorText());
    interpret(getIteratorText());
    interpret(getFileText());
    //interpret(getWindowText());
    //interpret(getMathText());

    vm.fileCompiler = loadFileCompiler();
    vm.sourceCompiler = loadSourceCompiler();
    

    if (runRepl) {
        vm.repl = 1;
        repl();
    } else {
        if (!has_extension(scriptPath, "gem") && !has_extension(scriptPath, "gemc")){
            printf("Source must be either .gem file or precompiled.");
            return 1;
        }

        if (has_extension(scriptPath, "gemc") && !run){
            printf("File already compiled.");
            return 1;
        }

        if (!enableGC) {
            
            runFileBootStrapped(scriptPath);
            return 0;
        }
        
        if (!run) {
            vm.noRun = true;
            vm.path = scriptPath;
            runFile(scriptPath);
            return 0;
        }
        if (showBytecode) {
           vm.showBytecode = true; // Set a VM flag, then respect it in your compiler
        }
        if(has_extension(scriptPath, "gemc")){
            load(scriptPath);
            return 0;
        }
        callFunction(vm.fileCompiler);
        return 0;
    }

    return 0;
}
