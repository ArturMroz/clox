#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "object.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token cur;
    Token prev;
    bool had_error;
    bool panic_mode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

Parser parser;
Chunk *compiling_chunk;

static Chunk *current_chunk() {
    return compiling_chunk;
}

// ERROR REPORTING

static void error_at(Token *tok, const char *msg) {
    if (parser.panic_mode) return;

    parser.panic_mode = true;

    fprintf(stderr, "[line %d] Error", tok->line);
    if (tok->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (tok->type == TOKEN_ERROR) {
        // nothing
    } else {
        fprintf(stderr, " at '%.*s'", tok->len, tok->start);
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

// ADVANCING

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

// EMITTING BYTES

static void emit_byte(uint8_t byte) {
    write_chunk(current_chunk(), byte, parser.prev.line);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}

static void emit_return() {
    emit_byte(OP_RETURN);
}

static void end_compiler() {
    emit_return();

#ifdef DEBUG_PRINT_CODE
    if (!parser.had_error) disassemble_chunk(current_chunk(), "code");
#endif
}

static uint8_t make_constant(Value val) {
    int constant = add_constant(current_chunk(), val);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emit_constant(Value val) {
    emit_bytes(OP_CONSTANT, make_constant(val));
}

// EXPRESSIONS

static ParseRule *get_rule(TokenType type);
static void parse_precedence(Precedence precedence);

static void expression() {
    parse_precedence(PREC_ASSIGNMENT);
}

static void grouping() {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number() {
    double val = strtod(parser.prev.start, NULL);
    emit_constant(NUMBER_VAL(val));
}

static void string() {
    emit_constant(OBJ_VAL(copy_string(parser.prev.start + 1, parser.prev.len - 2)));
}

static void unary() {
    TokenType operator_type = parser.prev.type;

    // compile the operand
    parse_precedence(PREC_ASSIGNMENT);

    // emit operator instruction
    switch (operator_type) {
    case TOKEN_MINUS:
        emit_byte(OP_NEGATE);
        break;
    case TOKEN_BANG:
        emit_byte(OP_NOT);
        break;

    default:
        return; // unreachable
    }
}

static void binary() {
    TokenType op_type = parser.prev.type;
    ParseRule *rule   = get_rule(op_type);

    parse_precedence((Precedence)(rule->precedence + 1));

    switch (op_type) {
    case TOKEN_BANG_EQUAL:
        emit_bytes(OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL:
        emit_byte(OP_EQUAL);
        break;
    case TOKEN_GREATER:
        emit_byte(OP_GREATER);
        break;
    case TOKEN_GREATER_EQUAL:
        emit_bytes(OP_LESS, OP_NOT);
        break;
    case TOKEN_LESS:
        emit_byte(OP_LESS);
        break;
    case TOKEN_LESS_EQUAL:
        emit_bytes(OP_GREATER, OP_NOT);
        break;
    case TOKEN_PLUS:
        emit_byte(OP_ADD);
        break;
    case TOKEN_MINUS:
        emit_byte(OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        emit_byte(OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        emit_byte(OP_DIVIDE);
        break;
    default:
        return; // unreachable
    }
}

static void literal() {
    switch (parser.prev.type) {
    case TOKEN_FALSE:
        emit_byte(OP_FALSE);
        break;
    case TOKEN_NIL:
        emit_byte(OP_NIL);
        break;
    case TOKEN_TRUE:
        emit_byte(OP_TRUE);
        break;
    default:
        return; // unreachable
    }
}

// PRECEDENCE

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE      },
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM      },
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM      },
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR    },
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR    },
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE      },
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY  },
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY  },
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE      },
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE      },
    [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE      },
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE      },
    [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE      },
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE      },
};

static ParseRule *get_rule(TokenType type) {
    return &rules[type];
}

static void parse_precedence(Precedence precedence) {
    advance();

    ParseFn prefix_rule = get_rule(parser.prev.type)->prefix;
    if (prefix_rule == NULL) {
        error("Expect expression.");
        return;
    }

    prefix_rule();

    while (precedence <= get_rule(parser.cur.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(parser.prev.type)->infix;
        infix_rule();
    }
}

// MAIN

bool compile(const char *source, Chunk *chunk) {
    init_scanner(source);
    compiling_chunk = chunk;

    parser.had_error  = false;
    parser.panic_mode = false;

    advance();
    expression();
    consume(TOKEN_EOF, "Expect end of expression.");
    end_compiler();
    return !parser.had_error;
}