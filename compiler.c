#include <stdio.h>

#include "common.h"
#include "compiler.h"

#include <stdlib.h>

#include "debug.h"

#include <string.h>
#include <math.h>

#include "GemWindow.h"
#include "memory.h"
#include "scanner.h"
#include "vm.h"

#include <limits.h>


typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

#define MAX_LOOP_DEPTH 64

typedef struct {
    int breakJumpOffsets[MAX_LOOP_DEPTH];
    int breakCount;
    int localCount;
} LoopContext;

static LoopContext loopStack[MAX_LOOP_DEPTH];
static int loopDepth = 0;
static int continueJumpOffset = -1;

static char** importedFiles = NULL;
static int importedCount = 0;
static int importedCapacity = 0;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);


typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
    TYPE_LAMBDA,
    TYPE_INITIALIZER,
    TYPE_STATIC_METHOD,
    TYPE_NAMESPACE,
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;    ObjFunction* function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;

    bool inLoop;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
} ClassCompiler;

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;
Chunk* compilingChunk;

static Chunk* currentChunk() {
    return &current->function->chunk;
}

static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void advance() {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void emitReturn() {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0);
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

static uint16_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    //if(constant > 255) error("too many constants");
    return constant;
}

static void emitShort(uint16_t constant){
    emitByte(constant >> 8);
    emitByte(constant);
}

static void emitConstant(Value value) {
    int constant = makeConstant(value);
    
    emitByte(OP_CONSTANT);
    emitShort(constant);
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void patchJumpTo(int offset, int target) {
    int jump = target - offset - 2; // -2 for the operand bytes
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void beginLoop() {
    if (loopDepth == MAX_LOOP_DEPTH) {
        error("Too many nested loops.");
        return;
    }

    LoopContext* loop = &loopStack[loopDepth++];
    loop->breakCount = 0;
    loop->localCount = current->localCount; // NEW
}


static void endLoop(int loopExitTarget) {
    loopDepth--;
    LoopContext* loop = &loopStack[loopDepth];

    for (int i = 0; i < loop->breakCount; i++) {
        patchJump(loop->breakJumpOffsets[i]);
    }
}

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    current = compiler;

    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    // type == TYPE_METHOD || type == TYPE_INITIALIZER to remove static methods refering to the class
    if (type == TYPE_METHOD || type == TYPE_INITIALIZER || type == TYPE_STATIC_METHOD) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
    if (vm.showBytecode)
        if (!parser.hadError) {
            disassembleChunk(&function->chunk, function->name != NULL ?
                function->name->chars : "<script>");
        }

    current = current->enclosing;
    return function;
}

static void beginScope() {
    current->scopeDepth++;
}

static void endScope() {
    current->scopeDepth--;

    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }        current->localCount--;
    }
}

static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static uint16_t identifierConstant(Token* name);

static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_PERCENT:       emitByte(OP_MOD); break;
        case TOKEN_INS:           emitByte(OP_INS); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_IS:            emitByte(OP_INSTANCEOF); break;
        default: return; // Unreachable.
    }
}

static uint8_t argumentList();

static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void hex_number(bool canAssign) {
    double value = (double)strtoll(parser.previous.start + 2, NULL, 16);
    emitConstant(NUMBER_VAL(value));
}

static void oct_number(bool canAssign) {
    double value = (double)strtoll(parser.previous.start + 2, NULL, 8);
    emitConstant(NUMBER_VAL(value));
}

static void bin_number(bool canAssign) {
    double value = (double)strtoll(parser.previous.start + 2, NULL, 2);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                    parser.previous.length - 2)));
}

static int resolveLocal(Compiler* compiler, Token* name);

static int addUpvalue(Compiler* compiler, uint8_t index,
                      bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    // Try to find the variable in the enclosing local scope
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, local, true); // isLocal = true
    }

    // Try to find the variable in the enclosing's upvalues
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, upvalue, false); // isLocal = false
    }
    return -1;
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;

        if (canAssign && match(TOKEN_EQUAL)) {
            expression();
            emitByte(setOp);
            emitShort(arg);
        } else {
            emitByte(getOp);
            emitShort(arg);
        }
        return;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign) {
    Token name = parser.previous;
    namedVariable(name, canAssign);
}

static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG: emitByte(OP_NOT); break;
        default: return; // Unreachable.
    }
}

static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return; // Unreachable.
    }
}

static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void or_(bool canAssign) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint16_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_SET_PROPERTY);
        emitShort(name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitByte(OP_INVOKE);
        emitShort(name);
        emitByte(argCount);
    } else {
        emitByte(OP_GET_PROPERTY);
        emitShort(name);
    }
}

static void this_(bool canAssign) {
    if (currentClass == NULL) {
        error("Can't use 'this' outside of a class.");
        return;
    }

    variable(false);
}

static Token syntheticToken(const char* text);

static void super_(bool canAssign) {
    if (currentClass == NULL) {
        error("Can't use 'super' outside of a class.");
    } else if (!currentClass->hasSuperclass) {
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint16_t name = identifierConstant(&parser.previous);

    namedVariable(syntheticToken("this"), false);
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(syntheticToken("super"), false);
        emitByte(OP_SUPER_INVOKE);
        emitShort(name);
        emitByte(argCount);
    } else {
        namedVariable(syntheticToken("super"), false);
        emitByte(OP_GET_SUPER);
        emitShort(name);
    }
}

static void index_(bool canAssign) {
    expression(); // parse the index
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    if (canAssign && match(TOKEN_EQUAL)) {
        expression(); // parse value
        emitByte(OP_SET_INDEX);
    } else {
        emitByte(OP_GET_INDEX);
    }
}

static void list(bool canAssign) {
    int elementCount = 0;

    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            expression();
            elementCount++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after list elements.");
    emitBytes(OP_LIST, elementCount); // Custom OP_LIST instruction
}

static void function(FunctionType type);

static void lambda(bool canAssign);


ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_CALL},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {list,     index_, PREC_CALL},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     dot,    PREC_CALL},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL,     binary, PREC_FACTOR},
    [TOKEN_INS]           = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_INCRE]         = {NULL,     NULL, PREC_UNARY},
    [TOKEN_DECRE]         = {NULL,     NULL, PREC_UNARY},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_BINARY_NUMBER] = {bin_number,NULL,   PREC_NONE},
    [TOKEN_OCTAL_NUMBER]  = {oct_number,NULL,   PREC_NONE},
    [TOKEN_HEX_NUMBER]    = {hex_number,NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LAMBDA]        = {lambda,   NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IS]            = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {super_,   NULL,   PREC_NONE},
    [TOKEN_THIS]          = {this_,    NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THROW]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOUBLE_COLON]  = {NULL,     NULL,   PREC_NONE},
    [TOKEN_STATIC]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PRINTLN]       = {NULL,     NULL,     PREC_NONE},
    [TOKEN_IMPORT]        = {NULL,     NULL,     PREC_NONE},
    [TOKEN_NAMESPACE]     = {NULL,     NULL,     PREC_NONE},
    [TOKEN_TRY]           = {NULL,     NULL,     PREC_NONE},
    [TOKEN_CATCH]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_FINALLY]       = {NULL,     NULL,     PREC_NONE},
    [TOKEN_OPERATOR]      = {NULL,     NULL,     PREC_NONE},
    [TOKEN_BREAK]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_CONTINUE]      = {NULL,     NULL,     PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static uint16_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start,
                                           name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static void declareVariable() {
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        /*
        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
        */
    }

    addLocal(*name);
}

static uint16_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    if (parser.previous.start[0] == '#' && currentClass == NULL) {
        error("Usage of private types outside of class.");
    }

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth =
        current->scopeDepth;
}

static void defineVariable(uint16_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitByte(OP_DEFINE_GLOBAL);
    emitShort(global);
}

static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void statement();
static void declaration();

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void lambda(bool canAssign){
    TokenType type = TYPE_LAMBDA;

    Compiler compiler;
    initCompiler(&compiler, type);

    compiler.function->name = copyString("lambda", 6);

    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after lambda.");

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint16_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }


    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before lambda body.");
    block();

    ObjFunction* function = endCompiler();
    int closureConstant = makeConstant(OBJ_VAL(function));

    // Emit OP_CLOSURE (pushes the closure on stack)
    emitByte(OP_CLOSURE);
    emitShort(closureConstant);

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);

    if(type != TYPE_LAMBDA){
        compiler.function->name =
            copyString(parser.previous.start,
                 parser.previous.length);
    }
    else 
        compiler.function->name = copyString("lambda", 6);

    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint16_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }


    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    int closureConstant = makeConstant(OBJ_VAL(function));

    // Emit OP_CLOSURE (pushes the closure on stack)
    emitByte(OP_CLOSURE);
    emitShort(closureConstant);

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method() {
    bool makeStatic = false;
    if (match(TOKEN_STATIC)) {
        makeStatic = true;
    }
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint16_t constant = identifierConstant(&parser.previous);

    FunctionType type = makeStatic? TYPE_STATIC_METHOD: TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }

    if (makeStatic && type == TYPE_INITIALIZER) {
        error("Initializer cannot be static..");
    }
    function(type);
    if (makeStatic){
        emitByte(OP_STATIC_METHOD);
        emitShort(constant);
    }
    else{
        emitByte(OP_METHOD);
        emitShort(constant);
    }
}

static Token syntheticToken(const char* text) {
    Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

#include <gc.h>

static void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    uint16_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    emitByte(OP_CLASS);
    emitShort(nameConstant);
    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    if (match(TOKEN_DOUBLE_COLON)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);

        if (identifiersEqual(&className, &parser.previous)) {
            error("A class can't inherit from itself.");
        }

        beginScope();
        addLocal(syntheticToken("super"));
        defineVariable(0);

        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }

    Chunk new;
    initChunk(&new);
    
    namedVariable(className, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (match(TOKEN_VAR)) {
            int startPoint = current->function->chunk.count;
            consume(TOKEN_IDENTIFIER, "Expect variable name.");
            uint16_t global = identifierConstant(&parser.previous);
            if (match(TOKEN_EQUAL)) {
                expression();
            } else {
                emitByte(OP_NIL);
            }
            consume(TOKEN_SEMICOLON,
                    "Expect ';' after variable declaration.");

            emitByte(OP_STATIC_VAR);
            emitShort(global);

            //Save all the static variable bytecode
            int endPoint = current->function->chunk.count;
            for(int i = startPoint; i < endPoint; i++){
                writeChunk(&new, current->function->chunk.code[i], current->function->chunk.lines[i]);
            }
            current->function->chunk.count = startPoint;
        }
        else {
            match(TOKEN_OPERATOR);
            method();
        }
    }

    //Insert the saved code at last to ensure the class and methods is well defined.
    for(int i = 0; i < new.count; i++){
        writeChunk(&current->function->chunk, new.code[i], new.lines[i]);
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OP_POP);

    if (classCompiler.hasSuperclass) {
        endScope();
    }

    currentClass = currentClass->enclosing;
}


static void funDeclaration() {
    uint16_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    emitByte(OP_DISPATCH);
    defineVariable(global);
}

static void varDeclaration() {
    uint16_t global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON,
            "Expect ';' after variable declaration.");

    defineVariable(global);
}

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void forStatement() {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    if (match(TOKEN_SEMICOLON)) {
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        expressionStatement();
    }

    int loopStart = currentChunk()->count;
    int exitJump = -1;

    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after condition.");
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    int bodyJump = -1;
    int incrementStart = -1;
    int continueTarget;

    if (!match(TOKEN_RIGHT_PAREN)) {
        bodyJump = emitJump(OP_JUMP);
        incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);

        continueTarget = incrementStart;
    } else {
        continueTarget = loopStart;
    }

    int prevContinue = continueJumpOffset;
    continueJumpOffset = continueTarget;

    beginLoop(); // break support
    statement(); // loop body
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    continueJumpOffset = prevContinue;

    endLoop(currentChunk()->count); // patch breaks
    endScope();
}

static void throwStatement() {
    // Parse the expression to be thrown
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after throw expression.");

    // Emit runtime type check and throw
    emitByte(OP_THROW);
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);

}

static void printStatement() {
    consume(TOKEN_LEFT_PAREN, "Expected (");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected )");
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void printlnStatement() {
    consume(TOKEN_LEFT_PAREN, "Expected (");
    if (match(TOKEN_RIGHT_PAREN)) {
        emitByte(OP_PRINTLN_BLANK);
    }
    else {
        expression();
        consume(TOKEN_RIGHT_PAREN, "Expected ')'.");
        emitByte(OP_PRINTLN);
    }
    consume(TOKEN_SEMICOLON, "Expected ';'.");
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        if (current->type == TYPE_INITIALIZER) {
            error("Can't return a value from an initializer.");
        }

        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void whileStatement() {
    int loopStart = currentChunk()->count;

    // Save and update the continue jump target
    int prevContinue = continueJumpOffset;
    continueJumpOffset = loopStart;

    beginLoop(); // enable break support

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression(); // condition
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Pop condition result

    statement(); // loop body

    emitLoop(loopStart); // jump back to condition

    patchJump(exitJump);
    emitByte(OP_POP); // Pop leftover condition result

    endLoop(currentChunk()->count); // patch breaks

    continueJumpOffset = prevContinue; // restore previous continue target
}

static void tryCatchStatement() {
    int tryStart = currentChunk()->count;

    emitByte(OP_TRY);
    emitByte(0); // Placeholder for relative offset to catch block

    // Compile try block
    beginScope();
    consume(TOKEN_LEFT_BRACE, "Expect '{' before try block.");
    block();
    endScope();


    // Emit jump to skip catch if no error occurred
    emitByte(OP_END_TRY);
    int jumpOverCatch = emitJump(OP_JUMP);

    // Catch block starts here
    int catchStart = currentChunk()->count;

    // Patch the OP_TRY's catch offset
    int catchOffset = catchStart - tryStart; // +2 = OP_TRY + offset byte
    currentChunk()->code[tryStart + 1] = (uint8_t)catchOffset;

    emitByte(OP_END_TRY); // Optional: denotes end of catch target marker

    // Parse and compile catch clause
    consume(TOKEN_CATCH, "Expect 'catch' after try block.");
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'catch'.");
    beginScope();

    consume(TOKEN_IDENTIFIER, "Expect variable name in catch block.");
    Token name = parser.previous;
    declareVariable();
    markInitialized();

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after catch variable name.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before catch block.");

    uint8_t slot = resolveLocal(current, &name);
    emitBytes(OP_SET_LOCAL, slot);
    emitByte(OP_POP);

    block();
    endScope();

    // Patch jump to land after catch block
    patchJump(jumpOverCatch);
}

static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                ; // Do nothing.
        }

        advance();
    }
}


static void declaration() {
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    } else if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    }
    else if(match(TOKEN_SEMICOLON)){
    } else {
        statement();
    }
    if (parser.panicMode) synchronize();
}
#include <unistd.h>
bool fileExists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

void replaceDotsWithSlashes(char* str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '.') str[i] = '/';
    }
}

char* appendStrings(const char* a, const char* b) {
    size_t len = strlen(a) + strlen(b) + 1;
    char* result = malloc(len);
    strcpy(result, a);
    strcat(result, b);
    return result;
}

char* readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);
    char* buffer = malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    return buffer;
}

// Build a path like "../" * depth + "dira/dirb/file.gem"
char* buildPath(int depth, const char* modulePath) {
    char prefix[PATH_MAX] = "";
    for (int i = 0; i < depth; i++) {
        strcat(prefix, "../");
    }

    char* fullPath = appendStrings(prefix, modulePath);
    return fullPath;
}

char* loadModuleFile(const char* fileName) {
    char* modulePath = strdup(fileName);
    replaceDotsWithSlashes(modulePath);

    char* relativePath = appendStrings(modulePath, ".gem");
    free(modulePath);

    // Try from current directory up to some limit (e.g., 10 levels)
    for (int depth = 0; depth < 10; depth++) {
        char* candidate = buildPath(depth, relativePath);
        if (fileExists(candidate)) {
            char* source = readFile(candidate);
            free(relativePath);
            free(candidate);
            return source;
        }
        free(candidate);
    }

    fprintf(stderr, "Module not found: %s\n", fileName);
    free(relativePath);
    return NULL;
}

static void breakStatement() {
    if (loopDepth == 0) {
        error("Can't use 'break' outside of a loop.");
        return;
    }

    LoopContext* loop = &loopStack[loopDepth - 1];

    // Pop locals created inside the loop.
    int localsToPop = current->localCount - loop->localCount;
    for (int i = 0; i < localsToPop; i++) {
        emitByte(OP_POP);
    }
    
    int jumpOffset = emitJump(OP_JUMP);

    if (loop->breakCount >= MAX_LOOP_DEPTH) {
        error("Too many breaks in one loop.");
        return;
    }

    loop->breakJumpOffsets[loop->breakCount++] = jumpOffset;
}

static void emitJumpTo(uint8_t instruction, int target) {
    emitByte(instruction);
    int offset = target - currentChunk()->count - 2;  // 2 = size of jump operands
    if (offset > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}


static void continueStatement() {
    if (loopDepth == 0) {
        error("Cannot use 'continue' outside of a loop.");
        return;
    }

    LoopContext* loop = &loopStack[loopDepth - 1];

    int localsToPop = current->localCount - loop->localCount;
    for (int i = 0; i < localsToPop; i++) {
        emitByte(OP_POP);
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
    emitLoop(continueJumpOffset);
}

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
}

void namespaceStatement(){
    uint16_t global = parseVariable("Expected namespace name.");
    ObjString* namespaceName = copyString(parser.previous.start,
                                         parser.previous.length);

    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    compiler.function->name = namespaceName;
    current->function->arity = 0;

    consume(TOKEN_LEFT_BRACE, "Expect '{' before body.");
    block();

    ObjFunction* function = endCompiler();
    int closureConstant = makeConstant(OBJ_VAL(function));

    emitByte(OP_CLOSURE);
    emitShort(closureConstant);

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }

    emitByte(OP_NAMESPACE);
    defineVariable(global);
}

void compileImport(const char* source);

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_IMPORT)) {
        consume(TOKEN_IDENTIFIER, "Expect file name after import.");
        ObjString* fileName = copyString(parser.previous.start,
                                         parser.previous.length);
        if (!check(TOKEN_SEMICOLON)) {
            errorAtCurrent("Expect ';' after file name.");
        }
        char* file = fileName->chars;

         bool alreadyImported = false;
        for (int i = 0; i < importedCount; i++) {
            if (strcmp(importedFiles[i], file) == 0) {
                alreadyImported = true;
                break;
            }
        }

        if (alreadyImported) {
            // Skip import but still consume the semicolon
            advance();
            return;
        }

        // Add this file name to the imported list
        if (importedCount + 1 > importedCapacity) {
            importedCapacity = importedCapacity < 8 ? 8 : importedCapacity * 2;
            importedFiles = realloc(importedFiles, sizeof(char*) * importedCapacity);
        }

        importedFiles[importedCount++] = strdup(file);
        
        char* source;
        if (memcmp(file, "Window", 6) == 0) {
            source = getWindowText();
        }
        else if (memcmp(file, "Math", 4) == 0){
            source = getMathText();
        }
        else{
            source = loadModuleFile(file);
            if (source == NULL) {
                exit(70);
            }
        }

        Scanner* sc = getScanner();

        int prevLine = sc->line;
        const char* prevStart = sc->start;
        const char* prevCurrent = sc->current;

        ObjFunction* function = compile(source);
        function->name = fileName;
        int constant = makeConstant(OBJ_VAL(function));
        emitByte(OP_CLOSURE);
        emitShort(constant);
            
        emitBytes(OP_CALL, 0);
        emitByte(OP_POP);

        sc->current = prevCurrent;
        sc->start = prevStart;
        sc->line = prevLine;
        
        advance();
        free(source);
    }
    else if (match(TOKEN_PRINTLN)) {
        printlnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    }else if (match(TOKEN_BREAK)) {
        breakStatement();
    }else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    }else if (match(TOKEN_THROW)) {
        throwStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    }
    else if (match(TOKEN_TRY)) {
        tryCatchStatement();
    }
    else if (match(TOKEN_RETURN)) {
        returnStatement();
    }else if(match(TOKEN_NAMESPACE)){
        namespaceStatement();
    }else {
        expressionStatement();
    }
}

/* preproc_macro_multiline.c
   Mini-preprocessor:
   - supports #macro lines (object-like and function-like)
   - macro bodies can span multiple lines using backslash (\) continuation
   - preserves "string literals" and // comments
   - expands macros (simple textual macro replacement)
   - desugars ++/-- and +=, -=, *=, /=, %=
   - desugars for(var x in expr) into a while iterator loop
   Compile: gcc -std=c11 preproc_macro_multiline.c -O2 -o preproc_macro_multiline
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* safe strdup/strndup */
static char* my_strndup(const char* s, int n) {
    char* p = malloc((size_t)n + 1);
    if (!p) return NULL;
    memcpy(p, s, (size_t)n);
    p[n] = '\0';
    return p;
}
static char* my_strdup(const char* s) { return my_strndup(s, (int)strlen(s)); }

/* --- Macro table --- */
typedef struct Macro {
    char* name;
    char** params;
    int param_count;
    char* body;
    struct Macro* next;
} Macro;

static Macro* macros = NULL;

void add_macro(const char* name, char** params, int param_count, const char* body) {
    Macro* m = malloc(sizeof(Macro));
    m->name = my_strdup(name);
    m->param_count = param_count;
    if (param_count > 0) {
        m->params = malloc(sizeof(char*) * param_count);
        for (int i = 0; i < param_count; ++i) m->params[i] = my_strdup(params[i]);
    } else m->params = NULL;
    m->body = my_strdup(body);
    m->next = macros;
    macros = m;
}

Macro* find_macro(const char* name) {
    for (Macro* m = macros; m; m = m->next) if (strcmp(m->name, name) == 0) return m;
    return NULL;
}

/* expand macro body by replacing parameter identifiers with argument strings */
char* expand_macro_body(Macro* m, char** args, int argc) {
    if (m->param_count == 0) return my_strdup(m->body);
    size_t cap = strlen(m->body) * 4 + 16;
    char* out = malloc(cap);
    size_t pos = 0;
    const char* s = m->body;
    while (*s) {
        if (isalpha((unsigned char)*s) || *s == '_') {
            const char* p = s+1;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
            int len = (int)(p - s);
            char* ident = my_strndup(s, len);
            int repl = -1;
            for (int k = 0; k < m->param_count; ++k) if (strcmp(ident, m->params[k]) == 0) { repl = k; break; }
            if (repl >= 0 && repl < argc) {
                size_t need = strlen(args[repl]);
                if (pos + need + 1 >= cap) { cap = (cap + need) * 2; out = realloc(out, cap); }
                memcpy(out + pos, args[repl], need); pos += need;
            } else {
                if (pos + len + 1 >= cap) { cap = (cap + len) * 2; out = realloc(out, cap); }
                memcpy(out + pos, s, len); pos += len;
            }
            free(ident);
            s = p;
        } else {
            if (pos + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[pos++] = *s++;
        }
    }
    out[pos] = '\0';
    return out;
}

/* helpers for expressions */
static int is_expr_char(char c) { return (isalnum((unsigned char)c) || c == '_' || c == '.'); }

static int find_prev_nonspace(const char* s, int idx) {
    int p = idx - 1;
    while (p >= 0 && isspace((unsigned char)s[p])) p--;
    return p;
}
static int find_next_nonspace(const char* s, int idx, int n) {
    int p = idx + 1;
    while (p < n && isspace((unsigned char)s[p])) p++;
    return (p < n) ? p : -1;
}

static int find_expr_start(const char* s, int end_idx) {
    int k = end_idx, dp=0, db=0;
    while (k >= 0) {
        char c = s[k];
        if (c == ')') { dp++; k--; continue; }
        if (c == ']') { db++; k--; continue; }
        if (c == '(') { if (dp > 0) { dp--; k--; continue; } break; }
        if (c == '[') { if (db > 0) { db--; k--; continue; } break; }
        if (dp > 0 || db > 0) { k--; continue; }
        if (is_expr_char(c)) { k--; continue; }
        break;
    }
    return k + 1;
}

static int forward_find_expr_end(const char* s, int start_idx, int n) {
    int i = start_idx, dp=0, db=0;
    while (i < n) {
        char c = s[i];
        if (c == '[') { db++; i++; continue; }
        if (c == ']') { if (db>0) { db--; i++; continue; } break; }
        if (c == '(') { dp++; i++; continue; }
        if (c == ')') { if (dp>0) { dp--; i++; continue; } break; }
        if (dp>0 || db>0) { i++; continue; }
        if (is_expr_char(c)) { i++; continue; }
        break;
    }
    int end = i - 1;
    while (end >= start_idx && isspace((unsigned char)s[end])) end--;
    return end;
}

static int forward_find_rhs_end(const char* s, int start_idx, int n) {
    int i = start_idx, dp=0, db=0;
    while (i < n) {
        char c = s[i];
        if (c == '"') {
            int j = i+1;
            while (j < n) { if (s[j] == '"' && s[j-1] != '\\') { j++; break; } j++; }
            i = j; continue;
        }
        if (c == '(') { dp++; i++; continue; }
        if (c == ')') { if (dp>0) { dp--; i++; continue; } break; }
        if (c == '[') { db++; i++; continue; }
        if (c == ']') { if (db>0) { db--; i++; continue; } break; }
        if (dp==0 && db==0 && (c==';' || c==',')) break;
        i++;
    }
    int end = i - 1;
    while (end >= start_idx && isspace((unsigned char) s[end])) end--;
    if (end < start_idx) return -1;
    return end;
}

static char* ensure_capacity(char* out, size_t *out_cap, size_t need) {
    if (*out_cap >= need) return out;
    size_t newcap = *out_cap;
    while (newcap < need) newcap *= 2;
    char* p = realloc(out, newcap);
    if (!p) return NULL;
    *out_cap = newcap;
    return p;
}

/* Step 1: strip macros and store them. Supports multiline macro bodies using backslash at line end. */
char* strip_macros_and_build_table(const char* src) {
    int n = (int)strlen(src);
    size_t cap = (size_t)n + 16;
    char* out = malloc(cap);
    size_t out_pos = 0;
    int i = 0;
    while (i < n) {
        int j = i;
        if (i == 0 || src[i-1] == '\n') {
            while (j < n && (src[j] == ' ' || src[j] == '\t')) j++;
            if (j < n && src[j] == '#' && strncmp(src + j, "#macro", 6) == 0) {
                int p = j + 6;
                while (p < n && isspace((unsigned char)src[p])) p++;
                int name_start = p;
                while (p < n && (isalnum((unsigned char)src[p]) || src[p] == '_')) p++;
                if (p == name_start) { while (p < n && src[p] != '\n') p++; i = p; continue; }
                char* name = my_strndup(src + name_start, p - name_start);
                char** params = NULL; int param_count = 0;
                if (p < n && src[p] == '(') {
                    p++;
                    while (p < n && src[p] != ')') {
                        while (p < n && isspace((unsigned char)src[p])) p++;
                        int ps = p;
                        while (p < n && (isalnum((unsigned char)src[p]) || src[p] == '_')) p++;
                        if (p > ps) {
                            params = realloc(params, sizeof(char*) * (param_count + 1));
                            params[param_count++] = my_strndup(src + ps, p - ps);
                        }
                        while (p < n && isspace((unsigned char)src[p])) p++;
                        if (p < n && src[p] == ',') p++;
                    }
                    if (p < n && src[p] == ')') p++;
                }
                while (p < n && isspace((unsigned char)src[p])) p++;
                /* Read body, but support backslash-continuation */
                size_t body_cap = 256;
                char* body = malloc(body_cap);
                size_t body_len = 0;
                int cont = 1;
                while (p < n && cont) {
                    int line_start = p;
                    while (p < n && src[p] != '\n') p++;
                    int line_end = p;
                    while (line_end > line_start && isspace((unsigned char)src[line_end-1])) line_end--;
                    cont = 0;
                    if (line_end > line_start && src[line_end-1] == '\\') { cont = 1; line_end--; }
                    int addlen = line_end - line_start;
                    if (body_len + addlen + 1 >= body_cap) { body_cap = (body_cap + addlen) * 2; body = realloc(body, body_cap); }
                    memcpy(body + body_len, src + line_start, addlen); body_len += addlen;
                    if (cont) {
                        if (body_len + 1 >= body_cap) { body_cap *= 2; body = realloc(body, body_cap); }
                        body[body_len++] = ' ';
                    }
                    if (p < n && src[p] == '\n') p++;
                }
                if (body_len == 0) { free(body); body = my_strdup(""); }
                else { body[body_len] = '\0'; body = realloc(body, body_len + 1); }
                add_macro(name, params, param_count, body);
                free(name); free(body);
                if (params) { for (int q=0;q<param_count;q++) free(params[q]); free(params); }
                i = p;
                continue;
            }
        }
        if (out_pos + 2 >= cap) { cap = cap * 2 + 16; out = realloc(out, cap); }
        out[out_pos++] = src[i++];
    }
    out[out_pos] = '\0';
    return out;
}

/* Step 2: expand macros (single pass) */
char* expand_macros_once(const char* src) {
    int n = (int)strlen(src);
    size_t cap = (size_t)n * 4 + 64;
    char* out = malloc(cap);
    size_t out_pos = 0;
    int i = 0;
    while (i < n) {
        if (src[i] == '"') {
            int j = i+1;
            while (j < n) { if (src[j] == '"' && src[j-1] != '\\') { j++; break; } j++; }
            int slen = j - i;
            if (out_pos + slen + 1 >= cap) { cap = (cap + slen) * 2; out = realloc(out, cap); }
            memcpy(out + out_pos, src + i, slen); out_pos += slen;
            i = j; continue;
        }
        if (i+1 < n && src[i] == '/' && src[i+1] == '/') {
            int j = i + 2;
            while (j < n && src[j] != '\n') j++;
            int clen = j - i;
            if (out_pos + clen + 1 >= cap) { cap = (cap + clen) * 2; out = realloc(out, cap); }
            memcpy(out + out_pos, src + i, clen); out_pos += clen;
            i = j; continue;
        }
        if (isalpha((unsigned char)src[i]) || src[i] == '_') {
            int j = i+1;
            while (j < n && (isalnum((unsigned char)src[j]) || src[j] == '_')) j++;
            char* ident = my_strndup(src + i, j - i);
            Macro* m = find_macro(ident);
            if (m) {
                if (m->param_count == 0) {
                    size_t len = strlen(m->body);
                    if (out_pos + len + 1 >= cap) { cap = (cap + len) * 2; out = realloc(out, cap); }
                    memcpy(out + out_pos, m->body, len); out_pos += len;
                    i = j; free(ident); continue;
                } else {
                    int k = j;
                    while (k < n && isspace((unsigned char)src[k])) k++;
                    if (k < n && src[k] == '(') {
                        k++;
                        int depth = 1;
                        int arg_start = k;
                        char** args = NULL; int argc = 0;
                        for (; k < n; k++) {
                            if (src[k] == '"') {
                                int q = k+1; while (q < n) { if (src[q] == '"' && src[q-1] != '\\') { q++; break; } q++; }
                                k = q - 1;
                            } else if (src[k] == '(') depth++;
                            else if (src[k] == ')') {
                                depth--;
                                if (depth == 0) {
                                    if (k > arg_start) {
                                        args = realloc(args, sizeof(char*) * (argc + 1));
                                        args[argc++] = my_strndup(src + arg_start, k - arg_start);
                                    } else {
                                        args = realloc(args, sizeof(char*) * (argc + 1));
                                        args[argc++] = my_strdup("");
                                    }
                                    k++;
                                    break;
                                }
                            } else if (src[k] == ',' && depth == 1) {
                                args = realloc(args, sizeof(char*) * (argc + 1));
                                args[argc++] = my_strndup(src + arg_start, k - arg_start);
                                arg_start = k + 1;
                            }
                        }
                        char* expanded = expand_macro_body(m, args, argc);
                        size_t elen = strlen(expanded);
                        if (out_pos + elen + 1 >= cap) { cap = (cap + elen) * 2; out = realloc(out, cap); }
                        memcpy(out + out_pos, expanded, elen); out_pos += elen;
                        free(expanded);
                        for (int a=0;a<argc;a++) free(args[a]); free(args);
                        i = k; free(ident); continue;
                    }
                }
            }
            if (out_pos + (j - i) + 1 >= cap) { cap = (cap + (j-i)) * 2; out = realloc(out, cap); }
            memcpy(out + out_pos, src + i, j - i); out_pos += j - i;
            i = j; free(ident); continue;
        }
        if (out_pos + 2 >= cap) { cap = cap * 2 + 16; out = realloc(out, cap); }
        out[out_pos++] = src[i++];
    }
    out[out_pos] = '\0';
    return out;
}

char* expand_macros(const char* src) {
    char* curr = my_strdup(src);
    for (int pass = 0; pass < 8; pass++) {
        char* next = expand_macros_once(curr);
        if (strcmp(next, curr) == 0) { free(curr); return next; }
        free(curr);
        curr = next;
    }
    return curr;
}

/* Step 3: operator desugaring (compound assignments and ++/--) */
char* desugar_operators(const char* src) {
    if (!src) return NULL;
    int n = (int)strlen(src);
    if (n == 0) return my_strdup("");

    size_t out_cap = (size_t)n * 8 + 64;
    char* out = malloc(out_cap);
    int i = 0;
    int last_emit = 0;
    size_t out_pos = 0;

    while (i < n) {
        /* string */
        if (src[i] == '"') {
            if (i > last_emit) {
                int pre_len = i - last_emit;
                out = ensure_capacity(out, &out_cap, out_pos + pre_len + 1);
                memcpy(out + out_pos, src + last_emit, pre_len);
                out_pos += pre_len;
            }
            int j = i+1;
            while (j < n) { if (src[j] == '"' && src[j-1] != '\\') { j++; break; } j++; }
            int slen = j - i;
            out = ensure_capacity(out, &out_cap, out_pos + slen + 1);
            memcpy(out + out_pos, src + i, slen); out_pos += slen;
            i = j; last_emit = i; continue;
        }
        /* comment */
        if (i+1 < n && src[i] == '/' && src[i+1] == '/') {
            if (i > last_emit) {
                int pre_len = i - last_emit;
                out = ensure_capacity(out, &out_cap, out_pos + pre_len + 1);
                memcpy(out + out_pos, src + last_emit, pre_len); out_pos += pre_len;
            }
            int j = i + 2; while (j < n && src[j] != '\n') j++;
            int clen = j - i;
            out = ensure_capacity(out, &out_cap, out_pos + clen + 1);
            memcpy(out + out_pos, src + i, clen); out_pos += clen;
            i = j; last_emit = i; continue;
        }

        /* compound assignment */
        if (i+1 < n) {
            char c = src[i], next = src[i+1];
            if ((c=='+'||c=='-'||c=='*'||c=='/'||c=='%') && next=='=') {
                int prev = find_prev_nonspace(src, i);
                if (prev >= 0 && (isalnum((unsigned char)src[prev]) || src[prev]==']' || src[prev]==')' || src[prev]=='_')) {
                    int lhs_end = prev;
                    int lhs_start = find_expr_start(src, lhs_end);
                    if (lhs_start < 0) { i++; continue; }
                    int rhs_first = find_next_nonspace(src, i+1, n);
                    if (rhs_first == -1) { i += 2; continue; }
                    int rhs_end = forward_find_rhs_end(src, rhs_first, n);
                    if (rhs_end < rhs_first) { i += 2; continue; }

                    if (lhs_start > last_emit) {
                        int pre_len = lhs_start - last_emit;
                        out = ensure_capacity(out, &out_cap, out_pos + pre_len + 1);
                        memcpy(out + out_pos, src + last_emit, pre_len); out_pos += pre_len;
                    }
                    char* lhs = my_strndup(src + lhs_start, lhs_end - lhs_start + 1);
                    char* rhs = my_strndup(src + rhs_first, rhs_end - rhs_first + 1);
                    const char* op = (c=='+')?"+":(c=='-')?"-":(c=='*')?"*":(c=='/')?"/":"%";
                    int needed = snprintf(NULL,0,"(%s = %s %s (%s))", lhs, lhs, op, rhs);
                    out = ensure_capacity(out, &out_cap, out_pos + needed + 1);
                    snprintf(out + out_pos, out_cap - out_pos, "(%s = %s %s (%s))", lhs, lhs, op, rhs);
                    out_pos += needed;
                    free(lhs); free(rhs);
                    i = rhs_end + 1; last_emit = i; continue;
                }
            }
        }

        /* ++ */
        if (i+1 < n && src[i] == '+' && src[i+1] == '+') {
            int prev = find_prev_nonspace(src, i);
            int nxt = find_next_nonspace(src, i+1, n);
            if (prev >= 0 && (isalnum((unsigned char)src[prev]) || src[prev]==']' || src[prev]==')' || src[prev]=='_')) {
                int expr_end = prev;
                int expr_start = find_expr_start(src, expr_end);
                if (expr_start > last_emit) {
                    int pre_len = expr_start - last_emit;
                    out = ensure_capacity(out, &out_cap, out_pos + pre_len + 1);
                    memcpy(out + out_pos, src + last_emit, pre_len); out_pos += pre_len;
                 }
                char* expr = my_strndup(src + expr_start, expr_end - expr_start + 1);
                int needed = snprintf(NULL,0,"((%s = %s + 1) - 1)", expr, expr);
                out = ensure_capacity(out, &out_cap, out_pos + needed + 1);
                snprintf(out + out_pos, out_cap - out_pos, "((%s = %s + 1) - 1)", expr, expr);
                out_pos += needed; free(expr);
                i += 2; last_emit = i; continue;
            } else {
                if (nxt != -1) {
                    int expr_start = nxt;
                    int expr_end = forward_find_expr_end(src, expr_start, n);
                    if (expr_end >= expr_start) {
                        if (i > last_emit) {
                            int pre_len = i - last_emit;
                            out = ensure_capacity(out, &out_cap, out_pos + pre_len + 1);
                            memcpy(out + out_pos, src + last_emit, pre_len); out_pos += pre_len;
                        }
                        char* expr = my_strndup(src + expr_start, expr_end - expr_start + 1);
                        int needed = snprintf(NULL,0,"(%s = %s + 1)", expr, expr);
                        out = ensure_capacity(out, &out_cap, out_pos + needed + 1);
                        snprintf(out + out_pos, out_cap - out_pos, "(%s = %s + 1)", expr, expr);
                        out_pos += needed; free(expr);
                        i = expr_end + 1; last_emit = i; continue;
                    }
                }
                i++; continue;
            }
        }

        /* -- */
        if (i+1 < n && src[i] == '-' && src[i+1] == '-') {
            int prev = find_prev_nonspace(src, i);
            int nxt = find_next_nonspace(src, i+1, n);
            if (prev >= 0 && (isalnum((unsigned char)src[prev]) || src[prev]==']' || src[prev]==')' || src[prev]=='_')) {
                int expr_end = prev;
                int expr_start = find_expr_start(src, expr_end);
                if (expr_start > last_emit) {
                    int pre_len = expr_start - last_emit;
                    out = ensure_capacity(out, &out_cap, out_pos + pre_len + 1);
                    memcpy(out + out_pos, src + last_emit, pre_len); out_pos += pre_len;
                }
                char* expr = my_strndup(src + expr_start, expr_end - expr_start + 1);
                int needed = snprintf(NULL,0,"((%s = %s - 1) + 1)", expr, expr);
                out = ensure_capacity(out, &out_cap, out_pos + needed + 1);
                snprintf(out + out_pos, out_cap - out_pos, "((%s = %s - 1) + 1)", expr, expr);
                out_pos += needed; free(expr);
                i += 2; last_emit = i; continue;
            } else {
                if (nxt != -1) {
                    int expr_start = nxt;
                    int expr_end = forward_find_expr_end(src, expr_start, n);
                    if (expr_end >= expr_start) {
                        if (i > last_emit) {
                            int pre_len = i - last_emit;
                            out = ensure_capacity(out, &out_cap, out_pos + pre_len + 1);
                            memcpy(out + out_pos, src + last_emit, pre_len); out_pos += pre_len;
                        }
                        char* expr = my_strndup(src + expr_start, expr_end - expr_start + 1);
                        int needed = snprintf(NULL,0,"(%s = %s - 1)", expr, expr);
                        out = ensure_capacity(out, &out_cap, out_pos + needed + 1);
                        snprintf(out + out_pos, out_cap - out_pos, "(%s = %s - 1)", expr, expr);
                        out_pos += needed; free(expr);
                        i = expr_end + 1; last_emit = i; continue;
                    }
                }
                i++; continue;
            }
        }

        i++;
    }

    if (last_emit < n) {
        int tail_len = n - last_emit;
        out = ensure_capacity(out, &out_cap, out_pos + tail_len + 1);
        memcpy(out + out_pos, src + last_emit, tail_len);
        out_pos += tail_len;
    }
    out[out_pos] = '\0';
    return out;
}

/* Insert or merge these helper functions into your preprocessor module. */
/* (my_strndup / my_strdup already exist in your posted code; keep them.) */

/* helper checks/parsers used by the desugaring routine */
static int is_token_for(const char* s, int i, int n) {
    if (i+3 > n) return 0;
    if (s[i] != 'f' || s[i+1] != 'o' || s[i+2] != 'r') return 0;
    if (i>0 && (isalnum((unsigned char)s[i-1]) || s[i-1]=='_' )) return 0;
    int j = i+3;
    if (j < n && (isalnum((unsigned char)s[j]) || s[j]=='_')) return 0;
    return 1;
}
static int parse_ident_end(const char* s, int idx, int n) {
    int j = idx;
    if (j < n && (isalpha((unsigned char)s[j]) || s[j]=='_')) {
        j++;
        while (j < n && (isalnum((unsigned char)s[j]) || s[j]=='_')) j++;
        return j;
    }
    return -1;
}
static int find_next_nonspace_at_or_after(const char* s, int idx, int n) {
    int p = idx;
    while (p < n && isspace((unsigned char)s[p])) p++;
    return (p < n) ? p : -1;
}
static int find_matching_paren(const char* s, int idx, int n) {
    if (idx>=n || s[idx] != '(') return -1;
    int i = idx+1;
    int depth = 1;
    while (i < n) {
        char c = s[i];
        if (c == '"') { /* skip string "..." */
            i++;
            while (i < n) { if (s[i] == '"' && s[i-1] != '\\') { i++; break; } i++; }
            continue;
        } else if (c == '\'') { /* skip '...' */
            i++;
            while (i < n) { if (s[i] == '\'' && s[i-1] != '\\') { i++; break; } i++; }
            continue;
        } else if (c == '(') { depth++; i++; continue; }
        else if (c == ')') { depth--; i++; if (depth==0) return i; continue; }
        else i++;
    }
    return -1;
}
static int find_matching_brace(const char* s, int idx, int n) {
    if (idx>=n || s[idx] != '{') return -1;
    int i = idx+1;
    int depth = 1;
    while (i < n) {
        char c = s[i];
        if (c == '"') { i++; while (i < n) { if (s[i] == '"' && s[i-1] != '\\') { i++; break; } i++; } continue; }
        else if (c == '\'') { i++; while (i < n) { if (s[i] == '\'' && s[i-1] != '\\') { i++; break; } i++; } continue; }
        else if (c == '{') { depth++; i++; continue; }
        else if (c == '}') { depth--; i++; if (depth==0) return i; continue; }
        else i++;
    }
    return -1;
}
static char* slice(const char* s, int start, int end) {
    if (end <= start) return my_strdup("");
    return my_strndup(s + start, end - start);
}

static int find_statement_end(const char* s, int idx, int n) {
    /* Find the end of a single statement starting at idx.
       We stop at the first semicolon that's not inside parens/braces/strings.
       If there's no semicolon, return -1. */
    int i = idx;
    int pdepth = 0;
    int bdepth = 0;
    while (i < n) {
        char c = s[i];
        if (c == '"') { i++; while (i < n) { if (s[i] == '"' && s[i-1] != '\\') { i++; break; } i++; } continue; }
        else if (c == '\'') { i++; while (i < n) { if (s[i] == '\'' && s[i-1] != '\\') { i++; break; } i++; } continue; }
        else if (c == '(') { pdepth++; i++; continue; }
        else if (c == ')') { if (pdepth>0) pdepth--; i++; continue; }
        else if (c == '{') { bdepth++; i++; continue; }
        else if (c == '}') { if (bdepth>0) bdepth--; i++; continue; }
        else if (c == ';' && pdepth==0 && bdepth==0) { return i+1; }
        else i++;
    }
    return -1;
}

/* For-in desugaring pass */
static unsigned long for_in_counter = 0;
static void seed_for_in() {
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)(time(NULL) ^ (uintptr_t)&for_in_counter));
    }
}

/* create unique iter name */
static char* make_iter_name() {
    seed_for_in();
    unsigned long c = ++for_in_counter;
    unsigned int r = (unsigned int)rand();
    char buf[64];
    snprintf(buf, sizeof(buf), "__iter_%lu_%u", c, r);
    return my_strdup(buf);
}

/* main desugaring function that replaces for(var id in expr) stmt_or_block */
static int iter_counter = 0;

// Skip whitespace
static void skip_ws(const char** p) {
    while (isspace(**p)) (*p)++;
}

// Extract block or single statement body
static char* extract_block(const char** p) {
    skip_ws(p);
    if (**p == '{') {
        int depth = 0;
        const char* start = *p;
        while (**p) {
            if (**p == '{') depth++;
            else if (**p == '}') {
                depth--;
                if (depth == 0) {
                    (*p)++;
                    break;
                }
            }
            (*p)++;
        }
        size_t len = *p - start;
        char* block = malloc(len + 1);
        memcpy(block, start, len);
        block[len] = '\0';
        return block;
    } else {
        // Single statement body until semicolon or newline
        const char* start = *p;
        while (**p && **p != ';' && **p != '\n') (*p)++;
        if (**p == ';') (*p)++;
        size_t len = *p - start;
        char* body = malloc(len + 1);
        memcpy(body, start, len);
        body[len] = '\0';
        return body;
    }
}

// Main transformation
static char* process_for_in(const char* src) {
    const char* p = src;
    size_t out_cap = strlen(src) * 3;
    char* out = calloc(1, out_cap);

    while (*p) {
        const char* match = strstr(p, "for");
        if (!match) {
            strcat(out, p);
            break;
        }
        strncat(out, p, match - p);
        p = match + 3;
        skip_ws(&p);

        if (*p != '(') {
            strcat(out, "for");
            continue;
        }

        const char* lp = ++p;
        skip_ws(&lp);

        int has_var = 0;
        if (strncmp(lp, "var", 3) == 0) {
            has_var = 1;
            lp += 3;
        }

        skip_ws(&lp);
        const char* name_start = lp;
        while (*lp && (isalnum(*lp) || *lp == '_')) lp++;
        char varname[64];
        strncpy(varname, name_start, lp - name_start);
        varname[lp - name_start] = 0;

        skip_ws(&lp);
        if (strncmp(lp, "in", 2) != 0) {
            strcat(out, "for(");
            continue;
        }
        lp += 2;
        skip_ws(&lp);

        const char* expr_start = lp;
        int paren_depth = 1;
        while (*lp && paren_depth > 0) {
            if (*lp == '(') paren_depth++;
            else if (*lp == ')') paren_depth--;
            lp++;
        }

        size_t expr_len = (lp - expr_start) - 1;
        char expr[256];
        strncpy(expr, expr_start, expr_len);
        expr[expr_len] = 0;

        skip_ws(&lp);
        const char* body_start = lp;
        char* body = extract_block(&lp);

        iter_counter++;
        char itername[64];
        sprintf(itername, "__iter_%d", iter_counter);

        char expansion[2048];
        snprintf(expansion, sizeof(expansion),
            "{var %s = (%s).iterator();for(;%s.hasNext();){var %s = %s.next();%s}}",
            itername, expr, itername, varname, itername, body);

        strcat(out, expansion);
        free(body);
        p = lp;
    }
    return out;
}

bool has_for_in(const char* code) {
    bool in_single = false;
    bool in_double = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool escaped = false;

    size_t len = strlen(code);
    for (size_t i = 0; i < len; i++) {
        char c = code[i];
        char n = (i + 1 < len) ? code[i + 1] : '\0';

        if (in_line_comment) {
            if (c == '\n') in_line_comment = false;
            continue;
        }

        if (in_block_comment) {
            if (c == '*' && n == '/') {
                in_block_comment = false;
                i++;
            }
            continue;
        }

        if (in_single) {
            if (!escaped && c == '\'') in_single = false;
            escaped = (c == '\\' && !escaped);
            continue;
        }

        if (in_double) {
            if (!escaped && c == '"') in_double = false;
            escaped = (c == '\\' && !escaped);
            continue;
        }

        // Entering comment or string
        if (c == '/' && n == '/') {
            in_line_comment = true;
            i++;
            continue;
        } else if (c == '/' && n == '*') {
            in_block_comment = true;
            i++;
            continue;
        } else if (c == '\'') {
            in_single = true;
            escaped = false;
            continue;
        } else if (c == '"') {
            in_double = true;
            escaped = false;
            continue;
        }

        // Detect "for(... in ...)"
        if (c == 'f' && strncmp(&code[i], "for", 3) == 0 && !isalnum(code[i + 3])) {
            const char *p = &code[i + 3];
            while (*p && isspace(*p)) p++;
            if (*p == '(') {
                const char *q = p + 1;
                while (*q && *q != ')') q++;
                if (*q == ')') {
                    char temp[256];
                    size_t l = q - &code[i] + 1;
                    if (l >= sizeof(temp)) l = sizeof(temp) - 1;
                    strncpy(temp, &code[i], l);
                    temp[l] = '\0';
                    if (strstr(temp, " in ")) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* Top-level preprocessor */
char* preprocessor(const char* src) {
    char* without = strip_macros_and_build_table(src);
    char* expanded = expand_macros(without);
    free(without);

    char* desugared_ops = desugar_operators(expanded);
    free(expanded);

    char* desugared_for = process_for_in(desugared_ops);
    free(desugared_ops);

    
    while(has_for_in(desugared_for)){
        char* temp = process_for_in(desugared_for);
        free(desugared_for);
        desugared_for = temp;
    }

    //printf(desugared_for);
    
    return desugared_for;
}

void compileImport(const char* source) {
    source = preprocessor(source);
    //printf(source);
    initScanner(source);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }
}

ObjFunction* compile(const char* source) {
    source = preprocessor(source);
    //printf(source);
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);


    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        compiler = compiler->enclosing;
    }
}
