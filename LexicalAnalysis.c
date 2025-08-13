#pragma warning(push)
#pragma warning(disable: 6001)

#include "utility.h"
#include "LexicalAnalysis.h"

static void add_token(Lexer* lexer, Token type, char* val);
static int is_keyword(const char* str);
static int is_number(const char* str);
static int is_operator_char(char c);

static int is_option(const char* str)
{
    static const char* all_options[] = {
        "NO_TABLES", "NO_GUI", "AUTO_COMMIT", "AUTO_CLOSE", "SHOW_GUI_FOR_EXISTING",
        "NO_AUTO_UPDATE", "CONTINUE_ON_CANCEL", "SCREEN_LOCATION",
        "ON_PICTURE", "TOOLTIP",
        "NO_AUTOSEL", "NO_FILTER", "DEPEND_ON_INPUT", "DEFAULT_FOR", "WIDTH",
        "DECIMAL_PLACES", "MODEL", "REQUIRED", "NO_UPDATE", "DISPLAY_ORDER",
        "MIN_VALUE", "MAX_VALUE", "NO_AUTOSEL", "NO_FILTER", "DEPEND_ON_INPUT",
        "INVALIDATE_ON_UNSELECT", "SHOW_AUTOSEL", "FILTER_RIGID", "FILTER_ONLY_COLUMN",
        "FILTER_COLUMN", "TABLE_HEIGHT", "ARRAY"
    };
    size_t num_options = sizeof(all_options) / sizeof(all_options[0]);
    for (size_t i = 0; i < num_options; i++) {
        if (strcmp(str, all_options[i]) == 0) return 1;
    }
    return 0;
}

static int is_type_specifier(const char* str) {
    static const char* type_specs[] = {
        "STRING", "INTEGER", "DOUBLE", "BOOL", "PLANE", "SURFACE", "POINT", "AXIS", "CURVE", "EDGE",
        "SUBTABLE", "SUBCOMP", "CONFIG_DELETE_IDS", "CONFIG_STATE", "NO_VALUE"
    };
    size_t num_type_specs = sizeof(type_specs) / sizeof(type_specs[0]);
    for (size_t i = 0; i < num_type_specs; i++) {
        if (strcmp(str, type_specs[i]) == 0) return 1;
    }
    return 0;
}

static int prev_allows_unary_minus(Token t) {
    switch (t) {
    case tok_eof:
    case tok_newline:
    case tok_lparen:
    case tok_lbracket:
    case tok_equal:
    case tok_eq:
    case tok_ne:
    case tok_lt:
    case tok_gt:
    case tok_le:
    case tok_ge:
    case tok_plus:
    case tok_minus:
    case tok_star:
    case tok_slash:
    case tok_comma:
    case tok_colon:
        return 1;
    default:
        return 0;
    }
}

int lex(Lexer* lexer) {
    lexer->in_table = 0;
    lexer->pending_table_start = 0;
    lexer->last_token = tok_newline;

    while (*lexer->cur_tok != '\0') {
        while (*lexer->cur_tok != '\0' && (isspace(*lexer->cur_tok) || *lexer->cur_tok == '\t')) {
            if (*lexer->cur_tok == '\n') {
                lexer->line_number++;
                lexer->line_start = lexer->cur_tok + 1;
                lexer->last_token = tok_newline;
                if (lexer->pending_table_start) {
                    lexer->in_table = 1;
                    lexer->pending_table_start = 0;
                }
            }
            lexer->cur_tok++;
        }
        if (*lexer->cur_tok == '\0') break;

        /* Comments starting with '!' to end-of-line */
        if (*lexer->cur_tok == '!') {
            while (*lexer->cur_tok != '\n' && *lexer->cur_tok != '\0') {
                lexer->cur_tok++;
            }
            continue;
        }

        /* Strings */
        if (*lexer->cur_tok == '"') {
            lexer->cur_tok++;
            char* str_start = lexer->cur_tok;
            char* buffer = (char*)malloc(16);
            if (!buffer) {
                printf("%zu:%zu: Memory allocation failed for string buffer\n",
                    lexer->line_number, (size_t)(str_start - lexer->line_start));
                return 1;
            }
            size_t buf_len = 0;
            size_t buf_cap = 16;

            while (*lexer->cur_tok != '\0' && *lexer->cur_tok != '\n') {
                if (*lexer->cur_tok == '\\') {
                    lexer->cur_tok++;
                    if (*lexer->cur_tok == '\0' || *lexer->cur_tok == '\n') {
                        free(buffer);
                        printf("%zu:%zu: Error: Unterminated string (incomplete escape)\n",
                            lexer->line_number, (size_t)(str_start - lexer->line_start));
                        return 1;
                    }
                    char esc = *lexer->cur_tok;
                    char actual;
                    switch (esc) {
                    case 'a': actual = '\a'; break;
                    case 'b': actual = '\b'; break;
                    case 'f': actual = '\f'; break;
                    case 'n': actual = '\n'; break;
                    case 'r': actual = '\r'; break;
                    case 't': actual = '\t'; break;
                    case 'v': actual = '\v'; break;
                    case '\'': actual = '\''; break;
                    case '"': actual = '"'; break;
                    case '\\': actual = '\\'; break;
                    case '?': actual = '?'; break;
                    default:
                        printf("%zu:%zu: Warning: Unknown escape sequence '\\%c' in string\n",
                            lexer->line_number, (size_t)(lexer->cur_tok - lexer->line_start), esc);
                        if (buf_len + 1 >= buf_cap) {
                            buf_cap *= 2;
                            char* newb = (char*)realloc(buffer, buf_cap);
                            if (!newb) {
                                free(buffer);
                                printf("%zu:%zu: Memory reallocation failed for string buffer\n",
                                    lexer->line_number, (size_t)(str_start - lexer->line_start));
                                return 1;
                            }
                            buffer = newb;
                        }
                        buffer[buf_len++] = '\\';
                        actual = esc;
                    }
                    if (buf_len + 1 >= buf_cap) {
                        buf_cap *= 2;
                        char* newb = (char*)realloc(buffer, buf_cap);
                        if (!newb) {
                            free(buffer);
                            printf("%zu:%zu: Memory reallocation failed for string buffer\n",
                                lexer->line_number, (size_t)(str_start - lexer->line_start));
                            return 1;
                        }
                        buffer = newb;
                    }
                    buffer[buf_len++] = actual;
                    lexer->cur_tok++;
                    continue;
                }
                if (*lexer->cur_tok == '"') break;
                if (buf_len + 1 >= buf_cap) {
                    buf_cap *= 2;
                    char* newb = (char*)realloc(buffer, buf_cap);
                    if (!newb) {
                        free(buffer);
                        printf("%zu:%zu: Memory reallocation failed for string buffer\n",
                            lexer->line_number, (size_t)(str_start - lexer->line_start));
                        return 1;
                    }
                    buffer = newb;
                }
                buffer[buf_len++] = *lexer->cur_tok;
                lexer->cur_tok++;
            }

            if (*lexer->cur_tok != '"') {
                free(buffer);
                printf("%zu:%zu: Error: Unterminated string\n",
                    lexer->line_number, (size_t)(str_start - lexer->line_start));
                return 1;
            }
            buffer[buf_len] = '\0';
            add_token(lexer, tok_string, buffer);
            lexer->cur_tok++;
            continue;
        }

        /* Logical words */
        if (strncmp(lexer->cur_tok, "AND", 3) == 0 && !isalnum(lexer->cur_tok[3])) {
            add_token(lexer, tok_and, "AND");
            lexer->cur_tok += 3;
            continue;
        }
        else if (strncmp(lexer->cur_tok, "OR", 2) == 0 && !isalnum(lexer->cur_tok[2])) {
            add_token(lexer, tok_or, "OR");
            lexer->cur_tok += 2;
            continue;
        }

        /* Minus: always tokenize as an operator */
        if (*lexer->cur_tok == '-') {
            add_token(lexer, tok_minus, "-");
            lexer->cur_tok++;
            continue;
        }

        /* Identifiers (and filenames): allow '-' only if followed by alpha/_; allow '.' for extensions */
        if (isalpha(*lexer->cur_tok) || *lexer->cur_tok == '_') {
            char* str_start = lexer->cur_tok;
            lexer->cur_tok++;
            for (;;) {
                char c = *lexer->cur_tok;
                if (isalnum(c) || c == '_' || c == '.') {
                    lexer->cur_tok++;
                    continue;
                }
                if (c == '-' && (isalpha(*(lexer->cur_tok + 1)) || *(lexer->cur_tok + 1) == '_')) {
                    lexer->cur_tok++; /* keep hyphen inside name-like segments */
                    continue;
                }
                break;
            }
            size_t str_len = (size_t)(lexer->cur_tok - str_start);
            if (str_len > 0) {
                char* token_str = (char*)malloc(str_len + 1);
                if (!token_str) {
                    ProPrintfChar("%zu:%zu: Memory allocation failed for token\n",
                        lexer->line_number, (size_t)(str_start - lexer->line_start));
                    return 1;
                }
                memcpy(token_str, str_start, str_len);
                token_str[str_len] = '\0';

                if (is_keyword(token_str)) {
                    add_token(lexer, tok_keyword, token_str);
                }
                else if (is_type_specifier(token_str)) {
                    add_token(lexer, tok_type, token_str);
                }
                else if (is_option(token_str)) {
                    add_token(lexer, tok_option, token_str);
                }
                else if (is_number(token_str)) {
                    add_token(lexer, tok_number, token_str);
                }
                else {
                    add_token(lexer, tok_identifier, token_str);
                }
            }
            continue;
        }

        /* Unsigned numbers (no leading sign; sign is an operator) */
        if (isdigit(*lexer->cur_tok) || (*lexer->cur_tok == '.' && isdigit(*(lexer->cur_tok + 1)))) {
            char* str_start = lexer->cur_tok;
            while (isdigit(*lexer->cur_tok) || *lexer->cur_tok == '.') {
                lexer->cur_tok++;
            }
            size_t str_len = (size_t)(lexer->cur_tok - str_start);
            char* num_str = (char*)malloc(str_len + 1);
            if (!num_str) {
                ProPrintfChar("%zu:%zu: Memory allocation failed for number\n",
                    lexer->line_number, (size_t)(str_start - lexer->line_start));
                return 1;
            }
            memcpy(num_str, str_start, str_len);
            num_str[str_len] = '\0';
            add_token(lexer, tok_number, num_str);
            continue;
        }

        /* Operators and punctuation */
        if (is_operator_char(*lexer->cur_tok) || *lexer->cur_tok == '(' || *lexer->cur_tok == ')' || *lexer->cur_tok == ',') {
            if (*lexer->cur_tok == '=' && *(lexer->cur_tok + 1) == '=') {
                add_token(lexer, tok_eq, "==");
                lexer->cur_tok += 2;
            }
            else if (*lexer->cur_tok == '<' && *(lexer->cur_tok + 1) == '>') {
                add_token(lexer, tok_ne, "<>");
                lexer->cur_tok += 2;
            }
            else if (*lexer->cur_tok == '<' && *(lexer->cur_tok + 1) == '=') {
                add_token(lexer, tok_le, "<=");
                lexer->cur_tok += 2;
            }
            else if (*lexer->cur_tok == '>' && *(lexer->cur_tok + 1) == '=') {
                add_token(lexer, tok_ge, ">=");
                lexer->cur_tok += 2;
            }
            else if (*lexer->cur_tok == '<') {
                add_token(lexer, tok_lt, "<");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '>') {
                add_token(lexer, tok_gt, ">");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '=') {
                add_token(lexer, tok_equal, "=");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '+') {
                add_token(lexer, tok_plus, "+");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '*') {
                add_token(lexer, tok_star, "*");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '/') {
                add_token(lexer, tok_slash, "/");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '\\') {
                add_token(lexer, tok_backslash, "\\");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '|') {
                add_token(lexer, tok_bar, "|");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '&') {
                add_token(lexer, tok_ampersand, "&");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '(') {
                add_token(lexer, tok_lparen, "(");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == ')') {
                add_token(lexer, tok_rparen, ")");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == ',') {
                add_token(lexer, tok_comma, ",");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '{') {
                add_token(lexer, tok_lbrace, "{");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '}') {
                add_token(lexer, tok_rbrace, "}");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == '[') {
                add_token(lexer, tok_lbracket, "[");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == ']') {
                add_token(lexer, tok_rbracket, "]");
                lexer->cur_tok++;
            }
            else if (*lexer->cur_tok == ':') {
                add_token(lexer, tok_colon, ":");
                lexer->cur_tok++;
            }
            continue;
        }

        /* Fallback tokenization (table fields or bare words) */
        {
            char* str_start = lexer->cur_tok;
            if (lexer->in_table) {
                while (*lexer->cur_tok != '\0' && *lexer->cur_tok != '\t' && *lexer->cur_tok != '\n') {
                    lexer->cur_tok++;
                }
            }
            else {
                while (*lexer->cur_tok != '\0' && !isspace(*lexer->cur_tok) &&
                    *lexer->cur_tok != '\t' && !is_operator_char(*lexer->cur_tok)) {
                    lexer->cur_tok++;
                }
            }
            size_t str_len = (size_t)(lexer->cur_tok - str_start);
            if (str_len > 0) {
                char* token_str = (char*)malloc(str_len + 1);
                if (!token_str) {
                    ProPrintfChar("%zu:%zu: Memory allocation failed for token\n",
                        lexer->line_number, (size_t)(str_start - lexer->line_start));
                    return 1;
                }
                memcpy(token_str, str_start, str_len);
                token_str[str_len] = '\0';

                if (is_keyword(token_str)) {
                    add_token(lexer, tok_keyword, token_str);
                    if (strcmp(token_str, "BEGIN_TABLE") == 0 || strcmp(token_str, "BEGIN_SUBTABLE") == 0) {
                        lexer->pending_table_start = 1;
                    }
                    else if (strcmp(token_str, "END_TABLE") == 0 || strcmp(token_str, "END_SUBTABLE") == 0) {
                        lexer->in_table = 0;
                        lexer->pending_table_start = 0;
                    }
                }
                else if (lexer->in_table) {
                    add_token(lexer, tok_field, token_str);
                }
                else if (is_type_specifier(token_str)) {
                    add_token(lexer, tok_type, token_str);
                }
                else if (is_option(token_str)) {
                    add_token(lexer, tok_option, token_str);
                }
                else if (is_number(token_str)) {
                    add_token(lexer, tok_number, token_str);
                }
                else {
                    add_token(lexer, tok_identifier, token_str);
                }
            }

            if (lexer->in_table && *lexer->cur_tok == '\t') {
                lexer->cur_tok++;
            }
        }
    }

    add_token(lexer, tok_eof, NULL);
    LogOnlyPrintfChar("Reached EOF at line %zu\n", lexer->line_number);
    return 0;
}

static void add_token(Lexer* lexer, Token type, char* val) {
    if (lexer->token_count >= lexer->capacity) {
        size_t new_capacity = lexer->capacity == 0 ? 8 : lexer->capacity * 2;
        TokenData* new_tokens = realloc(lexer->tokens, new_capacity * sizeof(TokenData));
        if (!new_tokens) {
            ProPrintfChar("Error reallocating memory for tokens at line %zu, col %zu\n",
                lexer->line_number, (size_t)(lexer->cur_tok - lexer->line_start));
            exit(1);
        }
        lexer->tokens = new_tokens;
        lexer->capacity = new_capacity;
    }

    lexer->tokens[lexer->token_count].type = type;
    lexer->tokens[lexer->token_count].val = val;
    lexer->tokens[lexer->token_count].loc.line = lexer->line_number;
    lexer->tokens[lexer->token_count].loc.col = (size_t)(lexer->cur_tok - lexer->line_start);
    lexer->token_count++;
}

static int is_keyword(const char* str) {
    static const char* keywords[] = {
        "BEGIN_GUI_DESCR", "END_GUI_DESCR", "BEGIN_TAB_DESCR", "END_TAB_DESCR",
        "BEGIN_TABLE", "END_TABLE", "DECLARE_VARIABLE", "GLOBAL_PICTURE",
        "SUB_PICTURE", "SHOW_PARAM", "USER_SELECT", "USER_INPUT_PARAM",
        "RADIOBUTTON_PARAM", "CHECKBOX_PARAM", "IF", "ELSE_IF", "ELSE", "END_IF",
        "TABLE_OPTION", "SEL_STRING", "BEGIN_ASM_DESCR", "END_ASM_DESCR", "CONFIG_ELEM",
        "NO_VALUE", "BEGIN_SUBTABLE", "END_SUBTABLE", "INVALIDATE_PARAM"
    };
    size_t num_keywords = sizeof(keywords) / sizeof(keywords[0]);
    for (size_t i = 0; i < num_keywords; i++) {
        if (strcmp(str, keywords[i]) == 0) return 1;
    }
    return 0;
}

static int is_number(const char* str) {
    const char* p = str;
    int dot_count = 0;
    int has_digits = 0;

    if (*p == '\0') return 0;
    if (*p == '-') p++;  // Skip optional negative sign (already supported, but confirm)

    for (; *p != '\0'; p++) {
        if (*p == '.') {
            dot_count++;
            if (dot_count > 1) return 0;
        }
        else if (isdigit(*p)) {
            has_digits = 1;
        }
        else {
            return 0;  // Invalid character
        }
    }
    return has_digits;  // Must have at least one digit
}

static int is_operator_char(char c) {
    return c == '=' || c == '<' || c == '>' || c == '+' || c == '-' ||
        c == '*' || c == '/' || c == '\\' || c == '&' ||
        c == '|' || c == '(' || c == ')' ||
        c == ',' || c == '{' || c == '}' || c == '[' || c == ']' || c == ':';
}
void free_lexer(Lexer* lexer) {
    for (size_t i = 0; i < lexer->token_count; i++) {
        if (lexer->tokens[i].val &&
            (lexer->tokens[i].type == tok_keyword ||
                lexer->tokens[i].type == tok_identifier ||
                lexer->tokens[i].type == tok_string ||
                lexer->tokens[i].type == tok_number ||
                lexer->tokens[i].type == tok_option ||
                lexer->tokens[i].type == tok_field ||
                lexer->tokens[i].type == tok_backslash)) {
            free(lexer->tokens[i].val);
        }
    }
    free(lexer->tokens);
}

char* token_to_string(Token token) {
    switch (token) {
    case tok_eof: return "TOK_EOF";
    case tok_keyword: return "TOK_KEYWORD";
    case tok_type: return "TOK_TYPE";
    case tok_option: return "TOK_OPTION";
    case tok_identifier: return "TOK_IDENTIFIER";
    case tok_string: return "TOK_STRING";
    case tok_number: return "TOK_NUMBER";
    case tok_equal: return "TOK_EQUAL";
    case tok_eq: return "TOK_EQ";
    case tok_ne: return "TOK_NE";
    case tok_lt: return "TOK_LT";
    case tok_gt: return "TOK_GT";
    case tok_plus: return "TOK_PLUS";
    case tok_minus: return "TOK_MINUS";
    case tok_star: return "TOK_STAR";
    case tok_slash: return "TOK_SLASH";
    case tok_backslash: return "TOK_BACKSLASH"; 
    case tok_bar: return "TOK_BAR";
    case tok_tab: return "TOK_TAB";
    case tok_dot: return "TOK_DOT";
    case tok_and: return "TOK_AND";
    case tok_or: return "TOK_OR";
    case tok_field: return "TOK_FIELD";
    case tok_newline: return "TOK_NEWLINE";
    case tok_le: return "TOK_LE";
    case tok_ge: return "TOK_GE";
    case tok_lparen: return "TOK_LPAREN";
    case tok_rparen: return "TOK_RPAREN";
    case tok_comma: return "TOK_COMMA";
    case tok_lbrace: return "TOK_LBRACE";
    case tok_rbrace: return "TOK_RBRACE";
    case tok_lbracket: return "TOK_LBRACKET";
    case tok_rbracket: return "TOK_RBRACKET";
    case tok_colon: return "TOK_COLON";
    case tok_ampersand: return "TOK_AMPERSAND";
    default: return "TOK_UNKNOWN";
    }
}