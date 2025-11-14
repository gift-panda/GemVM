#ifndef clox_scanner_h
#define clox_scanner_h

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_INCRE, TOKEN_DECRE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS, TOKEN_INS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, TOKEN_PERCENT,
    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_DOUBLE_COLON,
    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER, TOKEN_BINARY_NUMBER,
    TOKEN_OCTAL_NUMBER, TOKEN_HEX_NUMBER,
    // Keywords.
    TOKEN_AND, TOKEN_CLASS, TOKEN_STATIC, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR, TOKEN_LAMBDA,
    TOKEN_PRINT, TOKEN_PRINTLN, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS, TOKEN_IS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE, TOKEN_THROW, TOKEN_IMPORT, TOKEN_NAMESPACE,
    TOKEN_TRY, TOKEN_CATCH, TOKEN_FINALLY, TOKEN_OPERATOR, TOKEN_BREAK, TOKEN_CONTINUE,

    TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

typedef struct {
    const char* start;
    const char* current;
    int line;
    Token previous;

    const char* prevstart;
    const char* prevcurrent;
    int prevline;
} Scanner;

void initScanner(const char* source);
Token scanToken();
Token returnToken();

Scanner* getScanner();


#endif
