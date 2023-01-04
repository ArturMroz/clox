#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
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
    bool is_captured;
} Local;

typedef struct {
    uint8_t index;
    bool is_local;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_METHOD,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int local_count;
    Upvalue upvalues[UINT8_COUNT];
    int scope_depth;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
} ClassCompiler;

// globals to make things shorter: it's a toy singlethreaded compiler anyway
Parser parser;
Chunk *compiling_chunk;
Compiler *current            = NULL;
ClassCompiler *current_class = NULL;

static Chunk *cur_chunk() {
    return &current->function->chunk;
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
    write_chunk(cur_chunk(), byte, parser.prev.line);
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
    return cur_chunk()->len - 2;
}

static void emit_loop(int loop_start) {
    emit_byte(OP_LOOP);

    int offset = cur_chunk()->len - loop_start + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emit_byte((offset >> 8) & 0xff);
    emit_byte(offset & 0xff);
}

static void emit_return() {
    emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
}

static ObjFunction *end_compiler() {
    emit_return();

    ObjFunction *fn = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.had_error) {
        disassemble_chunk(cur_chunk(), fn->name != NULL ? fn->name->chars : "<script>");
    }
#endif

    current = current->enclosing;

    return fn;
}

static void begin_scope() {
    current->scope_depth++;
}

static void end_scope() {
    current->scope_depth--;

    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        if (current->locals[current->local_count - 1].is_captured) {
            emit_byte(OP_CLOSE_UPVALUE);
        } else {
            emit_byte(OP_POP);
        }

        current->local_count--;
    }
}

static uint8_t make_constant(Value val) {
    int constant = add_constant(cur_chunk(), val);
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
    // -2 to adjust for the bytecode for the jump offset itself
    int jump = cur_chunk()->len - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    cur_chunk()->code[offset]     = (jump >> 8) & 0xff;
    cur_chunk()->code[offset + 1] = jump & 0xff;
}

static void init_compiler(Compiler *compiler, FunctionType type) {
    compiler->enclosing   = current;
    compiler->function    = NULL;
    compiler->type        = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function    = new_function(); // assigning to function again, GC-related paranoia

    current = compiler;

    if (type != TYPE_SCRIPT) {
        current->function->name = copy_string(parser.prev.start, parser.prev.len);
    }

    Local *local       = &current->locals[current->local_count++];
    local->depth       = 0;
    local->is_captured = false;

    if (type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.len   = 4;
    } else {
        local->name.start = ""; // assign empty name so the user cannot refer to it
        local->name.len   = 0;
    }
}

// some forward declarations

static ParseRule *get_rule(TokenType type);
static void parse_precedence(Precedence precedence);
static void statement();
static void declaration();
static void var_declaration();

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
    if (current->local_count == UINT8_COUNT) {
        error("Too many local variables.");
        return;
    }

    Local *local       = &current->locals[current->local_count++];
    local->name        = name;
    local->depth       = -1;
    local->is_captured = false;
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

static void mark_initialised() {
    if (current->scope_depth == 0) return;

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

static uint8_t argument_list() {
    uint8_t arg_count = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (arg_count == 255) {
                error("Can't have more than 255 arguments.");
            }
            arg_count++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return arg_count;
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

static int add_upvalue(Compiler *compiler, uint8_t index, bool is_local) {
    int upvalue_count = compiler->function->upvalue_count;

    // check if we haven't captured upvalue for this variable already
    for (int i = 0; i < upvalue_count; i++) {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local) {
            return i;
        }
    }

    if (upvalue_count == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index    = index;

    return compiler->function->upvalue_count++;
}

static int resolve_upvalue_idx(Compiler *compiler, Token *name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolve_local_idx(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolve_upvalue_idx(compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(compiler, (uint8_t)upvalue, false);
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
    } else if ((arg = resolve_upvalue_idx(current, &name)) != -1) {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
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

static void this_(bool can_assign) {
    if (current_class == NULL) {
        error("Can't use 'this' outside of a class.");
        return;
    }

    variable(false);
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

static void call(bool can_assign) {
    uint8_t arg_count = argument_list();
    emit_bytes(OP_CALL, arg_count);
}

static void dot(bool can_assign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifier_constant(&parser.prev);

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(OP_SET_PROPERTY, name);
    } else {
        emit_bytes(OP_GET_PROPERTY, name);
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

static void and_(bool can_assign) {
    int end_jump = emit_jump(OP_JUMP_IF_FALSE);

    emit_byte(OP_POP);
    parse_precedence(PREC_AND);

    patch_jump(end_jump);
}

static void or_(bool can_assign) {
    int else_jump = emit_jump(OP_JUMP_IF_FALSE);
    int end_jump  = emit_jump(OP_JUMP);

    patch_jump(else_jump);
    emit_byte(OP_POP);

    parse_precedence(PREC_OR);
    patch_jump(end_jump);
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

static void return_statement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
        return;
    }

    if (match(TOKEN_SEMICOLON)) {
        emit_return();
        return;
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emit_byte(OP_RETURN);
}

static void while_statement() {
    int loop_start = cur_chunk()->len;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();

    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);
}

static void for_statement() {
    begin_scope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // initialiser
    if (match(TOKEN_SEMICOLON)) {
        // empty initialiser
    } else if (match(TOKEN_VAR)) {
        var_declaration();
    } else {
        expression_statement();
    }

    int loop_start = cur_chunk()->len;

    // condition expression to exit the loop
    int exit_jump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // jump out of the loop if the condition is false
        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP); // pop the condition value from the stack
    }

    // increment clause
    if (!match(TOKEN_RIGHT_PAREN)) {
        int body_jump       = emit_jump(OP_JUMP);
        int increment_start = cur_chunk()->len;

        expression();

        // discard the value from the stack as we only care about side effects in increment clause
        emit_byte(OP_POP);

        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emit_loop(loop_start);
        loop_start = increment_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != -1) {
        patch_jump(exit_jump);
        emit_byte(OP_POP); // condition
    }

    end_scope();
}

static void if_statement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();

    int else_jump = emit_jump(OP_JUMP);

    patch_jump(then_jump);
    emit_byte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patch_jump(else_jump);
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        print_statement();
    } else if (match(TOKEN_IF)) {
        if_statement();
    } else if (match(TOKEN_RETURN)) {
        return_statement();
    } else if (match(TOKEN_WHILE)) {
        while_statement();
    } else if (match(TOKEN_FOR)) {
        for_statement();
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

    // bail if we're in local scope - at runtime locals aren't loooked up by name
    if (current->scope_depth > 0) return 0;

    return identifier_constant(&parser.prev);
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

static void function(FunctionType type) {
    Compiler compiler;
    init_compiler(&compiler, type);
    begin_scope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                error_at_current("Can't have more than 255 parameters.");
            }
            uint8_t constant = parse_variable("Expect parameter name.");
            define_variable(constant);
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction *function = end_compiler();
    emit_bytes(OP_CLOSURE, make_constant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalue_count; i++) {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

static void method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifier_constant(&parser.prev);

    FunctionType type = TYPE_METHOD;
    function(type);

    emit_bytes(OP_METHOD, constant);
}

static void class_declaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token class_name      = parser.prev;
    uint8_t name_constant = identifier_constant(&parser.prev);

    declare_variable();

    emit_bytes(OP_CLASS, name_constant);
    define_variable(name_constant);

    ClassCompiler class_compiler;
    class_compiler.enclosing = current_class;
    current_class            = &class_compiler;

    named_variable(class_name, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        method();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emit_byte(OP_POP); // class_name

    current_class = current_class->enclosing;
}

static void fun_declaration() {
    uint8_t global = parse_variable("Expect function name.");
    mark_initialised();
    function(TYPE_FUNCTION);
    define_variable(global);
}

static void declaration() {
    if (match(TOKEN_CLASS)) {
        class_declaration();
    } else if (match(TOKEN_FUN)) {
        fun_declaration();
    } else if (match(TOKEN_VAR)) {
        var_declaration();
    } else {
        statement();
    }

    if (parser.panic_mode) sync();
}

// PRECEDENCE

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL      },
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_DOT]           = {NULL,     dot,    PREC_CALL      },
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
    [TOKEN_AND]           = {NULL,     and_,   PREC_NONE      },
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE      },
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE      },
    [TOKEN_OR]            = {NULL,     or_,    PREC_NONE      },
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE      },
    [TOKEN_THIS]          = {this_,    NULL,   PREC_NONE      },
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

ObjFunction *compile(const char *source) {
    init_scanner(source);

    Compiler compiler;
    init_compiler(&compiler, TYPE_SCRIPT);

    // compiling_chunk = chunk;

    parser.had_error  = false;
    parser.panic_mode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction *function = end_compiler();
    return parser.had_error ? NULL : function;
}

void mark_compiler_roots() {
    Compiler *compiler = current;
    while (compiler != NULL) {
        mark_object((Obj *)compiler->function);
        compiler = compiler->enclosing;
    }
}