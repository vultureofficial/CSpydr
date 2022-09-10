#ifndef CSPYDR_TOKEN_H
#define CSPYDR_TOKEN_H

#include "io/file.h"
#include "config.h"

#define __CSPYDR_INTERNAL_USE
#include "../../api/include/cspydr.h"
#include "util.h"

typedef enum CSPYDR_TOKEN_TYPE TokenType_T;

typedef struct CSPYDR_TOKEN_STRUCT {
    u32 line;
    u32 pos;
    TokenType_T type;

    File_T* source;

    bool in_macro_expansion;

    char value[];
} __attribute__((packed)) Token_T;

Token_T* init_token(char* value, u32 line, u32 position, TokenType_T type, File_T* source);
char* token_to_str(Token_T* token);

#endif