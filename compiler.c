#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef void (*ParseFn)(bool can_assign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
} Local;

typedef struct {
    Local locals[UINT8_MAX + 1];
    int local_count;
    int scope_depth;
} Compiler;

// globals to make things shorter: it's a toy singlethreaded compiler anyway
Parser parser;
Chunk *compiling_chunk;
Compiler *current = NULL;

static Chunk *current_chunk() {
    return compiling_chunk;
}

// ERROR REPORTING

static void error_at(Token *tok, const char *msg) {
    // don't log more errors if we're already panicking
    if (parser.panic_mode) return;

    parser.panic_mode = true;
    parser.had_error  = true;

    fprintf(stderr, "[line %d] Error", tok->line);
    if (tok->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (tok->type == TOKEN_ERROR) {
        // nothing
    } else {
        fprintf(stderr, " at '%.*s'", tok->len, tok->start);
    }
    fprintf(stderr, ": %s\n", msg);
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

static bool check(TokenType type) {
    return parser.cur.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;

    advance();
    return true;
}

// EMITTING BYTES

static void emit_byte(uint8_t byte) {
    write_chunk(current_chunk(), byte, parser.prev.line);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2) {
    emit_byte(byte1);
    emit_byte(byte2);
}

static int emit_jump(uint8_t instruction) {
    emit_byte(instruction);
    // placeholder values to be patched later
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->len - 2;
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

static void begin_scope() {
    current->scope_depth++;
}

static void end_scope() {
    current->scope_depth--;

    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth >
               current->scope_depth) {
        emit_byte(OP_POP);
        current->local_count--;
    }
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

static void patch_jump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = current_chunk()->len - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    current_chunk()->code[offset]     = (jump >> 8) & 0xff;
    current_chunk()->code[offset + 1] = jump & 0xff;
}

static void init_compiler(Compiler *compiler) {
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    current               = compiler;
}

// some forward declarations

static ParseRule *get_rule(TokenType type);
static void parse_precedence(Precedence precedence);
static void statement();
static void declaration();

// EXPRESSIONS

static void expression() {
    parse_precedence(PREC_ASSIGNMENT);
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void grouping(bool can_assign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool can_assign) {
    double val = strtod(parser.prev.start, NULL);
    emit_constant(NUMBER_VAL(val));
}

static void string(bool can_assign) {
    emit_constant(OBJ_VAL(copy_string(parser.prev.start + 1, parser.prev.len - 2)));
}

static uint8_t identifier_constant(Token *name) {
    // Global variables are looked up by name at runtime. That means the VM
    // needs access to the name. A whole string is too big to stuff into the
    // bytecode stream as an operand. Instead, we store the string in the
    // constant table and the instruction then refers to the name by its index.
    return make_constant(OBJ_VAL(copy_string(name->start, name->len)));
}

static bool identifiers_are_equal(Token *a, Token *b) {
    if (a->len != b->len) return false;
    return memcmp(a->start, b->start, a->len) == 0;
}

static void add_local(Token name) {
    if (current->local_count == UINT8_MAX + 1) {
        error("Too many local variables.");
        return;
    }

    Local *local = &current->locals[current->local_count++];
    local->name  = name;
    local->depth = -1;
}

static void declare_variable() {
    if (current->scope_depth == 0) return;

    Token *name = &parser.prev;

    // prevent redeclaring a variable inside the same scope
    for (int i = current->local_count - 1; i >= 0; i--) {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth) {
            break;
        }

        if (identifiers_are_equal(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    add_local(*name);
}

static int resolve_local_idx(Compiler *compiler, Token *name) {
    // take it from behind so we ensure we find last declared variable as we support shadowing
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (identifiers_are_equal(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static void named_variable(Token name, bool can_assign) {
    uint8_t get_op, set_op;
    int arg = resolve_local_idx(current, &name);

    if (arg == -1) {
        arg    = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    } else {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(set_op, (uint8_t)arg);
    } else {
        emit_bytes(get_op, (uint8_t)arg);
    }
}

static void variable(bool can_assign) {
    named_variable(parser.prev, can_assign);
}

static void unary(bool can_assign) {
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

static void binary(bool can_assign) {
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

static void literal(bool can_assign) {
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

// STATEMENTS

static void expression_statement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emit_byte(OP_POP);
}

static void print_statement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emit_byte(OP_PRINT);
}

static void if_statement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    statement();

    patch_jump(then_jump);
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        print_statement();
    } else if (match(TOKEN_IF)) {
        if_statement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        begin_scope();
        block();
        end_scope();
    } else {
        expression_statement();
    }
}

// recover from panic mode by fast fwding until statement's end
static void sync() {
    parser.panic_mode = false;

    while (parser.cur.type != TOKEN_EOF) {
        if (parser.prev.type == TOKEN_SEMICOLON) return;

        switch (parser.cur.type) {
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;

        default:; // do nothing, purposefully
        }

        advance();
    }
}

static uint8_t parse_variable(const char *err_msg) {
    consume(TOKEN_IDENTIFIER, err_msg);
    declare_variable();

    // bail if we're in olcal scope - at runtime locals aren't loooked up by name
    if (current->scope_depth > 0) return 0;

    return identifier_constant(&parser.prev);
}

static void mark_initialised() {
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global) {
    if (current->scope_depth > 0) {
        mark_initialised();
        // locals are already on the stack, there's nothing else for us to do here
        return;
    }

    emit_bytes(OP_DEFINE_GLOBAL, global);
}

static void var_declaration() {
    uint8_t global = parse_variable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        // desugar 'var a;' into 'var a = nil;'
        emit_byte(OP_NIL);
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    define_variable(global);
}

static void declaration() {
    if (match(TOKEN_VAR)) {
        var_declaration();
    } else {
        statement();
    }

    if (parser.panic_mode) sync();
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
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE      },
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

    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(parser.cur.type)->precedence) {
        advance();
        ParseFn infix_rule = get_rule(parser.prev.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

// MAIN

bool compile(const char *source, Chunk *chunk) {
    init_scanner(source);

    Compiler compiler;
    init_compiler(&compiler);

    compiling_chunk = chunk;

    parser.had_error  = false;
    parser.panic_mode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    end_compiler();
    return !parser.had_error;
}