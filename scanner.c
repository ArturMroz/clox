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
    scanner.cur = source;
    scanner.line = 1;
}

static bool is_at_end() {
    return *scanner.cur == '\0';
}

static Token make_token(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.len = (int)(scanner.cur - scanner.start);
    token.line = scanner.line;

    return token;
}

static Token error_token(const char *msg) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = msg;
    token.len = (int)strlen(msg);
    token.line = scanner.line;

    return token;
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
                while (peek() != '\n' && !is_at_end())
                    advance();
            } else {
                return;
            }

        default:
            return;
        }
    }
}

static bool is_digit(char c) {
    return '0' <= c && c <= '9';
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

static bool match(char expected) {
    if (is_at_end()) return false;
    if (*scanner.cur != expected) return false;

    scanner.cur++;
    return true;
}

Token scan_token() {
    skip_whitespace();

    // since scan_token scans a complete token, we know we are at the beggining
    // of a new token when we enter the function
    scanner.start = scanner.cur;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();

    if (is_digit(c)) return number();

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
    }

    return error_token("Unexpected character.");
}