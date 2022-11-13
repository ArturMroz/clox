#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "scanner.h"

typedef struct {
    Token cur;
    Token prev;
    bool had_error;
    bool panic_mode;
} Parser;

Parser parser;

static void error_at(Token *tok, const char *msg) {
    if (parser.panic_mode) return;

    parser.panic_mode = true;

    fprintf(stderr, "[line %d] Error", tok->line);
    if (tok->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (tok->type == TOKEN_ERROR) {
        // nothing
    } else {
        fprintf(stderr, "at '%.*s'", tok->len, tok->start);
    }
    fprintf(stderr, ": %s\n", msg);

    parser.had_error = true;
}

static void error(const char *msg) {
    error_at(&parser.prev, msg);
}

static void error_at_current(const char *msg) {
    error_at(&parser.cur, msg);
}

static void advance() {
    parser.prev = parser.cur;

    for (;;) {
        parser.cur = scan_token();
        if (parser.cur.type != TOKEN_ERROR) break;

        error_at_current(parser.cur.start);
    }
}

static void consume(TokenType type, const char *msg) {
    if (parser.cur.type == type) {
        advance();
        return;
    }

    error_at_current(msg);
}

bool compile(const char *source, Chunk *chunk) {
    init_scanner(source);

    parser.had_error = false;
    parser.panic_mode = false;

    advance();
    expression();
    consume(TOKEN_EOF, "Expect end of expression.");
    return !parser.had_error;
}