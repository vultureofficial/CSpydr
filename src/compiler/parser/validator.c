#include "validator.h"
#include "ast/ast_iterator.h"
#include "ast/types.h"
#include "list.h"
#include "error/error.h"
#include "io/log.h"
#include "optimizer/constexpr.h"
#include "toolchain.h"
#include "ast/types.h"
#include "ast/ast.h"
#include "codegen/codegen_utils.h"
#include "lexer/token.h"
#include "mem/mem.h"
#include "parser/parser.h"
#include "parser/utils.h"
#include "globals.h"

#include <stdarg.h>
#include <string.h>
#include <math.h>

#define GET_VALIDATOR(va) Validator_T* v = va_arg(va, Validator_T*)

// validator structs
typedef struct VALIDATOR_SCOPE_STRUCT VScope_T;
struct VALIDATOR_SCOPE_STRUCT
{
    VScope_T* prev;
    List_T* objs;
    ASTIdentifier_T* id;
} __attribute__((packed));

typedef struct VALIDATOR_STRUCT 
{
    ASTProg_T* ast;

    VScope_T* current_scope;
    VScope_T* global_scope;
    ASTObj_T* current_function;
    ASTNode_T* current_pipe;
    bool main_function_found;

    u32 scope_depth;  // depth of the current scope
} Validator_T;

// validator struct functions
static void init_validator(Validator_T* v)
{
    memset(v, 0, sizeof(struct VALIDATOR_STRUCT));
}

static void begin_obj_scope(Validator_T* v, ASTIdentifier_T* id, List_T* objs);
static void scope_add_obj(Validator_T* v, ASTObj_T* obj);
static inline void begin_scope(Validator_T* v, ASTIdentifier_T* id);
static inline void end_scope(Validator_T* v);

static ASTType_T* expand_typedef(Validator_T* v, ASTType_T* type);

static void check_exit_fns(Validator_T* v);

// iterator functions

// id
static void id_use(ASTIdentifier_T* id, va_list args);
static void id_def(ASTIdentifier_T* id, va_list args);

// obj
static void fn_start(ASTObj_T* fn, va_list args);
static void fn_end(ASTObj_T* fn, va_list args);
static void namespace_start(ASTObj_T* namespace, va_list args);
static void namespace_end(ASTObj_T* namespace, va_list args);
static void typedef_start(ASTObj_T* tydef, va_list args);
static void typedef_end(ASTObj_T* tydef, va_list args);
static void global_start(ASTObj_T* global, va_list args);
static void global_end(ASTObj_T* global, va_list args);
static void local_start(ASTObj_T* local, va_list args);
static void local_end(ASTObj_T* local, va_list args);
static void fn_arg_start(ASTObj_T* arg, va_list args);
static void fn_arg_end(ASTObj_T* arg, va_list args);
static void enum_member_end(ASTObj_T* en, va_list args);

// node
// statements
static void block_start(ASTNode_T* block, va_list args);
static void block_end(ASTNode_T* block, va_list args);
static void return_end(ASTNode_T* ret, va_list args);
static void for_start(ASTNode_T* _for, va_list args);
static void for_end(ASTNode_T* _for, va_list args);
static void case_end(ASTNode_T* _case, va_list args);
static void match_type_end(ASTNode_T* match, va_list args);
static void using_end(ASTNode_T* using, va_list args);
static void with_start(ASTNode_T* with, va_list args);
static void with_end(ASTNode_T* with, va_list args);
static void expr_stmt(ASTNode_T* expr_stmt, va_list args);

// expressions
static void call(ASTNode_T* call, va_list args);
static void identifier(ASTNode_T* id, va_list args);
static void closure(ASTNode_T* closure, va_list args);
static void reference(ASTNode_T* ref, va_list args);
static void dereference(ASTNode_T* deref, va_list args);
static void member(ASTNode_T* member, va_list args);
static void bin_operation(ASTNode_T* op, va_list args);
static void modulo(ASTNode_T* mod, va_list args);
static void negate(ASTNode_T* neg, va_list args);
static void bitwise_negate(ASTNode_T* neg, va_list args);
static void not(ASTNode_T* not, va_list args);
static void equals(ASTNode_T* equals, va_list args);
static void lt_gt(ASTNode_T* lt_gt, va_list args);
static void and_or(ASTNode_T* and_or, va_list args);
static void bitwise_op(ASTNode_T* op, va_list args);
static void inc_dec(ASTNode_T* op, va_list args);
static void index_(ASTNode_T* index, va_list args); // "index" was taken by string.h
static void cast(ASTNode_T* cast, va_list args);
static void assignment_start(ASTNode_T* assign, va_list args);
static void assignment_end(ASTNode_T* assign, va_list args);
static void struct_member(ASTNode_T* member, va_list args);
static void struct_lit(ASTNode_T* s_lit, va_list args);
static void array_lit(ASTNode_T* a_lit, va_list args);
static void ternary(ASTNode_T* ternary, va_list args);
static void else_expr(ASTNode_T* else_expr, va_list args);
static void closure(ASTNode_T* closure, va_list args);
static void len(ASTNode_T* len, va_list args);
static void type_expr(ASTNode_T* cmp, va_list args);
static void pipe_start(ASTNode_T* pipe, va_list args);
static void pipe_end(ASTNode_T* pipe, va_list args);
static void hole(ASTNode_T* hole, va_list args);
static void lambda_start(ASTNode_T* lambda, va_list args);
static void lambda_end(ASTNode_T* lambda, va_list args);
static void string_lit(ASTNode_T* str, va_list args);
static void char_lit(ASTNode_T* ch, va_list args);

//types
static void struct_type(ASTType_T* s_type, va_list args);
static void enum_type(ASTType_T* e_type, va_list args);
static void undef_type(ASTType_T* u_type, va_list args);
static void typeof_type(ASTType_T* typeof_type, va_list args);
static void type_begin(ASTType_T* type, va_list args);
static void type_end(ASTType_T* type, va_list args);
static i32 get_type_size(Validator_T* v, ASTType_T* type);

// iterator configuration
static const ASTIteratorList_T main_iterator_list = 
{
    .node_start_fns = 
    {
        [ND_BLOCK] = block_start,
        [ND_FOR] = for_start,
        [ND_ASSIGN] = assignment_start,
        [ND_WITH] = with_start,
        [ND_PIPE] = pipe_start,
        [ND_LAMBDA] = lambda_start,
    },

    .node_end_fns = 
    {
        // statements
        [ND_BLOCK] = block_end,
        [ND_RETURN] = return_end,
        [ND_FOR] = for_end,
        [ND_CASE] = case_end,
        [ND_MATCH_TYPE] = match_type_end,
        [ND_USING] = using_end,
        [ND_WITH] = with_end,
        [ND_EXPR_STMT] = expr_stmt,

        // expressions
        [ND_ID]      = identifier,
        [ND_CALL]    = call,
        [ND_REF]     = reference,
        [ND_DEREF]   = dereference,
        [ND_MEMBER]  = member,
        [ND_ADD]     = bin_operation,
        [ND_SUB]     = bin_operation,
        [ND_MUL]     = bin_operation,
        [ND_DIV]     = bin_operation,
        [ND_MOD]     = modulo,
        [ND_NEG]     = negate,
        [ND_BIT_NEG] = bitwise_negate,
        [ND_NOT]     = not,
        [ND_EQ]      = equals,
        [ND_NE]      = equals,
        [ND_LT]      = lt_gt,
        [ND_LE]      = lt_gt,
        [ND_GT]      = lt_gt,
        [ND_GE]      = lt_gt,
        [ND_AND]     = and_or,
        [ND_OR]      = and_or,
        [ND_XOR]     = bitwise_op,
        [ND_LSHIFT]  = bitwise_op,
        [ND_RSHIFT]  = bitwise_op,
        [ND_BIT_OR]  = bitwise_op,
        [ND_BIT_AND] = bitwise_op,
        [ND_INC]     = inc_dec,
        [ND_DEC]     = inc_dec,
        [ND_INDEX]   = index_,
        [ND_CAST]    = cast,
        [ND_ASSIGN]  = assignment_end,
        [ND_STRUCT]  = struct_lit,
        [ND_ARRAY]   = array_lit,
        [ND_TERNARY] = ternary,
        [ND_ELSE_EXPR] = else_expr,
        [ND_CLOSURE] = closure,
        [ND_LEN]     = len,
        [ND_TYPE_EXPR] = type_expr,
        [ND_PIPE]    = pipe_end,
        [ND_LAMBDA] = lambda_end,
        [ND_HOLE]    = hole,
        [ND_STR]  = string_lit,
        [ND_CHAR] = char_lit,
    },

    .type_fns = 
    {
        [TY_STRUCT] = struct_type,
        [TY_ENUM]   = enum_type,
        [TY_UNDEF]  = undef_type,
        [TY_TYPEOF] = typeof_type,
    },

    .obj_start_fns = 
    {
        [OBJ_FUNCTION]  = fn_start,
        [OBJ_NAMESPACE] = namespace_start,
        [OBJ_TYPEDEF]   = typedef_start,
        [OBJ_GLOBAL]    = global_start,
        [OBJ_LOCAL]     = local_start,
        [OBJ_FN_ARG]    = fn_arg_start,
    },

    .obj_end_fns = 
    {
        [OBJ_FUNCTION]  = fn_end,
        [OBJ_NAMESPACE] = namespace_end,
        [OBJ_TYPEDEF]   = typedef_end,
        [OBJ_GLOBAL]    = global_end,
        [OBJ_LOCAL]     = local_end,
        [OBJ_FN_ARG]    = fn_arg_end,
        [OBJ_ENUM_MEMBER] = enum_member_end,
    },

    .id_def_fn = id_def,
    .id_use_fn = id_use,

    .type_begin = type_begin,
    .type_end = type_end,

    .iterate_over_right_members = false
};

void validate_ast(ASTProg_T* ast)
{
    Validator_T v;
    init_validator(&v);

    global.current_fn = &v.current_function;

    v.ast = ast;

    begin_obj_scope(&v, NULL, ast->objs);
    v.global_scope = v.current_scope;

    ast_iterate(&main_iterator_list, ast, &v);

    end_scope(&v);
    v.global_scope = NULL;

    check_exit_fns(&v);

    global.current_fn = NULL;
    
    if(!v.main_function_found)
    {
        LOG_ERROR(COLOR_BOLD_RED "[Error]" COLOR_RESET COLOR_RED " mssing entrypoint; no `main` function declared.\n");
        global.emitted_errors++;
    }

    if(global.emitted_errors && global.emitted_warnings)
    {
        LOG_ERROR_F(COLOR_BOLD_RED "[Error]" COLOR_RESET COLOR_RED " %u error%s and %u warning%s thrown during code validation; aborting.\n", global.emitted_errors, global.emitted_errors == 1 ? "" : "s", global.emitted_warnings, global.emitted_warnings == 1 ? "" : "s");
        exit(1);
    }
    else if(global.emitted_errors)
    {
        LOG_ERROR_F(COLOR_BOLD_RED "[Error]" COLOR_RESET COLOR_RED " %u error%s thrown during code validation; aborting.\n", global.emitted_errors, global.emitted_errors == 1 ? "" : "s");
        exit(1);
    }
    else if(global.emitted_warnings)
        LOG_WARN_F(COLOR_BOLD_YELLOW "[Warning]" COLOR_RESET COLOR_YELLOW " %u warning%s thrown during code validation\n", global.emitted_warnings, global.emitted_warnings == 1 ? "" : "s");
}

static ASTObj_T* search_in_current_scope(VScope_T* scope, char* id)
{
    for(size_t i = 0; i < scope->objs->size; i++)
    {
        ASTObj_T* obj = scope->objs->items[i];
        if(obj->id && strcmp(obj->id->callee, id) == 0)
            return obj;
    }
    return NULL;
}

static ASTNode_T* search_node_in_current_scope(VScope_T* scope, char* id)
{
    for(size_t i = 0; i < scope->objs->size; i++)
    {
        ASTNode_T* node = scope->objs->items[i];
        if(strcmp(node->id->callee, id) == 0)
            return node;
    }
    return NULL;
}

static ASTObj_T* search_in_scope(VScope_T* scope, char* id)
{
    if(!scope)
        return NULL;

    ASTObj_T* found = search_in_current_scope(scope, id);
    if(found)
        return found;
    return search_in_scope(scope->prev, id);
}

static ASTObj_T* search_identifier(Validator_T* v, VScope_T* scope, ASTIdentifier_T* id)
{
    if(!v || !scope || !id)
        return NULL;
    
    if(id->global_scope)
        scope = v->global_scope;
    
    if(id->outer)
    {
        ASTObj_T* outer_obj = search_identifier(v, scope, id->outer);
        if(!outer_obj)
            return NULL;
        
        switch(outer_obj->kind)
        {
            case OBJ_TYPEDEF:
                {
                    ASTType_T* expanded = expand_typedef(v, outer_obj->data_type);
                    if(expanded->kind == TY_ENUM)
                    {
                        for(size_t i = 0; i < expanded->members->size; i++) 
                        {
                            ASTObj_T* member = expanded->members->items[i];
                            if(strcmp(member->id->callee, id->callee) == 0)
                                return member;
                        }
                    }
                    throw_error(ERR_UNDEFINED, id->outer->tok, "type `%s` has no member called `%s`", outer_obj->id->callee, id->callee);
                } break;
            case OBJ_NAMESPACE:
                {
                    for(size_t i = 0; i < outer_obj->objs->size; i++)
                    {
                        ASTObj_T* obj = outer_obj->objs->items[i];
                        if(strcmp(obj->id->callee, id->callee) == 0)
                            return obj;
                    }
                } break;
            default: 
                break;
        }

        return NULL;
    }
    else
    {
        ASTObj_T* found = search_in_current_scope(scope, id->callee);
        if(found)
            return found;
        return search_identifier(v, scope->prev, id);
    }
}

static void begin_obj_scope(Validator_T* v, ASTIdentifier_T* id, List_T* objs)
{
    begin_scope(v, id);

    for(size_t i = 0; i < objs->size; i++)
        scope_add_obj(v, objs->items[i]);
}

static inline void begin_scope(Validator_T* v, ASTIdentifier_T* id)
{
    VScope_T* scope = malloc(sizeof(VScope_T));
    scope->objs = init_list();
    scope->prev = v->current_scope;
    scope->id = id;
    v->current_scope = scope;
    v->scope_depth++;
}

static inline void end_scope(Validator_T* v)
{
    VScope_T* scope = v->current_scope;
    v->current_scope = scope->prev;
    free_list(scope->objs);
    free(scope);
    v->scope_depth--;
}

static void scope_add_obj(Validator_T* v, ASTObj_T* obj)
{
    ASTObj_T* found = search_in_current_scope(v->current_scope, obj->id->callee);
    if(found)
        throw_error(ERR_REDEFINITION, obj->id->tok, 
            "redefinition of %s `%s`.\nfirst defined in " COLOR_BOLD_WHITE "%s " COLOR_RESET "at line " COLOR_BOLD_WHITE "%u" COLOR_RESET " as %s.", 
            obj_kind_to_str(obj->kind), obj->id->callee, 
            found->tok->source->short_path ? found->tok->source->short_path : found->tok->source->path, 
            found->tok->line + 1,
            obj_kind_to_str(found->kind)
        );

    list_push(v->current_scope->objs, obj);
}

// only used for enum/struct members
static void scope_add_node(Validator_T* v, ASTNode_T* node)
{
    ASTNode_T* found = search_node_in_current_scope(v->current_scope, node->id->callee);
    if(found)
    {
        throw_error(ERR_REDEFINITION, node->id->tok, 
            "redefinition of member `%s`.\nfirst defined in " COLOR_BOLD_WHITE "%s " COLOR_RESET "at line " COLOR_BOLD_WHITE "%u" COLOR_RESET, 
            node->id->callee, 
            found->tok->source->short_path ? found->tok->source->short_path : found->tok->source->path, 
            found->tok->line + 1
        );
        exit(1);
    }
    list_push(v->current_scope->objs, node); // use the default obj list
}

static ASTType_T* expand_typedef(Validator_T* v, ASTType_T* type)
{
    if(!type || type->kind != TY_UNDEF)
        return type;

    ASTObj_T* ty_def = search_identifier(v, v->current_scope, type->id);
    if(!ty_def)
        throw_error(ERR_TYPE_ERROR, type->tok, "undefined data type `%s`", type->id->callee);
    if(ty_def->kind != OBJ_TYPEDEF)
        throw_error(ERR_TYPE_ERROR, type->tok, "identifier `%s` references object of kind `%s`, expect type", type->id->callee, obj_kind_to_str(ty_def->kind));

    return ty_def->data_type->kind == TY_UNDEF ? expand_typedef(v, ty_def->data_type) : ty_def->data_type;
}

static ASTNode_T* find_member_in_type(Validator_T* v, ASTType_T* type, ASTNode_T* id)
{
    if(id->kind != ND_ID)
        return NULL;

    bool is_ptr = false;

    while(type->kind == TY_PTR || type->kind == TY_UNDEF)
    {
        if(type->kind == TY_PTR)
        {
            if(!is_ptr)
            {
                is_ptr = true;
                type = type->base;
            }
            else
                break;
        }
        else if(type->kind == TY_UNDEF)
            type = expand_typedef(v, type);
    }

    if(type->kind != TY_STRUCT)
    {
        char buf[BUFSIZ] = {'\0'}; // FIXME: could cause segfault with big structs | identifiers
        throw_error(ERR_TYPE_ERROR, id->tok, "cannot get member of type `%s`", ast_type_to_str(buf, type, LEN(buf)));
        return NULL;
    }

    for(size_t i = 0; i < type->members->size; i++)
    {
        ASTNode_T* member = type->members->items[i];

        if(strcmp(member->id->callee, id->id->callee) == 0)
            return member;
    }

    return NULL;
}

static bool is_number(Validator_T* v, ASTType_T* type)
{
    if(!type)
        return false;

    if(type->kind == TY_UNDEF)
        type = expand_typedef(v, type);
    if(!type)
        return false;

    switch(type->kind)
    {
        case TY_I8:
        case TY_I16:
        case TY_I32:
        case TY_I64:
        case TY_U8:
        case TY_U16: 
        case TY_U32:
        case TY_U64:
        case TY_F32:
        case TY_F64:
        case TY_F80:
        case TY_CHAR:
        case TY_ENUM: // enums get at the moment treated as numbers to support operations
            return true;

        default:
            return false;
    }
}

static bool v_is_integer(Validator_T* v, ASTType_T* type)
{
    if(!type)
        return false;
    
    if(type->kind == TY_UNDEF)
        type = expand_typedef(v, type);
    if(!type)
        return false;

    switch(type->kind) {
        case TY_I8:
        case TY_I16:
        case TY_I32:
        case TY_I64:
        case TY_U8:
        case TY_U16: 
        case TY_U32:
        case TY_U64:
        case TY_CHAR:
        case TY_ENUM:
            return true;

        default: 
            return false;
    }
}

static bool is_ptr(Validator_T* v, ASTType_T* type)
{
    if(!type)
        return false;

    if(type->kind == TY_UNDEF)
        type = expand_typedef(v, type);
    if(!type)
        return false;
    
    return type->kind == TY_PTR;
}

static bool is_bool(Validator_T* v, ASTType_T* type)
{
    if(!type)
        return false;

    if(type->kind == TY_UNDEF)
        type = expand_typedef(v, type);
    if(!type)
        return false;
    
    return type->kind == TY_BOOL;
}

static bool is_void(Validator_T* v, ASTType_T* type)
{
    if(!type)
        return false;

    if(type->kind == TY_UNDEF)
        type = expand_typedef(v, type);
    if(!type)
        return false;
    
    return type->kind == TY_VOID;
}

static bool is_arr(Validator_T* v, ASTType_T* type)
{
    if(!type)
        return false;

    if(type->kind == TY_UNDEF)
        type = expand_typedef(v, type);
    if(!type)
        return false;
    
    return type->kind == TY_ARR;
}

static bool types_equal(ASTType_T* t1, ASTType_T* t2)
{
    if(t1->kind != t2->kind)
        return false;
    
    switch(t1->kind)
    {
        case TY_ARR:
        case TY_PTR:
            return types_equal(t1->base, t2->base);
        
        case TY_UNDEF:
            return strcmp(t1->id->callee, t2->id->callee) == 0;

        default:
            return true;
    }
}

static i32 align_type(ASTType_T* ty)
{
    switch(ty->kind)
    {
        case TY_ARR:
        case TY_PTR:
            return MAX(pow(2, floor(log(ty->base->size)/log(2))), 8);
        default:
            return MAX(pow(2, floor(log(ty->size)/log(2))), 1);
    }
}

// unwrap a node wrapped in closures (`((((4))))` => `4`)
static ASTNode_T* unwrap_node(ASTNode_T* node)
{
    return node->kind == ND_CLOSURE ? unwrap_node(node->expr) : node;
}

static ASTExitFnHandle_T* find_exit_fn(Validator_T* v, ASTType_T* ty)
{
    if(!v->ast->type_exit_fns)
        return NULL;
    for(size_t i = 0; i < v->ast->type_exit_fns->size; i++)
    {
        ASTExitFnHandle_T* handle = v->ast->type_exit_fns->items[i];
        if(types_equal(handle->type, ty))
            return handle;
    }
    return NULL;
}


static void check_exit_fns(Validator_T* v)
{
    if(!v->ast->type_exit_fns)
        return;
    for(size_t i = 0; i < v->ast->type_exit_fns->size; i++)
    {
        ASTExitFnHandle_T* handle = v->ast->type_exit_fns->items[i];
        ASTObj_T* fn = handle->fn;

        ASTExitFnHandle_T* found = find_exit_fn(v, handle->type);
        if(handle != found)
        {
            char buf[BUFSIZ] = {'\0'}; // FIXME: could cause segfault with big structs | identifiers
            throw_error(ERR_REDEFINITION_UNCR, handle->tok, "exit function for data type `%s` already defined", ast_type_to_str(buf, handle->type, LEN(buf)));
        }

        if(fn->args->size != 1)
            throw_error(ERR_TYPE_ERROR_UNCR, handle->tok, "exit function must have one argument");

        
        if(!types_equal(((ASTObj_T*) fn->args->items[0])->data_type, handle->type))
            throw_error(ERR_TYPE_ERROR_UNCR, handle->tok, "specified data type and first argument type of function `%s` do not match", fn->id->callee);

        if(expand_typedef(v, fn->return_type)->kind != TY_VOID)
            throw_error(ERR_UNUSED, handle->tok, "function `%s` returns a value that cannot be accessed", fn->id->callee);
    }
}

// id

static void id_def(ASTIdentifier_T* id, va_list args)
{
}

static void id_use(ASTIdentifier_T* id, va_list args)
{
    GET_VALIDATOR(args);
    ASTObj_T* found = search_identifier(v, v->current_scope, id);
    if(!found) {
        throw_error(ERR_UNDEFINED, id->tok, "undefined identifier `%s`.", id->callee);
    }
}

static void gen_id_path(VScope_T* v, ASTIdentifier_T* id)
{
    if(!v || !v->id)
        return;

    id->outer = v->id;
    gen_id_path(v->prev, id->outer);
}

// obj

static void check_main_fn(Validator_T* v, ASTObj_T* main_fn)
{
    main_fn->is_entry_point = true;
    v->ast->entry_point = main_fn;

    ASTType_T* return_type = expand_typedef(v, main_fn->return_type);
    if(return_type->kind != TY_I32)
        throw_error(ERR_TYPE_ERROR_UNCR, main_fn->return_type->tok ? main_fn->return_type->tok : main_fn->tok, "expect type `i32` as return type for function `main`");

    switch(main_fn->args->size)
    {
        case 0:
            // ()
            return;
        
        case 1:
            // check the types of the one argument (&&char)
            {
                ASTType_T* arg_type = expand_typedef(v, ((ASTObj_T*) main_fn->args->items[0])->data_type);
                if(arg_type->kind != TY_PTR || !arg_type->base ||arg_type->base->kind != TY_PTR || !arg_type->base->base || arg_type->base->base->kind != TY_CHAR)
                {
                    throw_error(ERR_TYPE_ERROR_UNCR, ((ASTObj_T*) main_fn->args->items[0])->tok, "expect argument of function `main` to be `&&char`");
                    return;
                }
            } break;
        
        case 2:
            // check the types of the two arguments (i32, &%char)
            {
                ASTType_T* arg0_type = expand_typedef(v, ((ASTObj_T*) main_fn->args->items[0])->data_type);
                if(arg0_type->kind != TY_I32)
                {
                    throw_error(ERR_TYPE_ERROR_UNCR, ((ASTObj_T*) main_fn->args->items[0])->tok, "expect first argument of function `main` to be `i32`");
                    return;
                }

                ASTType_T* arg1_type = expand_typedef(v, ((ASTObj_T*) main_fn->args->items[1])->data_type);
                if(arg1_type->kind != TY_PTR || arg1_type->base->kind != TY_PTR || arg1_type->base->base->kind != TY_CHAR)
                {
                    throw_error(ERR_TYPE_ERROR_UNCR, ((ASTObj_T*) main_fn->args->items[1])->tok, "expect first argument of function `main` to be `&&char`");
                    return;
                }
            } break;

        default:
            throw_error(ERR_UNDEFINED_UNCR, main_fn->tok, "expect 0 or 2 arguments for function `main`, got %ld", main_fn->args->size);
            return;
    }
}

static void fn_start(ASTObj_T* fn, va_list args)
{
    GET_VALIDATOR(args);
    begin_scope(v, fn->id);
    v->current_function = fn;

    
    if(fn->data_type->is_variadic && !fn->is_extern)
        scope_add_obj(v, fn->va_area);
}

static bool stmt_returns_value(ASTNode_T* node)
{
    switch(node->kind)
    {
        case ND_RETURN:
            return true;
        case ND_BLOCK:
            for(size_t i = 0; i < node->stmts->size; i++)
            {
                if(stmt_returns_value(node->stmts->items[i]))
                {
                    if(node->stmts->size - i > 1)
                        throw_error(ERR_UNREACHABLE, ((ASTNode_T*) node->stmts->items[i + 1])->tok, "unreachable code after return statement");
                    return true;
                }
            }
            return false;
        case ND_IF:
            return stmt_returns_value(node->if_branch) && node->else_branch ? stmt_returns_value(node->else_branch) : false;
        case ND_LOOP:
        case ND_FOR:
        case ND_WHILE:
            return stmt_returns_value(node->body);
        case ND_MATCH:
            if(!node->default_case)
                return false;
            
            {
                u64 cases_return = 0;
                for(size_t i = 0; i < node->cases->size; i++)
                    if(stmt_returns_value(((ASTNode_T*) node->cases->items[i])->body))
                        cases_return++;
                
                return cases_return == node->cases->size && stmt_returns_value(node->default_case->body);
            }
        default: 
            return false;
    }
}

static void fn_end(ASTObj_T* fn, va_list args)
{
    GET_VALIDATOR(args);

    if(strcmp(fn->id->callee, "main") == 0)
    {
        v->main_function_found = true;
        check_main_fn(v, fn);
    }

    ASTType_T* return_type = expand_typedef(v, fn->return_type);
    if(return_type->kind == TY_ARR) 
        throw_error(ERR_TYPE_ERROR_UNCR, fn->return_type->tok ? fn->return_type->tok : fn->tok, "cannot return an array type from a function");
    else if(global.ct == CT_ASM && return_type->kind == TY_STRUCT && return_type->size > 16)
    {
        fn->return_ptr = init_ast_obj(OBJ_LOCAL, fn->return_type->tok);
        fn->return_ptr->data_type = init_ast_type(TY_PTR, fn->return_type->tok);
        fn->return_ptr->data_type->base = fn->return_type;
        fn->return_ptr->data_type->size = get_type_size(v, fn->return_ptr->data_type);
        fn->return_ptr->data_type->align = 8;
    }

    end_scope(v);

    gen_id_path(v->current_scope, fn->id);

    if(return_type->kind != TY_VOID && !fn->is_extern && !fn->no_return && !stmt_returns_value(fn->body))
        throw_error(ERR_NORETURN, fn->tok, "function `%s` does not return a value", fn->id->callee);

    v->current_function = NULL;

    for(size_t i = 0; i < fn->args->size; i++)
    {
        ASTObj_T* arg = fn->args->items[i];
        if(arg->data_type->is_vla && fn->args->size - i > 1)
            throw_error(ERR_TYPE_ERROR_UNCR, arg->tok, "argument of type `vla` has to be the last");
        if(!arg->referenced && !fn->is_extern && !fn->ignore_unused)
            throw_error(ERR_UNUSED, arg->tok, "unused function argument `%s`", arg->id->callee);
    }

    if(v->scope_depth == 1 && strcmp(fn->id->callee, "_start") == 0)
        throw_error(ERR_MISC, fn->id->tok, "cannot name a function \"_start\" in global scope");
}

static void namespace_start(ASTObj_T* namespace, va_list args)
{
    GET_VALIDATOR(args);    
    begin_obj_scope(v, namespace->id, namespace->objs);
}

static void namespace_end(ASTObj_T* namespace, va_list args)
{
    GET_VALIDATOR(args);
    end_scope(v);
}

static void typedef_start(ASTObj_T* tydef, va_list args)
{
    if(tydef->data_type->kind == TY_ENUM)
        for(size_t i = 0; i < tydef->data_type->members->size; i++)
        {
            ASTObj_T* member = tydef->data_type->members->items[i];
            member->id->outer = tydef->id;
        }
}

static void typedef_end(ASTObj_T* tydef, va_list args)
{
    GET_VALIDATOR(args);

    gen_id_path(v->current_scope, tydef->id);
}

static bool vla_to_array_type(Validator_T* v, ASTType_T* ty, ASTNode_T* value)
{
    ASTType_T* arr_ty = expand_typedef(v, value->data_type);
    if(arr_ty->kind == TY_ARR && !arr_ty->is_vla && arr_ty->num_indices)
    {
        ty->num_indices = arr_ty->num_indices;
        ty->is_vla = false;
        return true;
    }
    return false;
}

static void global_start(ASTObj_T* global, va_list args)
{
}

static void global_end(ASTObj_T* global, va_list args)
{
    GET_VALIDATOR(args);

    if(!global->data_type)
    {
        if(!global->value->data_type)
        {
            throw_error(ERR_TYPE_ERROR, global->value->tok, "could not resolve datatype for `%s`", global->id->callee);
            return;
        }

        global->data_type = global->value->data_type;
    }
    gen_id_path(v->current_scope, global->id);

    ASTType_T* expanded = expand_typedef(v, global->data_type);

    if(expanded->is_vla && !vla_to_array_type(v, global->data_type, global->value))
        throw_error(ERR_TYPE_ERROR, global->data_type->tok, "vla type is not allowed for variables");
    
    if(expanded->is_constant)
        global->is_constant = true;
    
    if(expanded->kind == TY_VOID)
        throw_error(ERR_TYPE_ERROR, global->tok, "`void` type is not allowed for variables"); 
}

static void local_start(ASTObj_T* local, va_list args)
{
}

static void local_end(ASTObj_T* local, va_list args)
{
    GET_VALIDATOR(args);
}

static void fn_arg_start(ASTObj_T* arg, va_list args)
{
    GET_VALIDATOR(args);

    if(arg->data_type->kind == TY_ARR)
        arg->data_type->kind = TY_PTR;

    scope_add_obj(v, arg);
}

static void fn_arg_end(ASTObj_T* arg, va_list args)
{
    GET_VALIDATOR(args);

    ASTType_T* expanded = expand_typedef(v, arg->data_type);

    if(expanded->is_constant)
        arg->is_constant = true;
    if(expanded->kind == TY_VOID)
        throw_error(ERR_TYPE_ERROR, arg->tok, "`void` type is not allowed for function arguments");
}

static void enum_member_end(ASTObj_T* e_member, va_list args)
{
    if(!e_member->value->is_constant)
        throw_error(ERR_CONST_ASSIGN, e_member->value->tok, "cannot assign non-constant value to enum member");
}

// node
// statements

static void block_start(ASTNode_T* block, va_list args)
{
    GET_VALIDATOR(args);
    begin_obj_scope(v, NULL, block->locals);
}

static void block_end(ASTNode_T* block, va_list args)
{
    GET_VALIDATOR(args);
    end_scope(v);

    for(size_t i = 0; i < block->locals->size; i++)
    {
        ASTObj_T* var = block->locals->items[i];
        if(!var->referenced && !v->current_function->ignore_unused)
            throw_error(ERR_UNUSED, var->tok, "unused local variable `%s`", var->id->callee);
    }
}

static void return_end(ASTNode_T* ret, va_list args)
{
    GET_VALIDATOR(args);
    if(!v->current_function)
    {
        throw_error(ERR_SYNTAX_ERROR, ret->tok, "unexpected return statement outside of function");
        return;
    }

    // type checking
}

static void case_end(ASTNode_T* _case, va_list args)
{
}

static void for_start(ASTNode_T* _for, va_list args)
{
    GET_VALIDATOR(args);
    begin_obj_scope(v, NULL, _for->locals);
}

static void for_end(ASTNode_T* _for, va_list args)
{
    GET_VALIDATOR(args);
    end_scope(v);
}

static void match_type_end(ASTNode_T* match, va_list args)
{
    for(size_t i = 0; i < match->cases->size; i++)
    {
        ASTNode_T* case_stmt = match->cases->items[i];

        if(types_equal(match->data_type, case_stmt->data_type)) 
        {
            match->body = case_stmt->body;
            return;
        }
    }

    if(match->default_case)
        match->body = match->default_case->body;

}

static bool compatible(Validator_T* v, ASTType_T* a, ASTType_T* b)
{
    return true;
}

static void using_end(ASTNode_T* using, va_list args)
{
    GET_VALIDATOR(args);

    ASTObj_T* found = search_identifier(v, v->current_scope, using->id);
    if(!found)
    {
        throw_error(ERR_UNDEFINED_UNCR, using->id->tok, "using undefined namespace `%s`", using->id->callee);
        return;
    }
    
    if(found->kind != OBJ_NAMESPACE)
    {
        throw_error(ERR_UNDEFINED_UNCR, using->id->tok, "`%s` is a %s, can only have namespaces for `using`", using->id->callee, obj_kind_to_str(found->kind));
        return;
    }

    for(size_t i = 0; i < found->objs->size; i++)
    {
        ASTObj_T* obj = found->objs->items[i];
        if(search_in_current_scope(v->current_scope, obj->id->callee))
        {
            throw_error(ERR_REDEFINITION_UNCR, using->tok, "namespace `%s` is trying to implement a %s `%s`, \nwhich is already defined in this scope", found->id->callee, obj_kind_to_str(obj->kind), obj->id->callee);
            continue;
        }

        list_push(v->current_scope->objs, obj);
    }
}

static void with_start(ASTNode_T* with, va_list args)
{
    GET_VALIDATOR(args);
    begin_scope(v, NULL);
    scope_add_obj(v, with->obj);
}

static void with_end(ASTNode_T* with, va_list args)
{
    GET_VALIDATOR(args);

    if(!with->condition->data_type)
        throw_error(ERR_TYPE_ERROR, with->obj->tok, "could not resolve data type for `%s`", with->obj->id->callee);
    
    ASTExitFnHandle_T* handle = find_exit_fn(v, with->condition->data_type);
    if(!handle)
    {
        char buf[BUFSIZ] = {'\0'}; // FIXME: could cause segfault with big structs | identifiers
        throw_error(ERR_TYPE_ERROR_UNCR, with->obj->tok, "type `%s` does not have a registered exit function.\nRegister one by using the `exit_fn` compiler directive", ast_type_to_str(buf, with->condition->data_type, LEN(buf)));
    }
    with->exit_fn = handle->fn;

    end_scope(v);
}

static void expr_stmt(ASTNode_T* expr_stmt, va_list args)
{
    expr_stmt->expr->result_ignored = expr_stmt->expr->kind == ND_ASSIGN;
}

// expressions

static void call(ASTNode_T* call, va_list args)
{
    GET_VALIDATOR(args);

    ASTType_T* call_type = expand_typedef(v, call->expr->data_type);
    if(!call_type)
    {
        char* buf[BUFSIZ] = {};
        throw_error(ERR_TYPE_ERROR, call->tok, "could not resolve data type of expression trying to call");
    }

    switch(call_type->kind)
    {
        case TY_FN:
            call->data_type = call_type->base ? call_type->base : (ASTType_T*) primitives[TY_VOID];
            call->called_obj = call->expr->referenced_obj;
            break;
        default:
            {   
                char buf[BUFSIZ] = {};
                throw_error(ERR_TYPE_ERROR, call->tok, "cannot call expression of data type `%s`", ast_type_to_str(buf, call_type, LEN(buf)));
            } return;
    }

    if(call->expr->kind == ND_ID)
        call->expr->call = call;

    size_t expected_arg_num = call_type->arg_types->size;
    size_t received_arg_num = call->args->size;

    if(is_variadic(call_type) && received_arg_num < expected_arg_num)
    {
        char buf[BUFSIZ] = {};
        throw_error(ERR_CALL_ERROR, call->tok, "type `%s` expects %lu or more call arguments, got %lu", ast_type_to_str(buf, call_type, LEN(buf)), expected_arg_num, received_arg_num);
    }
    else if(!is_variadic(call_type) && received_arg_num != expected_arg_num)
    {
        char buf[BUFSIZ] = {};
        throw_error(ERR_CALL_ERROR, call->tok, "type `%s` expects %lu call arguments, got %lu", ast_type_to_str(buf, call_type, LEN(buf)), expected_arg_num, received_arg_num);
    }

    for(size_t i = 0; i < expected_arg_num; i++)
    {
        ASTType_T* expected_arg = call_type->arg_types->items[i];
        ASTType_T* received_arg = ((ASTNode_T*) call->args->items[i])->data_type;

        if(!compatible(v, expected_arg, received_arg))
        {
            char expected_buf[BUFSIZ] = {};
            char received_buf[BUFSIZ] = {};
            throw_error(ERR_CALL_ERROR, ((ASTNode_T*) call->args->items[i])->tok, "call argument %ld expects type `%s`, got `%s`", i + 1, 
                ast_type_to_str(expected_buf, expected_arg, LEN(expected_buf)),
                ast_type_to_str(received_buf, received_arg, LEN(received_buf))
            );
        }
    }
    
    // if we compile using the assembly compiler, a buffer for the return value is needed when handling big structs
    if(global.ct == CT_ASM && call->data_type && expand_typedef(v, call->data_type)->kind == TY_STRUCT)
    {
        ASTObj_T* ret_buf = init_ast_obj(OBJ_LOCAL, call->tok);
        ret_buf->data_type = call->data_type;
        
        list_push(v->current_function->objs, ret_buf);
        call->return_buffer = ret_buf;
    }
}

static void identifier(ASTNode_T* id, va_list args)
{
    GET_VALIDATOR(args);

    ASTObj_T* referenced_obj = search_identifier(v, v->current_scope, id->id);
    if(!referenced_obj)
    {
        throw_error(ERR_UNDEFINED, id->id->tok, "refferring to undefined identifier `%s`", id->id->callee);
        return;
    }

    switch(referenced_obj->kind)
    {
        case OBJ_GLOBAL:
        case OBJ_FUNCTION:
        case OBJ_ENUM_MEMBER:
            break;
        
        case OBJ_LOCAL:
        case OBJ_FN_ARG:
            referenced_obj->referenced = true;
            break;

        default:
            throw_error(ERR_TYPE_ERROR, id->id->tok, 
                "identifier `%s` is of kind %s, expect variable or function name", 
                id->id->callee, obj_kind_to_str(referenced_obj->kind)
            );
            return;
    }

    //debug: printf("[%3d: %3d] refferring to %s `%s` with type %d\n", id->tok->line + 1, id->tok->pos + 1, obj_kind_to_str(referenced_obj->kind), referenced_obj->id->callee, referenced_obj->data_type->kind);
    if(!id->data_type)
        id->data_type = referenced_obj->data_type;
    id->referenced_obj = referenced_obj;

    if(referenced_obj->id->outer) id->id->outer = referenced_obj->id->outer;
}

static void closure(ASTNode_T* closure, va_list args)
{
    closure->data_type = closure->expr->data_type;
}

static void reference(ASTNode_T* ref, va_list args)
{
    if(!ref->data_type)
    {
        ref->data_type = init_ast_type(TY_PTR, ref->tok);
        ref->data_type->base = ref->right->data_type;
    }
}

static void dereference(ASTNode_T* deref, va_list args)
{
    GET_VALIDATOR(args);

    ASTType_T* right_dt = expand_typedef(v, deref->right->data_type);
    if(right_dt->kind != TY_PTR && right_dt->kind != TY_ARR)
    {
        throw_error(ERR_TYPE_ERROR, deref->tok, "can only dereference variables with pointer type");
        return;
    }

    deref->data_type = right_dt->base;
}

static void member(ASTNode_T* member, va_list args)
{
    if(!member->left->data_type)
    {
        throw_error(ERR_TYPE_CAST_WARN, member->left->tok, "could not resolve data type for `%s`", member->right->id->callee);
        return;
    }

    GET_VALIDATOR(args);

    ASTType_T* tuple_type = NULL;
    ASTNode_T* found = find_member_in_type(v, member->left->data_type, member->right);
    if(!found)
    {
        char buf[BUFSIZ] = {'\0'}; // FIXME: could cause segfault with big structs | identifiers
        throw_error(ERR_TYPE_ERROR, member->tok, "type `%s` has no member named `%s`", ast_type_to_str(buf, member->left->data_type, LEN(buf)), member->right->id->callee);
        return;
    }
    member->data_type = found->data_type;
    member->body = found;
    if(is_ptr(v, member->left->data_type))
    {
        // convert x->y to (*x).y
        ASTNode_T* new_left = init_ast_node(ND_DEREF, member->left->tok);
        new_left->data_type = member->left->data_type->base;
        new_left->right = member->left;
        member->left = new_left;
    }
}

static void bin_operation(ASTNode_T* op, va_list args)
{
    GET_VALIDATOR(args);

    if(!is_number(v, op->left->data_type) && !is_ptr(v, op->left->data_type))
    {
        throw_error(ERR_TYPE_ERROR, op->tok, "left: expect integer or pointer type");
        return;
    }

    if(!is_number(v, op->right->data_type) && !is_ptr(v, op->right->data_type))
    {
        throw_error(ERR_TYPE_ERROR, op->tok, "right: expect integer or pointer type");
        return;
    }

    if(op->kind == ND_ADD && expand_typedef(v, op->left->data_type)->base && !expand_typedef(v, op->right->data_type)->base)
    {
        ASTNode_T* tmp = op->left;
        op->left = op->right;
        op->right = tmp;
    }

    op->data_type = op->right->data_type;
}

static void modulo(ASTNode_T* mod, va_list args)
{
    GET_VALIDATOR(args);

    if(!v_is_integer(v, mod->left->data_type))
    {
        throw_error(ERR_TYPE_ERROR, mod->tok, "left: expect integer type for modulo operation");
        return;
    }

    if(!v_is_integer(v, mod->right->data_type))
    {
        throw_error(ERR_TYPE_ERROR, mod->tok, "right: expect integer type for modulo operation");
        return;
    }

    mod->data_type = mod->left->data_type;
}

static void negate(ASTNode_T* neg, va_list args)
{
    GET_VALIDATOR(args);

    if(!is_number(v, neg->right->data_type))
    {
        throw_error(ERR_TYPE_ERROR, neg->tok, "can only do bitwise operations on integer types");
        return;
    }

    // todo: change type of unsigned integers
    neg->data_type = neg->right->data_type;
}

static void bitwise_negate(ASTNode_T* neg, va_list args)
{
    GET_VALIDATOR(args);

    if(!v_is_integer(v, neg->right->data_type))
    {
        throw_error(ERR_TYPE_ERROR, neg->tok, "expect integer type for bitwise negation");
        return;
    }

    neg->data_type = neg->right->data_type;
}

static void not(ASTNode_T* not, va_list args)
{
    not->data_type = (ASTType_T*) primitives[TY_BOOL];
}

static void equals(ASTNode_T* equals, va_list args)
{
    equals->data_type = (ASTType_T*) primitives[TY_BOOL];
}

static void lt_gt(ASTNode_T* op, va_list args)
{
    GET_VALIDATOR(args);

    if(!is_number(v, op->left->data_type) && !is_ptr(v, op->left->data_type))
    {
        throw_error(ERR_TYPE_ERROR, op->tok, "left: expect integer or pointer type");
        return;
    }

    if(!is_number(v, op->right->data_type) && !is_ptr(v, op->right->data_type))
    {
        throw_error(ERR_TYPE_ERROR, op->tok, "right: expect integer or pointer type");
        return;
    }

    op->data_type = (ASTType_T*) primitives[TY_BOOL];
}

static void and_or(ASTNode_T* op, va_list args)
{
    GET_VALIDATOR(args);

    op->data_type = op->left->data_type;
}

static void bitwise_op(ASTNode_T* op, va_list args)
{
    GET_VALIDATOR(args);

    if(!v_is_integer(v, op->left->data_type))
    {
        throw_error(ERR_TYPE_ERROR, op->tok, "left: can only do bitwise operations on integer types");
        return;
    }

    if(!v_is_integer(v, op->right->data_type))
    {
        throw_error(ERR_TYPE_ERROR, op->tok, "right: can only do bitwise operations with integer types");
        return;
    }

    op->data_type = op->right->data_type->size > op->left->data_type->size ? op->right->data_type : op->left->data_type;
}

static void inc_dec(ASTNode_T* op, va_list args)
{
    GET_VALIDATOR(args);

    if(!is_number(v, op->left->data_type) && !is_ptr(v, op->left->data_type))
    {
        throw_error(ERR_TYPE_ERROR, op->tok, "expect a number type");
        return;
    }

    op->data_type = op->left->data_type;
}

// "index" was taken by string.h
static void index_(ASTNode_T* index, va_list args)
{
    GET_VALIDATOR(args);

    ASTType_T* left_type = expand_typedef(v, index->left->data_type);
    if(!left_type)
        return;

    if(left_type->kind != TY_ARR && left_type->kind != TY_PTR)
    {
        throw_error(ERR_TYPE_ERROR, index->tok, "left: cannot get an index value; wrong type");
        return;
    }

    if(!v_is_integer(v, index->expr->data_type))
    {
        throw_error(ERR_TYPE_ERROR, index->tok, "index: expect an integer type");
        return;
    }
    index->data_type = left_type->base;

    if(index->from_back)
    {
        if(left_type->kind != TY_ARR || vla_type(left_type))
        {
            char buf[BUFSIZ] = {};
            throw_error(ERR_TYPE_ERROR, index->tok, "cannot get reverse index of type `%s`, need fixed-size array", ast_type_to_str(buf, index->left->data_type, LEN(buf)));
        }

        ASTNode_T* idx = init_ast_node(ND_SUB, index->tok);
        idx->left = left_type->num_indices;
        idx->right = index->expr;
        idx->data_type = idx->right->data_type;
        idx->left->data_type = (ASTType_T*) primitives[TY_U64];
        index->expr = idx;
    }
}   

static void cast(ASTNode_T* cast, va_list args)
{
    //todo: check, if type conversion is valid and safe
}

static void local_initializer(Validator_T* v, ASTNode_T* assign, ASTObj_T* local)
{
    if(!local->data_type)
    {
        if(!assign->right->data_type)
        {
            throw_error(ERR_TYPE_ERROR, local->id->tok, "could not resolve datatype for `%s`", local->id->callee);
            return;
        }
        local->data_type = assign->right->data_type;
    }
    ASTType_T* expanded = expand_typedef(v, local->data_type);
    if(expanded->is_vla && !vla_to_array_type(v, local->data_type, assign->right))
            throw_error(ERR_TYPE_ERROR, local->data_type->tok, "vla type is not allowed for local variables");
    if(expanded->kind == TY_VOID)
        throw_error(ERR_TYPE_ERROR, local->tok, "`void` type is not allowed for variables");
    if(local->data_type->is_constant)
        local->is_constant = true;
    assign->left->data_type = local->data_type;

    local->data_type->size = get_type_size(v, local->data_type);
}

static void assignment_start(ASTNode_T* assign, va_list args)
{
    GET_VALIDATOR(args);
    if(assign->right->kind == ND_ARRAY)
        assign->right->is_assigning = true;
}

static void assignment_end(ASTNode_T* assign, va_list args)
{
    GET_VALIDATOR(args);

    // if the assignments initializes a local variable, resolve the variables type
    if(assign->is_initializing && assign->referenced_obj)
        local_initializer(v, assign, assign->referenced_obj);

    switch(assign->left->kind)
    {
        case ND_MEMBER:
        case ND_INDEX:
        case ND_DEREF:
        case ND_REF:
            // todo: implement checking for constant types
            break;
        case ND_ID: 
        {
            ASTObj_T* assigned_obj = assign->is_initializing ? assign->referenced_obj : search_in_scope(v->current_scope, assign->left->id->callee);
            if(!assigned_obj)
                return;

            switch(assigned_obj->kind)
            {
                case OBJ_GLOBAL:
                case OBJ_LOCAL:
                case OBJ_FN_ARG:
                    //if(assigned_obj->is_constant)
                    //    throw_error(ERR_CONST_ASSIGN, assigned_obj->tok, "cannot assign a value to constant %s `%s`", obj_kind_to_str(assigned_obj->kind), assigned_obj->id->callee);

                    break;

                case OBJ_FUNCTION:
                case OBJ_NAMESPACE:
                case OBJ_ENUM_MEMBER:
                case OBJ_TYPEDEF:
                default:
                    throw_error(ERR_MISC, assign->tok, "cannot assign value to %s `%s`", obj_kind_to_str(assigned_obj->kind), assigned_obj->id->callee);
            }
        } break;

        default:
            throw_error(ERR_MISC, assign->left->tok, "cannot assign value to `%s`", assign->left->tok->value);
    }

    assign->data_type = assign->left->data_type;

    if(expand_typedef(v, assign->data_type)->kind == TY_VOID)
        throw_error(ERR_TYPE_ERROR, assign->tok, "cannot assign type `void`");
}

static void anonymous_struct_lit(ASTNode_T* s_lit, Validator_T* v)
{
    if(!s_lit->args->size)
    {
        throw_error(ERR_TYPE_ERROR_UNCR, s_lit->tok, "cannot resolve data type of empty anonymous struct literal `{}`");
        return;
    }
    ASTType_T* type = init_ast_type(TY_STRUCT, s_lit->tok);
    mem_add_list(type->members = init_list());
    
    for(size_t i = 0; i < s_lit->args->size; i++)
    {
        ASTNode_T* arg = s_lit->args->items[i];
        if(arg->data_type)
        {
            ASTNode_T* member = init_ast_node(ND_STRUCT_MEMBER, arg->tok);
            char buffer[100];
            sprintf(buffer, "_%ld", i);
            member->id = init_ast_identifier(arg->tok, buffer);
            member->data_type = arg->data_type;

            list_push(type->members, member);
        }
        else
            throw_error(ERR_TYPE_ERROR, arg->tok, "cannot resolve data type");
    }
    s_lit->data_type = type;
}

static void struct_lit(ASTNode_T* s_lit, va_list args)
{
    GET_VALIDATOR(args);

    if(!s_lit->data_type)
        anonymous_struct_lit(s_lit, v);

    // When compiling to assembly, we have to allocate the memory on the stack.
    // this is done by creating an "anonymous" local variable of the struct type
    // and then assigning the struct literal to it
    // foo :: {0, 1} gets converted to:
    // let <anonymous>: foo = foo :: {0, 1};
    if(v->scope_depth > 1 && global.ct == CT_ASM && !s_lit->is_assigning)
    {
        static u64 count = 0;
        ASTObj_T* local = init_ast_obj(OBJ_LOCAL, s_lit->tok);
        local->data_type = s_lit->data_type;
        local->referenced = true;
        local->id = init_ast_identifier(s_lit->tok, "");
        sprintf(local->id->callee, "__csp_structlit_%ld__", count++);
        list_push(v->current_function->objs, local);

        ASTNode_T assignment = {
            .kind = ND_ASSIGN,
            .tok = s_lit->tok,
            .id = local->id,
            .data_type = s_lit->data_type,
        };

        assignment.right = init_ast_node(ND_ARRAY, s_lit->tok);
        *assignment.right = *s_lit;
        
        assignment.left = init_ast_node(ND_ID, s_lit->tok);
        assignment.left->id = local->id;
        assignment.left->data_type = local->data_type;
        assignment.left->referenced_obj = local;

        *s_lit = assignment;
    }
}

static void array_lit(ASTNode_T* a_lit, va_list args)
{
    GET_VALIDATOR(args);

    a_lit->data_type = init_ast_type(TY_ARR, a_lit->tok);
    a_lit->data_type->num_indices = init_ast_node(ND_LONG, a_lit->tok);
    a_lit->data_type->num_indices->long_val = a_lit->args->size;
    a_lit->data_type->base = a_lit->args->size ?
        ((ASTNode_T*) a_lit->args->items[0])->data_type
        : ({
            throw_error(ERR_TYPE_ERROR, a_lit->tok, "cannot get base data type of empty array literal");
            (ASTType_T*) primitives[TY_VOID];
        });

    // When compiling to assembly, we have to allocate the memory on the stack.
    // this is done by creating an "anonymous" local variable of the array type
    // and then assigning the array literal to it
    // [0, 1, 2] gets converted to:
    // let <anonymous>: i32[3] = [0, 1, 2];
    if(v->scope_depth > 1 && global.ct == CT_ASM && !a_lit->is_assigning)
    {
        static u64 count = 0;
        ASTObj_T* local = init_ast_obj(OBJ_LOCAL, a_lit->tok);
        local->data_type = a_lit->data_type;
        local->referenced = true;
        local->id = init_ast_identifier(a_lit->tok, "");
        sprintf(local->id->callee, "__csp_arrlit_%ld__", count++);
        list_push(v->current_function->objs, local);
        list_push(v->current_scope->objs, local);

        ASTNode_T assignment = {
            .kind = ND_ASSIGN,
            .tok = a_lit->tok,
            .id = local->id,
            .data_type = a_lit->data_type,
        };

        assignment.right = init_ast_node(ND_ARRAY, a_lit->tok);
        *assignment.right = *a_lit;
        assignment.right->is_assigning = true;
        
        assignment.left = init_ast_node(ND_ID, a_lit->tok);
        assignment.left->id = local->id;
        assignment.left->data_type = local->data_type;
        assignment.left->referenced_obj = local;

        *a_lit = assignment;
    }
}

static void ternary(ASTNode_T* ternary, va_list args)
{
    GET_VALIDATOR(args);

    if(!is_bool(v, ternary->condition->data_type))
        throw_error(ERR_TYPE_ERROR_UNCR, ternary->condition->tok, "expect `bool` type for if condition");
    
    ternary->data_type = ternary->if_branch->data_type;

    if(!types_equal(ternary->if_branch->data_type, ternary->else_branch->data_type))
        throw_error(ERR_TYPE_ERROR_UNCR, ternary->tok, "data types for ternary branches do not match");
}

static void else_expr(ASTNode_T* else_expr, va_list args)
{
    GET_VALIDATOR(args);

    else_expr->data_type = else_expr->left->data_type;

    if(!types_equal(else_expr->left->data_type, else_expr->right->data_type))
        throw_error(ERR_TYPE_ERROR_UNCR, else_expr->tok, "data types of `else` branches do not match");
}

static void len(ASTNode_T* len, va_list args)
{
    GET_VALIDATOR(args);

    ASTType_T* ty = expand_typedef(v, len->expr->data_type);
    if(ty->kind != TY_ARR || ty->is_vla)
        throw_error(ERR_TYPE_ERROR, len->tok, "cannot get length of given expression");
}

static void pipe_start(ASTNode_T* pipe, va_list args)
{
    GET_VALIDATOR(args);

    pipe->expr = v->current_pipe;
    v->current_pipe = pipe;
}

static void pipe_end(ASTNode_T* pipe, va_list args)
{
    GET_VALIDATOR(args);
    pipe->data_type = pipe->right->data_type;

    if(pipe->right->kind == ND_HOLE)
    {
        throw_error(ERR_SYNTAX_WARNING, pipe->tok, "unnecessary `|>` expression");
        *pipe = *pipe->left;
    }

    v->current_pipe = pipe->expr;
}

static void hole(ASTNode_T* hole, va_list args)
{
    GET_VALIDATOR(args);
    if(!v->current_pipe)
        throw_error(ERR_SYNTAX_ERROR, hole->tok, "hole expression not in pipe");
    
    if(!v->current_pipe->left->data_type)
        throw_error(ERR_TYPE_ERROR_UNCR, hole->tok, "cannot resolve data type of pipe input expression");
    hole->data_type = v->current_pipe->left->data_type;
    hole->referenced_obj = v->current_pipe->left->referenced_obj;
}

static void lambda_start(ASTNode_T* lambda, va_list args)
{
    static u64 id = 0;
    lambda->long_val = id++;

    GET_VALIDATOR(args);
    begin_scope(v, NULL);
}

static void lambda_end(ASTNode_T* lambda, va_list args)
{
    GET_VALIDATOR(args);
    end_scope(v);

    if(global.ct == CT_ASM)
    {
        ASTObj_T* lambda_stack_ptr = init_ast_obj(OBJ_GLOBAL, lambda->tok);
        lambda_stack_ptr->data_type = (ASTType_T*) void_ptr_type;
        lambda_stack_ptr->id = init_ast_identifier(lambda->tok, "");
        sprintf(lambda_stack_ptr->id->callee, "lambda.stackptr.%ld", lambda->long_val);

        list_push(v->ast->objs, lambda_stack_ptr);
        lambda->stack_ptr = lambda_stack_ptr;

        ASTType_T* return_type = expand_typedef(v, lambda->data_type->base);
        if(return_type->kind == TY_ARR) 
            throw_error(ERR_TYPE_ERROR_UNCR, return_type->tok ? return_type->tok : lambda->tok, "cannot return an array type from a function");
        else if(return_type->kind == TY_STRUCT && return_type->size > 16)
        {
            lambda->return_ptr = init_ast_obj(OBJ_LOCAL, lambda->data_type->base->tok);
            lambda->return_ptr->data_type = init_ast_type(TY_PTR, lambda->data_type->base->tok);
            lambda->return_ptr->data_type->base = lambda->data_type->base;
            lambda->return_ptr->data_type->size = get_type_size(v, lambda->return_ptr->data_type);
            lambda->return_ptr->data_type->align = 8;
        }
    }
}

static void string_lit(ASTNode_T* str, va_list args)
{
    // check for invalid escape sequences
    for(size_t i = 0; i < strlen(str->str_val); i++)
    {
        if(str->str_val[i] == '\\')
        {
            switch(str->str_val[++i])
            {
                case 'a':
                case 'b':
                case 't':
                case 'v':
                case 'n':
                case 'r':
                case 'f':
                case '"':
                case '\'':
                case '\\':  
                case '0':
                    continue;

                default:
                    throw_error(ERR_SYNTAX_ERROR_UNCR, str->tok, "invalid escape sequence `\\%c` found in string literal", str->str_val[i]);
                    return;
            }
        }
    }
}

static void char_lit(ASTNode_T* ch, va_list args)
{
    // check for invalid escape sequences and evaluate them
    char c = ch->str_val[0];

    if(c == '\\')
    {
        switch(ch->str_val[1])
        {
            case 'a':
                c = '\a';
                break;
            case 'b':
                c = '\b';
                break;
            case 't':
                c = '\t';
                break;
            case 'v':
                c = '\v';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 'f':
                c = '\f';
                break;
            case '\'':
                c = '\'';
                break;
            case '"':
                c = '"';
                break;
            case '\\':
                c = '\\';
                break;  
            case '0':
                c = '\0';
                break;
            default:
                throw_error(ERR_SYNTAX_ERROR_UNCR, ch->tok, "invalid escape sequence `\\%c` found in char literal", ch->str_val[1]);
                return;
        }
    }
    
    free(ch->str_val);
    ch->int_val = c;
}

static void type_expr(ASTNode_T* cmp, va_list args)
{
    GET_VALIDATOR(args);

    // type comparisons have to be evaluated at compile-time
    bool result = false;

    switch(cmp->cmp_kind)
    {
        case TOKEN_EQ:
            result = types_equal(cmp->l_type, cmp->r_type);
            break;
        case TOKEN_NOT_EQ:
            result = !types_equal(cmp->l_type, cmp->r_type);
            break;
        case TOKEN_GT:
            result = cmp->l_type->size > cmp->r_type->size;
            break;
        case TOKEN_GT_EQ:
            result = cmp->l_type->size >= cmp->r_type->size;
            break;
        case TOKEN_LT:
            result = cmp->l_type->size < cmp->r_type->size;
            break;
        case TOKEN_LT_EQ:
            result = cmp->l_type->size <= cmp->r_type->size;
            break;
        case TOKEN_BUILTIN_REG_CLASS:
            {
                cmp->kind = ND_INT;
                cmp->int_val = 2;

                ASTType_T* expanded = expand_typedef(v, cmp->r_type);
                if(is_integer(expanded) || is_ptr(v, expanded))
                    cmp->int_val = 0;
                else if(is_flonum(expanded))
                    cmp->int_val = 1;
            } return;
        case TOKEN_BUILTIN_IS_INT...TOKEN_BUILTIN_IS_UNION:
            {
                cmp->kind = ND_BOOL;
                ASTType_T* expanded = expand_typedef(v, cmp->r_type);
                switch(cmp->cmp_kind)
                {
                    case TOKEN_BUILTIN_IS_INT:
                        cmp->bool_val = is_integer(expanded) && !is_unsigned(expanded);
                        break;
                    case TOKEN_BUILTIN_IS_UINT:
                        cmp->bool_val = is_integer(expanded) && is_unsigned(expanded);
                        break;
                    case TOKEN_BUILTIN_IS_FLOAT:
                        cmp->bool_val = is_flonum(expanded);
                        break;
                    case TOKEN_BUILTIN_IS_POINTER:
                        cmp->bool_val = expanded->kind == TY_PTR;
                        break;
                    case TOKEN_BUILTIN_IS_ARRAY:
                        cmp->bool_val = expanded->kind == TY_ARR;
                        break;
                    case TOKEN_BUILTIN_IS_STRUCT:
                        cmp->bool_val = expanded->kind == TY_STRUCT && !expanded->is_union;
                    case TOKEN_BUILTIN_IS_UNION:
                        cmp->bool_val = expanded->kind == TY_STRUCT && expanded->is_union;
                        break;
                    default:
                        unreachable();
                }
            } return;
        case TOKEN_BUILTIN_TO_STR:
            {
                char buf[BUFSIZ] = {'\0'}; // FIXME: could cause segfault with big structs | identifiers
                *cmp = *build_str_lit(cmp->tok, strdup(ast_type_to_str(buf, cmp->r_type, LEN(buf))), v->current_function, v->ast->objs);
            } return;
        default: 
            unreachable();
    }   

    // convert the expression to a constant value
    cmp->kind = ND_BOOL;
    cmp->bool_val = result;
    cmp->data_type = (ASTType_T*) primitives[TY_BOOL];
}

// types

static void struct_type(ASTType_T* s_type, va_list args)
{
    GET_VALIDATOR(args);
    begin_scope(v, NULL);

    for(size_t i = 0; i < s_type->members->size; i++)
    {
        ASTNode_T* member = s_type->members->items[i];
        ASTType_T* expanded = expand_typedef(v, member->data_type);
        
        if(expanded->is_vla && s_type->members->size - i > 1 && !s_type->is_union)
            throw_error(ERR_TYPE_ERROR, member->data_type->tok, "member of type `vla` has to be the last struct member");
        
        if(expanded->kind == TY_VOID)
            throw_error(ERR_TYPE_ERROR, member->data_type->tok, "struct member cannot be of type `void`");

        scope_add_node(v, member);
    }
    end_scope(v);
}

static void enum_type(ASTType_T* e_type, va_list args)
{
    GET_VALIDATOR(args);
    begin_obj_scope(v, NULL, e_type->members);
    end_scope(v);

    for(size_t i = 0; i < e_type->members->size; i++)
    {
        ASTObj_T* member = e_type->members->items[i];
        if(member->value->kind != ND_NOOP)
        {
            member->value->int_val = (i32) const_i64(member->value);
            member->value->kind = ND_INT;
        }
        else 
        {
            member->value->int_val = i ? ((ASTObj_T*) e_type->members->items[i - 1])->value->int_val + 1 : 0;
            member->value->kind = ND_INT;
        }
    }
}

static void undef_type(ASTType_T* u_type, va_list args)
{
    GET_VALIDATOR(args);

    ASTObj_T* found = search_identifier(v, v->current_scope, u_type->id);
    if(!found)
        throw_error(ERR_TYPE_ERROR, u_type->tok, "could not find data type named `%s`", u_type->id->callee);
    
    u_type->id->outer = found->id->outer;
    u_type->base = found->data_type;
}

static void typeof_type(ASTType_T* typeof_type, va_list args)
{
    GET_VALIDATOR(args);

    ASTType_T* found = typeof_type->num_indices->data_type;
    if(!found)
        throw_error(ERR_TYPE_ERROR, typeof_type->num_indices->tok, "could not resolve data type");
    
    *typeof_type = *found;
}

static void type_begin(ASTType_T* type, va_list args)
{
    GET_VALIDATOR(args);
}

static void type_end(ASTType_T* type, va_list args)
{
    GET_VALIDATOR(args);

    ASTType_T* exp = expand_typedef(v, type);

    if(exp->kind == TY_ARR && !exp->num_indices)
    {
        exp->is_vla = true;
        type->is_vla = true;
    }

    type->size = get_type_size(v, expand_typedef(v, type));
    type->align = align_type(exp);
    exp->align = type->align;
}

static i32 get_union_size(Validator_T* v, ASTType_T* u_type)
{
    i32 biggest = 0;
    for(size_t i = 0; i < u_type->members->size; i++)
    {
        ASTNode_T* member = u_type->members->items[i];
        if(member->data_type->size > biggest)
            biggest = member->data_type->size;
    }
    return biggest;
}

static i32 get_struct_size(Validator_T* v, ASTType_T* s_type)
{
    i64 bits = 0;
    for(size_t i = 0; i < s_type->members->size; i++)
    {
        ASTNode_T* member = s_type->members->items[i];
        member->data_type->size = get_type_size(v, member->data_type);
        bits = align_to(bits, align_type(member->data_type) * 8);
        member->offset = bits / 8;
        bits += member->data_type->size * 8;
    }

    return align_to(bits, align_type(s_type) * 8) / 8;
}

static i32 get_type_size(Validator_T* v, ASTType_T* type)
{
    switch(type->kind)
    {
        case TY_I8:
            return I8_S;
        case TY_U8:
            return U8_S;
        case TY_CHAR:
            return CHAR_S;
        case TY_BOOL:
            return BOOL_S;
        case TY_I16:
            return I16_S;
        case TY_U16:
            return U16_S;
        case TY_I32:
            return I32_S;
        case TY_U32:
            return U32_S;
        case TY_ENUM:
            return ENUM_S;
        case TY_I64:
            return I64_S;
        case TY_U64:
            return U64_S;
        case TY_F32:
            return F32_S;
        case TY_F64:
            return F64_S;
        case TY_F80:
            return F80_S;
        case TY_VOID:
            return VOID_S;
        case TY_PTR:
        case TY_FN:
            return PTR_S;
        case TY_TYPEOF:
            return get_type_size(v, expand_typedef(v, type->num_indices->data_type));
        case TY_UNDEF:
            return get_type_size(v, expand_typedef(v, type));
        case TY_ARR:
            if(type->num_indices)
            {
                i64 len = const_i64(type->num_indices);
                if(len < 1)
                    throw_error(ERR_TYPE_ERROR, type->num_indices->tok, "cannot get array type with negative index size (%ld)", len);
                return get_type_size(v, type->base) * len;
            }
            else
                return 0;
        case TY_STRUCT:
            if(type->is_union)
                return get_union_size(v, type);
            else
                return get_struct_size(v, type);
        default:
            return 0;
    }
    
    throw_error(ERR_TYPE_ERROR, type->tok, "could not resolve data type size");
    return 0;
}