#ifndef CSPYDR_AST_H
#define CSPYDR_AST_H

#include "stdbool.h"

#include "../list.h"
#include "../lexer/token.h"
#include "../config.h"

#include <stdint.h>
#include <stdio.h>

typedef struct AST_NODE_STRUCT ASTNode_T;
typedef struct AST_IDENTIFIER_STRUCT ASTIdentifier_T;
typedef struct AST_TYPE_STRUCT ASTType_T;
typedef struct AST_OBJ_STRUCT ASTObj_T;

typedef enum {
    ND_NOOP,

    // identifiers
    ND_ID,      // x

    // literals
    ND_INT,     // 0
    ND_LONG,
    ND_ULONG, 
    ND_FLOAT,   // 0.1
    ND_DOUBLE,
    ND_BOOL,    // true, false
    ND_CHAR,    // 'x'
    ND_STR,     // "..."
    ND_NIL,     // nil

    ND_ARRAY,   // [2, 4, ...]
    ND_STRUCT,  // {3, 4, ...}

    // operators
    ND_ADD,     // +
    ND_SUB,     // -
    ND_MUL,     // *
    ND_DIV,     // /
    ND_MOD,     // %

    ND_NEG,     // unary -
    ND_BIT_NEG, // unary ~
    ND_NOT,     // unary !
    ND_REF,     // unary &
    ND_DEREF,   // unary *

    ND_EQ,      // ==
    ND_NE,      // !=
    ND_GT,      // >
    ND_GE,      // >=
    ND_LT,      // <
    ND_LE,      // <=

    ND_AND, // &&
    ND_OR,  // ||

    ND_LSHIFT,  // <<
    ND_RSHIFT,  // >>
    ND_XOR,     // ^
    ND_BIT_OR,  // |
    ND_BIT_AND, // &

    ND_INC,     // ++
    ND_DEC,     // --

    ND_CLOSURE, // ()
    ND_ASSIGN,  // x = y

    ND_MEMBER,  // x.y
    ND_CALL,    // x(y, z)
    ND_INDEX,   // x[y]
    ND_CAST,    // x:i32

    ND_SIZEOF,  // sizeof x
    ND_ALIGNOF, // alignof x

    ND_PIPE,    // x |> y
    ND_HOLE,    // _
    ND_LAMBDA,  // |x: i32| => {}

    ND_ELSE_EXPR, // x else y

    ND_TYPE_EXPR, // type expressions like: "(type) T == U" or "(type) reg_class(T)"

    // statements
    ND_BLOCK,   // {...}
    ND_IF,      // if x {}
    ND_TERNARY, // if x => y <> z
    ND_LOOP,    // loop {}
    ND_WHILE,   // while x {}
    ND_FOR,     // for let i: i32 = 0; i < x; i++ {}
    ND_MATCH,   // match x {}
    ND_MATCH_TYPE, // match (type) T {}
    ND_CASE,    // x => {} !!only in match statements!!
    ND_CASE_TYPE, // i32 => {}
    ND_RETURN,  // ret x;
    ND_EXPR_STMT, // "executable" expressions
    ND_BREAK,     // break;
    ND_CONTINUE,  // continue;
    ND_LEN,       // len x
    ND_USING,     // using x::y
    ND_WITH,      // with x = y {}
    ND_STRUCT_MEMBER,  // struct members

    ND_ASM, // inline assembly

    ND_KIND_LEN
} ASTNodeKind_T;

typedef enum {
    TY_I8,
    TY_I16,
    TY_I32,
    TY_I64,

    TY_U8,
    TY_U16,
    TY_U32,
    TY_U64,

    TY_F32,
    TY_F64,
    TY_F80,

    TY_BOOL,
    TY_VOID,
    TY_CHAR,

    TY_PTR,
    TY_ARR,
    TY_STRUCT,
    TY_ENUM,

    TY_FN,

    TY_UNDEF,
    TY_TYPEOF,
    
    TY_KIND_LEN
} ASTTypeKind_T;

typedef enum {
    OBJ_GLOBAL,
    OBJ_LOCAL,
    OBJ_FUNCTION,
    OBJ_FN_ARG,
    OBJ_TYPEDEF,
    OBJ_NAMESPACE,
    OBJ_ENUM_MEMBER,

    OBJ_LAMBDA, // lambda implementation used internally

    OBJ_KIND_LEN
} ASTObjKind_T;

struct AST_NODE_STRUCT
{
    ASTNodeKind_T kind;
    Token_T* tok;

    ASTType_T* data_type;

    // id
    ASTIdentifier_T* id;

    // literals
    union {
        i32 int_val;
        i64 offset;
        i64 long_val;
        u64 ulong_val;
        f32 float_val;
        f64 double_val;
        bool bool_val;
        char* str_val;

        TokenType_T cmp_kind; // kind of type comparisons
    };

    union {
        // expressions
        struct {
            ASTNode_T* left;
            ASTNode_T* right;
        };

        // type comparisons
        struct {
            ASTType_T* l_type;
            ASTType_T* r_type;
        };
        
        // condition for loop, match, case and if statements
        ASTNode_T* condition;
    };

    union {
        // if statement
        struct {
            ASTNode_T* if_branch;
            ASTNode_T* else_branch;
        };

        // loop statement
        struct {
            ASTNode_T* body;
            ASTNode_T* init_stmt;
        };
        
        // block statement
        List_T* stmts;  // list of ASTNode_Ts

        // references
        ASTObj_T* called_obj;
        ASTObj_T* referenced_obj;

        ASTNode_T* return_val; // return

        // match statement
        ASTNode_T* default_case;
    };

    union {
        // loop, block statement
        List_T* locals; // list of ASTObj_Ts

        // calls, array literals
        List_T* args;   // list of ASTNode_Ts

        // match statement
        List_T* cases;           // list of ASTNode_Ts

        // with statement, single object
        ASTObj_T* obj;
    };

    union {
        // call
        ASTObj_T* return_buffer;

        // with statement
        ASTObj_T* exit_fn;

        // lambda
        ASTObj_T* return_ptr;
    };

    union {
        ASTNode_T* expr;
        ASTNode_T* call;
        
        // sizeof
        ASTType_T* the_type;

        // lambda
        ASTObj_T* stack_ptr;
    };

    union { 
        bool is_default_case : 1; // case
        bool pass_by_stack   : 1; // call
        bool from_back       : 1; // index
    };

    // assignment expression
    bool is_assigning    : 1;
    bool is_initializing : 1;
    bool result_ignored  : 1;

    // expression statement
    bool is_constant : 1;
    
} __attribute__((packed));

struct AST_IDENTIFIER_STRUCT
{
    ASTObjKind_T kind; // kind of the object, which the name is referring to
    Token_T* tok;

    char callee[__CSP_MAX_TOKEN_SIZE];
    
    ASTIdentifier_T* outer;

    bool global_scope : 1;
} __attribute__((packed));

struct AST_TYPE_STRUCT 
{
    ASTTypeKind_T kind;
    Token_T* tok;

    i32 size;
    i32 align;

    ASTType_T* base;
    ASTIdentifier_T* id;

    union {
        struct {
            bool is_primitive : 1;
            bool is_constant  : 1;
            bool is_fn        : 1;
            bool is_union     : 1;
            bool is_vla       : 1;
            bool is_variadic  : 1;
        };
        u8 flags;
    };

    union {
        // functions
        List_T* arg_types;  // list of ASTType_Ts
        // arrays
        ASTNode_T* num_indices;

        // enums, structs
        List_T* members;    // list of ASTNode_Ts
    };
} __attribute__((packed));

struct AST_OBJ_STRUCT 
{
    ASTObjKind_T kind;
    Token_T* tok;

    ASTIdentifier_T* id;
    i32 offset;
    i32 stack_size;

    // variables
    union {
        struct {
            bool is_constant    : 1;
            bool is_extern      : 1;
            bool is_extern_c    : 1;
            bool referenced     : 1;
            bool is_entry_point : 1;
            bool no_return      : 1;
            bool ignore_unused  : 1;
        };
        u8 flags;
    };

    ASTType_T* data_type;
    ASTNode_T* value;
    List_T* args;
    List_T* objs;

    // functions
    ASTType_T* return_type;
    ASTNode_T* body;
    ASTObj_T* alloca_size;
    ASTObj_T* alloca_bottom;
    ASTObj_T* va_area;
    ASTObj_T* return_ptr;
} __attribute__((packed));

typedef struct AST_EXIT_FN_HANDLE_STRUCT
{
    Token_T* tok;
    ASTObj_T* fn;
    ASTType_T* type;
} ASTExitFnHandle_T;

typedef struct AST_PROG_STRUCT
{
    const char* main_file_path;
    const char* target_binary;

    List_T* imports;
    List_T* tuple_structs;
    List_T* type_exit_fns;

    ASTObj_T* entry_point;

    List_T* objs;   // list of ASTObj_Ts
} ASTProg_T;

ASTNode_T* init_ast_node(ASTNodeKind_T kind, Token_T* tok);
ASTType_T* init_ast_type(ASTTypeKind_T kind, Token_T* tok);

ASTIdentifier_T* init_ast_identifier(Token_T* tok, char* callee);

ASTObj_T* init_ast_obj(ASTObjKind_T kind, Token_T* tok);

void init_ast_prog(ASTProg_T* prog, const char* main_file_path, const char* target_binary, List_T* imports);

const char* obj_kind_to_str(ASTObjKind_T kind);
const char* type_kind_to_str(ASTTypeKind_T kind);
char* ast_type_to_str(char* dest, ASTType_T* ty, size_t size);
char* ast_id_to_str(char* dest, ASTIdentifier_T* id, size_t size);

void merge_ast_progs(ASTProg_T* dest, ASTProg_T* src);

extern const ASTIdentifier_T empty_id;

#endif