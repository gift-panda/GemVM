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
} LoopContext;

static LoopContext loopStack[MAX_LOOP_DEPTH];
static int loopDepth = 0;
static int continueJumpOffset = -1;

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
    TYPE_INITIALIZER,
    TYPE_STATIC_METHOD,
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

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    return constant;
}

static void emitConstant(Value value) {
    int constant = makeConstant(value);
    emitByte(OP_CONSTANT);
    emitByte(constant);
}

static void patchJump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
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
    if (type != TYPE_FUNCTION) {
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
static uint8_t identifierConstant(Token* name);

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
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
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
    uint8_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
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
    uint8_t name = identifierConstant(&parser.previous);

    namedVariable(syntheticToken("this"), false);
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_SUPER_INVOKE, name);
        emitByte(argCount);
    } else {
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_GET_SUPER, name);
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
    [TOKEN_INS]       = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable,     NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
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

static uint8_t identifierConstant(Token* name) {
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

static uint8_t parseVariable(const char* errorMessage) {
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

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
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

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);

    compiler.function->name =
      copyString(parser.previous.start,
                 parser.previous.length);

    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }


    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    int closureConstant = makeConstant(OBJ_VAL(function));

    // Emit OP_CLOSURE (pushes the closure on stack)
    emitBytes(OP_CLOSURE, closureConstant);

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
    uint8_t constant = identifierConstant(&parser.previous);

    FunctionType type = makeStatic? TYPE_STATIC_METHOD: TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }

    if (makeStatic && type == TYPE_INITIALIZER) {
        error("Initializer cannot be static..");
    }
    function(type);
    if (makeStatic)
        emitBytes(OP_STATIC_METHOD, constant);
    else
        emitBytes(OP_METHOD, constant);
}

static Token syntheticToken(const char* text) {
    Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

static void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    emitBytes(OP_CLASS, nameConstant);
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

    namedVariable(className, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (match(TOKEN_VAR)) {
            uint8_t global = parseVariable("Expect variable name.");

            if (match(TOKEN_EQUAL)) {
                expression();
            } else {
                emitByte(OP_NIL);
            }
            consume(TOKEN_SEMICOLON,
                    "Expect ';' after variable declaration.");

            emitBytes(OP_STATIC_VAR, global);
        }
        else {
            match(TOKEN_OPERATOR);
            method();
        }
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OP_POP);

    if (classCompiler.hasSuperclass) {
        endScope();
    }

    currentClass = currentClass->enclosing;
}


static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    emitByte(OP_DISPATCH);
    defineVariable(global);
}

static void varDeclaration() {
    uint8_t global = parseVariable("Expect variable name.");

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
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
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
        error("Cannot use 'break' outside of a loop.");
        return;
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");

    int jump = emitJump(OP_JUMP);

    LoopContext* loop = &loopStack[loopDepth - 1];
    loop->breakJumpOffsets[loop->breakCount++] = jump;
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

    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
    printf("continue statement to: %d\n", continueJumpOffset);
    emitLoop(continueJumpOffset);
}

char* getWindowText() {
    char* buf = malloc(Window_gem_len + 1);
    if (!buf) return NULL;

    memcpy(buf, Window_gem, Window_gem_len);
    buf[Window_gem_len] = '\0';
    return buf;
}

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

        char* source;
        if (memcmp(file, "Window", 6) == 0) {
            source = getWindowText();
        }
        else{
            source = loadModuleFile(file);
            if (source == NULL) {
                exit(70);
            }
        }

        Scanner* sc = getScanner();

        //Storing state
        int prevLine = sc->line;
        const char* prevStart = sc->start;
        const char* prevCurrent = sc->current;

        ObjFunction* function = compile(source);

        //Restoring state
        sc->current = prevCurrent;
        sc->start = prevStart;
        sc->line = prevLine;

        function->name = fileName;
        int constant = makeConstant(OBJ_VAL(function));
        emitBytes(OP_CLOSURE, constant);
        emitBytes(OP_CALL, 0);

        advance();
        emitByte(OP_POP);

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
    }else {
        expressionStatement();
    }
}

ObjFunction* compile(const char* source) {
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
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
