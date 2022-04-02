#define PARSER_TESTS {"parsing simple main function", test_parsing_simple_main_func},    \
                     {"parsing complex main function", test_parsing_complex_main_func}   \

#include "lexer/token.h"
#include "parser/parser.h"

#define PARSER_TEST_FUNC(name, src, code) \
void name(void) {                         \
    File_T* file = get_file(1, src);      \
    TEST_ASSERT(file != NULL);            \
    List_T* files = init_list();          \
    list_push(files, file);               \
    ASTProg_T prog;                       \
    parse(&prog, files, true);            \
    code                                  \
}                                                                

PARSER_TEST_FUNC(test_parsing_simple_main_func, "fn main(): i32 { ret 0; }",)

PARSER_TEST_FUNC(test_parsing_complex_main_func, "fn main(argc: i32, argv: &&char): i32 { ret 0; } [ignore_unused(\"main\")]",)