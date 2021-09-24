#ifndef CSPYDR_PREPROCESSOR_H
#define CSPYDR_PREPROCESSOR_H

#include "../list.h"
#include "lexer.h"

typedef struct PREPROCESSOR_STRUCT Preprocessor_T;

List_T* lex_and_preprocess_tokens(Lexer_T* lex, List_T* files, bool is_silent);

#endif