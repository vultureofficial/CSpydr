#include "ast_iterator.h"
#include "../io/log.h"
#include "ast.h"

#include <stdarg.h>
#include <stdio.h>

#define list_fn(fn, ast, custom_args) \
    if(fn && ast)                     \
    {                                 \
        va_list copy;                 \
        va_copy(copy, custom_args);   \
        fn(ast, copy);                \
    }

static void ast_obj(ASTIteratorList_T* list, ASTObj_T* obj, va_list custom_args);
static void ast_node(ASTIteratorList_T* list, ASTNode_T* node, va_list custom_args);
static void ast_type(ASTIteratorList_T* list, ASTType_T* type, va_list custom_args);
static void ast_id(ASTIteratorList_T* list, bool is_definition, ASTIdentifier_T* id, va_list custom_args);

void ast_iterate(ASTIteratorList_T* list, ASTProg_T* ast, ...)
{
    va_list custom_args;
    va_start(custom_args, ast);

    // iterate over every object
    for(size_t i = 0; i < ast->objs->size; i++)
    {
        ast_obj(list, ast->objs->items[i], custom_args);
    }

    va_end(custom_args);
}

static void ast_obj(ASTIteratorList_T* list, ASTObj_T* obj, va_list custom_args)
{
    if(!obj)
        return;
    
    list_fn(list->obj_start_fns[obj->kind], obj, custom_args);
    switch(obj->kind)
    {
        case OBJ_FUNCTION:
            ast_type(list, obj->return_type, custom_args);
            ast_id(list, true, obj->id, custom_args);

            for(size_t i = 0; i < obj->args->size; i++)
                ast_obj(list, obj->args->items[i], custom_args);

            ast_node(list, obj->body, custom_args);     
            break;

        case OBJ_FN_ARG:
            ast_id(list, true, obj->id, custom_args);
            ast_type(list, obj->data_type, custom_args);
            break;

        case OBJ_NAMESPACE:
            ast_id(list, true, obj->id, custom_args);
            
            for(size_t i = 0; i < obj->objs->size; i++)
                ast_obj(list, obj->objs->items[i], custom_args);

            break;

        case OBJ_TYPEDEF:
            ast_id(list, true, obj->id, custom_args);
            ast_type(list, obj->data_type, custom_args);
            break;

        case OBJ_LOCAL:
        case OBJ_GLOBAL:
            ast_id(list, true, obj->id, custom_args);
            ast_type(list, obj->data_type, custom_args);
            //ast_node(list, obj->value, custom_args);
            break;

        default:
            // ignore
            break;
    }
    list_fn(list->obj_end_fns[obj->kind], obj, custom_args);
}

static void ast_node(ASTIteratorList_T* list, ASTNode_T* node, va_list custom_args)
{
    if(!node)
        return;
    list_fn(list->node_start_fns[node->kind], node, custom_args);

    switch(node->kind)
    {
        case ND_NOOP:
        case ND_BREAK:
        case ND_CONTINUE:
            break;
    
        case ND_ID:
            if(node->data_type)
                ast_type(list, node->data_type, custom_args);
            ast_id(list, false, node->id, custom_args);
            break;
    
        case ND_INT:
        case ND_LONG:
        case ND_LLONG:
        case ND_FLOAT:
        case ND_DOUBLE:
        case ND_BOOL:
        case ND_CHAR:
        case ND_STR:
        case ND_NIL:
            break;
        
        // x op y
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_EQ:
        case ND_NE:
        case ND_GT:
        case ND_GE:
        case ND_LT:
        case ND_LE:
        case ND_AND:
        case ND_OR:
        case ND_LSHIFT:
        case ND_RSHIFT:
        case ND_XOR:
        case ND_BIT_OR:
        case ND_BIT_AND:
        case ND_ASSIGN:
            ast_node(list, node->left, custom_args);
            ast_node(list, node->right, custom_args);
            if(node->data_type) 
                ast_type(list, node->data_type, custom_args);
            break;

        // x.y
        case ND_MEMBER:
            ast_node(list, node->left, custom_args);
            if(node->data_type) 
            {
                ast_node(list, node->right, custom_args);
                ast_type(list, node->data_type, custom_args);
            }
            break;

        // op x
        case ND_NEG:
        case ND_BIT_NEG:
        case ND_NOT:
        case ND_REF:
        case ND_DEREF:
            ast_node(list, node->right, custom_args);
            if(node->data_type) 
                ast_type(list, node->data_type, custom_args);
            break;
        
        // x op
        case ND_INC:
        case ND_DEC:
            ast_node(list, node->left, custom_args);
            if(node->data_type) 
                ast_type(list, node->data_type, custom_args);
            break;
        
        case ND_CLOSURE:
            ast_node(list, node->expr, custom_args);
            ast_type(list, node->data_type, custom_args);
            break;

        case ND_CALL:
            ast_node(list, node->expr, custom_args);
            if(node->data_type) 
                ast_type(list, node->data_type, custom_args);
            for(size_t i = 0; i < node->args->size; i++)
                ast_node(list, node->args->items[i], custom_args);
            break;

        case ND_INDEX:
            ast_node(list, node->left, custom_args);
            ast_node(list, node->expr, custom_args);
            if(node->data_type) 
                ast_type(list, node->data_type, custom_args);
            break;

        case ND_CAST:
            ast_node(list, node->left, custom_args);
            if(node->data_type) 
                ast_type(list, node->data_type, custom_args);
            break;

        case ND_LEN:
        case ND_SIZEOF:
            ast_node(list, node->expr, custom_args);
            if(node->data_type) 
                ast_type(list, node->data_type, custom_args);
            break;

        case ND_BLOCK:
            for(size_t i = 0; i < node->locals->size; i++)
                ast_obj(list, node->locals->items[i], custom_args);
            for(size_t i = 0; i < node->stmts->size; i++)
                ast_node(list, node->stmts->items[i], custom_args);
            break;

        case ND_IF:
        case ND_CASE:
        case ND_WHILE:
            ast_node(list, node->condition, custom_args);
            ast_node(list, node->body, custom_args);
            break;

        case ND_LOOP:
            ast_node(list, node->body, custom_args);
            break;

        case ND_FOR:
            if(node->init_stmt)
                ast_node(list, node->init_stmt, custom_args);
            if(node->condition)
                ast_node(list, node->condition, custom_args);
            if(node->expr)
                ast_node(list, node->expr, custom_args);
            ast_node(list, node->body, custom_args);
            break;

        case ND_MATCH:
            ast_node(list, node->body, custom_args);
            for(size_t i = 0; i < node->cases->size; i++)
                ast_node(list, node->cases->items[i], custom_args);
            ast_node(list, node->body, custom_args);
            break;

        case ND_RETURN:
            ast_node(list, node->return_val, custom_args);
            break;

        case ND_VA_ARG:
            ast_type(list, node->data_type, custom_args);
        case ND_EXPR_STMT:
        case ND_ASM:
            ast_node(list, node->expr, custom_args);
            break;

        case ND_LAMBDA:
            for(size_t i = 0; i < node->args->size; i++)
                ast_obj(list, node->args->items[i], custom_args);
            ast_type(list, node->data_type, custom_args);
            ast_node(list, node->body, custom_args);
            break;

        case ND_ARRAY:
        case ND_STRUCT:
            for(size_t i = 0; i < node->args->size; i++)
                ast_node(list, node->args->items[i], custom_args);
            if(node->data_type)
                ast_type(list, node->data_type, custom_args);
            break;

        case ND_STRUCT_MEMBER:
            ast_id(list, true, node->id, custom_args);
            ast_type(list, node->data_type, custom_args);
            break;

        case ND_ENUM_MEMBER:
            ast_id(list, true, node->id, custom_args);
            if(node->expr)
                ast_node(list, node->expr, custom_args);
            break;
        
        default:
            // ignore
            break;
    }

    list_fn(list->node_end_fns[node->kind], node, custom_args);
}

static void ast_type(ASTIteratorList_T* list, ASTType_T* type, va_list custom_args)
{
    if(!type)
        return;

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
        case TY_BOOL:
        case TY_CHAR:
        case TY_VOID:
        case TY_UNDEF:
        case TY_TEMPLATE: // FIXME: temporary
            list_fn(list->type_fns[type->kind], type, custom_args);
            break;
        
        case TY_PTR:
            ast_type(list, type->base, custom_args);
            list_fn(list->type_fns[TY_PTR], type, custom_args);
            break;

        case TY_ARR:
            ast_type(list, type->base, custom_args);
            if(type->num_indices)
                ast_node(list, type->num_indices, custom_args);
            
            list_fn(list->type_fns[TY_ARR], type, custom_args);
            break;

        case TY_OPAQUE_STRUCT:
            ast_id(list, type->id, false, custom_args);
            list_fn(list->type_fns[TY_OPAQUE_STRUCT], type, custom_args);
            break;

        case TY_ENUM:
            for(size_t i = 0; i < type->members->size; i++)
                ast_node(list, type->members->items[i], custom_args);
            list_fn(list->type_fns[TY_ENUM], type, custom_args);
            break;

        case TY_STRUCT:
            for(size_t i = 0; i < type->members->size; i++)
                ast_node(list, type->members->items[i], custom_args);
            list_fn(list->type_fns[TY_STRUCT], type, custom_args);
            break;
        
        case TY_LAMBDA:
            ast_type(list, type->base, custom_args);
            for(size_t i = 0; i < type->arg_types->size; i++)
                ast_type(list, type->arg_types->items[i], custom_args);
            list_fn(list->type_fns[TY_LAMBDA], type, custom_args);
            break;

        case TY_TUPLE:
            for(size_t i = 0; i < type->arg_types->size; i++)
                ast_type(list, type->arg_types->items[i], custom_args);
            list_fn(list->type_fns[TY_TUPLE], type, custom_args);
            break;
        
        default:
            // ignore
            break;
    }
}

static void ast_id(ASTIteratorList_T* list, bool is_definition, ASTIdentifier_T* id, va_list custom_args)
{
    if(is_definition) 
    {
        list_fn(list->id_def_fn, id, custom_args);
    }
    else
    {
        list_fn(list->id_use_fn, id, custom_args);
    }
}