#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char *start;
    const char *cur;
    int line;
} Scanner;

Scanner scanner;

void init_scanner(const char *source) {
    scanner.start = source;
    scanner.cur   = source;
    scanner.line  = 1;
}

static Token make_token(TokenType type) {
    Token tok = {
        .type  = type,
        .start = scanner.start,
        .len   = (int)(scanner.cur - scanner.start),
        .line  = scanner.line,
    };

    return tok;
}

static Token error_token(const char *msg) {
    Token tok = {
        .type  = TOKEN_ERROR,
        .start = msg,
        .len   = (int)strlen(msg),
        .line  = scanner.line,
    };

    return tok;
}

static bool is_at_end() {
    return *scanner.cur == '\0';
}

static bool is_digit(char c) {
    return '0' <= c && c <= '9';
}

static bool is_alpha(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static char advance() {
    scanner.cur++;
    return scanner.cur[-1];
}

static char peek() {
    return *scanner.cur;
}

static char peek_next() {
    if (is_at_end()) return '\0';
    return scanner.cur[1];
}

static bool match(char expected) {
    if (is_at_end()) return false;
    if (*scanner.cur != expected) return false;

    scanner.cur++;
    return true;
}

static void skip_whitespace() {
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

        // treating comments as whitespace, sue me
        case '/':
            if (peek_next() == '/') {
                // comment goes until the end of line
                while (peek() != '\n' && !is_at_end()) {
                    advance();
                }
            } else {
                return;
            }

        default:
            return;
        }
    }
}

static Token number() {
    while (is_digit(peek())) {
        advance();
    }

    // look for a fractional part
    if (peek() == '.' && is_digit(peek_next())) {
        // consume the dot
        advance();

        while (is_digit(peek())) {
            advance();
        }
    }

    return make_token(TOKEN_NUMBER);
}

static Token string() {
    while (peek() != '"' && !is_at_end()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (is_at_end()) return error_token("Unterminated string.");

    // closing quote
    advance();
    return make_token(TOKEN_STRING);
}

static TokenType check_keyword(int start, int len, const char *rest, TokenType type) {
    // 1. lexeme needs to be exactly as long as keyword
    // 2. remaining characters must match exactly
    if ((scanner.cur - scanner.start == start + len) && memcmp(scanner.start + start, rest, len) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifier_type() {
    switch (scanner.start[0]) {
    case 'a':
        return check_keyword(1, 2, "nd", TOKEN_AND);
    case 'c':
        return check_keyword(1, 4, "lass", TOKEN_CLASS);
    case 'e':
        return check_keyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
        if (scanner.cur - scanner.start > 1) { // check if there even is a 2nd letter
            switch (scanner.start[1]) {
            case 'a':
                return check_keyword(2, 3, "lse", TOKEN_FALSE);
            case 'o':
                return check_keyword(2, 1, "r", TOKEN_FOR);
            case 'u':
                return check_keyword(2, 1, "n", TOKEN_FUN);
            }
        }
        break;
    case 'i':
        return check_keyword(1, 1, "f", TOKEN_IF);
    case 'n':
        return check_keyword(1, 2, "il", TOKEN_NIL);
    case 'o':
        return check_keyword(1, 1, "r", TOKEN_OR);
    case 'p':
        return check_keyword(1, 4, "rint", TOKEN_PRINT);
    case 'r':
        return check_keyword(1, 5, "eturn", TOKEN_RETURN);
    case 's':
        return check_keyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
        if (scanner.cur - scanner.start > 1) {
            switch (scanner.start[1]) {
            case 'h':
                return check_keyword(2, 2, "is", TOKEN_THIS);
            case 'r':
                return check_keyword(2, 2, "ue", TOKEN_TRUE);
            }
        }
        break;
    case 'v':
        return check_keyword(1, 2, "ar", TOKEN_VAR);
    case 'w':
        return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    while (is_alpha(peek()) || is_digit(peek())) {
        advance();
    }

    return make_token(identifier_type());
}

Token scan_token() {
    skip_whitespace();

    // since scan_token scans a complete token, we know we are at the beggining
    // of a new token when we enter the function
    scanner.start = scanner.cur;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();

    switch (c) {
    case '(':
        return make_token(TOKEN_LEFT_PAREN);
    case ')':
        return make_token(TOKEN_RIGHT_PAREN);
    case '{':
        return make_token(TOKEN_LEFT_BRACE);
    case '}':
        return make_token(TOKEN_RIGHT_BRACE);
    case ';':
        return make_token(TOKEN_SEMICOLON);
    case ',':
        return make_token(TOKEN_COMMA);
    case '.':
        return make_token(TOKEN_DOT);
    case '-':
        return make_token(TOKEN_MINUS);
    case '+':
        return make_token(TOKEN_PLUS);
    case '/':
        return make_token(TOKEN_SLASH);
    case '*':
        return make_token(TOKEN_STAR);

    case '!':
        return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
        return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
        return make_token(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
        return make_token(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

    case '"':
        return string();

    default:
        if (is_alpha(c)) return identifier();
        if (is_digit(c)) return number();

        return error_token("Unexpected character.");
    }
}