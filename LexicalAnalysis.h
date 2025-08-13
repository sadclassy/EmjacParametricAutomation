#ifndef LEXICAL_ANALYSIS_H
#define LEXICAL_ANALYSIS_H

#include "utility.h"

typedef enum {
    tok_eof,
    tok_keyword,
    tok_type,
    tok_option,
    tok_identifier,
    tok_string,
    tok_number,
    tok_equal,   // =
    tok_eq,      // ==
    tok_ne,      // <>
    tok_lt,      // <
    tok_gt,      // >
    tok_le,      // <= (new)
    tok_ge,      // >= (new)
    tok_plus,    // +
    tok_minus,   // -
    tok_star,    // *
    tok_slash,   // /
    tok_backslash, // \:
    tok_bar,     // |
    tok_ampersand, // &
    tok_lparen,  // ( (new)
    tok_rparen,  // ) (new)
    tok_lbrace,  // {
    tok_rbrace,  // }
    tok_lbracket,// [
    tok_rbracket,// ]
    tok_colon,   // :
    tok_comma,   // , (new)
    tok_tab,
    tok_dot,
    tok_and,
    tok_or,
    tok_field,
    tok_newline
} Token;

typedef struct {
    size_t line;
    size_t col;
} Location;

typedef struct {
    Token type;
    char* val;
    Location loc;
} TokenData;

typedef struct {
    char* start_tok;
    char* cur_tok;
    TokenData* tokens;
    size_t token_count;
    size_t capacity;
    size_t line_number;
    char* line_start;
    int in_table; // Flag for table mode
    int pending_table_start;
    Token last_token;
} Lexer;

int lex(Lexer* lexer);
void free_lexer(Lexer* lexer);
char* token_to_string(Token token);




#endif