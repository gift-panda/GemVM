#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "GemError.h"
#include "GemMath.h"
#include "vm.h"

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define TB_IMPL
#include "termbox2.h"

#define MAX_LINES 1000
#define MAX_COL_LEN 512

const char *tmp_filename = "tmp_code.txt";

typedef enum { LINE_INPUT, LINE_OUTPUT } LineType;

char history[MAX_LINES][MAX_COL_LEN]; // all lines: input and output
LineType line_types[MAX_LINES];
int history_count = 0;

int cur_line = 0;
int cur_col = 0;

int current_nesting = 0;


void cleanup() {
    remove(tmp_filename);
}

void handle_sigint(int sig) {
    cleanup();
    tb_shutdown();
    exit(0);
}

// Calculate block nesting for a given code buffer
int get_nesting_level(const char *code) {
    int balance = 0;
    for (const char *p = code; *p; p++) {
        if (*p == '{') balance++;
        else if (*p == '}') balance--;
    }
    return balance >= 0 ? balance : 0; // never negative nesting
}

// Check if block is complete (nesting == 0 and non-empty)
int is_block_complete(const char *code) {
    return get_nesting_level(code) == 0 && strlen(code) > 0;
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

// run_backend captures output from interpret
void run_backend(const char *code) {
    int stdout_fd = dup(STDOUT_FILENO);
    if (stdout_fd == -1) {
        append_output_line("[ERROR: dup stdout failed]");
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        append_output_line("[ERROR: pipe failed]");
        close(stdout_fd);
        return;
    }

    if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
        append_output_line("[ERROR: dup2 failed]");
        close(pipefd[0]);
        close(pipefd[1]);
        close(stdout_fd);
        return;
    }
    close(pipefd[1]);

    interpret(code);

    fflush(stdout);
    dup2(stdout_fd, STDOUT_FILENO);
    close(stdout_fd);

    char buffer[1024];
    int n;
    char line[512];
    int line_pos = 0;

    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buffer[i] == '\n' || line_pos >= (int)(sizeof(line) - 2)) {
                line[line_pos] = 0;
                append_output_line(line);
                line_pos = 0;
            } else {
                line[line_pos++] = buffer[i];
            }
        }
    }
    if (line_pos > 0) {
        line[line_pos] = 0;
        append_output_line(line);
    }
    close(pipefd[0]);
}

// Return prompt based on whether line is input, and if continuation line or not
const char *get_prompt_for_line(int line_index) {
    if (line_types[line_index] != LINE_INPUT)
        return "   "; // output lines indent two spaces

    // Input lines:
    // Determine if this line is the start of a new block or a continuation line
    // We'll determine this by checking the cumulative code up to this line

    // Rebuild code up to this line (only input lines)
    char code_so_far[8192] = {0};
    for (int i = 0; i <= line_index; i++) {
        if (line_types[i] == LINE_INPUT) {
            strncat(code_so_far, history[i], sizeof(code_so_far) - strlen(code_so_far) - 2);
            strcat(code_so_far, "\n");
        }
    }

    int nesting = get_nesting_level(code_so_far);

    if (line_index == cur_line) {
        return nesting == 0 ? ">>> " : "...";
    } else {
        // For previous input lines:
        // If block incomplete at this line, use continuation prompt
        // else use normal prompt

        // To decide, rebuild code up to this line excluding current line
        char code_before[8192] = {0};
        for (int i = 0; i < line_index; i++) {
            if (line_types[i] == LINE_INPUT) {
                strncat(code_before, history[i], sizeof(code_before) - strlen(code_before) - 2);
                strcat(code_before, "\n");
            }
        }
        int nesting_before = get_nesting_level(code_before);

        if (nesting_before > 0) {
            return "... ";
        } else {
            return ">>> ";
        }
    }
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
// Count the total block nesting level for the input lines up to given line (exclusive)
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
            if (strcmp(prompt, "... ") == 0) {
                nesting = cumulative_nesting_up_to(i);
            }
            for (int t = 0; t < nesting; t++) {
                tb_print(strlen(prompt) + t, y, TB_WHITE, TB_DEFAULT, "    ");
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
    if (strcmp(prompt, "... ") == 0) {
        nesting = cumulative_nesting_up_to(cur_line);
    }
    int cursor_x = (int)strlen(prompt) + nesting + cur_col;
    int cursor_y = history_count > h ? h  : cur_line - start;
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

                // Check if block complete
                if (is_block_complete(code_buffer)) {
                    run_backend(code_buffer);
                    code_len = 0;
                    code_buffer[0] = 0;

                    // Add a new empty input line for next command, no indentation
                    append_input_line("");
                    cur_line = history_count - 1;
                    cur_col = 0;
                } else {
                    // Not complete: add continuation input line with indentation (tabs)
                    current_nesting = get_nesting_level(code_buffer);
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
                    // Normal backspace inside line
                    memmove(&line[cur_col-1], &line[cur_col], strlen(line) - cur_col + 1);
                    cur_col--;
                } else {
                    if (strcmp(prompt, "...") == 0 && cur_line > 0) {
                        // Find previous input line (skip output lines)
                        int prev_line = cur_line - 1;
                        while (prev_line >= 0 && line_types[prev_line] != LINE_INPUT) prev_line--;
                        if (prev_line >= 0) {
                            int prev_len = (int)strlen(history[prev_line]);
                            int cur_len = (int)strlen(history[cur_line]);

                            // Remove current line and append its content (after tabs) to previous line
                            // Skip leading tabs from current line
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
