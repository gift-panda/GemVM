#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

Scanner* getScanner() {
    return &scanner;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
            c == '_';
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAtEnd() {
    return *scanner.current == '\0';
}

static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;

    scanner.previous = token;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static char peek() {
    return *scanner.current;
}

static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}


static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    // A comment goes until the end of the line.
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            case '#':
                if (peekNext() == '!')
                    while (peek() != '\n' && !isAtEnd()) advance();
                else
                    return;
                break;
            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length,
    const char* rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0) {
        return type;
        }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'a': return checkKeyword(2, 3, "tch", TOKEN_CATCH);
                    case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
                }
            }
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'i':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 's': return checkKeyword(2, 0, "", TOKEN_IS);
                    case 'f': return checkKeyword(2, 0, "", TOKEN_IF);
                    case 'm': return checkKeyword(2, 4, "port", TOKEN_IMPORT);
                }
            }
        case 'l': return checkKeyword(1, 5, "ambda", TOKEN_LAMBDA);
        case 'n': 
            if(scanner.current - scanner.start > 1){
                switch(scanner.start[1]){
                    case 'a': return checkKeyword(2, 7, "mespace", TOKEN_NAMESPACE);
                    case 'i': return checkKeyword(2, 1, "l", TOKEN_NIL);
                }
            }
        case 'o':
            return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p':
            if ((scanner.current - scanner.start) == 5 &&
                memcmp(scanner.start, "print", 5) == 0) {
                return TOKEN_PRINT;
                }
            if ((scanner.current - scanner.start) == 7 &&
                memcmp(scanner.start, "println", 7) == 0) {
                return TOKEN_PRINTLN;
                }
            return TOKEN_IDENTIFIER;


        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 't': return checkKeyword(2, 4, "atic", TOKEN_STATIC);
                    case 'u': return checkKeyword(2, 3, "per", TOKEN_SUPER);
                }
            }
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 2, "nc", TOKEN_FUN);
                    case 'i': return checkKeyword(2, 5, "nally", TOKEN_FINALLY);
                }
            }
            break;
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h':
                        if (scanner.current - scanner.start > 2) {
                            switch (scanner.start[2]) {
                                case 'i': return checkKeyword(3, 1, "s", TOKEN_THIS);
                                case 'r': return checkKeyword(3, 2, "ow", TOKEN_THROW);
                            }
                        }
                        return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r':
                        if (scanner.current - scanner.start > 2){
                            switch (scanner.start[2]) {
                                case 'u': return checkKeyword(3, 1, "e", TOKEN_TRUE);
                                case 'y': return checkKeyword(3, 0, "", TOKEN_TRY);
                            }
                        }
                }
            }
            break;
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    // Allow # only at the first position
    if (scanner.start[0] == '#') {
        if (!isAlpha(peek()) && !isDigit(peek())) {
            return errorToken("Invalid identifier after '#'.");
        }
    }

    switch (scanner.start[0]) {
        case '*':
        case '/':
        case '+':
        case '-': return makeToken(TOKEN_IDENTIFIER);
        default: ;
    }

    while (isAlpha(peek()) || isDigit(peek()) || scanner.previous.type == TOKEN_IMPORT && peek() == '.') advance();
    return makeToken(identifierType());
}

bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isOctDigit(char c) {
    return c >= '0' && c <= '7';
}

bool isBinDigit(char c) {
    return c == '0' || c == '1';
}


static Token number() {
    // Handle radix prefixes: 0x (hex), 0o (octal), 0b (binary) //fflush(stdout);
    if (peek() == '0') {
        switch (peekNext()) {
        case 'x': case 'X':
            advance(); // consume '0'
            advance(); // consume 'x'
            while (isHexDigit(peek())) advance();
            return makeToken(TOKEN_HEX_NUMBER);
        case 'o': case 'O':
            advance(); // consume '0'
            advance(); // consume 'o'
            while (isOctDigit(peek())) advance();
            return makeToken(TOKEN_OCTAL_NUMBER);
        case 'b': case 'B':
            advance(); // consume '0'
            advance(); // consume 'b'
            while (isBinDigit(peek())) advance();
            return makeToken(TOKEN_BINARY_NUMBER);
        }
    }

    while (isDigit(peek())) advance();

    if (peek() == '.' && isDigit(peekNext())) {
        advance();
        while (isDigit(peek())) advance();
    }

    if (peek() == 'e' || peek() == 'E') {
        advance(); // consume 'e'
        if (peek() == '+' || peek() == '-') advance(); // optional +/-
        if (!isDigit(peek())) return errorToken("Invalid exponent format");
        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}


static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;

        if (peek() == '\\' && (peekNext() == '"' || peekNext() == '\\')) {
            advance(); // skip '\'
            advance(); // skip '"'
        } else {
            advance();
        }
    }


    if (isAtEnd()) return errorToken("Unterminated string.");

    advance();
    return makeToken(TOKEN_STRING);
}

Token scanToken() {
    skipWhitespace();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c) || c == '#') return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case '[': return makeToken(TOKEN_LEFT_BRACKET);
        case ']': return makeToken(TOKEN_RIGHT_BRACKET);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': if(match('-')) return makeToken(TOKEN_DECRE); else return makeToken(TOKEN_MINUS);
        case '+': if(match('+')) return makeToken(TOKEN_INCRE); else return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);
        case '%': return makeToken(TOKEN_PERCENT);
        case '\\': return makeToken(TOKEN_INS);
        case ':': if (match(':')) return makeToken(TOKEN_DOUBLE_COLON); else return makeToken(TOKEN_COLON);
        case '!': return makeToken(
            match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return makeToken(
            match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return makeToken(
            match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return makeToken(
            match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string();

    }

    return errorToken("Unexpected character.");
}
