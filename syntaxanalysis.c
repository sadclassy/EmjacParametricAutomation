#pragma warning(push)
#pragma warning(disable: 6001)

#include "utility.h"
#include "LexicalAnalysis.h"
#include "syntaxanalysis.h"

static int s_if_id_counter = 0;
static int s_assign_id_counter = 0; /* new: monotonically increasing assignment ids */
static int s_table_id_counter = 0;


void free_command_node(CommandNode* node);
void free_expression(ExpressionNode* expr);
ExpressionNode* parse_expression(Lexer* lexer, size_t* i, SymbolTable* st);

char* tokens_to_string(TokenData* tokens, size_t count) {
    size_t len = 0;
    for (size_t i = 0; i < count; i++) {
        len += strlen(tokens[i].val) + 1; // +1 for space or null terminator
    }
    char* str = malloc(len);
    if (!str) return NULL;
    str[0] = '\0'; // Initialize as empty string
    for (size_t i = 0; i < count; i++) {
        if (strcat_s(str, len, tokens[i].val) != 0) {
            free(str);
            return NULL; // Error handling
        }
        if (i < count - 1) {
            if (strcat_s(str, len, " ") != 0) {
                free(str);
                return NULL; // Error handling
            }
        }
    }
    return str;
}

// Static helper functions for adding variables to the configuration map
 bool add_bool_to_map(HashTable* map, const char* key, bool value) {
    Variable* var = malloc(sizeof(Variable));
    if (!var) return false;
    var->type = TYPE_INTEGER;
    var->data.int_value = value ? 1 : 0;
    hash_table_insert(map, key, var);
    return true;
}

 bool add_double_to_map(HashTable* map, const char* key, double value) {
    Variable* var = malloc(sizeof(Variable));
    if (!var) return false;
    var->type = TYPE_DOUBLE;
    var->data.double_value = value;
    hash_table_insert(map, key, var);
    return true;
}

 bool add_int_to_map(HashTable* map, const char* key, int value) {
    Variable* var = malloc(sizeof(Variable));
    if (!var) return false;
    var->type = TYPE_INTEGER;
    var->data.int_value = value;
    hash_table_insert(map, key, var);
    return true;
}

 bool add_string_to_map(HashTable* map, const char* key, char* value) {
    if (!value) return true; // No string to add
    Variable* var = malloc(sizeof(Variable));
    if (!var) return false;
    var->type = TYPE_STRING;
    var->data.string_value = _strdup(value);
    if (!var->data.string_value) {
        free(var);
        return false;
    }
    hash_table_insert(map, key, var);
    return true;
}

// helper function to add an array of strings as a concatenated string
bool add_string_array_to_map(HashTable* map, const char* key, char** values, size_t count) {
    if (count == 0) {
        return add_string_to_map(map, key, NULL);
    }
    size_t total_len = 0;
    for (size_t k = 0; k < count; k++) {
        if (values[k]) {
            total_len += strlen(values[k]) + 2; // +2 for ", "
        }
    }
    if (total_len >= 2) total_len -= 2; // Remove extra ", "
    char* concatenated = malloc(total_len + 1);
    if (!concatenated) return false;
    concatenated[0] = '\0';
    for (size_t k = 0; k < count; k++) {
        if (values[k]) {
            strcat_s(concatenated, total_len + 1, values[k]);
            if (k < count - 1) {
                strcat_s(concatenated, total_len + 1, ", ");
            }
        }
    }
    bool result = add_string_to_map(map, key, concatenated);
    free(concatenated);
    return result;
}

// Basic function to convert ExpressionNode to string for logging (recursive)
char* expression_to_string(ExpressionNode* expr) {
    if (!expr) return _strdup("NULL");

    char buf[256];  // Buffer for simple cases
    switch (expr->type) {
    case EXPR_LITERAL_INT:
        snprintf(buf, sizeof(buf), "%ld", expr->data.int_val);
        return _strdup(buf);
    case EXPR_LITERAL_DOUBLE:
        snprintf(buf, sizeof(buf), "%.4f", expr->data.double_val);
        return _strdup(buf);
    case EXPR_LITERAL_STRING:
        snprintf(buf, sizeof(buf), "\"%s\"", expr->data.string_val);
        return _strdup(buf);
    case EXPR_LITERAL_BOOL:
        return _strdup(expr->data.int_val ? "true" : "false");
    case EXPR_CONSTANT:
        return _strdup("PI");  // Only PI for now; extend as needed
    case EXPR_VARIABLE_REF:
        return _strdup(expr->data.string_val);
    case EXPR_UNARY_OP: {
        char* operand_str = expression_to_string(expr->data.unary.operand);
        if (!operand_str) return NULL;
        snprintf(buf, sizeof(buf), "-%s", operand_str);
        free(operand_str);
        return _strdup(buf);
    }
    case EXPR_BINARY_OP: {
        char* left_str = expression_to_string(expr->data.binary.left);
        char* right_str = expression_to_string(expr->data.binary.right);
        if (!left_str || !right_str) {
            free(left_str);
            free(right_str);
            return NULL;
        }
        const char* op_str = NULL;
        switch (expr->data.binary.op) {
        case BINOP_ADD: op_str = "+"; break;
        case BINOP_SUB: op_str = "-"; break;
        case BINOP_MUL: op_str = "*"; break;
        case BINOP_DIV: op_str = "/"; break;
        case BINOP_EQ: op_str = "=="; break;
        case BINOP_NE: op_str = "<>"; break;
        case BINOP_LT: op_str = "<"; break;
        case BINOP_GT: op_str = ">"; break;
        case BINOP_LE: op_str = "<="; break;
        case BINOP_GE: op_str = ">="; break;
        case BINOP_AND: op_str = "AND"; break;  // Assuming language uses "AND"
        case BINOP_OR: op_str = "OR"; break;    // Assuming language uses "OR"
        default: op_str = "?"; break;
        }
        snprintf(buf, sizeof(buf), "%s %s %s", left_str, op_str, right_str);
        free(left_str);
        free(right_str);
        return _strdup(buf);
    }
    case EXPR_FUNCTION_CALL: {
        // Basic: function name + arg count; extend for full args
        char func_name[32];
        snprintf(func_name, sizeof(func_name), "func(%zu args)", expr->data.func_call.arg_count);
        return _strdup(func_name);
    }
                           // Add cases for EXPR_ARRAY_INDEX, EXPR_MAP_LOOKUP, EXPR_STRUCT_ACCESS (e.g., "base[index]")
    default:
        return _strdup("unsupported_expr");
    }
}

// Static helper: Advance token index and return current token
static TokenData* current_token(Lexer* lexer, size_t* i) {
    if (*i >= lexer->token_count) return NULL;
    return &lexer->tokens[*i];
}

// Static helper: Consume token if matches type, advance i
static int consume(Lexer* lexer, size_t* i, Token expected) {
    TokenData* tok = current_token(lexer, i);
    if (tok && tok->type == expected) {
        (*i)++;
        return 1;
    }
    return 0;
}  

// Helper: Map token to BinaryOpType
static BinaryOpType token_to_binary_op(Token tok_type) {
    switch (tok_type) {
    case tok_plus: return BINOP_ADD;
    case tok_minus: return BINOP_SUB;
    case tok_star: return BINOP_MUL;
    case tok_slash: return BINOP_DIV;
    case tok_eq: return BINOP_EQ;
    case tok_ne: return BINOP_NE;
    case tok_lt: return BINOP_LT;
    case tok_gt: return BINOP_GT;
    case tok_le: return BINOP_LE;
    case tok_ge: return BINOP_GE;
    case tok_and: return BINOP_AND;  // tok_and in lexer for "AND"
    case tok_or: return BINOP_OR;    // tok_or in lexer for "OR"
    default: return -1;  // Invalid
    }
}

// Helper: Map string to FunctionType (for built-in functions)
static FunctionType string_to_function(const char* name) {
    if (strcmp(name, "sin") == 0) return FUNC_SIN;
    if (strcmp(name, "asin") == 0) return FUNC_ASIN;
    if (strcmp(name, "cos") == 0) return FUNC_COS;
    if (strcmp(name, "acos") == 0) return FUNC_ACOS;
    if (strcmp(name, "tan") == 0) return FUNC_TAN;
    if (strcmp(name, "atan") == 0) return FUNC_ATAN;
    if (strcmp(name, "sinh") == 0) return FUNC_SINH;
    if (strcmp(name, "cosh") == 0) return FUNC_COSH;
    if (strcmp(name, "tanh") == 0) return FUNC_TANH;
    if (strcmp(name, "log") == 0) return FUNC_LOG;
    if (strcmp(name, "ln") == 0) return FUNC_LN;
    if (strcmp(name, "exp") == 0) return FUNC_EXP;
    if (strcmp(name, "ceil") == 0) return FUNC_CEIL;
    if (strcmp(name, "floor") == 0) return FUNC_FLOOR;
    if (strcmp(name, "abs") == 0) return FUNC_ABS;
    if (strcmp(name, "sqrt") == 0) return FUNC_SQRT;
    if (strcmp(name, "sqr") == 0) return FUNC_SQR;
    if (strcmp(name, "pow") == 0) return FUNC_POW;
    if (strcmp(name, "mod") == 0) return FUNC_MOD;
    if (strcmp(name, "round") == 0) return FUNC_ROUND;
    if (strcmp(name, "strfind") == 0) return FUNC_STRFIND;
    if (strcmp(name, "strfindcs") == 0) return FUNC_STRFINDCS;
    if (strcmp(name, "strlen") == 0) return FUNC_STRLEN;
    if (strcmp(name, "strcmp") == 0) return FUNC_STRCMP;
    if (strcmp(name, "strcmpcs") == 0) return FUNC_STRCMPCS;
    if (strcmp(name, "stof") == 0) return FUNC_STOF;
    if (strcmp(name, "stoi") == 0) return FUNC_STOI;
    if (strcmp(name, "stob") == 0) return FUNC_STOB;
    if (strcmp(name, "asc") == 0) return FUNC_ASC;
    if (strcmp(name, "isnumber") == 0) return FUNC_ISNUMBER;
    if (strcmp(name, "isinteger") == 0) return FUNC_ISINTEGER;
    if (strcmp(name, "isdouble") == 0) return FUNC_ISDOUBLE;
    if (strcmp(name, "equal") == 0) return FUNC_EQUAL;
    if (strcmp(name, "less") == 0) return FUNC_LESS;
    if (strcmp(name, "lessorequal") == 0) return FUNC_LESSOREQUAL;
    if (strcmp(name, "greater") == 0) return FUNC_GREATER;
    if (strcmp(name, "greaterorequal") == 0) return FUNC_GREATEROREQUAL;
    return -1;  // Unknown function
}

// Helper: Get precedence for binary operators (higher number = higher precedence)
int get_operator_precedence(BinaryOpType op) {
    switch (op) {
    case BINOP_MUL:
    case BINOP_DIV: return 20;  // Highest: multiplicative
    case BINOP_ADD:
    case BINOP_SUB: return 10;  // Additive
    case BINOP_EQ:
    case BINOP_NE:
    case BINOP_LT:
    case BINOP_GT:
    case BINOP_LE:
    case BINOP_GE: return 5;   // Comparisons (lowest)
    case BINOP_AND:
    case BINOP_OR: return 1;
    default: return -1;
    }
}

// Helper: Parse primary expressions (literals, variables, constants, parentheses, functions)
static ExpressionNode* parse_primary(Lexer* lexer, size_t* i, SymbolTable* st) {
    TokenData* tok = current_token(lexer, i);
    if (!tok) return NULL;

    ExpressionNode* expr = malloc(sizeof(ExpressionNode));
    if (!expr) {
        ProPrintfChar("Error: Memory allocation failed for expression node\n");
        return NULL;
    }

    if (tok->type == tok_number) {  /* numbers */
        if (strchr(tok->val, '.')) {
            expr->type = EXPR_LITERAL_DOUBLE;
            expr->data.double_val = atof(tok->val);
        }
        else {
            expr->type = EXPR_LITERAL_INT;
            expr->data.int_val = atol(tok->val);
        }
        (*i)++;
    }
    else if (tok->type == tok_identifier) {
        /* Split patterns like ELEVY-10 that were lexed as a single identifier.
           Do NOT split filenames (they contain a '.') */
        const char* s = tok->val;
        if (strchr(s, '-') && !strchr(s, '.')) {
            const char* dash = strchr(s, '-');
            if (dash > s && dash[1] != '\0') {
                /* left and right slices */
                size_t left_len = (size_t)(dash - s);
                char left[128], right[128];
                if (left_len >= sizeof(left)) left_len = sizeof(left) - 1;

                if (strncpy_s(left, sizeof(left), s, left_len) != 0) {
                    free(expr);
                    ProPrintfChar("Error: Failed copying left slice in parse_primary\n");
                    return NULL;
                }
                if (strncpy_s(right, sizeof(right), dash + 1, _TRUNCATE) != 0) {
                    free(expr);
                    ProPrintfChar("Error: Failed copying right slice in parse_primary\n");
                    return NULL;
                }

                /* build left expr (identifier or number) */
                ExpressionNode* L = (ExpressionNode*)malloc(sizeof(ExpressionNode));
                if (!L) { free(expr); return NULL; }
                if (strspn(left, "0123456789.") == strlen(left)) {
                    L->type = (strchr(left, '.') ? EXPR_LITERAL_DOUBLE : EXPR_LITERAL_INT);
                    if (L->type == EXPR_LITERAL_DOUBLE) L->data.double_val = atof(left);
                    else L->data.int_val = atol(left);
                }
                else {
                    L->type = EXPR_VARIABLE_REF;
                    L->data.string_val = _strdup(left);
                    if (!L->data.string_val) { free(L); free(expr); return NULL; }
                }

                /* build right expr (identifier or number) */
                ExpressionNode* R = (ExpressionNode*)malloc(sizeof(ExpressionNode));
                if (!R) { free_expression(L); free(expr); return NULL; }
                if (strspn(right, "0123456789.") == strlen(right)) {
                    R->type = (strchr(right, '.') ? EXPR_LITERAL_DOUBLE : EXPR_LITERAL_INT);
                    if (R->type == EXPR_LITERAL_DOUBLE) R->data.double_val = atof(right);
                    else R->data.int_val = atol(right);
                }
                else {
                    R->type = EXPR_VARIABLE_REF;
                    R->data.string_val = _strdup(right);
                    if (!R->data.string_val) { free_expression(L); free(R); free(expr); return NULL; }
                }

                /* build binary SUB node */
                ExpressionNode* B = (ExpressionNode*)malloc(sizeof(ExpressionNode));
                if (!B) { free_expression(L); free_expression(R); free(expr); return NULL; }
                B->type = EXPR_BINARY_OP;
                B->data.binary.op = BINOP_SUB;
                B->data.binary.left = L;
                B->data.binary.right = R;

                (*i)++; /* we consumed this identifier token */

                /* optional: avoid leaking the prealloc'd expr */
                free(expr);
                return B;
            }
        }

        /* existing behavior */
        expr->type = EXPR_VARIABLE_REF;
        expr->data.string_val = _strdup(tok->val);
        if (!expr->data.string_val) {
            free(expr);
            ProPrintfChar("Error: Memory allocation failed for variable reference\n");
            return NULL;
        }
        (*i)++;
    }
    else if (tok->type == tok_lparen) {  /* grouped */
        (*i)++;
        expr = parse_expression(lexer, i, st);
        if (!expr || !consume(lexer, i, tok_rparen)) {
            free_expression(expr);
            ProPrintfChar("Error: Mismatched parentheses at line %zu\n", tok->loc.line);
            return NULL;
        }
    }
    else if (tok->type == tok_minus) {  /* unary negation */
        (*i)++;
        expr->type = EXPR_UNARY_OP;
        expr->data.unary.op = UNOP_NEG;
        expr->data.unary.operand = parse_primary(lexer, i, st);
        if (!expr->data.unary.operand) { free(expr); return NULL; }
    }
    else if (tok->type == tok_type || tok->type == tok_option || tok->type == tok_string) {
        expr->type = EXPR_LITERAL_STRING;
        expr->data.string_val = _strdup(tok->val);
        if (!expr->data.string_val) { free(expr); ProPrintfChar("Error: Memory allocation failed for string literal at line %zu\n", tok->loc.line); return NULL; }
        (*i)++;
    }
    else if (tok->type == tok_keyword && strcmp(tok->val, "NO_VALUE") == 0) {
        expr->type = EXPR_LITERAL_STRING;
        expr->data.string_val = _strdup("");
        if (!expr->data.string_val) { free(expr); ProPrintfChar("Error: Memory allocation failed for NO_VALUE at line %zu\n", tok->loc.line); return NULL; }
        (*i)++;
    }
    else {
        free(expr);
        ProPrintfChar("Error: Unsupported primary expression token %d at line %zu\n", tok->type, tok->loc.line);
        return NULL;
    }

    return expr;
}

// Helper: Parse unary expressions (e.g., -expr)
static ExpressionNode* parse_unary(Lexer* lexer, size_t* i, SymbolTable* st) {
    TokenData* tok = current_token(lexer, i);
    if (tok && tok->type == tok_minus) {
        (*i)++;  // Consume -
        ExpressionNode* operand = parse_primary(lexer, i, st);
        if (!operand) return NULL;
        ExpressionNode* unary = malloc(sizeof(ExpressionNode));
        if (!unary) {
            free_expression(operand);
            return NULL;
        }
        unary->type = EXPR_UNARY_OP;
        unary->data.unary.op = UNOP_NEG;
        unary->data.unary.operand = operand;
        return unary;
    }
    return parse_primary(lexer, i, st);  // No unary, just primary
}

// Helper: Parse multiplicative factors (*, /) with left-associativity
static ExpressionNode* parse_factor(Lexer* lexer, size_t* i, SymbolTable* st) {
    ExpressionNode* left = parse_unary(lexer, i, st);
    if (!left) return NULL;

    while (true) {
        TokenData* tok = current_token(lexer, i);
        BinaryOpType op = token_to_binary_op(tok ? tok->type : tok_eof);
        if (op != BINOP_MUL && op != BINOP_DIV) break;
        (*i)++;  // Consume operator
        ExpressionNode* right = parse_unary(lexer, i, st);
        if (!right) {
            free_expression(left);
            return NULL;
        }
        ExpressionNode* binop = malloc(sizeof(ExpressionNode));
        if (!binop) {
            free_expression(left);
            free_expression(right);
            return NULL;
        }
        binop->type = EXPR_BINARY_OP;
        binop->data.binary.op = op;
        binop->data.binary.left = left;
        binop->data.binary.right = right;
        left = binop;  // Update left for associativity
    }
    return left;
}

// Helper: Parse additive terms (+, -) with left-associativity
static ExpressionNode* parse_term(Lexer* lexer, size_t* i, SymbolTable* st) {
    ExpressionNode* left = parse_factor(lexer, i, st);
    if (!left) return NULL;

    while (true) {
        TokenData* tok = current_token(lexer, i);
        BinaryOpType op = token_to_binary_op(tok ? tok->type : tok_eof);
        if (op != BINOP_ADD && op != BINOP_SUB) break;
        (*i)++;  // Consume operator
        ExpressionNode* right = parse_factor(lexer, i, st);
        if (!right) {
            free_expression(left);
            return NULL;
        }
        ExpressionNode* binop = malloc(sizeof(ExpressionNode));
        if (!binop) {
            free_expression(left);
            free_expression(right);
            return NULL;
        }
        binop->type = EXPR_BINARY_OP;
        binop->data.binary.op = op;
        binop->data.binary.left = left;
        binop->data.binary.right = right;
        left = binop;
    }
    return left;
}

// Helper: parse a chain of comparisons (==, <>, <, >, <=, >=) with left associativity
ExpressionNode* parse_comparison(Lexer* lexer, size_t* i, SymbolTable* st)
{
    ExpressionNode* left = parse_term(lexer, i, st);
    if (!left) return NULL;

    while (1) {
        TokenData* tok = current_token(lexer, i);
        BinaryOpType op = token_to_binary_op(tok ? tok->type : tok_eof);
        if (op < BINOP_EQ || op > BINOP_GE) break; /* not a comparison */

        (*i)++; /* consume operator */

        ExpressionNode* right = parse_term(lexer, i, st);
        if (!right) {
            free_expression(left);
            return NULL;
        }

        ExpressionNode* binop = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!binop) {
            free_expression(left);
            free_expression(right);
            return NULL;
        }
        binop->type = EXPR_BINARY_OP;
        binop->data.binary.op = op;
        binop->data.binary.left = left;
        binop->data.binary.right = right;
        left = binop; /* fold left-associatively */
    }
    return left;
}

// Revised: Parse full expression (lowest precedence: comparisons)
ExpressionNode* parse_expression(Lexer* lexer, size_t* i, SymbolTable* st)
{
    /* first parse a comparison unit */
    ExpressionNode* left = parse_comparison(lexer, i, st);
    if (!left) return NULL;

    /* then fold chains of AND / OR (lowest precedence) */
    while (1) {
        TokenData* tok = current_token(lexer, i);
        BinaryOpType op = token_to_binary_op(tok ? tok->type : tok_eof);
        if (op != BINOP_AND && op != BINOP_OR) break;

        (*i)++; /* consume AND/OR */

        ExpressionNode* right = parse_comparison(lexer, i, st);
        if (!right) {
            free_expression(left);
            return NULL;
        }

        ExpressionNode* binop = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!binop) {
            free_expression(left);
            free_expression(right);
            return NULL;
        }
        binop->type = EXPR_BINARY_OP;
        binop->data.binary.op = op;
        binop->data.binary.left = left;
        binop->data.binary.right = right;
        left = binop;
    }

    /* keep your existing post-processing for accesses */
    TokenData* tok = current_token(lexer, i);
    if (tok) {
        if (tok->type == tok_lbracket) {  /* base[index] */
            (*i)++;
            ExpressionNode* index = parse_expression(lexer, i, st);
            if (!index || !consume(lexer, i, tok_rbracket)) {
                ProPrintfChar("Error: Invalid array index\n");
                free_expression(left);
                free_expression(index);
                return NULL;
            }
            ExpressionNode* access = (ExpressionNode*)malloc(sizeof(ExpressionNode));
            if (!access) {
                ProPrintfChar("Memory allocation failed for ExpressionNode (array index)\n");
                free_expression(left);
                free_expression(index);
                return NULL;
            }
            access->type = EXPR_ARRAY_INDEX;
            access->data.array_index.base = left;
            access->data.array_index.index = index;
            left = access;
        }
        else if (tok->type == tok_dot) {   /* struct.member */
            (*i)++;
            tok = current_token(lexer, i);
            if (!tok || tok->type != tok_identifier) {
                ProPrintfChar("Error: Expected member name after .\n");
                free_expression(left);
                return NULL;
            }
            ExpressionNode* access = (ExpressionNode*)malloc(sizeof(ExpressionNode));
            if (!access) {
                ProPrintfChar("Memory allocation failed for ExpressionNode (struct access)\n");
                free_expression(left);
                return NULL;
            }
            access->type = EXPR_STRUCT_ACCESS;
            access->data.struct_access.structure = left;
            access->data.struct_access.member = _strdup(tok->val);
            (*i)++;
            left = access;
        }
        else if (tok->type == tok_colon) { /* map:key */
            (*i)++;
            tok = current_token(lexer, i);
            if (!tok || (tok->type != tok_identifier && tok->type != tok_string)) {
                ProPrintfChar("Error: Expected key after :\n");
                free_expression(left);
                return NULL;
            }
            ExpressionNode* access = (ExpressionNode*)malloc(sizeof(ExpressionNode));
            if (!access) {
                ProPrintfChar("Memory allocation failed for ExpressionNode (map lookup)\n");
                free_expression(left);
                return NULL;
            }
            access->type = EXPR_MAP_LOOKUP;
            access->data.map_lookup.map = left;
            access->data.map_lookup.key = _strdup(tok->val);
            (*i)++;
            left = access;
        }
    }

    return left;
}

//helper for integer being true/false
int parse_bool_literal(Lexer* lexer, size_t* i, int* out) {
    TokenData* t = current_token(lexer, i);
    if (!t) return -1;

    if (t->type == tok_identifier) {
        if (strcmp(t->val, "TRUE") == 0) { *out = 1; (*i)++; return 0; }
        if (strcmp(t->val, "FALSE") == 0) { *out = 0; (*i)++; return 0; }
    }
    if (t->type == tok_number) {
        // Treat any nonzero number as true
        long v = strtol(t->val, NULL, 10);
        *out = (v != 0);
        (*i)++;
        return 0;
    }
    return -1;
}

int add_expr(ExpressionNode*** arr, size_t* count, size_t* cap, ExpressionNode* e)
{
    if (!e) return -1;
    if (*count >= *cap) {
        size_t ncap = (*cap ? *cap * 2 : 4);
        ExpressionNode** tmp = (ExpressionNode**)realloc(*arr, ncap * sizeof(*tmp));
        if (!tmp) return -1;
        *arr = tmp;
        *cap = ncap;
    }
    (*arr)[(*count)++] = e;
    return 0;
}


/*=================================================*\
* 
* //parse_declare_variable with type-specific handling
* Perform syntax analysis for DECLARE_VARIABLE command
* 
\*=================================================*/
void free_declare_variable_node(DeclareVariableNode* node) {
    if (!node) return;

    // Free common fields
    free(node->name);
    node->name = NULL;

    // Type-specific deallocation using union fields
    switch (node->var_type) {
    case VAR_PARAMETER:
        free_expression(node->data.parameter.default_expr);
        node->data.parameter.default_expr = NULL;
        break;

    case VAR_REFERENCE:
        free(node->data.reference.entity_type);
        node->data.reference.entity_type = NULL;
        free_expression(node->data.reference.default_ref);
        node->data.reference.default_ref = NULL;
        break;

    case VAR_FILE_DESCRIPTOR:
        free(node->data.file_desc.mode);
        node->data.file_desc.mode = NULL;
        free(node->data.file_desc.path);
        node->data.file_desc.path = NULL;
        break;

    case VAR_ARRAY:
        // Free initializers array
        for (size_t idx = 0; idx < node->data.array.init_count; ++idx) {
            free_expression(node->data.array.initializers[idx]);
        }
        free(node->data.array.initializers);
        node->data.array.initializers = NULL;
        node->data.array.init_count = 0;
        // Note: element_type is a scalar; no free needed
        break;

    case VAR_MAP:
        // Free pairs array
        for (size_t idx = 0; idx < node->data.map.pair_count; ++idx) {
            free(node->data.map.pairs[idx].key);
            free_expression(node->data.map.pairs[idx].value);
        }
        free(node->data.map.pairs);
        node->data.map.pairs = NULL;
        node->data.map.pair_count = 0;
        break;

    case VAR_GENERAL:
        // Recursively free inner data if allocated
        if (node->data.general.inner_data) {
            // Create a temporary node for the inner union to recurse safely
            DeclareVariableNode inner_node;
            inner_node.var_type = node->data.general.inner_type;
            inner_node.data = node->data.general.inner_data->data;  // Access wrapped union
            inner_node.name = NULL;  // Inner has no name
            free_declare_variable_node(&inner_node);
            free(node->data.general.inner_data);
            node->data.general.inner_data = NULL;
        }
        break;

    case VAR_STRUCTURE:
        // Free members array
        for (size_t idx = 0; idx < node->data.structure.member_count; ++idx) {
            free(node->data.structure.members[idx].member_name);
            free_expression(node->data.structure.members[idx].default_expr);
            // member_type is a scalar; no free needed
        }
        free(node->data.structure.members);
        node->data.structure.members = NULL;
        node->data.structure.member_count = 0;
        break;

    default:
        // Unknown type: log if needed, but no action
        break;
    }

    // Reset union to zero for safety
    memset(&node->data, 0, sizeof(VariableData));
}

int parse_declare_variable(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    DeclareVariableNode* node = &parsed_data->declare_variable;

    node->name = NULL;
    // Initialize union to zero/NULL
    memset(&node->data, 0, sizeof(VariableData));

    char* type_str = NULL;
    TokenData* tok = current_token(lexer, i);
    int result = 0;

    // Parse variable type: handle tok_type for simple parameters or tok_keyword for complex types
    if (!tok || (tok->type != tok_keyword && tok->type != tok_type)) {
        ProPrintfChar("Error: Expected variable type (keyword or type token)\n");
        result = -1;
        goto cleanup;
    }
    type_str = _strdup(tok->val);
    (*i)++;

    // Handle simple parameter types directly (e.g., STRING, INTEGER, DOUBLE)
    if (tok->type == tok_type) {
        node->var_type = VAR_PARAMETER;
        if (strcmp(type_str, "INT") == 0 || strcmp(type_str, "INTEGER") == 0) {
            node->data.parameter.subtype = PARAM_INT;
        }
        else if (strcmp(type_str, "DOUBLE") == 0) {
            node->data.parameter.subtype = PARAM_DOUBLE;
        }
        else if (strcmp(type_str, "STRING") == 0) {
            node->data.parameter.subtype = PARAM_STRING;
        }
        else if (strcmp(type_str, "BOOL") == 0) {
            node->data.parameter.subtype = PARAM_BOOL;
        }
        else {
            ProPrintfChar("Error: Unknown simple parameter type '%s'\n", type_str);
            result = -1;
            goto cleanup;
        }
    }
    else {  // tok_keyword for complex types
        if (strcmp(type_str, "PARAMETER") == 0) {
            node->var_type = VAR_PARAMETER;
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_type) {  // Subtype like INT, DOUBLE
                if (strcmp(tok->val, "INT") == 0 || strcmp(tok->val, "INTEGER") == 0) node->data.parameter.subtype = PARAM_INT;
                else if (strcmp(tok->val, "DOUBLE") == 0) node->data.parameter.subtype = PARAM_DOUBLE;
                else if (strcmp(tok->val, "STRING") == 0) node->data.parameter.subtype = PARAM_STRING;
                else if (strcmp(tok->val, "BOOL") == 0) node->data.parameter.subtype = PARAM_BOOL;
                else {
                    ProPrintfChar("Error: Unknown parameter subtype '%s'\n", tok->val);
                    result = -1;
                    goto cleanup;
                }
                (*i)++;
            }
            else {
                ProPrintfChar("Error: Expected subtype for PARAMETER\n");
                result = -1;
                goto cleanup;
            }
            
        }
        else if (strcmp(type_str, "REFERENCE") == 0) {
            node->var_type = VAR_REFERENCE;
            // Parse entity_type (e.g., SURFACE)
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_string) {
                node->data.reference.entity_type = _strdup(tok->val);
                (*i)++;
            }
        }
        else if (strcmp(type_str, "FILE_DESCRIPTOR") == 0) {
            node->var_type = VAR_FILE_DESCRIPTOR;
            // Parse mode and path
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_string) {
                node->data.file_desc.mode = _strdup(tok->val);
                (*i)++;
                tok = current_token(lexer, i);
                if (tok && tok->type == tok_string) {
                    node->data.file_desc.path = _strdup(tok->val);
                    (*i)++;
                }
            }
        }
        else if (strcmp(type_str, "ARRAY") == 0) {
            node->var_type = VAR_ARRAY;
            // Parse element_type recursively (e.g., ARRAY PARAMETER DOUBLE)
            size_t sub_i = *i;
            CommandData temp_data;
            int sub_result = parse_declare_variable(lexer, &sub_i, &temp_data);  // Recursive call for element type
            *i = sub_i;  // Advance index after recursion
            if (sub_result == 0) {
                node->data.array.element_type = temp_data.declare_variable.var_type;
                free_declare_variable_node(&temp_data.declare_variable);
            }
            else {
                ProPrintfChar("Error: Invalid element type for ARRAY\n");
                result = -1;
                goto cleanup;
            }
            // Parse initializers {expr, expr, ...}
            if (consume(lexer, i, tok_lbrace)) {
                while (((tok = current_token(lexer, i)) != NULL) && tok->type != tok_rbrace) {
                    ExpressionNode* init = parse_expression(lexer, i, NULL);
                    if (init) {
                        ExpressionNode** new_initializers = realloc(node->data.array.initializers,
                            (node->data.array.init_count + 1) * sizeof(ExpressionNode*));
                        if (!new_initializers) {
                            ProPrintfChar("Memory reallocation failed for array initializers\n");
                            free_expression(init);  // Free the new init to avoid leak
                            result = -1;
                            goto cleanup;
                        }
                        node->data.array.initializers = new_initializers;
                        node->data.array.initializers[node->data.array.init_count++] = init;
                    }
                    consume(lexer, i, tok_comma);  // Optional comma
                }
                consume(lexer, i, tok_rbrace);
            }
        }
        else if (strcmp(type_str, "MAP") == 0) {
            node->var_type = VAR_MAP;
            // Parse pairs {key: expr, key: expr}
            if (consume(lexer, i, tok_lbrace)) {
                while (((tok = current_token(lexer, i)) != NULL) && tok->type != tok_rbrace) {
                    if (tok->type != tok_string && tok->type != tok_identifier) {
                        ProPrintfChar("Error: Expected key for MAP\n");
                        result = -1;
                        goto cleanup;
                    }
                    char* key = _strdup(tok->val);
                    (*i)++;
                    consume(lexer, i, tok_colon);
                    ExpressionNode* value = parse_expression(lexer, i, NULL);
                    if (!value) {
                        free(key);
                        ProPrintfChar("Error: Invalid value for MAP key\n");
                        result = -1;
                        goto cleanup;
                    }
                    MapPair* new_pairs = realloc(node->data.map.pairs,
                        (node->data.map.pair_count + 1) * sizeof(MapPair));
                    if (!new_pairs) {
                        ProPrintfChar("Memory reallocation failed for map pairs\n");
                        free(key);
                        free_expression(value);  // Free new resources to avoid leak
                        result = -1;
                        goto cleanup;
                    }
                    node->data.map.pairs = new_pairs;
                    node->data.map.pairs[node->data.map.pair_count].key = key;
                    node->data.map.pairs[node->data.map.pair_count++].value = value;
                    consume(lexer, i, tok_comma);
                }
                consume(lexer, i, tok_rbrace);
            }
        }
        else if (strcmp(type_str, "GENERAL") == 0) {
            node->var_type = VAR_GENERAL;
            // Recursive for inner type
            VariableDataStruct* inner = malloc(sizeof(VariableDataStruct));
            if (!inner) {
                ProPrintfChar("Memory allocation failed for general inner data\n");
                result = -1;
                goto cleanup;
            }
            size_t sub_i = *i;
            CommandData temp_data;
            if (parse_declare_variable(lexer, &sub_i, &temp_data) == 0) {
                node->data.general.inner_type = temp_data.declare_variable.var_type;
                inner->data = temp_data.declare_variable.data;  // Copy union
                free(temp_data.declare_variable.name);  // Free unused name from recursive parse
                node->data.general.inner_data = inner;  // Set only on success
                *i = sub_i;  // Advance index after recursion
            }
            else {
                free(inner);
                result = -1;
                goto cleanup;
            }
        }
        else if (strcmp(type_str, "STRUCTURE") == 0) {
            node->var_type = VAR_STRUCTURE;
            // Parse members {member_name: type default_expr, ...}
            if (consume(lexer, i, tok_lbrace)) {
                while (((tok = current_token(lexer, i)) != NULL) && tok->type != tok_rbrace) {
                    if (tok->type != tok_identifier) {
                        ProPrintfChar("Error: Expected member name for STRUCTURE\n");
                        result = -1;
                        goto cleanup;
                    }
                    char* member_name = _strdup(tok->val);
                    (*i)++;
                    consume(lexer, i, tok_colon);
                    // Parse member type recursively
                    size_t sub_i = *i;
                    CommandData temp_data;
                    if (parse_declare_variable(lexer, &sub_i, &temp_data) != 0) {
                        free(member_name);
                        ProPrintfChar("Error: Invalid member type in STRUCTURE\n");
                        result = -1;
                        goto cleanup;
                    }
                    VariableType member_type = temp_data.declare_variable.var_type;
                    *i = sub_i;  // Advance index after type parse
                    ExpressionNode* default_expr = parse_expression(lexer, i, NULL);  // Optional default; advance i directly
                    StructMember* new_members = realloc(node->data.structure.members,
                        (node->data.structure.member_count + 1) * sizeof(StructMember));
                    if (!new_members) {
                        ProPrintfChar("Memory reallocation failed for structure members\n");
                        free(member_name);
                        free_expression(default_expr);  // Free new resources to avoid leak
                        result = -1;
                        goto cleanup;
                    }
                    node->data.structure.members = new_members;
                    node->data.structure.members[node->data.structure.member_count].member_name = member_name;
                    node->data.structure.members[node->data.structure.member_count].member_type = member_type;
                    node->data.structure.members[node->data.structure.member_count++].default_expr = default_expr;
                    free_declare_variable_node(&temp_data.declare_variable);  // Free temporary parse data
                    consume(lexer, i, tok_comma);
                }
                consume(lexer, i, tok_rbrace);
            }
        }
        else {
            ProPrintfChar("Error: Unknown variable type '%s'\n", type_str);
            result = -1;
            goto cleanup;
        }
    }
    free(type_str);
    type_str = NULL;

    // Parse name (required identifier)
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected variable name\n");
        result = -1;
        goto cleanup;
    }
    node->name = _strdup(tok->val);
    (*i)++;

    // Optional default for non-initializer types (e.g., PARAMETER defaults) - no = required
    tok = current_token(lexer, i);
    if (tok && (tok->type == tok_string || tok->type == tok_number)) {
        if (node->var_type == VAR_PARAMETER) {
            node->data.parameter.default_expr = parse_expression(lexer, i, NULL);
        }
        else if (node->var_type == VAR_REFERENCE) {
            node->data.reference.default_ref = parse_expression(lexer, i, NULL);
        }
    }

    // Log for debugging: include default value if present
    char* value_str = NULL;
    if (node->var_type == VAR_PARAMETER && node->data.parameter.default_expr != NULL) {
        ExpressionNode* expr = node->data.parameter.default_expr;
        char buf[64];  // Buffer for numeric values
        switch (expr->type) {
        case EXPR_LITERAL_STRING:
            // Enclose string in quotes
        {
            size_t len = strlen(expr->data.string_val) + 3;  // For quotes and null terminator
            value_str = malloc(len);
            if (value_str) {
                snprintf(value_str, len, "\"%s\"", expr->data.string_val);
            }
            else {
                value_str = _strdup("malloc_failed");  // Fallback on allocation failure
            }
        }
        break;
        case EXPR_LITERAL_INT:
            snprintf(buf, sizeof(buf), "%ld", expr->data.int_val);
            value_str = _strdup(buf);
            break;
        case EXPR_LITERAL_DOUBLE:
            snprintf(buf, sizeof(buf), "%f", expr->data.double_val);
            value_str = _strdup(buf);
            break;
        default:
            // Fallback for unsupported expression types
            value_str = _strdup("unsupported_expr");
            break;
        }
    }
    LogOnlyPrintfChar("DeclareVariableNode: type=%d, name=%s, value=%s\n",
        node->var_type, node->name, value_str ? value_str : "NULL");
    free(value_str);  // Free allocated string if any

    return 0;

cleanup:
    if (type_str) free(type_str);
    free_declare_variable_node(node);
    return -1;
}

/*=================================================*\
* 
* // GLOBAL_PICTURE parsing
* Performs syntax analysis for GLOBAL_PICTURE command
* 
\*=================================================*/
int parse_global_picture(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    GlobalPictureNode* node = &parsed_data->global_picture;
    node->picture_expr = parse_expression(lexer, i, NULL);
    if (!node->picture_expr) {
        ProPrintfChar("Error: Expected string expression for GLOBAL_PICTURE\n");
        goto cleanup;
    }
    char* expr_str = expression_to_string(node->picture_expr);
    LogOnlyPrintfChar("GlobalPictureNode: picture_file_name=%s\n", expr_str ? expr_str : "NULL");
    free(expr_str);
    return 0;

cleanup:
    free_expression(node->picture_expr);
    return -1;
}

/*=================================================*\
* 
* // SUB_PICTURE parse
* Performs syntax analysis for SUB_PICTURE COMMAND
* 
\*=================================================*/
int parse_sub_picture(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    SubPictureNode* node = &parsed_data->sub_picture;
    node->picture_expr = parse_expression(lexer, i, NULL);
    if (!node->picture_expr) {
        ProPrintfChar("Error: Expected expression for picture_file_name in SUB_PICTURE\n");
        return -1;
    }
    node->posX_expr = parse_expression(lexer, i, NULL);
    if (!node->posX_expr) {
        ProPrintfChar("Error: Expected expression for posX in SUB_PICTURE\n");
        free_expression(node->picture_expr);
        return -1;
    }
    node->posY_expr = parse_expression(lexer, i, NULL);
    if (!node->posY_expr) {
        ProPrintfChar("Error: Expected expression for posY in SUB_PICTURE\n");
        free_expression(node->picture_expr);
        free_expression(node->posX_expr);
        return -1;
    }
    char* pic_str = expression_to_string(node->picture_expr);
    char* x_str = expression_to_string(node->posX_expr);
    char* y_str = expression_to_string(node->posY_expr);
    LogOnlyPrintfChar("SubPictureNode: picture_file_name=%s, posX_str=%s, posY_str=%s\n",
        pic_str ? pic_str : "NULL", x_str ? x_str : "NULL", y_str ? y_str : "NULL");
    free(pic_str);
    free(x_str);
    free(y_str);
    return 0;
}

/*=================================================*\
* 
* // Free function for ConfigElemNode &
* // Parse CONFIG_ELEM command
* 
\*=================================================*/
void free_config_elem_node(ConfigElemNode* node) {
    if (!node) return;

    // Free allocated expressions; booleans and flags are scalars, no free needed
    free_expression(node->location_option);
    node->location_option = NULL;

    free_expression(node->width);
    node->width = NULL;

    free_expression(node->height);
    node->height = NULL;

    // Optionally reset flags for safety, though not strictly necessary
    node->no_tables = false;
    node->no_gui = false;
    node->auto_commit = false;
    node->auto_close = false;
    node->show_gui_for_existing = false;
    node->no_auto_update = false;
    node->continue_on_cancel = false;
    node->has_screen_location = false;
}

int parse_config_elem(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    CommandData* config = (CommandData*)parsed_data;

    if (!config) {
        ProPrintfChar("Error: config is NULL\n");
        return -1;
    }

    // Initialize defaults (pointers to NULL for safe freeing)
    config->config_elem.no_tables = false;
    config->config_elem.no_gui = false;
    config->config_elem.auto_commit = false;
    config->config_elem.auto_close = false;
    config->config_elem.show_gui_for_existing = false;
    config->config_elem.no_auto_update = false;
    config->config_elem.continue_on_cancel = false;
    config->config_elem.has_screen_location = false;
    config->config_elem.location_option = NULL;
    config->config_elem.width = NULL;
    config->config_elem.height = NULL;

    TokenData* tok = NULL;
    bool parsing_options = true;

    // Parse tokens until non-matching
    while (1) {
        tok = current_token(lexer, i);
        if (tok == NULL) break;

        if (parsing_options && tok->type == tok_option) {
            (*i)++;  // Consume option
            if (strcmp(tok->val, "NO_TABLES") == 0) config->config_elem.no_tables = true;
            else if (strcmp(tok->val, "NO_GUI") == 0) config->config_elem.no_gui = true;
            else if (strcmp(tok->val, "AUTO_COMMIT") == 0) config->config_elem.auto_commit = true;
            else if (strcmp(tok->val, "AUTO_CLOSE") == 0) config->config_elem.auto_close = true;
            else if (strcmp(tok->val, "SHOW_GUI_FOR_EXISTING") == 0) config->config_elem.show_gui_for_existing = true;
            else if (strcmp(tok->val, "NO_AUTO_UPDATE") == 0) config->config_elem.no_auto_update = true;
            else if (strcmp(tok->val, "CONTINUE_ON_CANCEL") == 0) config->config_elem.continue_on_cancel = true;
            else if (strcmp(tok->val, "SCREEN_LOCATION") == 0) {
                if (config->config_elem.has_screen_location) {
                    ProPrintfChar("Error: Duplicate SCREEN_LOCATION option\n");
                    goto cleanup;
                }
                config->config_elem.has_screen_location = true;
                // Parse location as expression (expect string literal)
                config->config_elem.location_option = parse_expression(lexer, i, NULL);
                if (!config->config_elem.location_option) {
                    ProPrintfChar("Error: Failed to parse expression after SCREEN_LOCATION\n");
                    goto cleanup;
                }
                if (config->config_elem.location_option->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after SCREEN_LOCATION\n");
                    goto cleanup;
                }
            }
            else {
                ProPrintfChar("Error: Unknown option '%s' for CONFIG_ELEM\n", tok->val);
                goto cleanup;
            }
        }
        else if (tok->type == tok_number) {
            parsing_options = false;
            ExpressionNode* num_expr = parse_expression(lexer, i, NULL);
            if (!num_expr) {
                ProPrintfChar("Error: Failed to parse numeric argument for CONFIG_ELEM\n");
                goto cleanup;
            }
            if (config->config_elem.width == NULL) {
                config->config_elem.width = num_expr;
            }
            else if (config->config_elem.height == NULL) {
                config->config_elem.height = num_expr;
            }
            else {
                free_expression(num_expr);  // Free unused expression immediately
                ProPrintfChar("Error: Too many numeric arguments for CONFIG_ELEM\n");
                goto cleanup;
            }
        }
        else {
            break;  // Stop at non-matching token (e.g., next command)
        }
    }

    if (config->config_elem.height != NULL && config->config_elem.width == NULL) {
        ProPrintfChar("Error: Height specified without width for CONFIG_ELEM\n");
        goto cleanup;
    }

    // Log the parsed configuration (extract values for logging)
    char* loc_val = (config->config_elem.location_option && config->config_elem.location_option->type == EXPR_LITERAL_STRING) ? config->config_elem.location_option->data.string_val : "NULL";
    double log_width = (config->config_elem.width && (config->config_elem.width->type == EXPR_LITERAL_DOUBLE || config->config_elem.width->type == EXPR_LITERAL_INT)) ?
        (config->config_elem.width->type == EXPR_LITERAL_DOUBLE ? config->config_elem.width->data.double_val : (double)config->config_elem.width->data.int_val) : -1.0;
    double log_height = (config->config_elem.height && (config->config_elem.height->type == EXPR_LITERAL_DOUBLE || config->config_elem.height->type == EXPR_LITERAL_INT)) ?
        (config->config_elem.height->type == EXPR_LITERAL_DOUBLE ? config->config_elem.height->data.double_val : (double)config->config_elem.height->data.int_val) : -1.0;
    LogOnlyPrintfChar("ConfigElemNode: no_tables=%d, no_gui=%d, auto_commit=%d, auto_close=%d, show_gui_for_existing=%d, no_auto_update=%d, continue_on_cancel=%d, has_screen_location=%d, location_option=%s, width=%.2f, height=%.2f\n",
        config->config_elem.no_tables, config->config_elem.no_gui, config->config_elem.auto_commit, config->config_elem.auto_close,
        config->config_elem.show_gui_for_existing, config->config_elem.no_auto_update, config->config_elem.continue_on_cancel,
        config->config_elem.has_screen_location, loc_val, log_width, log_height);

    return 0;

cleanup:
    free_config_elem_node(&config->config_elem);
    return -1;
}

/*=================================================*\
* 
* // Parse SHOW_PARAM command
* Perform syntax analysis for SHOW_PARAM command 
* 
\*=================================================*/
int parse_show_param(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    CommandData* node = (CommandData*)parsed_data;

    if (!node) {
        ProPrintfChar("Error: SHOW_PARAM node is NULL\n");
        return -1;
    }

    /* initialize defaults */
    node->show_param.var_type = VAR_PARAMETER;
    node->show_param.subtype = PARAM_DOUBLE;
    node->show_param.parameter = NULL;
    node->show_param.tooltip_message = NULL;
    node->show_param.image_name = NULL;
    node->show_param.on_picture = false;
    node->show_param.posX = NULL;
    node->show_param.posY = NULL;

    char* type_str = NULL;
    TokenData* tok = NULL;
    int result = 0;

    /* ---- parameter type (subtype) ---- */
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_type) {
        ProPrintfChar("Error: Expected parameter type (e.g. DOUBLE) in SHOW_PARAM\n");
        result = -1;
        goto cleanup;
    }
    type_str = _strdup(tok->val);
    if (!type_str) {
        ProPrintfChar("Error: Memory allocation failed for temporary type string\n");
        result = -1;
        goto cleanup;
    }
    if (strcmp(type_str, "INT") == 0 || strcmp(type_str, "INTEGER") == 0) {
        node->show_param.subtype = PARAM_INT;
    }
    else if (strcmp(type_str, "DOUBLE") == 0) {
        node->show_param.subtype = PARAM_DOUBLE;
    }
    else if (strcmp(type_str, "STRING") == 0) {
        node->show_param.subtype = PARAM_STRING;
    }
    else if (strcmp(type_str, "BOOL") == 0) {
        node->show_param.subtype = PARAM_BOOL;
    }
    else {
        ProPrintfChar("Error: Unknown parameter subtype '%s' in SHOW_PARAM\n", tok->val);
        result = -1;
        goto cleanup;
    }
    (*i)++;
    free(type_str);
    type_str = NULL;

    /* ---- parameter name ---- */
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected parameter name in SHOW_PARAM\n");
        result = -1;
        goto cleanup;
    }
    node->show_param.parameter = _strdup(tok->val);
    if (!node->show_param.parameter) {
        ProPrintfChar("Error: Memory allocation failed for parameter\n");
        result = -1;
        goto cleanup;
    }
    (*i)++;

    /* ---- options loop ---- */
    while (1) {
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_option) break;

        (*i)++; /* consume option keyword */

        if (strcmp(tok->val, "TOOLTIP") == 0) {
            /* TOOLTIP <string-expr> [IMAGE <string-expr>] */
            node->show_param.tooltip_message = parse_expression(lexer, i, NULL);
            if (!node->show_param.tooltip_message ||
                node->show_param.tooltip_message->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                result = -1;
                goto cleanup;
            }
            /* optional IMAGE */
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                (*i)++;
                node->show_param.image_name = parse_expression(lexer, i, NULL);
                if (!node->show_param.image_name ||
                    node->show_param.image_name->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after IMAGE\n");
                    result = -1;
                    goto cleanup;
                }
            }
        }
        else if (strcmp(tok->val, "ON_PICTURE") == 0) {
            /* ON_PICTURE <expr posX> <expr posY>
               Accept general expressions; numeric validation happens in semantics. */
            node->show_param.posX = parse_expression(lexer, i, NULL);
            if (!node->show_param.posX) {
                ProPrintfChar("Error: Expected expression for posX after ON_PICTURE\n");
                result = -1;
                goto cleanup;
            }

            node->show_param.posY = parse_expression(lexer, i, NULL);
            if (!node->show_param.posY) {
                ProPrintfChar("Error: Expected expression for posY after posX\n");
                result = -1;
                goto cleanup;
            }

            node->show_param.on_picture = true;
        }
        else {
            ProPrintfChar("Error: Unknown option '%s' in SHOW_PARAM\n", tok->val);
            result = -1;
            goto cleanup;
        }
    }

    /* ---- logging (stringify expressions for clarity) ---- */
    {
        char* tooltip_str = expression_to_string(node->show_param.tooltip_message);
        char* image_str = expression_to_string(node->show_param.image_name);
        char* posx_str = expression_to_string(node->show_param.posX);
        char* posy_str = expression_to_string(node->show_param.posY);

        LogOnlyPrintfChar(
            "ShowParamNode: var_type=%d, subtype=%d, parameter=%s, tooltip=%s, image=%s, on_picture=%d, posX=%s, posY=%s\n",
            node->show_param.var_type, node->show_param.subtype,
            node->show_param.parameter ? node->show_param.parameter : "NULL",
            tooltip_str ? tooltip_str : "NULL",
            image_str ? image_str : "NULL",
            node->show_param.on_picture,
            posx_str ? posx_str : "NULL",
            posy_str ? posy_str : "NULL"
        );

        free(tooltip_str);
        free(image_str);
        free(posx_str);
        free(posy_str);
    }

    return 0;

cleanup:
    if (type_str) { free(type_str); type_str = NULL; }
    if (node->show_param.parameter) { free(node->show_param.parameter); node->show_param.parameter = NULL; }
    free_expression(node->show_param.tooltip_message); node->show_param.tooltip_message = NULL;
    free_expression(node->show_param.image_name);      node->show_param.image_name = NULL;
    free_expression(node->show_param.posX);            node->show_param.posX = NULL;
    free_expression(node->show_param.posY);            node->show_param.posY = NULL;
    return result;
}

/*=================================================*\
* 
* // Parse CHECKBOX_PARAM command
* perform syntax analysis for CHECKBOX_PARAM command
* 
\*=================================================*/
int parse_checkbox_param(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    CommandData* node = (CommandData*)parsed_data;

    if (!node) {
        ProGenericMsg(L"Error: CHECKBOX_PARAM node is NULL\n");
        return PRO_TK_NO_ERROR;
    }

    // Initialize node fields
    node->checkbox_param.subtype = PARAM_DOUBLE; // Default subtype; will be set based on parsing
    node->checkbox_param.parameter = NULL;
    node->checkbox_param.required = false;
    node->checkbox_param.display_order = NULL;
    node->checkbox_param.tooltip_message = NULL;
    node->checkbox_param.image_name = NULL;
    node->checkbox_param.on_picture = false;
    node->checkbox_param.posX = NULL;
    node->checkbox_param.posY = NULL;
    node->checkbox_param.tag = NULL;

    char* type_str = NULL;
    TokenData* tok = NULL;
    int result = 0;

    // Parse parameter_type (required) and map to subtype
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_type) {
        ProPrintfChar("Error: Expected parameter type (e.g., INTEGER) in CHECKBOX_PARAM\n");
        result = -1;
        goto cleanup;
    }
    type_str = _strdup(tok->val);
    if (!type_str) {
        ProPrintfChar("Error: Memory allocation failed for temporary type string\n");
        result = -1;
        goto cleanup;
    }
    if (strcmp(type_str, "INT") == 0 || strcmp(type_str, "INTEGER") == 0) {
        node->checkbox_param.subtype = PARAM_INT;
    }
    else if (strcmp(type_str, "DOUBLE") == 0) {
        node->checkbox_param.subtype = PARAM_DOUBLE;
    }
    else if (strcmp(type_str, "STRING") == 0) {
        node->checkbox_param.subtype = PARAM_STRING;
    }
    else if (strcmp(type_str, "BOOL") == 0) {
        node->checkbox_param.subtype = PARAM_BOOL;
    }
    else {
        ProPrintfChar("Error: Unknown parameter subtype '%s' in CHECKBOX_PARAM\n", tok->val);
        result = -1;
        goto cleanup;
    }
    (*i)++;
    free(type_str);
    type_str = NULL;

    // Parse parameter (required)
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected parameter name in CHECKBOX_PARAM\n");
        result = -1;
        goto cleanup;
    }
    node->checkbox_param.parameter = _strdup(tok->val);
    if (!node->checkbox_param.parameter) {
        ProPrintfChar("Error: Memory allocation failed for parameter\n");
        result = -1;
        goto cleanup;
    }
    (*i)++;

    // Parse options and tag in any order until non-option/non-string token
    while ((tok = current_token(lexer, i)) != NULL) {
        if (tok->type == tok_option) {
            (*i)++;  // Consume option
            const char* option = tok->val;

            if (strcmp(option, "REQUIRED") == 0) {
                node->checkbox_param.required = true;
            }
            else if (strcmp(option, "DISPLAY_ORDER") == 0) {
                node->checkbox_param.display_order = parse_expression(lexer, i, NULL);
                if (!node->checkbox_param.display_order || (node->checkbox_param.display_order->type != EXPR_LITERAL_INT && node->checkbox_param.display_order->type != EXPR_LITERAL_DOUBLE)) {
                    ProPrintfChar("Error: Expected numeric expression after DISPLAY_ORDER\n");
                    result = -1;
                    goto cleanup;
                }
            }
            else if (strcmp(option, "TOOLTIP") == 0) {
                node->checkbox_param.tooltip_message = parse_expression(lexer, i, NULL);
                if (!node->checkbox_param.tooltip_message || node->checkbox_param.tooltip_message->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                    result = -1;
                    goto cleanup;
                }

                // Optional IMAGE
                tok = current_token(lexer, i);
                if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                    (*i)++;
                    node->checkbox_param.image_name = parse_expression(lexer, i, NULL);
                    if (!node->checkbox_param.image_name || node->checkbox_param.image_name->type != EXPR_LITERAL_STRING) {
                        ProPrintfChar("Error: Expected string expression after IMAGE\n");
                        result = -1;
                        goto cleanup;
                    }
                }
            }
            else if (strcmp(option, "ON_PICTURE") == 0) {
                node->checkbox_param.posX = parse_expression(lexer, i, NULL);
                if (!node->checkbox_param.posX) {
                    ProPrintfChar("Error: Expected expression for posX after ON_PICTURE\n");
                    result = -1;
                    goto cleanup;
                }
                node->checkbox_param.posY = parse_expression(lexer, i, NULL);
                if (!node->checkbox_param.posY) {
                    ProPrintfChar("Error: Expected expression for posY after posX\n");
                    result = -1;
                    goto cleanup;
                }
                node->checkbox_param.on_picture = true;
            }
            else {
                ProPrintfChar("Error: Unknown option '%s' in CHECKBOX_PARAM\n", option);
                result = -1;
                goto cleanup;
            }
        }
        else if (tok->type == tok_string) {
            if (node->checkbox_param.tag != NULL) {
                ProPrintfChar("Error: Duplicate tag in CHECKBOX_PARAM\n");
                result = -1;
                goto cleanup;
            }
            node->checkbox_param.tag = parse_expression(lexer, i, NULL);
            if (!node->checkbox_param.tag || node->checkbox_param.tag->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression for tag\n");
                result = -1;
                goto cleanup;
            }
        }
        else {
            // Unexpected token; assume end of command parameters
            break;
        }
    }

    // Log parsed data
    char* tooltip_val = (node->checkbox_param.tooltip_message && node->checkbox_param.tooltip_message->type == EXPR_LITERAL_STRING) ? node->checkbox_param.tooltip_message->data.string_val : "NULL";
    char* image_val = (node->checkbox_param.image_name && node->checkbox_param.image_name->type == EXPR_LITERAL_STRING) ? node->checkbox_param.image_name->data.string_val : "NULL";
    int log_display_order = (node->checkbox_param.display_order && node->checkbox_param.display_order->type == EXPR_LITERAL_INT) ? (int)node->checkbox_param.display_order->data.int_val :
        (node->checkbox_param.display_order && node->checkbox_param.display_order->type == EXPR_LITERAL_DOUBLE ? (int)node->checkbox_param.display_order->data.double_val : -1);
    char* log_posX_str = "expression";
    int log_posX = 0;
    if (node->checkbox_param.posX) {
        if (node->checkbox_param.posX->type == EXPR_LITERAL_INT) {
            log_posX = (int)node->checkbox_param.posX->data.int_val;
            log_posX_str = NULL;  // Will use numeric value
        }
        else if (node->checkbox_param.posX->type == EXPR_LITERAL_DOUBLE) {
            log_posX = (int)node->checkbox_param.posX->data.double_val;
            log_posX_str = NULL;  // Will use numeric value
        }
    }
    char* log_posY_str = "expression";
    int log_posY = 0;
    if (node->checkbox_param.posY) {
        if (node->checkbox_param.posY->type == EXPR_LITERAL_INT) {
            log_posY = (int)node->checkbox_param.posY->data.int_val;
            log_posY_str = NULL;  // Will use numeric value
        }
        else if (node->checkbox_param.posY->type == EXPR_LITERAL_DOUBLE) {
            log_posY = (int)node->checkbox_param.posY->data.double_val;
            log_posY_str = NULL;  // Will use numeric value
        }
    }
    char* tag_val = (node->checkbox_param.tag && node->checkbox_param.tag->type == EXPR_LITERAL_STRING) ? node->checkbox_param.tag->data.string_val : "NULL";
    if (log_posX_str == NULL && log_posY_str == NULL) {
        LogOnlyPrintfChar("CheckboxParamNode: subtype=%d, parameter=%s, required=%d, display_order=%d, tooltip_message=%s, image_name=%s, on_picture=%d, posX=%d, posY=%d, tag=%s",
            node->checkbox_param.subtype, node->checkbox_param.parameter, node->checkbox_param.required, log_display_order,
            tooltip_val, image_val, node->checkbox_param.on_picture, log_posX, log_posY, tag_val);
    }
    else {
        LogOnlyPrintfChar("CheckboxParamNode: subtype=%d, parameter=%s, required=%d, display_order=%d, tooltip_message=%s, image_name=%s, on_picture=%d, posX=%s, posY=%s, tag=%s",
            node->checkbox_param.subtype, node->checkbox_param.parameter, node->checkbox_param.required, log_display_order,
            tooltip_val, image_val, node->checkbox_param.on_picture, log_posX_str ? log_posX_str : "", log_posY_str ? log_posY_str : "", tag_val);
    }
    return 0;

cleanup:
    if (type_str) {
        free(type_str);
        type_str = NULL;
    }
    if (node->checkbox_param.parameter) {
        free(node->checkbox_param.parameter);
        node->checkbox_param.parameter = NULL;
    }
    free_expression(node->checkbox_param.display_order);
    node->checkbox_param.display_order = NULL;
    free_expression(node->checkbox_param.tooltip_message);
    node->checkbox_param.tooltip_message = NULL;
    free_expression(node->checkbox_param.image_name);
    node->checkbox_param.image_name = NULL;
    free_expression(node->checkbox_param.posX);
    node->checkbox_param.posX = NULL;
    free_expression(node->checkbox_param.posY);
    node->checkbox_param.posY = NULL;
    free_expression(node->checkbox_param.tag);
    node->checkbox_param.tag = NULL;
    return -1;
}
/*=================================================*\
* 
* // Parse USER_INPUT_PARAM command
* Perform syntax analysis for USER_INPUT_PARAM command
* 
\*=================================================*/
int parse_user_input_param(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    CommandData* node = (CommandData*)parsed_data;

    if (!node) {
        ProPrintfChar("Error: USER_INPUT_PARAM node is NULL\n");
        return -1;
    }

    node->user_input_param.subtype = PARAM_DOUBLE;  // Default subtype
    node->user_input_param.parameter = NULL;
    node->user_input_param.default_expr = NULL;
    node->user_input_param.default_for_params = NULL;
    node->user_input_param.default_for_count = 0;
    node->user_input_param.width = NULL;
    node->user_input_param.decimal_places = NULL;
    node->user_input_param.model = NULL;
    node->user_input_param.required = false;
    node->user_input_param.no_update = false;
    node->user_input_param.display_order = NULL;
    node->user_input_param.min_value = NULL;
    node->user_input_param.max_value = NULL;
    node->user_input_param.tooltip_message = NULL;
    node->user_input_param.image_name = NULL;
    node->user_input_param.on_picture = false;
    node->user_input_param.posX = NULL;
    node->user_input_param.posY = NULL;

    char* type_str = NULL;
    TokenData* tok = NULL;
    int result = 0;

    // Require parameter type first (map to subtype)
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_type) {
        ProPrintfChar("Error: Expected parameter type (e.g., DOUBLE) in USER_INPUT_PARAM\n");
        result = -1;
        goto cleanup;
    }
    type_str = _strdup(tok->val);
    if (!type_str) {
        ProPrintfChar("Error: Memory allocation failed for temporary type string\n");
        result = -1;
        goto cleanup;
    }
    if (strcmp(type_str, "INT") == 0 || strcmp(type_str, "INTEGER") == 0) {
        node->user_input_param.subtype = PARAM_INT;
    }
    else if (strcmp(type_str, "DOUBLE") == 0) {
        node->user_input_param.subtype = PARAM_DOUBLE;
    }
    else if (strcmp(type_str, "STRING") == 0) {
        node->user_input_param.subtype = PARAM_STRING;
    }
    else if (strcmp(type_str, "BOOL") == 0) {
        node->user_input_param.subtype = PARAM_BOOL;
    }
    else {
        ProPrintfChar("Error: Unknown parameter subtype '%s' in USER_INPUT_PARAM\n", tok->val);
        result = -1;
        goto cleanup;
    }
    (*i)++;
    free(type_str);
    type_str = NULL;

    // Require parameter name second
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected parameter name in USER_INPUT_PARAM\n");
        result = -1;
        goto cleanup;
    }
    node->user_input_param.parameter = _strdup(tok->val);
    if (!node->user_input_param.parameter) {
        ProPrintfChar("Error: Memory allocation failed for parameter\n");
        result = -1;
        goto cleanup;
    }
    (*i)++;

    // Optional default expression (numeric)
    tok = current_token(lexer, i);
    if (tok && (tok->type == tok_number || tok->type == tok_identifier || tok->type == tok_lparen || tok->type == tok_minus)) {
        node->user_input_param.default_expr = parse_expression(lexer, i, NULL);
        if (!node->user_input_param.default_expr) {
            ProPrintfChar("Error: Failed to parse default expression in USER_INPUT_PARAM\n");
            result = -1;
            goto cleanup;
        }
    }

    // Parse options until non-option token
    while (1) {
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_option) break;
        (*i)++;  // Consume option
        const char* option = tok->val;

        if (strcmp(option, "DEFAULT_FOR") == 0) {
            size_t count = 0;
            char** params = NULL;
            tok = current_token(lexer, i);
            while (tok && tok->type == tok_identifier) {
                if (count == 0) {
                    params = malloc(sizeof(char*));
                    if (!params) {
                        ProPrintfChar("Error: Initial memory allocation failed for default_for_params\n");
                        result = -1;
                        goto cleanup;
                    }
                }
                else {
                    char** new_params = realloc(params, (count + 1) * sizeof(char*));
                    if (!new_params) {
                        ProPrintfChar("Error: Memory reallocation failed for default_for_params\n");
                        for (size_t j = 0; j < count; j++) free(params[j]);
                        free(params);
                        result = -1;
                        goto cleanup;
                    }
                    params = new_params;
                }
                params[count] = _strdup(tok->val);
                if (!params[count]) {
                    ProPrintfChar("Error: Memory allocation failed for default_for_param\n");
                    for (size_t j = 0; j < count; j++) free(params[j]);
                    free(params);
                    result = -1;
                    goto cleanup;
                }
                count++;
                (*i)++;
                tok = current_token(lexer, i);
            }
            if (count == 0) {
                ProPrintfChar("Error: Expected at least one parameter after DEFAULT_FOR\n");
                result = -1;
                goto cleanup;
            }
            node->user_input_param.default_for_params = params;
            node->user_input_param.default_for_count = count;
        }
        else if (strcmp(option, "WIDTH") == 0) {
            node->user_input_param.width = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.width || (node->user_input_param.width->type != EXPR_LITERAL_INT && node->user_input_param.width->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after WIDTH\n");
                result = -1;
                goto cleanup;
            }
        }
        else if (strcmp(option, "DECIMAL_PLACES") == 0) {
            node->user_input_param.decimal_places = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.decimal_places || (node->user_input_param.decimal_places->type != EXPR_LITERAL_INT && node->user_input_param.decimal_places->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after DECIMAL_PLACES\n");
                result = -1;
                goto cleanup;
            }
        }
        else if (strcmp(option, "MODEL") == 0) {
            node->user_input_param.model = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.model || (node->user_input_param.model->type != EXPR_LITERAL_STRING && node->user_input_param.model->type != EXPR_VARIABLE_REF)) {
                ProPrintfChar("Error: Expected string or identifier expression after MODEL\n");
                result = -1;
                goto cleanup;
            }
        }
        else if (strcmp(option, "REQUIRED") == 0) {
            node->user_input_param.required = true;
        }
        else if (strcmp(option, "NO_UPDATE") == 0) {
            node->user_input_param.no_update = true;
        }
        else if (strcmp(option, "DISPLAY_ORDER") == 0) {
            node->user_input_param.display_order = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.display_order || (node->user_input_param.display_order->type != EXPR_LITERAL_INT && node->user_input_param.display_order->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after DISPLAY_ORDER\n");
                result = -1;
                goto cleanup;
            }
        }
        else if (strcmp(option, "MIN_VALUE") == 0) {
            node->user_input_param.min_value = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.min_value || (node->user_input_param.min_value->type != EXPR_LITERAL_INT && node->user_input_param.min_value->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after MIN_VALUE\n");
                result = -1;
                goto cleanup;
            }
        }
        else if (strcmp(option, "MAX_VALUE") == 0) {
            node->user_input_param.max_value = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.max_value || (node->user_input_param.max_value->type != EXPR_LITERAL_INT && node->user_input_param.max_value->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after MAX_VALUE\n");
                result = -1;
                goto cleanup;
            }
        }
        else if (strcmp(option, "TOOLTIP") == 0) {
            node->user_input_param.tooltip_message = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.tooltip_message || node->user_input_param.tooltip_message->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                result = -1;
                goto cleanup;
            }
            // Optional IMAGE
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                (*i)++;
                node->user_input_param.image_name = parse_expression(lexer, i, NULL);
                if (!node->user_input_param.image_name || node->user_input_param.image_name->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after IMAGE\n");
                    result = -1;
                    goto cleanup;
                }
            }
        }
        else if (strcmp(option, "ON_PICTURE") == 0) {
            node->user_input_param.posX = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.posX || (node->user_input_param.posX->type != EXPR_LITERAL_INT && node->user_input_param.posX->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posX after ON_PICTURE\n");
                result = -1;
                goto cleanup;
            }
            node->user_input_param.posY = parse_expression(lexer, i, NULL);
            if (!node->user_input_param.posY || (node->user_input_param.posY->type != EXPR_LITERAL_INT && node->user_input_param.posY->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posY after posX\n");
                result = -1;
                goto cleanup;
            }
            node->user_input_param.on_picture = true;
        }
        else {
            ProPrintfChar("Error: Unknown option '%s' in USER_INPUT_PARAM\n", option);
            result = -1;
            goto cleanup;
        }
    }

    // Log the parsed USER_INPUT_PARAM (extract values for logging)
    char* default_str = expression_to_string(node->user_input_param.default_expr);
    char* width_str = expression_to_string(node->user_input_param.width);
    char* dec_places_str = expression_to_string(node->user_input_param.decimal_places);
    char* model_str = expression_to_string(node->user_input_param.model);
    char* display_order_str = expression_to_string(node->user_input_param.display_order);
    char* min_val_str = expression_to_string(node->user_input_param.min_value);
    char* max_val_str = expression_to_string(node->user_input_param.max_value);
    char* tooltip_val = expression_to_string(node->user_input_param.tooltip_message);
    char* image_val = expression_to_string(node->user_input_param.image_name);
    char* posX_str = expression_to_string(node->user_input_param.posX);
    char* posY_str = expression_to_string(node->user_input_param.posY);

    // Concatenate default_for_params
    char* default_for_str = NULL;
    if (node->user_input_param.default_for_count > 0) {
        size_t total_len = 0;
        for (size_t k = 0; k < node->user_input_param.default_for_count; k++) {
            if (node->user_input_param.default_for_params[k]) {
                total_len += strlen(node->user_input_param.default_for_params[k]) + 2; // +2 for ", "
            }
        }
        if (total_len >= 2) total_len -= 2; // Remove extra ", "
        default_for_str = malloc(total_len + 1);
        if (default_for_str) {
            default_for_str[0] = '\0';
            for (size_t k = 0; k < node->user_input_param.default_for_count; k++) {
                if (node->user_input_param.default_for_params[k]) {
                    strcat_s(default_for_str, total_len + 1, node->user_input_param.default_for_params[k]);
                    if (k < node->user_input_param.default_for_count - 1) {
                        strcat_s(default_for_str, total_len + 1, ", ");
                    }
                }
            }
        }
        else {
            default_for_str = NULL;
        }
    }
    else {
        default_for_str = NULL;
    }

    LogOnlyPrintfChar("UserInputParamNode: subtype=%d, parameter=%s, default_expr=%s, default_for_params=%s, default_for_count=%zu, width=%s, decimal_places=%s, model=%s, required=%d, no_update=%d, display_order=%s, min_value=%s, max_value=%s, tooltip_message=%s, image_name=%s, on_picture=%d, posX=%s, posY=%s\n",
        node->user_input_param.subtype, node->user_input_param.parameter ? node->user_input_param.parameter : "NULL",
        default_str ? default_str : "NULL", default_for_str ? default_for_str : "NULL", node->user_input_param.default_for_count,
        width_str ? width_str : "NULL", dec_places_str ? dec_places_str : "NULL", model_str ? model_str : "NULL",
        node->user_input_param.required, node->user_input_param.no_update,
        display_order_str ? display_order_str : "NULL", min_val_str ? min_val_str : "NULL", max_val_str ? max_val_str : "NULL",
        tooltip_val ? tooltip_val : "NULL", image_val ? image_val : "NULL",
        node->user_input_param.on_picture, posX_str ? posX_str : "NULL", posY_str ? posY_str : "NULL");

    free(default_str);
    free(width_str);
    free(dec_places_str);
    free(model_str);
    free(display_order_str);
    free(min_val_str);
    free(max_val_str);
    free(tooltip_val);
    free(image_val);
    free(posX_str);
    free(posY_str);
    if (default_for_str) free(default_for_str);

    return 0;

cleanup:
    if (type_str) {
        free(type_str);
        type_str = NULL;
    }
    // Note: Full cleanup handled by free_command_node case; here just return error
    return result;
}

/*=================================================*\
* 
* // Parse RADIOBUTTON_PARAM command
* Perform syntax analysis for RADIOBUTTON_PARAM command
* 
\*=================================================*/
int parse_radiobutton_param(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    RadioButtonParamNode* node = &parsed_data->radiobutton_param;

    if (!node) {
        ProPrintfChar("Error: RADIOBUTTON_PARAM node is NULL\n");
        return -1;
    }

    node->subtype = PARAM_INT;  // Default subtype (since value is index)
    node->parameter = NULL;
    node->options = NULL;
    node->option_count = 0;
    node->required = false;
    node->display_order = NULL;
    node->tooltip_message = NULL;
    node->image_name = NULL;
    node->on_picture = false;
    node->posX = NULL;
    node->posY = NULL;

    char* type_str = NULL;
    TokenData* tok = NULL;
    int result = 0;

    // Require parameter type first (map to subtype)
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_type) {
        ProPrintfChar("Error: Expected parameter type (e.g., INTEGER) in RADIOBUTTON_PARAM\n");
        result = -1;
        goto cleanup;
    }
    type_str = _strdup(tok->val);
    if (!type_str) {
        ProPrintfChar("Error: Memory allocation failed for temporary type string\n");
        result = -1;
        goto cleanup;
    }
    if (strcmp(type_str, "INT") == 0 || strcmp(type_str, "INTEGER") == 0) {
        node->subtype = PARAM_INT;
    }
    else if (strcmp(type_str, "DOUBLE") == 0) {
        node->subtype = PARAM_DOUBLE;
    }
    else if (strcmp(type_str, "STRING") == 0) {
        node->subtype = PARAM_STRING;
    }
    else if (strcmp(type_str, "BOOL") == 0) {
        node->subtype = PARAM_BOOL;
    }
    else {
        ProPrintfChar("Error: Unknown parameter subtype '%s' in RADIOBUTTON_PARAM\n", tok->val);
        result = -1;
        goto cleanup;
    }
    (*i)++;
    free(type_str);
    type_str = NULL;

    // Require parameter name second
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected parameter name in RADIOBUTTON_PARAM\n");
        result = -1;
        goto cleanup;
    }
    node->parameter = _strdup(tok->val);
    if (!node->parameter) {
        ProPrintfChar("Error: Memory allocation failed for parameter\n");
        result = -1;
        goto cleanup;
    }
    (*i)++;

    // Parse options as list of expressions (e.g., strings or identifiers) until option token or next command
    size_t opt_capacity = 4;
    node->options = malloc(opt_capacity * sizeof(ExpressionNode*));
    if (!node->options) {
        ProPrintfChar("Error: Memory allocation failed for options array\n");
        result = -1;
        goto cleanup;
    }
    tok = current_token(lexer, i);
    while (tok && tok->type != tok_option && tok->type != tok_keyword && tok->type != tok_eof) {
        ExpressionNode* opt = parse_expression(lexer, i, NULL);
        if (!opt) {
            ProPrintfChar("Error: Failed to parse option expression in RADIOBUTTON_PARAM\n");
            result = -1;
            goto cleanup;
        }
        // Optionally validate type (e.g., EXPR_LITERAL_STRING or EXPR_VARIABLE_REF)
        if (opt->type != EXPR_LITERAL_STRING && opt->type != EXPR_VARIABLE_REF) {
            ProPrintfChar("Error: Option must be string literal or identifier\n");
            free_expression(opt);
            result = -1;
            goto cleanup;
        }
        if (node->option_count >= opt_capacity) {
            opt_capacity *= 2;
            ExpressionNode** new_options = realloc(node->options, opt_capacity * sizeof(ExpressionNode*));
            if (!new_options) {
                ProPrintfChar("Error: Memory reallocation failed for options array\n");
                free_expression(opt);
                result = -1;
                goto cleanup;
            }
            node->options = new_options;
        }
        node->options[node->option_count++] = opt;
        consume(lexer, i, tok_comma);  // Consume optional comma
        tok = current_token(lexer, i);
    }

    if (node->option_count == 0) {
        ProPrintfChar("Warning: No options provided for RADIOBUTTON_PARAM '%s'; assuming defaults or skipping\n", node->parameter);
    }

    // Parse options until non-option token
    while (1) {
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_option) break;
        (*i)++;  // Consume option
        const char* option = tok->val;

        if (strcmp(option, "REQUIRED") == 0) {
            node->required = true;
        }
        else if (strcmp(option, "DISPLAY_ORDER") == 0) {
            node->display_order = parse_expression(lexer, i, NULL);
            if (!node->display_order) {
                ProPrintfChar("Error: Expected expression after DISPLAY_ORDER\n");
                result = -1;
                goto cleanup;
            }
            // Note: Allow general expressions (e.g., variables or operations); assume numeric evaluation at runtime.
        }
        else if (strcmp(option, "TOOLTIP") == 0) {
            node->tooltip_message = parse_expression(lexer, i, NULL);
            if (!node->tooltip_message || node->tooltip_message->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                result = -1;
                goto cleanup;
            }
            // Optional IMAGE
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                (*i)++;
                node->image_name = parse_expression(lexer, i, NULL);
                if (!node->image_name || node->image_name->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after IMAGE\n");
                    result = -1;
                    goto cleanup;
                }
            }
        }
        else if (strcmp(option, "ON_PICTURE") == 0) {
            node->posX = parse_expression(lexer, i, NULL);
            if (!node->posX) {
                ProPrintfChar("Error: Expected expression for posX after ON_PICTURE\n");
                result = -1;
                goto cleanup;
            }
            // Note: Allow general expressions (e.g., variables or operations); assume numeric evaluation at runtime.
            node->posY = parse_expression(lexer, i, NULL);
            if (!node->posY) {
                ProPrintfChar("Error: Expected expression for posY after posX\n");
                result = -1;
                goto cleanup;
            }
            // Note: Allow general expressions (e.g., variables or operations); assume numeric evaluation at runtime.
            node->on_picture = true;
        }
        else {
            ProPrintfChar("Error: Unknown option '%s' in RADIOBUTTON_PARAM\n", option);
            result = -1;
            goto cleanup;
        }
    }

    // Log the parsed RADIOBUTTON_PARAM (extract values for logging)
    char* display_order_str = expression_to_string(node->display_order);
    char* tooltip_val = expression_to_string(node->tooltip_message);
    char* image_val = expression_to_string(node->image_name);
    char* posX_str = expression_to_string(node->posX);
    char* posY_str = expression_to_string(node->posY);

    // Concatenate options
    char* options_str = NULL;
    if (node->option_count > 0) {
        size_t total_len = 0;
        for (size_t k = 0; k < node->option_count; k++) {
            char* opt_str = expression_to_string(node->options[k]);
            if (opt_str) {
                total_len += strlen(opt_str) + 2; // +2 for ", "
                free(opt_str);
            }
        }
        if (total_len >= 2) total_len -= 2;
        options_str = malloc(total_len + 1);
        if (options_str) {
            options_str[0] = '\0';
            for (size_t k = 0; k < node->option_count; k++) {
                char* opt_str = expression_to_string(node->options[k]);
                if (opt_str) {
                    strcat_s(options_str, total_len + 1, opt_str);
                    if (k < node->option_count - 1) {
                        strcat_s(options_str, total_len + 1, ", ");
                    }
                    free(opt_str);
                }
            }
        }
    }

    LogOnlyPrintfChar("RadioButtonParamNode: subtype=%d, parameter=%s, options=%s, option_count=%zu, required=%d, display_order=%s, tooltip_message=%s, image_name=%s, on_picture=%d, posX=%s, posY=%s\n",
        node->subtype, node->parameter ? node->parameter : "NULL",
        options_str ? options_str : "NULL", node->option_count,
        node->required,
        display_order_str ? display_order_str : "NULL",
        tooltip_val ? tooltip_val : "NULL", image_val ? image_val : "NULL",
        node->on_picture, posX_str ? posX_str : "NULL", posY_str ? posY_str : "NULL");

    free(display_order_str);
    free(tooltip_val);
    free(image_val);
    free(posX_str);
    free(posY_str);
    if (options_str) free(options_str);

    return 0;

cleanup:
    if (type_str) free(type_str);
    free(node->parameter);
    for (size_t k = 0; k < node->option_count; k++) {
        free_expression(node->options[k]);
    }
    free(node->options);
    free_expression(node->display_order);
    free_expression(node->tooltip_message);
    free_expression(node->image_name);
    free_expression(node->posX);
    free_expression(node->posY);
    return result;
}

/*=================================================*\
* 
* Parse USER_SELECT command  
* performs syntax analysis for USER_SELECT
* 
\*=================================================*/
int parse_user_select(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    UserSelectNode* node = &parsed_data->user_select;
    memset(node, 0, sizeof(UserSelectNode));  // Initialize to zeros/NULLs

    TokenData* tok = current_token(lexer, i);
    if (!tok) {
        ProPrintfChar("Error: Unexpected end of input in USER_SELECT\n");
        return -1;
    }

    /*-----------------------------------------
      Parse types: either &identifier, or
      a | separated list of tok_type (AXIS|PLANE)
    ------------------------------------------*/
    size_t type_capacity = 4;
    node->types = (ExpressionNode**)malloc(type_capacity * sizeof(ExpressionNode*));
    if (!node->types) {
        ProPrintfChar("Error: Memory allocation failed for types array\n");
        return -1;
    }

    if (tok->type == tok_ampersand) {
        (*i)++;  // consume '&'
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_identifier) {
            ProPrintfChar("Error: Expected identifier after & in USER_SELECT types\n");
            goto cleanup;
        }
        ExpressionNode* var_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!var_expr) {
            ProPrintfChar("Error: Memory allocation failed for type expression\n");
            goto cleanup;
        }
        var_expr->type = EXPR_VARIABLE_REF;
        {
            size_t buf_size = strlen(tok->val) + 2; // '&' + name + NUL
            var_expr->data.string_val = (char*)malloc(buf_size);
            if (!var_expr->data.string_val) {
                free(var_expr);
                ProPrintfChar("Error: Memory allocation failed for variable type string\n");
                goto cleanup;
            }
            sprintf_s(var_expr->data.string_val, buf_size, "&%s", tok->val);
        }
        node->types[0] = var_expr;
        node->type_count = 1;
        (*i)++; // consumed identifier
    }
    else {
        // Parse sequence like: AXIS | PLANE | EDGE
        while (tok && tok->type == tok_type) {
            ExpressionNode* type_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
            if (!type_expr) {
                ProPrintfChar("Error: Memory allocation failed for type expression\n");
                goto cleanup;
            }
            type_expr->type = EXPR_LITERAL_STRING;
            type_expr->data.string_val = _strdup(tok->val);
            if (!type_expr->data.string_val) {
                free(type_expr);
                ProPrintfChar("Error: Memory allocation failed for type string\n");
                goto cleanup;
            }
            if (node->type_count >= type_capacity) {
                type_capacity *= 2;
                ExpressionNode** new_types =
                    (ExpressionNode**)realloc(node->types, type_capacity * sizeof(ExpressionNode*));
                if (!new_types) {
                    free_expression(type_expr);
                    ProPrintfChar("Error: Memory reallocation failed for types array\n");
                    goto cleanup;
                }
                node->types = new_types;
            }
            node->types[node->type_count++] = type_expr;
            (*i)++; // consumed this type
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_bar) {
                (*i)++; // consume '|'
                tok = current_token(lexer, i);
            }
            else {
                break;
            }
        }
        if (node->type_count == 0) {
            ProPrintfChar("Error: Expected at least one reference type or a variable (&myType) in USER_SELECT\n");
            goto cleanup;
        }
    }

    /*-----------------------------------------
      Parse reference identifier
    ------------------------------------------*/
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected reference identifier in USER_SELECT\n");
        goto cleanup;
    }
    node->reference = _strdup(tok->val);
    if (!node->reference) {
        ProPrintfChar("Error: Memory allocation failed for reference\n");
        goto cleanup;
    }
    (*i)++;

    /*-----------------------------------------
      Parse options (same logic as before)
    ------------------------------------------*/
    tok = current_token(lexer, i);
    while (tok && tok->type == tok_option) {
        (*i)++;  // consume option
        const char* option = tok->val;

        if (strcmp(option, "DISPLAY_ORDER") == 0) {
            if (node->display_order) {
                ProPrintfChar("Error: DISPLAY_ORDER specified more than once\n");
                goto cleanup;
            }
            node->display_order = parse_expression(lexer, i, NULL);
            if (!node->display_order ||
                (node->display_order->type != EXPR_LITERAL_INT &&
                    node->display_order->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after DISPLAY_ORDER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "ALLOW_RESELECT") == 0) {
            if (node->allow_reselect) {
                ProPrintfChar("Error: ALLOW_RESELECT specified more than once\n");
                goto cleanup;
            }
            node->allow_reselect = true;
        }
        else if (strcmp(option, "FILTER_MDL") == 0) {
            if (node->filter_mdl) {
                ProPrintfChar("Error: FILTER_MDL specified more than once\n");
                goto cleanup;
            }
            node->filter_mdl = parse_expression(lexer, i, NULL);
            if (!node->filter_mdl) {
                ProPrintfChar("Error: Expected expression after FILTER_MDL\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_FEAT") == 0) {
            if (node->filter_feat) {
                ProPrintfChar("Error: FILTER_FEAT specified more than once\n");
                goto cleanup;
            }
            node->filter_feat = parse_expression(lexer, i, NULL);
            if (!node->filter_feat) {
                ProPrintfChar("Error: Expected expression after FILTER_FEAT\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_GEOM") == 0) {
            if (node->filter_geom) {
                ProPrintfChar("Error: FILTER_GEOM specified more than once\n");
                goto cleanup;
            }
            node->filter_geom = parse_expression(lexer, i, NULL);
            if (!node->filter_geom) {
                ProPrintfChar("Error: Expected expression after FILTER_GEOM\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_REF") == 0) {
            if (node->filter_ref) {
                ProPrintfChar("Error: FILTER_REF specified more than once\n");
                goto cleanup;
            }
            node->filter_ref = parse_expression(lexer, i, NULL);
            if (!node->filter_ref) {
                ProPrintfChar("Error: Expected expression after FILTER_REF\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_IDENTIFIER") == 0) {
            if (node->filter_identifier) {
                ProPrintfChar("Error: FILTER_IDENTIFIER specified more than once\n");
                goto cleanup;
            }
            node->filter_identifier = parse_expression(lexer, i, NULL);
            if (!node->filter_identifier || node->filter_identifier->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after FILTER_IDENTIFIER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "SELECT_BY_BOX") == 0) {
            if (node->select_by_box) {
                ProPrintfChar("Error: SELECT_BY_BOX specified more than once\n");
                goto cleanup;
            }
            node->select_by_box = true;
        }
        else if (strcmp(option, "SELECT_BY_MENU") == 0) {
            if (node->select_by_menu) {
                ProPrintfChar("Error: SELECT_BY_MENU specified more than once\n");
                goto cleanup;
            }
            node->select_by_menu = true;
        }
        else if (strcmp(option, "INCLUDE_MULTI_CAD") == 0) {
            if (node->include_multi_cad) {
                ProPrintfChar("Error: INCLUDE_MULTI_CAD specified more than once\n");
                goto cleanup;
            }
            node->include_multi_cad = parse_expression(lexer, i, NULL);
            if (!node->include_multi_cad || node->include_multi_cad->type != EXPR_VARIABLE_REF) {
                ProPrintfChar("Error: Expected TRUE or FALSE after INCLUDE_MULTI_CAD\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "TOOLTIP") == 0) {
            if (node->tooltip_message) {
                ProPrintfChar("Error: TOOLTIP specified more than once\n");
                goto cleanup;
            }
            node->tooltip_message = parse_expression(lexer, i, NULL);
            if (!node->tooltip_message || node->tooltip_message->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                goto cleanup;
            }
            // Optional IMAGE
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                (*i)++;
                node->image_name = parse_expression(lexer, i, NULL);
                if (!node->image_name || node->image_name->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after IMAGE\n");
                    goto cleanup;
                }
            }
        }
        else if (strcmp(option, "ON_PICTURE") == 0) {
            if (node->on_picture) {
                ProPrintfChar("Error: ON_PICTURE specified more than once\n");
                goto cleanup;
            }
            node->posX = parse_expression(lexer, i, NULL);
            if (!node->posX ||
                (node->posX->type != EXPR_LITERAL_INT && node->posX->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posX after ON_PICTURE\n");
                goto cleanup;
            }
            node->posY = parse_expression(lexer, i, NULL);
            if (!node->posY ||
                (node->posY->type != EXPR_LITERAL_INT && node->posY->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posY after posX\n");
                goto cleanup;
            }
            node->on_picture = true;
        }
        else {
            ProPrintfChar("Error: Unknown option '%s' in USER_SELECT\n", option);
            goto cleanup;
        }

        tok = current_token(lexer, i);
    }

    /*-----------------------------------------
      Trailing tag revised to match CHECKBOX_PARAM
      Primary behavior: if next token is a STRING, accept it.
      (Optional) Back-compat: accept numeric expr too.
    ------------------------------------------*/
    tok = current_token(lexer, i);
    if (tok) {
        if (tok->type == tok_string) {
            node->tag = parse_expression(lexer, i, NULL);
            if (!node->tag || node->tag->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression for tag\n");
                goto cleanup;
            }
        
        }
    }

    {
        char* types_str = NULL;
        if (node->type_count > 0) {
            size_t total_len = 0;
            for (size_t k = 0; k < node->type_count; k++) {
                char* t = expression_to_string(node->types[k]);
                if (t) { total_len += strlen(t) + 2; free(t); }
            }
            if (total_len >= 2) total_len -= 2;
            types_str = (char*)malloc(total_len + 1);
            if (types_str) {
                types_str[0] = '\0';
                for (size_t k = 0; k < node->type_count; k++) {
                    char* t = expression_to_string(node->types[k]);
                    if (t) {
                        strcat_s(types_str, total_len + 1, t);
                        if (k < node->type_count - 1) strcat_s(types_str, total_len + 1, ", ");
                        free(t);
                    }
                }
            }
        }
        char* display_order_str = expression_to_string(node->display_order);
        char* filter_mdl_str = expression_to_string(node->filter_mdl);
        char* filter_feat_str = expression_to_string(node->filter_feat);
        char* filter_geom_str = expression_to_string(node->filter_geom);
        char* filter_ref_str = expression_to_string(node->filter_ref);
        char* filter_id_str = expression_to_string(node->filter_identifier);
        char* include_str = expression_to_string(node->include_multi_cad);
        char* tooltip_str = expression_to_string(node->tooltip_message);
        char* image_str = expression_to_string(node->image_name);
        char* posX_str = expression_to_string(node->posX);
        char* posY_str = expression_to_string(node->posY);
        char* tag_str = expression_to_string(node->tag);

        LogOnlyPrintfChar(
            "UserSelectNode: types=%s, reference=%s, display_order=%s, allow_reselect=%d, "
            "filter_mdl=%s, filter_feat=%s, filter_geom=%s, filter_ref=%s, filter_identifier=%s, "
            "select_by_box=%d, select_by_menu=%d, include_multi_cad=%s, tooltip=%s, image=%s, "
            "on_picture=%d, posX=%s, posY=%s, tag=%s\n",
            types_str ? types_str : "NULL",
            node->reference ? node->reference : "NULL",
            display_order_str ? display_order_str : "NULL",
            node->allow_reselect,
            filter_mdl_str ? filter_mdl_str : "NULL",
            filter_feat_str ? filter_feat_str : "NULL",
            filter_geom_str ? filter_geom_str : "NULL",
            filter_ref_str ? filter_ref_str : "NULL",
            filter_id_str ? filter_id_str : "NULL",
            node->select_by_box, node->select_by_menu,
            include_str ? include_str : "NULL",
            tooltip_str ? tooltip_str : "NULL",
            image_str ? image_str : "NULL",
            node->on_picture,
            posX_str ? posX_str : "NULL",
            posY_str ? posY_str : "NULL",
            tag_str ? tag_str : "NULL"
        );

        if (types_str) free(types_str);
        if (display_order_str) free(display_order_str);
        if (filter_mdl_str) free(filter_mdl_str);
        if (filter_feat_str) free(filter_feat_str);
        if (filter_geom_str) free(filter_geom_str);
        if (filter_ref_str) free(filter_ref_str);
        if (filter_id_str) free(filter_id_str);
        if (include_str) free(include_str);
        if (tooltip_str) free(tooltip_str);
        if (image_str) free(image_str);
        if (posX_str) free(posX_str);
        if (posY_str) free(posY_str);
        if (tag_str) free(tag_str);
    }

    return 0;

cleanup:
    // Free partial allocations
    for (size_t k = 0; k < node->type_count; k++) {
        free_expression(node->types[k]);
    }
    free(node->types);
    node->types = NULL;
    node->type_count = 0;

    if (node->reference) { free(node->reference); node->reference = NULL; }
    free_expression(node->display_order);
    free_expression(node->filter_mdl);
    free_expression(node->filter_feat);
    free_expression(node->filter_geom);
    free_expression(node->filter_ref);
    free_expression(node->filter_identifier);
    free_expression(node->include_multi_cad);
    free_expression(node->tooltip_message);
    free_expression(node->image_name);
    free_expression(node->posX);
    free_expression(node->posY);
    free_expression(node->tag);

    return -1;
}

int parse_user_select_multiple(Lexer* lexer, size_t* i, CommandData* parsed_data)
{
    if (!lexer || !parsed_data) return -1;

    UserSelectMultipleNode* node = &parsed_data->user_select_multiple;
    memset(node, 0, sizeof(*node));

    TokenData* tok = NULL;

    /* -------- types: either &var or TYPE | TYPE | ... -------- */
    size_t type_capacity = 4;
    node->types = (ExpressionNode**)malloc(type_capacity * sizeof(ExpressionNode*));
    if (!node->types) {
        ProPrintfChar("Error: Memory allocation failed for types array\n");
        return -1;
    }
    node->type_count = 0;

    tok = current_token(lexer, i);
    if (!tok) { ProPrintfChar("Error: Unexpected end while parsing USER_SELECT_MULTIPLE\n"); goto cleanup; }

    if (tok->type == tok_ampersand) {
        (*i)++; /* consume '&' */
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_identifier) {
            ProPrintfChar("Error: Expected identifier after & in USER_SELECT_MULTIPLE types\n");
            goto cleanup;
        }
        ExpressionNode* var_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!var_expr) { ProPrintfChar("Error: Memory allocation failed for type expression\n"); goto cleanup; }
        var_expr->type = EXPR_VARIABLE_REF;
        {
            size_t buf_size = strlen(tok->val) + 2;
            var_expr->data.string_val = (char*)malloc(buf_size);
            if (!var_expr->data.string_val) { free(var_expr); ProPrintfChar("Error: Memory allocation failed for variable type string\n"); goto cleanup; }
            sprintf_s(var_expr->data.string_val, buf_size, "&%s", tok->val);
        }
        node->types[0] = var_expr;
        node->type_count = 1;
        (*i)++; /* consumed identifier */
    }
    else {
        /* Parse sequence like: AXIS | PLANE | EDGE */
        while (tok && tok->type == tok_type) {
            ExpressionNode* type_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
            if (!type_expr) { ProPrintfChar("Error: Memory allocation failed for type expression\n"); goto cleanup; }
            type_expr->type = EXPR_LITERAL_STRING;
            type_expr->data.string_val = _strdup(tok->val);
            if (!type_expr->data.string_val) { free(type_expr); ProPrintfChar("Error: Memory allocation failed for type string\n"); goto cleanup; }
            if (node->type_count >= type_capacity) {
                type_capacity *= 2;
                ExpressionNode** new_types = (ExpressionNode**)realloc(node->types, type_capacity * sizeof(ExpressionNode*));
                if (!new_types) { free_expression(type_expr); ProPrintfChar("Error: Memory reallocation failed for types array\n"); goto cleanup; }
                node->types = new_types;
            }
            node->types[node->type_count++] = type_expr;
            (*i)++; /* consumed this type */
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_bar) { (*i)++; tok = current_token(lexer, i); }
            else break;
        }
        if (node->type_count == 0) {
            ProPrintfChar("Error: Expected at least one reference type or a variable (&myType) in USER_SELECT_MULTIPLE\n");
            goto cleanup;
        }
    }

    /* -------- max_sel (INT; allow negative via unary minus) -------- */
    node->max_sel = parse_expression(lexer, i, NULL);
    if (!node->max_sel) {
        ProPrintfChar("Error: Expected max_sel integer in USER_SELECT_MULTIPLE\n");
        goto cleanup;
    }
    /* Robust guard: forbid string for max_sel, allow numeric literals or unary/binary numeric expr */
    if (node->max_sel->type == EXPR_LITERAL_STRING) {
        ProPrintfChar("Error: max_sel must be numeric in USER_SELECT_MULTIPLE\n");
        goto cleanup;
    }

    /* -------- array identifier (optionally followed by <:out>) -------- */
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected array identifier in USER_SELECT_MULTIPLE\n");
        goto cleanup;
    }
    node->array = _strdup(tok->val);
    if (!node->array) { ProPrintfChar("Error: Memory allocation failed for array name\n"); goto cleanup; }
    (*i)++;

    /* Optional <:out> decoration: array<:out> */
    tok = current_token(lexer, i);
    if (tok && tok->type == tok_lt) {
        (*i)++;
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_colon) { ProPrintfChar("Error: Expected ':' in '<:out>' after array name\n"); goto cleanup; }
        (*i)++;
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_identifier || _stricmp(tok->val, "out") != 0) {
            ProPrintfChar("Error: Expected 'out' in '<:out>' after array name\n");
            goto cleanup;
        }
        (*i)++;
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_gt) { ProPrintfChar("Error: Expected '>' in '<:out>' after array name\n"); goto cleanup; }
        (*i)++;
    }

    /* -------- options (same logic as USER_SELECT) -------- */
    tok = current_token(lexer, i);
    while (tok && tok->type == tok_option) {
        (*i)++;
        const char* option = tok->val;

        if (strcmp(option, "DISPLAY_ORDER") == 0) {
            if (node->display_order) { ProPrintfChar("Error: DISPLAY_ORDER specified more than once\n"); goto cleanup; }
            node->display_order = parse_expression(lexer, i, NULL);
            if (!node->display_order || (node->display_order->type != EXPR_LITERAL_INT && node->display_order->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after DISPLAY_ORDER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "ALLOW_RESELECT") == 0) {
            if (node->allow_reselect) { ProPrintfChar("Error: ALLOW_RESELECT specified more than once\n"); goto cleanup; }
            node->allow_reselect = true;
        }
        else if (strcmp(option, "FILTER_MDL") == 0) {
            if (node->filter_mdl) { ProPrintfChar("Error: FILTER_MDL specified more than once\n"); goto cleanup; }
            node->filter_mdl = parse_expression(lexer, i, NULL);
            if (!node->filter_mdl) { ProPrintfChar("Error: Expected expression after FILTER_MDL\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_FEAT") == 0) {
            if (node->filter_feat) { ProPrintfChar("Error: FILTER_FEAT specified more than once\n"); goto cleanup; }
            node->filter_feat = parse_expression(lexer, i, NULL);
            if (!node->filter_feat) { ProPrintfChar("Error: Expected expression after FILTER_FEAT\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_GEOM") == 0) {
            if (node->filter_geom) { ProPrintfChar("Error: FILTER_GEOM specified more than once\n"); goto cleanup; }
            node->filter_geom = parse_expression(lexer, i, NULL);
            if (!node->filter_geom) { ProPrintfChar("Error: Expected expression after FILTER_GEOM\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_REF") == 0) {
            if (node->filter_ref) { ProPrintfChar("Error: FILTER_REF specified more than once\n"); goto cleanup; }
            node->filter_ref = parse_expression(lexer, i, NULL);
            if (!node->filter_ref) { ProPrintfChar("Error: Expected expression after FILTER_REF\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_IDENTIFIER") == 0) {
            if (node->filter_identifier) { ProPrintfChar("Error: FILTER_IDENTIFIER specified more than once\n"); goto cleanup; }
            node->filter_identifier = parse_expression(lexer, i, NULL);
            if (!node->filter_identifier || node->filter_identifier->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after FILTER_IDENTIFIER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "SELECT_BY_BOX") == 0) {
            if (node->select_by_box) { ProPrintfChar("Error: SELECT_BY_BOX specified more than once\n"); goto cleanup; }
            node->select_by_box = true;
        }
        else if (strcmp(option, "SELECT_BY_MENU") == 0) {
            if (node->select_by_menu) { ProPrintfChar("Error: SELECT_BY_MENU specified more than once\n"); goto cleanup; }
            node->select_by_menu = true;
        }
        else if (strcmp(option, "INCLUDE_MULTI_CAD") == 0) {
            if (node->include_multi_cad) { ProPrintfChar("Error: INCLUDE_MULTI_CAD specified more than once\n"); goto cleanup; }
            node->include_multi_cad = parse_expression(lexer, i, NULL);
            if (!node->include_multi_cad || node->include_multi_cad->type != EXPR_VARIABLE_REF) {
                ProPrintfChar("Error: Expected TRUE or FALSE after INCLUDE_MULTI_CAD\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "TOOLTIP") == 0) {
            if (node->tooltip_message) { ProPrintfChar("Error: TOOLTIP specified more than once\n"); goto cleanup; }
            node->tooltip_message = parse_expression(lexer, i, NULL);
            if (!node->tooltip_message || node->tooltip_message->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                goto cleanup;
            }
            /* Optional IMAGE */
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                (*i)++;
                node->image_name = parse_expression(lexer, i, NULL);
                if (!node->image_name || node->image_name->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after IMAGE\n");
                    goto cleanup;
                }
            }
        }
        else if (strcmp(option, "ON_PICTURE") == 0) {
            if (node->on_picture) { ProPrintfChar("Error: ON_PICTURE specified more than once\n"); goto cleanup; }
            node->posX = parse_expression(lexer, i, NULL);
            if (!node->posX || (node->posX->type != EXPR_LITERAL_INT && node->posX->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posX after ON_PICTURE\n");
                goto cleanup;
            }
            node->posY = parse_expression(lexer, i, NULL);
            if (!node->posY || (node->posY->type != EXPR_LITERAL_INT && node->posY->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posY after posX\n");
                goto cleanup;
            }
            node->on_picture = true;
        }
        else {
            ProPrintfChar("Error: Unknown option '%s' in USER_SELECT_MULTIPLE\n", option);
            goto cleanup;
        }

        tok = current_token(lexer, i);
    }

    /* -------- trailing optional tag (same policy as USER_SELECT) -------- */
    tok = current_token(lexer, i);
    if (tok) {
        if (tok->type == tok_string) {
            node->tag = parse_expression(lexer, i, NULL);
            if (!node->tag || node->tag->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression for tag\n");
                goto cleanup;
            }
        }
    }

    /* -------- summary log -------- */
    {
        char* types_str = NULL;
        if (node->type_count > 0) {
            size_t total_len = 0;
            for (size_t k = 0; k < node->type_count; k++) {
                char* t = expression_to_string(node->types[k]);
                if (t) { total_len += strlen(t) + 2; free(t); }
            }
            if (total_len >= 2) total_len -= 2;
            types_str = (char*)malloc(total_len + 1);
            if (types_str) {
                types_str[0] = '\0';
                for (size_t k = 0; k < node->type_count; k++) {
                    char* t = expression_to_string(node->types[k]);
                    if (t) {
                        strcat_s(types_str, total_len + 1, t);
                        if (k < node->type_count - 1) strcat_s(types_str, total_len + 1, ", ");
                        free(t);
                    }
                }
            }
        }
        char* max_sel_str = expression_to_string(node->max_sel);
        char* display_order_str = expression_to_string(node->display_order);
        char* filter_mdl_str = expression_to_string(node->filter_mdl);
        char* filter_feat_str = expression_to_string(node->filter_feat);
        char* filter_geom_str = expression_to_string(node->filter_geom);
        char* filter_ref_str = expression_to_string(node->filter_ref);
        char* filter_id_str = expression_to_string(node->filter_identifier);
        char* include_str = expression_to_string(node->include_multi_cad);
        char* tooltip_str = expression_to_string(node->tooltip_message);
        char* image_str = expression_to_string(node->image_name);
        char* posX_str = expression_to_string(node->posX);
        char* posY_str = expression_to_string(node->posY);
        char* tag_str = expression_to_string(node->tag);

        LogOnlyPrintfChar(
            "UserSelectMultipleNode: types=%s, max_sel=%s, array=%s, display_order=%s, allow_reselect=%d, "
            "filter_mdl=%s, filter_feat=%s, filter_geom=%s, filter_ref=%s, filter_identifier=%s, "
            "select_by_box=%d, select_by_menu=%d, include_multi_cad=%s, tooltip=%s, image=%s, "
            "on_picture=%d, posX=%s, posY=%s, tag=%s\n",
            types_str ? types_str : "NULL",
            max_sel_str ? max_sel_str : "NULL",
            node->array ? node->array : "NULL",
            display_order_str ? display_order_str : "NULL",
            node->allow_reselect,
            filter_mdl_str ? filter_mdl_str : "NULL",
            filter_feat_str ? filter_feat_str : "NULL",
            filter_geom_str ? filter_geom_str : "NULL",
            filter_ref_str ? filter_ref_str : "NULL",
            filter_id_str ? filter_id_str : "NULL",
            (int)node->select_by_box, (int)node->select_by_menu,
            include_str ? include_str : "NULL",
            tooltip_str ? tooltip_str : "NULL",
            image_str ? image_str : "NULL",
            (int)node->on_picture,
            posX_str ? posX_str : "NULL",
            posY_str ? posY_str : "NULL",
            tag_str ? tag_str : "NULL"
        );

        free(types_str); free(max_sel_str); free(display_order_str); free(filter_mdl_str);
        free(filter_feat_str); free(filter_geom_str); free(filter_ref_str); free(filter_id_str);
        free(include_str); free(tooltip_str); free(image_str); free(posX_str); free(posY_str); free(tag_str);
    }

    return 0;

cleanup:
    /* Free on failure */
    if (node->types) {
        for (size_t k = 0; k < node->type_count; k++) free_expression(node->types[k]);
        free(node->types);
        node->types = NULL;
    }
    free_expression(node->max_sel);
    free(node->array);

    free_expression(node->display_order);
    free_expression(node->filter_mdl);
    free_expression(node->filter_feat);
    free_expression(node->filter_geom);
    free_expression(node->filter_ref);
    free_expression(node->filter_identifier);
    free_expression(node->include_multi_cad);
    free_expression(node->tooltip_message);
    free_expression(node->image_name);
    free_expression(node->posX);
    free_expression(node->posY);
    free_expression(node->tag);
    memset(node, 0, sizeof(*node));
    return -1;
}

int parse_user_select_multiple_optional(Lexer* lexer, size_t* i, CommandData* parsed_data)
{
    if (!lexer || !parsed_data) return -1;

    UserSelectMultipleOptionalNode* node = &parsed_data->user_select_multiple_optional;
    memset(node, 0, sizeof(*node));

    TokenData* tok = NULL;

    /* -------- types: either &var or TYPE | TYPE | ... -------- */
    size_t type_capacity = 4;
    node->types = (ExpressionNode**)malloc(type_capacity * sizeof(ExpressionNode*));
    if (!node->types) {
        ProPrintfChar("Error: Memory allocation failed for types array\n");
        return -1;
    }
    node->type_count = 0;

    tok = current_token(lexer, i);
    if (!tok) { ProPrintfChar("Error: Unexpected end while parsing USER_SELECT_MULTIPLE\n"); goto cleanup; }

    if (tok->type == tok_ampersand) {
        (*i)++; /* consume '&' */
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_identifier) {
            ProPrintfChar("Error: Expected identifier after & in USER_SELECT_MULTIPLE types\n");
            goto cleanup;
        }
        ExpressionNode* var_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!var_expr) { ProPrintfChar("Error: Memory allocation failed for type expression\n"); goto cleanup; }
        var_expr->type = EXPR_VARIABLE_REF;
        {
            size_t buf_size = strlen(tok->val) + 2;
            var_expr->data.string_val = (char*)malloc(buf_size);
            if (!var_expr->data.string_val) { free(var_expr); ProPrintfChar("Error: Memory allocation failed for variable type string\n"); goto cleanup; }
            sprintf_s(var_expr->data.string_val, buf_size, "&%s", tok->val);
        }
        node->types[0] = var_expr;
        node->type_count = 1;
        (*i)++; /* consumed identifier */
    }
    else {
        /* Parse sequence like: AXIS | PLANE | EDGE */
        while (tok && tok->type == tok_type) {
            ExpressionNode* type_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
            if (!type_expr) { ProPrintfChar("Error: Memory allocation failed for type expression\n"); goto cleanup; }
            type_expr->type = EXPR_LITERAL_STRING;
            type_expr->data.string_val = _strdup(tok->val);
            if (!type_expr->data.string_val) { free(type_expr); ProPrintfChar("Error: Memory allocation failed for type string\n"); goto cleanup; }
            if (node->type_count >= type_capacity) {
                type_capacity *= 2;
                ExpressionNode** new_types = (ExpressionNode**)realloc(node->types, type_capacity * sizeof(ExpressionNode*));
                if (!new_types) { free_expression(type_expr); ProPrintfChar("Error: Memory reallocation failed for types array\n"); goto cleanup; }
                node->types = new_types;
            }
            node->types[node->type_count++] = type_expr;
            (*i)++; /* consumed this type */
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_bar) { (*i)++; tok = current_token(lexer, i); }
            else break;
        }
        if (node->type_count == 0) {
            ProPrintfChar("Error: Expected at least one reference type or a variable (&myType) in USER_SELECT_MULTIPLE\n");
            goto cleanup;
        }
    }

    /* -------- max_sel (INT; allow negative via unary minus) -------- */
    node->max_sel = parse_expression(lexer, i, NULL);
    if (!node->max_sel) {
        ProPrintfChar("Error: Expected max_sel integer in USER_SELECT_MULTIPLE\n");
        goto cleanup;
    }
    /* Robust guard: forbid string for max_sel, allow numeric literals or unary/binary numeric expr */
    if (node->max_sel->type == EXPR_LITERAL_STRING) {
        ProPrintfChar("Error: max_sel must be numeric in USER_SELECT_MULTIPLE\n");
        goto cleanup;
    }

    /* -------- array identifier (optionally followed by <:out>) -------- */
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected array identifier in USER_SELECT_MULTIPLE\n");
        goto cleanup;
    }
    node->array = _strdup(tok->val);
    if (!node->array) { ProPrintfChar("Error: Memory allocation failed for array name\n"); goto cleanup; }
    (*i)++;

    /* Optional <:out> decoration: array<:out> */
    tok = current_token(lexer, i);
    if (tok && tok->type == tok_lt) {
        (*i)++;
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_colon) { ProPrintfChar("Error: Expected ':' in '<:out>' after array name\n"); goto cleanup; }
        (*i)++;
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_identifier || _stricmp(tok->val, "out") != 0) {
            ProPrintfChar("Error: Expected 'out' in '<:out>' after array name\n");
            goto cleanup;
        }
        (*i)++;
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_gt) { ProPrintfChar("Error: Expected '>' in '<:out>' after array name\n"); goto cleanup; }
        (*i)++;
    }

    /* -------- options (same logic as USER_SELECT) -------- */
    tok = current_token(lexer, i);
    while (tok && tok->type == tok_option) {
        (*i)++;
        const char* option = tok->val;

        if (strcmp(option, "DISPLAY_ORDER") == 0) {
            if (node->display_order) { ProPrintfChar("Error: DISPLAY_ORDER specified more than once\n"); goto cleanup; }
            node->display_order = parse_expression(lexer, i, NULL);
            if (!node->display_order || (node->display_order->type != EXPR_LITERAL_INT && node->display_order->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after DISPLAY_ORDER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "ALLOW_RESELECT") == 0) {
            if (node->allow_reselect) { ProPrintfChar("Error: ALLOW_RESELECT specified more than once\n"); goto cleanup; }
            node->allow_reselect = true;
        }
        else if (strcmp(option, "FILTER_MDL") == 0) {
            if (node->filter_mdl) { ProPrintfChar("Error: FILTER_MDL specified more than once\n"); goto cleanup; }
            node->filter_mdl = parse_expression(lexer, i, NULL);
            if (!node->filter_mdl) { ProPrintfChar("Error: Expected expression after FILTER_MDL\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_FEAT") == 0) {
            if (node->filter_feat) { ProPrintfChar("Error: FILTER_FEAT specified more than once\n"); goto cleanup; }
            node->filter_feat = parse_expression(lexer, i, NULL);
            if (!node->filter_feat) { ProPrintfChar("Error: Expected expression after FILTER_FEAT\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_GEOM") == 0) {
            if (node->filter_geom) { ProPrintfChar("Error: FILTER_GEOM specified more than once\n"); goto cleanup; }
            node->filter_geom = parse_expression(lexer, i, NULL);
            if (!node->filter_geom) { ProPrintfChar("Error: Expected expression after FILTER_GEOM\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_REF") == 0) {
            if (node->filter_ref) { ProPrintfChar("Error: FILTER_REF specified more than once\n"); goto cleanup; }
            node->filter_ref = parse_expression(lexer, i, NULL);
            if (!node->filter_ref) { ProPrintfChar("Error: Expected expression after FILTER_REF\n"); goto cleanup; }
        }
        else if (strcmp(option, "FILTER_IDENTIFIER") == 0) {
            if (node->filter_identifier) { ProPrintfChar("Error: FILTER_IDENTIFIER specified more than once\n"); goto cleanup; }
            node->filter_identifier = parse_expression(lexer, i, NULL);
            if (!node->filter_identifier || node->filter_identifier->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after FILTER_IDENTIFIER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "SELECT_BY_BOX") == 0) {
            if (node->select_by_box) { ProPrintfChar("Error: SELECT_BY_BOX specified more than once\n"); goto cleanup; }
            node->select_by_box = true;
        }
        else if (strcmp(option, "SELECT_BY_MENU") == 0) {
            if (node->select_by_menu) { ProPrintfChar("Error: SELECT_BY_MENU specified more than once\n"); goto cleanup; }
            node->select_by_menu = true;
        }
        else if (strcmp(option, "INCLUDE_MULTI_CAD") == 0) {
            if (node->include_multi_cad) { ProPrintfChar("Error: INCLUDE_MULTI_CAD specified more than once\n"); goto cleanup; }
            node->include_multi_cad = parse_expression(lexer, i, NULL);
            if (!node->include_multi_cad || node->include_multi_cad->type != EXPR_VARIABLE_REF) {
                ProPrintfChar("Error: Expected TRUE or FALSE after INCLUDE_MULTI_CAD\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "TOOLTIP") == 0) {
            if (node->tooltip_message) { ProPrintfChar("Error: TOOLTIP specified more than once\n"); goto cleanup; }
            node->tooltip_message = parse_expression(lexer, i, NULL);
            if (!node->tooltip_message || node->tooltip_message->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                goto cleanup;
            }
            /* Optional IMAGE */
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                (*i)++;
                node->image_name = parse_expression(lexer, i, NULL);
                if (!node->image_name || node->image_name->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after IMAGE\n");
                    goto cleanup;
                }
            }
        }
        else if (strcmp(option, "ON_PICTURE") == 0) {
            if (node->on_picture) { ProPrintfChar("Error: ON_PICTURE specified more than once\n"); goto cleanup; }
            node->posX = parse_expression(lexer, i, NULL);
            if (!node->posX || (node->posX->type != EXPR_LITERAL_INT && node->posX->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posX after ON_PICTURE\n");
                goto cleanup;
            }
            node->posY = parse_expression(lexer, i, NULL);
            if (!node->posY || (node->posY->type != EXPR_LITERAL_INT && node->posY->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posY after posX\n");
                goto cleanup;
            }
            node->on_picture = true;
        }
        else {
            ProPrintfChar("Error: Unknown option '%s' in USER_SELECT_MULTIPLE\n", option);
            goto cleanup;
        }

        tok = current_token(lexer, i);
    }

    /* -------- trailing optional tag (same policy as USER_SELECT) -------- */
    tok = current_token(lexer, i);
    if (tok) {
        if (tok->type == tok_string) {
            node->tag = parse_expression(lexer, i, NULL);
            if (!node->tag || node->tag->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression for tag\n");
                goto cleanup;
            }
        }
    }

    /* -------- summary log -------- */
    {
        char* types_str = NULL;
        if (node->type_count > 0) {
            size_t total_len = 0;
            for (size_t k = 0; k < node->type_count; k++) {
                char* t = expression_to_string(node->types[k]);
                if (t) { total_len += strlen(t) + 2; free(t); }
            }
            if (total_len >= 2) total_len -= 2;
            types_str = (char*)malloc(total_len + 1);
            if (types_str) {
                types_str[0] = '\0';
                for (size_t k = 0; k < node->type_count; k++) {
                    char* t = expression_to_string(node->types[k]);
                    if (t) {
                        strcat_s(types_str, total_len + 1, t);
                        if (k < node->type_count - 1) strcat_s(types_str, total_len + 1, ", ");
                        free(t);
                    }
                }
            }
        }
        char* max_sel_str = expression_to_string(node->max_sel);
        char* display_order_str = expression_to_string(node->display_order);
        char* filter_mdl_str = expression_to_string(node->filter_mdl);
        char* filter_feat_str = expression_to_string(node->filter_feat);
        char* filter_geom_str = expression_to_string(node->filter_geom);
        char* filter_ref_str = expression_to_string(node->filter_ref);
        char* filter_id_str = expression_to_string(node->filter_identifier);
        char* include_str = expression_to_string(node->include_multi_cad);
        char* tooltip_str = expression_to_string(node->tooltip_message);
        char* image_str = expression_to_string(node->image_name);
        char* posX_str = expression_to_string(node->posX);
        char* posY_str = expression_to_string(node->posY);
        char* tag_str = expression_to_string(node->tag);

        LogOnlyPrintfChar(
            "UserSelectMultipleNode: types=%s, max_sel=%s, array=%s, display_order=%s, allow_reselect=%d, "
            "filter_mdl=%s, filter_feat=%s, filter_geom=%s, filter_ref=%s, filter_identifier=%s, "
            "select_by_box=%d, select_by_menu=%d, include_multi_cad=%s, tooltip=%s, image=%s, "
            "on_picture=%d, posX=%s, posY=%s, tag=%s\n",
            types_str ? types_str : "NULL",
            max_sel_str ? max_sel_str : "NULL",
            node->array ? node->array : "NULL",
            display_order_str ? display_order_str : "NULL",
            node->allow_reselect,
            filter_mdl_str ? filter_mdl_str : "NULL",
            filter_feat_str ? filter_feat_str : "NULL",
            filter_geom_str ? filter_geom_str : "NULL",
            filter_ref_str ? filter_ref_str : "NULL",
            filter_id_str ? filter_id_str : "NULL",
            (int)node->select_by_box, (int)node->select_by_menu,
            include_str ? include_str : "NULL",
            tooltip_str ? tooltip_str : "NULL",
            image_str ? image_str : "NULL",
            (int)node->on_picture,
            posX_str ? posX_str : "NULL",
            posY_str ? posY_str : "NULL",
            tag_str ? tag_str : "NULL"
        );

        free(types_str); free(max_sel_str); free(display_order_str); free(filter_mdl_str);
        free(filter_feat_str); free(filter_geom_str); free(filter_ref_str); free(filter_id_str);
        free(include_str); free(tooltip_str); free(image_str); free(posX_str); free(posY_str); free(tag_str);
    }

    return 0;

cleanup:
    /* Free on failure */
    if (node->types) {
        for (size_t k = 0; k < node->type_count; k++) free_expression(node->types[k]);
        free(node->types);
        node->types = NULL;
    }
    free_expression(node->max_sel);
    free(node->array);

    free_expression(node->display_order);
    free_expression(node->filter_mdl);
    free_expression(node->filter_feat);
    free_expression(node->filter_geom);
    free_expression(node->filter_ref);
    free_expression(node->filter_identifier);
    free_expression(node->include_multi_cad);
    free_expression(node->tooltip_message);
    free_expression(node->image_name);
    free_expression(node->posX);
    free_expression(node->posY);
    free_expression(node->tag);
    memset(node, 0, sizeof(*node));
    return -1;
}

int parse_user_select_optional(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    UserSelectOptionalNode* node = &parsed_data->user_select_optional;
    memset(node, 0, sizeof(UserSelectOptionalNode));  // Initialize to zeros/NULLs

    TokenData* tok = current_token(lexer, i);
    if (!tok) {
        ProPrintfChar("Error: Unexpected end of input in USER_SELECT\n");
        return -1;
    }

    /*-----------------------------------------
      Parse types: either &identifier, or
      a | separated list of tok_type (AXIS|PLANE)
    ------------------------------------------*/
    size_t type_capacity = 4;
    node->types = (ExpressionNode**)malloc(type_capacity * sizeof(ExpressionNode*));
    if (!node->types) {
        ProPrintfChar("Error: Memory allocation failed for types array\n");
        return -1;
    }

    if (tok->type == tok_ampersand) {
        (*i)++;  // consume '&'
        tok = current_token(lexer, i);
        if (!tok || tok->type != tok_identifier) {
            ProPrintfChar("Error: Expected identifier after & in USER_SELECT types\n");
            goto cleanup;
        }
        ExpressionNode* var_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!var_expr) {
            ProPrintfChar("Error: Memory allocation failed for type expression\n");
            goto cleanup;
        }
        var_expr->type = EXPR_VARIABLE_REF;
        {
            size_t buf_size = strlen(tok->val) + 2; // '&' + name + NUL
            var_expr->data.string_val = (char*)malloc(buf_size);
            if (!var_expr->data.string_val) {
                free(var_expr);
                ProPrintfChar("Error: Memory allocation failed for variable type string\n");
                goto cleanup;
            }
            sprintf_s(var_expr->data.string_val, buf_size, "&%s", tok->val);
        }
        node->types[0] = var_expr;
        node->type_count = 1;
        (*i)++; // consumed identifier
    }
    else {
        // Parse sequence like: AXIS | PLANE | EDGE
        while (tok && tok->type == tok_type) {
            ExpressionNode* type_expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
            if (!type_expr) {
                ProPrintfChar("Error: Memory allocation failed for type expression\n");
                goto cleanup;
            }
            type_expr->type = EXPR_LITERAL_STRING;
            type_expr->data.string_val = _strdup(tok->val);
            if (!type_expr->data.string_val) {
                free(type_expr);
                ProPrintfChar("Error: Memory allocation failed for type string\n");
                goto cleanup;
            }
            if (node->type_count >= type_capacity) {
                type_capacity *= 2;
                ExpressionNode** new_types =
                    (ExpressionNode**)realloc(node->types, type_capacity * sizeof(ExpressionNode*));
                if (!new_types) {
                    free_expression(type_expr);
                    ProPrintfChar("Error: Memory reallocation failed for types array\n");
                    goto cleanup;
                }
                node->types = new_types;
            }
            node->types[node->type_count++] = type_expr;
            (*i)++; // consumed this type
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_bar) {
                (*i)++; // consume '|'
                tok = current_token(lexer, i);
            }
            else {
                break;
            }
        }
        if (node->type_count == 0) {
            ProPrintfChar("Error: Expected at least one reference type or a variable (&myType) in USER_SELECT\n");
            goto cleanup;
        }
    }

    /*-----------------------------------------
      Parse reference identifier
    ------------------------------------------*/
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier) {
        ProPrintfChar("Error: Expected reference identifier in USER_SELECT\n");
        goto cleanup;
    }
    node->reference = _strdup(tok->val);
    if (!node->reference) {
        ProPrintfChar("Error: Memory allocation failed for reference\n");
        goto cleanup;
    }
    (*i)++;

    /*-----------------------------------------
      Parse options (same logic as before)
    ------------------------------------------*/
    tok = current_token(lexer, i);
    while (tok && tok->type == tok_option) {
        (*i)++;  // consume option
        const char* option = tok->val;

        if (strcmp(option, "DISPLAY_ORDER") == 0) {
            if (node->display_order) {
                ProPrintfChar("Error: DISPLAY_ORDER specified more than once\n");
                goto cleanup;
            }
            node->display_order = parse_expression(lexer, i, NULL);
            if (!node->display_order ||
                (node->display_order->type != EXPR_LITERAL_INT &&
                    node->display_order->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression after DISPLAY_ORDER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "ALLOW_RESELECT") == 0) {
            if (node->allow_reselect) {
                ProPrintfChar("Error: ALLOW_RESELECT specified more than once\n");
                goto cleanup;
            }
            node->allow_reselect = true;
        }
        else if (strcmp(option, "FILTER_MDL") == 0) {
            if (node->filter_mdl) {
                ProPrintfChar("Error: FILTER_MDL specified more than once\n");
                goto cleanup;
            }
            node->filter_mdl = parse_expression(lexer, i, NULL);
            if (!node->filter_mdl) {
                ProPrintfChar("Error: Expected expression after FILTER_MDL\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_FEAT") == 0) {
            if (node->filter_feat) {
                ProPrintfChar("Error: FILTER_FEAT specified more than once\n");
                goto cleanup;
            }
            node->filter_feat = parse_expression(lexer, i, NULL);
            if (!node->filter_feat) {
                ProPrintfChar("Error: Expected expression after FILTER_FEAT\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_GEOM") == 0) {
            if (node->filter_geom) {
                ProPrintfChar("Error: FILTER_GEOM specified more than once\n");
                goto cleanup;
            }
            node->filter_geom = parse_expression(lexer, i, NULL);
            if (!node->filter_geom) {
                ProPrintfChar("Error: Expected expression after FILTER_GEOM\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_REF") == 0) {
            if (node->filter_ref) {
                ProPrintfChar("Error: FILTER_REF specified more than once\n");
                goto cleanup;
            }
            node->filter_ref = parse_expression(lexer, i, NULL);
            if (!node->filter_ref) {
                ProPrintfChar("Error: Expected expression after FILTER_REF\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "FILTER_IDENTIFIER") == 0) {
            if (node->filter_identifier) {
                ProPrintfChar("Error: FILTER_IDENTIFIER specified more than once\n");
                goto cleanup;
            }
            node->filter_identifier = parse_expression(lexer, i, NULL);
            if (!node->filter_identifier || node->filter_identifier->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after FILTER_IDENTIFIER\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "SELECT_BY_BOX") == 0) {
            if (node->select_by_box) {
                ProPrintfChar("Error: SELECT_BY_BOX specified more than once\n");
                goto cleanup;
            }
            node->select_by_box = true;
        }
        else if (strcmp(option, "SELECT_BY_MENU") == 0) {
            if (node->select_by_menu) {
                ProPrintfChar("Error: SELECT_BY_MENU specified more than once\n");
                goto cleanup;
            }
            node->select_by_menu = true;
        }
        else if (strcmp(option, "INCLUDE_MULTI_CAD") == 0) {
            if (node->include_multi_cad) {
                ProPrintfChar("Error: INCLUDE_MULTI_CAD specified more than once\n");
                goto cleanup;
            }
            node->include_multi_cad = parse_expression(lexer, i, NULL);
            if (!node->include_multi_cad || node->include_multi_cad->type != EXPR_VARIABLE_REF) {
                ProPrintfChar("Error: Expected TRUE or FALSE after INCLUDE_MULTI_CAD\n");
                goto cleanup;
            }
        }
        else if (strcmp(option, "TOOLTIP") == 0) {
            if (node->tooltip_message) {
                ProPrintfChar("Error: TOOLTIP specified more than once\n");
                goto cleanup;
            }
            node->tooltip_message = parse_expression(lexer, i, NULL);
            if (!node->tooltip_message || node->tooltip_message->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression after TOOLTIP\n");
                goto cleanup;
            }
            // Optional IMAGE
            tok = current_token(lexer, i);
            if (tok && tok->type == tok_option && strcmp(tok->val, "IMAGE") == 0) {
                (*i)++;
                node->image_name = parse_expression(lexer, i, NULL);
                if (!node->image_name || node->image_name->type != EXPR_LITERAL_STRING) {
                    ProPrintfChar("Error: Expected string expression after IMAGE\n");
                    goto cleanup;
                }
            }
        }
        else if (strcmp(option, "ON_PICTURE") == 0) {
            if (node->on_picture) {
                ProPrintfChar("Error: ON_PICTURE specified more than once\n");
                goto cleanup;
            }
            node->posX = parse_expression(lexer, i, NULL);
            if (!node->posX ||
                (node->posX->type != EXPR_LITERAL_INT && node->posX->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posX after ON_PICTURE\n");
                goto cleanup;
            }
            node->posY = parse_expression(lexer, i, NULL);
            if (!node->posY ||
                (node->posY->type != EXPR_LITERAL_INT && node->posY->type != EXPR_LITERAL_DOUBLE)) {
                ProPrintfChar("Error: Expected numeric expression for posY after posX\n");
                goto cleanup;
            }
            node->on_picture = true;
        }
        else {
            ProPrintfChar("Error: Unknown option '%s' in USER_SELECT\n", option);
            goto cleanup;
        }

        tok = current_token(lexer, i);
    }

    /*-----------------------------------------
      Trailing tag revised to match CHECKBOX_PARAM
      Primary behavior: if next token is a STRING, accept it.
      (Optional) Back-compat: accept numeric expr too.
    ------------------------------------------*/
    tok = current_token(lexer, i);
    if (tok) {
        if (tok->type == tok_string) {
            node->tag = parse_expression(lexer, i, NULL);
            if (!node->tag || node->tag->type != EXPR_LITERAL_STRING) {
                ProPrintfChar("Error: Expected string expression for tag\n");
                goto cleanup;
            }
        }
    }

    {
        char* types_str = NULL;
        if (node->type_count > 0) {
            size_t total_len = 0;
            for (size_t k = 0; k < node->type_count; k++) {
                char* t = expression_to_string(node->types[k]);
                if (t) { total_len += strlen(t) + 2; free(t); }
            }
            if (total_len >= 2) total_len -= 2;
            types_str = (char*)malloc(total_len + 1);
            if (types_str) {
                types_str[0] = '\0';
                for (size_t k = 0; k < node->type_count; k++) {
                    char* t = expression_to_string(node->types[k]);
                    if (t) {
                        strcat_s(types_str, total_len + 1, t);
                        if (k < node->type_count - 1) strcat_s(types_str, total_len + 1, ", ");
                        free(t);
                    }
                }
            }
        }
        char* display_order_str = expression_to_string(node->display_order);
        char* filter_mdl_str = expression_to_string(node->filter_mdl);
        char* filter_feat_str = expression_to_string(node->filter_feat);
        char* filter_geom_str = expression_to_string(node->filter_geom);
        char* filter_ref_str = expression_to_string(node->filter_ref);
        char* filter_id_str = expression_to_string(node->filter_identifier);
        char* include_str = expression_to_string(node->include_multi_cad);
        char* tooltip_str = expression_to_string(node->tooltip_message);
        char* image_str = expression_to_string(node->image_name);
        char* posX_str = expression_to_string(node->posX);
        char* posY_str = expression_to_string(node->posY);
        char* tag_str = expression_to_string(node->tag);

        LogOnlyPrintfChar(
            "UserSelectionOptionalNode: types=%s, reference=%s, display_order=%s, allow_reselect=%d, "
            "filter_mdl=%s, filter_feat=%s, filter_geom=%s, filter_ref=%s, filter_identifier=%s, "
            "select_by_box=%d, select_by_menu=%d, include_multi_cad=%s, tooltip=%s, image=%s, "
            "on_picture=%d, posX=%s, posY=%s, tag=%s\n",
            types_str ? types_str : "NULL",
            node->reference ? node->reference : "NULL",
            display_order_str ? display_order_str : "NULL",
            node->allow_reselect,
            filter_mdl_str ? filter_mdl_str : "NULL",
            filter_feat_str ? filter_feat_str : "NULL",
            filter_geom_str ? filter_geom_str : "NULL",
            filter_ref_str ? filter_ref_str : "NULL",
            filter_id_str ? filter_id_str : "NULL",
            node->select_by_box, node->select_by_menu,
            include_str ? include_str : "NULL",
            tooltip_str ? tooltip_str : "NULL",
            image_str ? image_str : "NULL",
            node->on_picture,
            posX_str ? posX_str : "NULL",
            posY_str ? posY_str : "NULL",
            tag_str ? tag_str : "NULL"
        );

        if (types_str) free(types_str);
        if (display_order_str) free(display_order_str);
        if (filter_mdl_str) free(filter_mdl_str);
        if (filter_feat_str) free(filter_feat_str);
        if (filter_geom_str) free(filter_geom_str);
        if (filter_ref_str) free(filter_ref_str);
        if (filter_id_str) free(filter_id_str);
        if (include_str) free(include_str);
        if (tooltip_str) free(tooltip_str);
        if (image_str) free(image_str);
        if (posX_str) free(posX_str);
        if (posY_str) free(posY_str);
        if (tag_str) free(tag_str);
    }

    return 0;

cleanup:
    // Free partial allocations
    for (size_t k = 0; k < node->type_count; k++) {
        free_expression(node->types[k]);
    }
    free(node->types);
    node->types = NULL;
    node->type_count = 0;

    if (node->reference) { free(node->reference); node->reference = NULL; }
    free_expression(node->display_order);
    free_expression(node->filter_mdl);
    free_expression(node->filter_feat);
    free_expression(node->filter_geom);
    free_expression(node->filter_ref);
    free_expression(node->filter_identifier);
    free_expression(node->include_multi_cad);
    free_expression(node->tooltip_message);
    free_expression(node->image_name);
    free_expression(node->posX);
    free_expression(node->posY);
    free_expression(node->tag);

    return -1;
}


/*=================================================*\
* 
* BEGIN_TABLE Parser
* 
* 
\*=================================================*/
int parse_begin_table(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    TableNode* node = &parsed_data->begin_table;

    // Initialize node fields to defaults
    node->identifier = NULL;
    node->name = NULL;
    node->options = NULL;
    node->option_count = 0;
    node->sel_strings = NULL;
    node->sel_string_count = 0;
    node->data_types = NULL;
    node->data_type_count = 0;
    node->rows = NULL;
    node->row_count = 0;
    node->column_count = 0;

    // Additional flags and values for specific TABLE_OPTION handling
    node->no_autosel = false;
    node->no_filter = false;
    node->depend_on_input = false;
    node->invalidate_on_unselect = false;
    node->show_autosel = false;
    node->filter_rigid = false;
    node->filter_only_column = -1;  // -1 indicates not used
    node->filter_column = -1;       // -1 indicates not used
    node->table_height = 12;        // Default is 12 (as per docs); track if explicitly set
    node->table_height_set = false; // New: Track if TABLE_HEIGHT was explicitly provided
    node->array = false;

    // Step 1: Assume 'BEGIN_TABLE' has already been consumed by the caller; parse TABLE_IDENTIFIER
    TokenData* tok = current_token(lexer, i);
    if (!tok || (tok->type != tok_field && tok->type != tok_identifier)) {
        ProPrintfChar("Error: Expected TABLE_IDENTIFIER after BEGIN_TABLE at line %zu\n", tok ? tok->loc.line : (*i > 0 ? lexer->tokens[*i - 1].loc.line : 0));
        return -1;
    }
    node->identifier = _strdup(tok->val);
    if (!node->identifier) {
        ProPrintfChar("Error: Memory allocation failed for identifier\n");
        return -1;
    }
    (*i)++;

    // Step 2: Parse optional table name (as ExpressionNode*)
    tok = current_token(lexer, i);
    if (tok && (tok->type == tok_string || tok->type == tok_identifier || tok->type == tok_number || tok->type == tok_lparen || tok->type == tok_minus)) {  // Potential start of expression
        node->name = parse_expression(lexer, i, NULL);  // Pass NULL for st if not needed yet
        if (!node->name) {
            ProPrintfChar("Error: Failed to parse table name expression at line %zu\n", tok->loc.line);
            goto cleanup;
        }
    }
    else {
        // Default to identifier as string literal if no name provided
        node->name = malloc(sizeof(ExpressionNode));
        if (!node->name) {
            ProPrintfChar("Error: Memory allocation failed for default name\n");
            goto cleanup;
        }
        node->name->type = EXPR_LITERAL_STRING;
        node->name->data.string_val = _strdup(node->identifier);
        if (!node->name->data.string_val) {
            ProPrintfChar("Error: Memory allocation failed for default name string\n");
            goto cleanup;
        }
    }

    // Step 3: Parse optional TABLE_OPTION (array of ExpressionNode*)
    tok = current_token(lexer, i);
    int actual_option_count = 0;  // New: Track logical options (names), excluding args
    if (tok && tok->type == tok_keyword && strcmp(tok->val, "TABLE_OPTION") == 0) {
        (*i)++;  // Consume TABLE_OPTION
        int option_capacity = 4;
        node->options = malloc(option_capacity * sizeof(ExpressionNode*));
        if (!node->options) {
            ProPrintfChar("Error: Memory allocation failed for options\n");
            goto cleanup;
        }
        size_t current_line = tok->loc.line;  // Use line of TABLE_OPTION for reference
        tok = current_token(lexer, i);
        while (tok && tok->loc.line == current_line && tok->type != tok_keyword) {  // Parse expressions on the same line
            ExpressionNode* option = parse_expression(lexer, i, NULL);
            if (!option) {
                ProPrintfChar("Error: Failed to parse TABLE_OPTION expression at line %zu\n", tok->loc.line);
                goto cleanup;
            }
            if (node->option_count >= option_capacity) {
                option_capacity *= 2;
                ExpressionNode** new_options = realloc(node->options, option_capacity * sizeof(ExpressionNode*));
                if (!new_options) {
                    ProPrintfChar("Error: Memory reallocation failed for options\n");
                    goto cleanup;
                }
                node->options = new_options;
            }
            node->options[node->option_count++] = option;
            tok = current_token(lexer, i);
        }
    }

    // New: Process the parsed options to set specific flags/values (track logical count)
    // This iterates through the options array and matches known options, handling arguments where needed
    for (size_t opt_idx = 0; opt_idx < node->option_count; ) {
        ExpressionNode* opt = node->options[opt_idx];
        if (opt->type != EXPR_VARIABLE_REF && opt->type != EXPR_LITERAL_STRING) {
            ProPrintfChar("Error: TABLE_OPTION at index %zu is not a string or identifier\n", opt_idx);
            goto cleanup;
        }
        char* opt_name = opt->data.string_val;  // Assumes string_val is set for both types

        if (strcmp(opt_name, "NO_AUTOSEL") == 0) {
            node->no_autosel = true;
            opt_idx++;
            actual_option_count++;  // Count the logical option
        }
        else if (strcmp(opt_name, "NO_FILTER") == 0) {
            node->no_filter = true;
            opt_idx++;
            actual_option_count++;
        }
        else if (strcmp(opt_name, "DEPEND_ON_INPUT") == 0) {
            node->depend_on_input = true;
            opt_idx++;
            actual_option_count++;
        }
        else if (strcmp(opt_name, "INVALIDATE_ON_UNSELECT") == 0) {
            node->invalidate_on_unselect = true;
            opt_idx++;
            actual_option_count++;
        }
        else if (strcmp(opt_name, "SHOW_AUTOSEL") == 0) {
            node->show_autosel = true;
            opt_idx++;
            actual_option_count++;
        }
        else if (strcmp(opt_name, "FILTER_RIGID") == 0) {
            node->filter_rigid = true;
            opt_idx++;
            actual_option_count++;
        }
        else if (strcmp(opt_name, "ARRAY") == 0) {
            node->array = true;
            opt_idx++;
            actual_option_count++;
        }
        else if (strcmp(opt_name, "FILTER_ONLY_COLUMN") == 0) {
            actual_option_count++;  // Count the logical option
            opt_idx++;
            if (opt_idx >= node->option_count) {
                ProPrintfChar("Error: FILTER_ONLY_COLUMN missing integer argument\n");
                goto cleanup;
            }
            ExpressionNode* arg = node->options[opt_idx];
            if (arg->type != EXPR_LITERAL_INT) {
                ProPrintfChar("Error: FILTER_ONLY_COLUMN argument must be an integer literal\n");
                goto cleanup;
            }
            node->filter_only_column = (int)arg->data.int_val;
            opt_idx++;  // Consume arg (but don't count it as a separate option)
        }
        else if (strcmp(opt_name, "FILTER_COLUMN") == 0) {
            actual_option_count++;  // Count the logical option
            opt_idx++;
            if (opt_idx >= node->option_count) {
                ProPrintfChar("Error: FILTER_COLUMN missing integer argument\n");
                goto cleanup;
            }
            ExpressionNode* arg = node->options[opt_idx];
            if (arg->type != EXPR_LITERAL_INT) {
                ProPrintfChar("Error: FILTER_COLUMN argument must be an integer literal\n");
                goto cleanup;
            }
            node->filter_column = (int)arg->data.int_val;
            opt_idx++;  // Consume arg
        }
        else if (strcmp(opt_name, "TABLE_HEIGHT") == 0) {
            actual_option_count++;  // Count the logical option
            opt_idx++;
            if (opt_idx >= node->option_count) {
                ProPrintfChar("Error: TABLE_HEIGHT missing integer argument\n");
                goto cleanup;
            }
            ExpressionNode* arg = node->options[opt_idx];
            if (arg->type != EXPR_LITERAL_INT) {
                ProPrintfChar("Error: TABLE_HEIGHT argument must be an integer literal\n");
                goto cleanup;
            }
            node->table_height = (int)arg->data.int_val;
            node->table_height_set = true;  // Mark as explicitly set (useful if you need to distinguish default vs. user-provided)
            opt_idx++;  // Consume arg
        }
        else {
            ProPrintfChar("Warning: Unknown TABLE_OPTION '%s' at index %zu\n", opt_name, opt_idx);
            opt_idx++;  // Skip unknown options (don't count them)
        }
    }

    // Optional: Free the options array now that everything is processed into flags/values
    // This saves memory and reinforces that raw expressions are no longer needed
    if (node->options) {
        for (int j = 0; j < node->option_count; j++) free_expression(node->options[j]);
        free(node->options);
        node->options = NULL;
        node->option_count = 0;  // Reset count to reflect no unprocessed options
    }

    // Step 4: Parse SEL_STRING (array of ExpressionNode*)
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_keyword || strcmp(tok->val, "SEL_STRING") != 0) {
        ProPrintfChar("Error: Expected 'SEL_STRING' at line %zu\n", tok ? tok->loc.line : (*i > 0 ? lexer->tokens[*i - 1].loc.line : 0));
        goto cleanup;
    }
    (*i)++;  // Consume SEL_STRING

    // Allocate initial capacity for sel_strings
    int sel_capacity = 4;
    node->sel_strings = malloc(sel_capacity * sizeof(ExpressionNode*));
    if (!node->sel_strings) {
        ProPrintfChar("Error: Memory allocation failed for sel_strings\n");
        goto cleanup;
    }

    // Insert "SEL_STRING" as the first sel_string (for the implicit first column header)
    ExpressionNode* first_sel = malloc(sizeof(ExpressionNode));
    if (!first_sel) {
        ProPrintfChar("Error: Memory allocation failed for first sel_string\n");
        goto cleanup;
    }
    first_sel->type = EXPR_LITERAL_STRING;
    first_sel->data.string_val = _strdup("SEL_STRING");
    if (!first_sel->data.string_val) {
        ProPrintfChar("Error: Memory allocation failed for 'SEL_STRING' string\n");
        free(first_sel);
        goto cleanup;
    }
    node->sel_strings[0] = first_sel;
    node->sel_string_count = 1;

    // Parse remaining sel_strings on the same line
    size_t sel_line = tok->loc.line;
    tok = current_token(lexer, i);
    while (tok && tok->loc.line == sel_line && tok->type != tok_keyword) {
        ExpressionNode* sel = parse_expression(lexer, i, NULL);
        if (!sel) {
            ProPrintfChar("Error: Failed to parse SEL_STRING expression at line %zu\n", tok->loc.line);
            goto cleanup;
        }
        if (node->sel_string_count >= sel_capacity) {
            sel_capacity *= 2;
            ExpressionNode** new_sels = realloc(node->sel_strings, sel_capacity * sizeof(ExpressionNode*));
            if (!new_sels) {
                ProPrintfChar("Error: Memory reallocation failed for sel_strings\n");
                goto cleanup;
            }
            node->sel_strings = new_sels;
        }
        node->sel_strings[node->sel_string_count++] = sel;
        tok = current_token(lexer, i);
    }
    if (node->sel_string_count == 1) {  // Only the implicit one; expect at least one explicit
        ProPrintfChar("Warning: No explicit SEL_STRING parameters provided at line %zu; using implicit only\n", sel_line);
    }
    node->column_count = node->sel_string_count;  // Adjusted: no +1, as implicit is now included

    // Step 5: Parse data types (array of ExpressionNode*)
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_type || strcmp(tok->val, "STRING") != 0) {
        ProPrintfChar("Error: Expected 'STRING' as first data type at line %zu\n", tok ? tok->loc.line : (*i > 0 ? lexer->tokens[*i - 1].loc.line : 0));
        goto cleanup;
    }
    int type_capacity = node->column_count;
    node->data_types = malloc(type_capacity * sizeof(ExpressionNode*));
    if (!node->data_types) {
        ProPrintfChar("Error: Memory allocation failed for data_types\n");
        goto cleanup;
    }
    size_t type_line = tok->loc.line;
    int type_idx = 0;
    while (tok && tok->loc.line == type_line && tok->type != tok_keyword && type_idx < node->column_count) {
        ExpressionNode* dtype = parse_expression(lexer, i, NULL);
        if (!dtype) {
            ProPrintfChar("Error: Failed to parse data type expression at line %zu\n", tok->loc.line);
            goto cleanup;
        }
        node->data_types[type_idx++] = dtype;
        tok = current_token(lexer, i);
    }
    node->data_type_count = type_idx;
    if (node->data_type_count != node->column_count) {
        ProPrintfChar("Error: Number of data types (%d) does not match column count (%d) at line %zu\n",
            node->data_type_count, node->column_count, type_line);
        goto cleanup;
    }

    // Step 6: Parse rows (ExpressionNode***: array of rows, each an array of ExpressionNode*)
    int row_capacity = 4;
    node->rows = malloc(row_capacity * sizeof(ExpressionNode**));
    if (!node->rows) {
        ProPrintfChar("Error: Memory allocation failed for rows\n");
        goto cleanup;
    }
    tok = current_token(lexer, i);
    while (tok && !(tok->type == tok_keyword && strcmp(tok->val, "END_TABLE") == 0)) {
        if (node->row_count >= row_capacity) {
            row_capacity *= 2;
            ExpressionNode*** new_rows = realloc(node->rows, row_capacity * sizeof(ExpressionNode**));
            if (!new_rows) {
                ProPrintfChar("Error: Memory reallocation failed for rows\n");
                goto cleanup;
            }
            node->rows = new_rows;
        }
        // Allocate row
        ExpressionNode** row = malloc(node->column_count * sizeof(ExpressionNode*));
        if (!row) {
            ProPrintfChar("Error: Memory allocation failed for row %d\n", node->row_count);
            goto cleanup;
        }
        // Initialize to NULL
        for (int c = 0; c < node->column_count; c++) {
            row[c] = NULL;
        }
        size_t row_line = tok->loc.line;
        int col_idx = 0;
        while (tok && tok->loc.line == row_line && !(tok->type == tok_keyword && strcmp(tok->val, "END_TABLE") == 0) && col_idx < node->column_count) {
            ExpressionNode* cell = parse_expression(lexer, i, NULL);
            if (!cell) {
                ProPrintfChar("Error: Failed to parse row %d column %d expression at line %zu\n",
                    node->row_count, col_idx, tok ? tok->loc.line : 0);
                for (int c = 0; c < col_idx; c++) free_expression(row[c]);
                free(row);
                goto cleanup;
            }
            row[col_idx++] = cell;
            tok = current_token(lexer, i);
        }

        if (col_idx > node->column_count) {
            ProPrintfChar("Error: Row %d has too many columns (%d > %d) at line %zu\n", node->row_count, col_idx, node->column_count, row_line);
            for (int c = 0; c < col_idx; c++) free_expression(row[c]);
            free(row);
            goto cleanup;
        }
        node->rows[node->row_count++] = row;
        tok = current_token(lexer, i);  // Check for next row or END_TABLE
    }

    // Step 7: Consume END_TABLE
    if (!tok || tok->type != tok_keyword || strcmp(tok->val, "END_TABLE") != 0) {
        ProPrintfChar("Error: Expected 'END_TABLE' to close table block\n");
        goto cleanup;
    }
    (*i)++;  // Consume END_TABLE

    // Logging (optional summary) - Use actual_option_count for accurate reporting
    LogOnlyPrintfChar("Parsed table '%s' with %d options, %d sel_strings, %d data_types, %d rows, %d columns\n",
        node->identifier, actual_option_count, node->sel_string_count, node->data_type_count, node->row_count, node->column_count);

    // Detailed row and cell logging
    if (node->row_count > 0) {
        LogOnlyPrintfChar("Detailed table rows and cells:\n");
        for (int r = 0; r < node->row_count; r++) {
            LogOnlyPrintfChar(" Row %d:\n", r);
            for (int c = 0; c < node->column_count; c++) {
                char* cell_str = expression_to_string(node->rows[r][c]);
                LogOnlyPrintfChar("  Column %d: %s\n", c, cell_str ? cell_str : "NULL");
                free(cell_str);  // Free to prevent memory leaks
            }
        }
    }

    return 0;

cleanup:
    // Free allocated resources
    free(node->identifier);
    free_expression(node->name);
    if (node->options) {
        for (int j = 0; j < node->option_count; j++) free_expression(node->options[j]);
        free(node->options);
    }
    if (node->sel_strings) {
        for (int j = 0; j < node->sel_string_count; j++) free_expression(node->sel_strings[j]);
        free(node->sel_strings);
    }
    if (node->data_types) {
        for (int j = 0; j < node->data_type_count; j++) free_expression(node->data_types[j]);
        free(node->data_types);
    }
    if (node->rows) {
        for (int r = 0; r < node->row_count; r++) {
            for (int c = 0; c < node->column_count; c++) free_expression(node->rows[r][c]);
            free(node->rows[r]);
        }
        free(node->rows);
    }
    // Reset node
    memset(node, 0, sizeof(TableNode));
    return -1;
}

/*=================================================*\
* 
* Invalidate_param syntax analysis
* 
* 
\*=================================================*/
int parse_invlaidate_param(Lexer* lexer, size_t* i, CommandData* parsed_data)
{
    InvalidateParamNode* node = &parsed_data->invalidate_param;
    node->parameter = NULL;

    TokenData* tok = current_token(lexer, i);
    if (!tok || tok->type != tok_identifier)
    {
        ProPrintfChar("Error: Expected parameter identifer after INVALIDATE_PARAM\n");
        return -1;
    }
    node->parameter = _strdup(tok->val);
    if (!node->parameter)
    {
        ProPrintfChar("Error: Memory allocation failed for parameter name \n");
        return -1;
    }
    (*i)++;

    // Check for optional <:in> suffix if required
    tok = current_token(lexer, i);
    if (tok && tok->type == tok_colon)
    {
        (*i)++;
        tok = current_token(lexer, i);
        if (tok && tok->type == tok_identifier && strcmp(tok->val, "in") == 0)
        {
            (*i)++;
        }
        else {
            ProPrintfChar("Warning: Expected <:in> suffix; proceeding without it\n");
        }
    }

    LogOnlyPrintfChar("InvalidateParamNode: parameter=%s\n", node->parameter ? node->parameter : "NULL");
    return 0;
}


/*=================================================*\
* 
* MEASURE_DISTANCE syntax_analysis  
* 
* 
\*=================================================*/
/* --- free helper (mirrors your other node free-ers) --- */
static void free_measure_distance_node(MeasureDistanceNode* n) {
    if (!n) return;
    free_expression(n->reference1);     n->reference1 = NULL;
    free_expression(n->reference2);     n->reference2 = NULL;
    free_expression(n->parameterResult); n->parameterResult = NULL;
    memset(n, 0, sizeof(*n));
}

/* --- optional <:tag> consumer used below --- */
static void consume_optional_inout_suffix(Lexer* lexer, size_t* i, const char* tag /* "in" or "out" */) {
    TokenData* t = current_token(lexer, i);
    if (t && t->type == tok_colon) {
        (*i)++;
        t = current_token(lexer, i);
        if (t && t->type == tok_identifier && _stricmp(t->val, tag) == 0) {
            (*i)++; /* swallow expected tag */
        }
        else {
            ProPrintfChar("Warning: Expected <:%s> after argument; proceeding without it\n", tag);
        }
    }
}

/*=================================================*\
*  MEASURE_DISTANCE syntax analysis
*  Syntax:
*    MEASURE_DISTANCE [ENABLE_CHECKBOX1 bool] [ENABLE_CHECKBOX2 bool]
*                     reference1<:in> reference2<:in> parameterResult<:out>
\*=================================================*/
int parse_measure_distance(Lexer* lexer, size_t* i, CommandData* parsed_data)
{
    MeasureDistanceNode* node = &parsed_data->measure_distance;

    /* defaults */
    memset(node, 0, sizeof(*node));
    node->enable_cb1 = true;
    node->enable_cb2 = true;

    /* ---- options: ENABLE_CHECKBOX1 / ENABLE_CHECKBOX2 ---- */
    for (;;) {
        TokenData* t = current_token(lexer, i);
        if (!t) break;

        if ((t->type == tok_option || t->type == tok_identifier) &&
            (!strcmp(t->val, "ENABLE_CHECKBOX1") || !strcmp(t->val, "ENABLE_CHECKBOX2")))
        {
            int val = 1;
            (*i)++; /* consume option token */
            if (parse_bool_literal(lexer, i, &val) != 0) {
                ProPrintfChar("Error: Expected TRUE/FALSE or 0/1 after %s at line %zu\n", t->val, t->loc.line);
                free_measure_distance_node(node);
                return -1;
            }
            if (!strcmp(t->val, "ENABLE_CHECKBOX1")) node->enable_cb1 = (val != 0);
            else                                     node->enable_cb2 = (val != 0);
            continue;
        }
        break; /* no more options */
    }

    /* ---- reference1 (expr) + optional <:in> ---- */
    node->reference1 = parse_expression(lexer, i, NULL);
    if (!node->reference1) {
        ProPrintfChar("Error: Expected reference1 expression\n");
        free_measure_distance_node(node);
        return -1;
    }
    consume_optional_inout_suffix(lexer, i, "in");  /* accepts <:in> */

    /* ---- reference2 (expr) + optional <:in> ---- */
    node->reference2 = parse_expression(lexer, i, NULL);
    if (!node->reference2) {
        ProPrintfChar("Error: Expected reference2 expression\n");
        free_measure_distance_node(node);
        return -1;
    }
    consume_optional_inout_suffix(lexer, i, "in");  /* accepts <:in> */

    /* ---- parameterResult: require an identifier token; wrap as expr; optional <:out> ---- */
    {
        TokenData* t = current_token(lexer, i);
        if (!t || t->type != tok_identifier) {
            ProPrintfChar("Error: Expected result identifier for parameterResult\n");
            free_measure_distance_node(node);
            return -1;
        }
        node->parameterResult = parse_expression(lexer, i, NULL); /* becomes EXPR_VARIABLE_REF */
        if (!node->parameterResult) {
            ProPrintfChar("Error: Failed to parse parameterResult identifier\n");
            free_measure_distance_node(node);
            return -1;
        }
        consume_optional_inout_suffix(lexer, i, "out"); /* accepts <:out> */
    }

    /* log (same style as others) */
    char* r1 = expression_to_string(node->reference1);
    char* r2 = expression_to_string(node->reference2);
    char* pr = expression_to_string(node->parameterResult);
    LogOnlyPrintfChar("MEASURE_DISTANCE: cb1=%d, cb2=%d, ref1=%s, ref2=%s, out=%s\n",
        node->enable_cb1, node->enable_cb2,
        r1 ? r1 : "<null>", r2 ? r2 : "<null>", pr ? pr : "<null>");
    free(r1); free(r2); free(pr);

    return 0;
}

/*=================================================*\
*
* MEASURE_LENGTH syntax_analysis
* Syntax:
*   MEASURE_LENGTH reference1<:in> parameterResult<:out>
*
\*=================================================*/

/* --- free helper --- */
static void free_measure_length_node(MeasureLengthNode* n) {
    if (!n) return;
    free_expression(n->reference1);       n->reference1 = NULL;
    free_expression(n->parameterResult);  n->parameterResult = NULL;
    memset(n, 0, sizeof(*n));
}

/* Parser */
int parse_measure_length(Lexer* lexer, size_t* i, CommandData* parsed_data)
{
    MeasureLengthNode* node = &parsed_data->measure_length;
    memset(node, 0, sizeof(*node));

    /* ---- reference1 (expr) + optional <:in> ---- */
    node->reference1 = parse_expression(lexer, i, NULL);
    if (!node->reference1) {
        ProPrintfChar("Error: Expected reference1 expression in MEASURE_LENGTH\n");
        free_measure_length_node(node);
        return -1;
    }
    /* accept optional <:in>, same behavior as other commands */
    consume_optional_inout_suffix(lexer, i, "in");

    /* ---- parameterResult: require identifier token, parse as expr; optional <:out> ---- */
    {
        TokenData* t = current_token(lexer, i);
        if (!t || t->type != tok_identifier) {
            ProPrintfChar("Error: Expected result identifier for parameterResult in MEASURE_LENGTH\n");
            free_measure_length_node(node);
            return -1;
        }
        node->parameterResult = parse_expression(lexer, i, NULL); /* becomes EXPR_VARIABLE_REF */
        if (!node->parameterResult) {
            ProPrintfChar("Error: Failed to parse parameterResult identifier in MEASURE_LENGTH\n");
            free_measure_length_node(node);
            return -1;
        }
        consume_optional_inout_suffix(lexer, i, "out"); /* accepts <:out> */
    }

    /* log (same style as others) */
    {
        char* r1 = expression_to_string(node->reference1);
        char* pr = expression_to_string(node->parameterResult);
        LogOnlyPrintfChar("MeasureLengthNode: reference1=%s, parameterResult=%s\n",
            r1 ? r1 : "NULL", pr ? pr : "NULL");
        free(r1);
        free(pr);
    }

    return 0;
}


/*=================================================*
 *  SEARCH_MDL_REF syntax analysis
 *
 *  Syntax:
 *    SEARCH_MDL_REF [options] model "type" "search_string"
 *                   [WITH_CONTENT expr]*
 *                   [WITH_CONTENT_NOT expr]*
 *                   [WITH_IDENTIFIER expr]*
 *                   [WITH_IDENTIFIER_NOT expr]*
 *                   reference<:out>
 *
 *  Notes:
 *  - INCLUDE_MULTI_CAD takes one expression (bool-ish), default FALSE.
 *  - Each WITH_* clause adds exactly one expression; repeat clauses to add more.
 *=================================================*/
 /* ---------- SEARCH_MDL_REF: helpers ---------- */
static void free_search_mdl_ref_node(SearchMdlRefNode* n)
{
    if (!n) return;

    free_expression(n->include_multi_cad);
    free_expression(n->model);
    free_expression(n->type_expr);
    free_expression(n->search_string);

    if (n->with_content) {
        for (size_t k = 0; k < n->with_content_count; ++k) free_expression(n->with_content[k]);
        free(n->with_content);
    }
    if (n->with_content_not) {
        for (size_t k = 0; k < n->with_content_not_count; ++k) free_expression(n->with_content_not[k]);
        free(n->with_content_not);
    }
    if (n->with_identifier) {
        for (size_t k = 0; k < n->with_identifier_count; ++k) free_expression(n->with_identifier[k]);
        free(n->with_identifier);
    }
    if (n->with_identifier_not) {
        for (size_t k = 0; k < n->with_identifier_not_count; ++k) free_expression(n->with_identifier_not[k]);
        free(n->with_identifier_not);
    }

    free(n->out_reference);
    memset(n, 0, sizeof(*n));
}

int parse_search_mdl_ref(Lexer* lexer, size_t* i, CommandData* parsed_data)
{
    TokenData* t = NULL;
    SearchMdlRefNode* n = &parsed_data->search_mdl_ref;
    memset(n, 0, sizeof(*n));

    /* default INCLUDE_MULTI_CAD := FALSE (as an expr node) */
    {
        ExpressionNode* lit_false = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!lit_false) { ProPrintfChar("Error: OOM in SEARCH_MDL_REF\n"); return -1; }
        lit_false->type = EXPR_LITERAL_BOOL;
        lit_false->data.bool_val = 0;
        n->include_multi_cad = lit_false;
    }

    /* ---- pre-argument options ---- (reuse flags + INCLUDE_MULTI_CAD like REFS) */
    for (;;) {
        t = current_token(lexer, i);
        if (!t) break;
        if (t->type != tok_option && t->type != tok_identifier) break;

        if (_stricmp(t->val, "RECURSIVE") == 0) { n->recursive = true; (*i)++; continue; }
        else if (_stricmp(t->val, "ALLOW_SUPPRESSED") == 0) { n->allow_suppressed = true; (*i)++; continue; }
        else if (_stricmp(t->val, "ALLOW_SIMPREP_SUPPRESSED") == 0) { n->allow_simprep_suppressed = true; (*i)++; continue; }
        else if (_stricmp(t->val, "EXCLUDE_INHERITED") == 0) { n->exclude_inherited = true; (*i)++; continue; }
        else if (_stricmp(t->val, "EXCLUDE_FOOTER") == 0) { n->exclude_footer = true; (*i)++; continue; }
        else if (_stricmp(t->val, "NO_UPDATE") == 0) { n->no_update = true; (*i)++; continue; }
        else if (_stricmp(t->val, "INCLUDE_MULTI_CAD") == 0) {
            (*i)++; /* consume option */
            ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) {
                ProPrintfChar("Error: Expected expression after INCLUDE_MULTI_CAD at line %zu\n", t->loc.line);
                free_search_mdl_ref_node(n); return -1;
            }
            free_expression(n->include_multi_cad);
            n->include_multi_cad = e;
            continue;
        }

        /* If we hit WITH_* it's the next phase */
        if (_stricmp(t->val, "WITH_CONTENT") == 0 ||
            _stricmp(t->val, "WITH_CONTENT_NOT") == 0 ||
            _stricmp(t->val, "WITH_IDENTIFIER") == 0 ||
            _stricmp(t->val, "WITH_IDENTIFIER_NOT") == 0) {
            break;
        }
        break;
    }

    /* ---- positional: model, type, search_string ---- */
    n->model = parse_expression(lexer, i, NULL);
    if (!n->model) {
        ProPrintfChar("Error: Expected model expression in SEARCH_MDL_REF\n");
        free_search_mdl_ref_node(n); return -1;
    }

    n->type_expr = parse_expression(lexer, i, NULL);
    if (!n->type_expr) {
        ProPrintfChar("Error: Expected \"type\" expression in SEARCH_MDL_REF\n");
        free_search_mdl_ref_node(n); return -1;
    }

    n->search_string = parse_expression(lexer, i, NULL);
    if (!n->search_string) {
        ProPrintfChar("Error: Expected \"search_string\" expression in SEARCH_MDL_REF\n");
        free_search_mdl_ref_node(n); return -1;
    }

    /* ---- optional WITH_* clauses (repeatable) ---- */
    size_t cap_c = 0, cap_cn = 0, cap_i = 0, cap_in = 0;
    for (;;) {
        t = current_token(lexer, i);
        if (!t || (t->type != tok_option && t->type != tok_identifier)) break;

        if (_stricmp(t->val, "WITH_CONTENT") == 0) {
            (*i)++; ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) {
                ProPrintfChar("Error: WITH_CONTENT requires an expression\n");
                free_search_mdl_ref_node(n); return -1;
            }
            if (add_expr(&n->with_content, &n->with_content_count, &cap_c, e) != 0) {
                free_expression(e); free_search_mdl_ref_node(n); return -1;
            }
            continue;
        }
        if (_stricmp(t->val, "WITH_CONTENT_NOT") == 0) {
            (*i)++; ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) {
                ProPrintfChar("Error: WITH_CONTENT_NOT requires an expression\n");
                free_search_mdl_ref_node(n); return -1;
            }
            if (add_expr(&n->with_content_not, &n->with_content_not_count, &cap_cn, e) != 0) {
                free_expression(e); free_search_mdl_ref_node(n); return -1;
            }
            continue;
        }
        if (_stricmp(t->val, "WITH_IDENTIFIER") == 0) {
            (*i)++; ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) {
                ProPrintfChar("Error: WITH_IDENTIFIER requires an expression\n");
                free_search_mdl_ref_node(n); return -1;
            }
            if (add_expr(&n->with_identifier, &n->with_identifier_count, &cap_i, e) != 0) {
                free_expression(e); free_search_mdl_ref_node(n); return -1;
            }
            continue;
        }
        if (_stricmp(t->val, "WITH_IDENTIFIER_NOT") == 0) {
            (*i)++; ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) {
                ProPrintfChar("Error: WITH_IDENTIFIER_NOT requires an expression\n");
                free_search_mdl_ref_node(n); return -1;
            }
            if (add_expr(&n->with_identifier_not, &n->with_identifier_not_count, &cap_in, e) != 0) {
                free_expression(e); free_search_mdl_ref_node(n); return -1;
            }
            continue;
        }

        /* not a WITH_* -> done */
        break;
    }

    /* ---- result: single reference identifier + optional <:out> ---- */
    t = current_token(lexer, i);
    if (!t || t->type != tok_identifier) {
        ProPrintfChar("Error: Expected result reference identifier in SEARCH_MDL_REF\n");
        free_search_mdl_ref_node(n); return -1;
    }
    n->out_reference = _strdup(t->val);
    if (!n->out_reference) {
        ProPrintfChar("Error: OOM for out reference\n");
        free_search_mdl_ref_node(n); return -1;
    }
    (*i)++;
    consume_optional_inout_suffix(lexer, i, "out"); /* accepts <:out> */

    /* debug log (consistent with others) */
    {
        char* m = expression_to_string(n->model);
        char* ty = expression_to_string(n->type_expr);
        char* ss = expression_to_string(n->search_string);
        LogOnlyPrintfChar("SEARCH_MDL_REF: model=%s, type=%s, search=%s, out=%s\n",
            m ? m : "<null>", ty ? ty : "<null>", ss ? ss : "<null>", n->out_reference ? n->out_reference : "<null>");
        free(m); free(ty); free(ss);
    }

    return 0;
}


/*=================================================*\
 *  SEARCH_MDL_REFS syntax analysis
 *
 *  Syntax:
 *    SEARCH_MDL_REFS [options] model "type" "search_string"
 *                    [WITH_CONTENT expr]*
 *                    [WITH_CONTENT_NOT expr]*
 *                    [WITH_IDENTIFIER expr]*
 *                    [WITH_IDENTIFIER_NOT expr]*
 *                    array<:out>
 *
 *  Notes:
 *  - INCLUDE_MULTI_CAD takes one expression (bool-ish), default FALSE.
 *  - Each WITH_* clause adds exactly one expression; repeat clauses to add more.
\*=================================================*/
void free_search_mdl_refs_node(SearchMdlRefsNode* n)
{
    if (!n) return;

    free_expression(n->include_multi_cad);
    free_expression(n->model);
    free_expression(n->type_expr);
    free_expression(n->search_string);

    if (n->with_content) {
        for (size_t k = 0; k < n->with_content_count; ++k) free_expression(n->with_content[k]);
        free(n->with_content);
    }
    if (n->with_content_not) {
        for (size_t k = 0; k < n->with_content_not_count; ++k) free_expression(n->with_content_not[k]);
        free(n->with_content_not);
    }
    if (n->with_identifier) {
        for (size_t k = 0; k < n->with_identifier_count; ++k) free_expression(n->with_identifier[k]);
        free(n->with_identifier);
    }
    if (n->with_identifier_not) {
        for (size_t k = 0; k < n->with_identifier_not_count; ++k) free_expression(n->with_identifier_not[k]);
        free(n->with_identifier_not);
    }

    free(n->out_array);
    memset(n, 0, sizeof(*n));
}

int parse_search_mdl_refs(Lexer* lexer, size_t* i, CommandData* parsed_data)
{
    TokenData* t = NULL;
    SearchMdlRefsNode* n = &parsed_data->search_mdl_refs;
    memset(n, 0, sizeof(*n));

    /* default for INCLUDE_MULTI_CAD: FALSE (as an expression node) */
    {
        ExpressionNode* lit_false = (ExpressionNode*)malloc(sizeof(ExpressionNode));
        if (!lit_false) { ProPrintfChar("Error: OOM in SEARCH_MDL_REFS\n"); return -1; }
        lit_false->type = EXPR_LITERAL_BOOL;
        lit_false->data.bool_val = 0;
        n->include_multi_cad = lit_false;
    }

    /* ---- pre-argument options (flags or INCLUDE_MULTI_CAD <expr>) ---- */
    for (;;) {
        t = current_token(lexer, i);
        if (!t) break;
        if (t->type != tok_option && t->type != tok_identifier) break;

        if (_stricmp(t->val, "RECURSIVE") == 0) {
            n->recursive = true; (*i)++; continue;
        }
        else if (_stricmp(t->val, "ALLOW_SUPPRESSED") == 0) {
            n->allow_suppressed = true; (*i)++; continue;
        }
        else if (_stricmp(t->val, "ALLOW_SIMPREP_SUPPRESSED") == 0) {
            n->allow_simprep_suppressed = true; (*i)++; continue;
        }
        else if (_stricmp(t->val, "EXCLUDE_INHERITED") == 0) {
            n->exclude_inherited = true; (*i)++; continue;
        }
        else if (_stricmp(t->val, "EXCLUDE_FOOTER") == 0) {
            n->exclude_footer = true; (*i)++; continue;
        }
        else if (_stricmp(t->val, "NO_UPDATE") == 0) {
            n->no_update = true; (*i)++; continue;
        }
        else if (_stricmp(t->val, "INCLUDE_MULTI_CAD") == 0) {
            (*i)++; /* consume the option */
            /* one expression (bool-ish), e.g. TRUE or &flag */
            ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) {
                ProPrintfChar("Error: Expected expression after INCLUDE_MULTI_CAD at line %zu\n", t->loc.line);
                free_search_mdl_refs_node(n);
                return -1;
            }
            free_expression(n->include_multi_cad);
            n->include_multi_cad = e;
            continue;
        }

        /* stop if we hit a different option (likely a WITH_* clause) */
        if (_stricmp(t->val, "WITH_CONTENT") == 0 ||
            _stricmp(t->val, "WITH_CONTENT_NOT") == 0 ||
            _stricmp(t->val, "WITH_IDENTIFIER") == 0 ||
            _stricmp(t->val, "WITH_IDENTIFIER_NOT") == 0) {
            break;
        }
        /* not one of ours -> end options */
        break;
    }

    /* ---- positional: model, type, search_string ---- */
    n->model = parse_expression(lexer, i, NULL);
    if (!n->model) {
        ProPrintfChar("Error: Expected model expression in SEARCH_MDL_REFS\n");
        free_search_mdl_refs_node(n);
        return -1;
    }

    n->type_expr = parse_expression(lexer, i, NULL);
    if (!n->type_expr) {
        ProPrintfChar("Error: Expected \"type\" expression in SEARCH_MDL_REFS\n");
        free_search_mdl_refs_node(n);
        return -1;
    }

    n->search_string = parse_expression(lexer, i, NULL);
    if (!n->search_string) {
        ProPrintfChar("Error: Expected \"search_string\" expression in SEARCH_MDL_REFS\n");
        free_search_mdl_refs_node(n);
        return -1;
    }

    /* ---- optional WITH_* clauses (repeatable) ---- */
    size_t cap_c = 0, cap_cn = 0, cap_i = 0, cap_in = 0;
    for (;;) {
        t = current_token(lexer, i);
        if (!t || (t->type != tok_option && t->type != tok_identifier)) break;

        if (_stricmp(t->val, "WITH_CONTENT") == 0) {
            (*i)++; /* consume */
            ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) { ProPrintfChar("Error: WITH_CONTENT requires an expression\n"); free_search_mdl_refs_node(n); return -1; }
            if (add_expr(&n->with_content, &n->with_content_count, &cap_c, e) != 0) { free_expression(e); free_search_mdl_refs_node(n); return -1; }
            continue;
        }
        if (_stricmp(t->val, "WITH_CONTENT_NOT") == 0) {
            (*i)++;
            ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) { ProPrintfChar("Error: WITH_CONTENT_NOT requires an expression\n"); free_search_mdl_refs_node(n); return -1; }
            if (add_expr(&n->with_content_not, &n->with_content_not_count, &cap_cn, e) != 0) { free_expression(e); free_search_mdl_refs_node(n); return -1; }
            continue;
        }
        if (_stricmp(t->val, "WITH_IDENTIFIER") == 0) {
            (*i)++;
            ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) { ProPrintfChar("Error: WITH_IDENTIFIER requires an expression\n"); free_search_mdl_refs_node(n); return -1; }
            if (add_expr(&n->with_identifier, &n->with_identifier_count, &cap_i, e) != 0) { free_expression(e); free_search_mdl_refs_node(n); return -1; }
            continue;
        }
        if (_stricmp(t->val, "WITH_IDENTIFIER_NOT") == 0) {
            (*i)++;
            ExpressionNode* e = parse_expression(lexer, i, NULL);
            if (!e) { ProPrintfChar("Error: WITH_IDENTIFIER_NOT requires an expression\n"); free_search_mdl_refs_node(n); return -1; }
            if (add_expr(&n->with_identifier_not, &n->with_identifier_not_count, &cap_in, e) != 0) { free_expression(e); free_search_mdl_refs_node(n); return -1; }
            continue;
        }

        /* not a WITH_* clause -> leave loop to parse result array */
        break;
    }

    /* ---- result array identifier + optional <:out> ---- */
    t = current_token(lexer, i);
    if (!t || t->type != tok_identifier) {
        ProPrintfChar("Error: Expected result array identifier in SEARCH_MDL_REFS\n");
        free_search_mdl_refs_node(n);
        return -1;
    }
    n->out_array = _strdup(t->val);
    if (!n->out_array) {
        ProPrintfChar("Error: OOM for array name in SEARCH_MDL_REFS\n");
        free_search_mdl_refs_node(n);
        return -1;
    }
    (*i)++;
    consume_optional_inout_suffix(lexer, i, "out"); /* accept <:out> silently */

    /* ---- debug log (consistent with others) ---- */
    {
        char* m = expression_to_string(n->model);
        char* ty = expression_to_string(n->type_expr);
        char* ss = expression_to_string(n->search_string);
        LogOnlyPrintfChar("SEARCH_MDL_REFS: rec=%d, sup=%d, simprep=%d, ex_inh=%d, ex_foot=%d, nupd=%d, arr=%s\n",
            (int)n->recursive, (int)n->allow_suppressed, (int)n->allow_simprep_suppressed,
            (int)n->exclude_inherited, (int)n->exclude_footer, (int)n->no_update,
            n->out_array ? n->out_array : "<null>");
        LogOnlyPrintfChar("    model=%s, type=%s, search=%s\n",
            m ? m : "<null>", ty ? ty : "<null>", ss ? ss : "<null>");
        free(m); free(ty); free(ss);
    }

    return 0;
}

/* ---------- BEGIN_CATCH_ERROR / END_CATCH_ERROR ---------------------- */
/* free helper mirrors others (e.g., IF) */
static void free_catch_error_node(CatchErrorNode* n) {
    if (!n) return;
    if (n->commands) {
        for (size_t k = 0; k < n->command_count; ++k) {
            free_command_node(n->commands[k]);
        }
        free(n->commands);
    }
    memset(n, 0, sizeof(*n));
}

/* Parser:
   BEGIN_CATCH_ERROR [FIX_FAIL_UDF] [FIX_FAIL_COMPONENT]
       <nested commands...>
   END_CATCH_ERROR
*/
static int parse_begin_catch_error(Lexer* lexer, size_t* i, CommandData* parsed_data) {
    CatchErrorNode* node = &parsed_data->begin_catch_error;
    memset(node, 0, sizeof(*node));

    /* ---- options (flags) ---- */
    for (;;) {
        TokenData* t = current_token(lexer, i);
        if (!t) break;
        if (t->type != tok_option && t->type != tok_identifier) break;

        if (_stricmp(t->val, "FIX_FAIL_UDF") == 0) {
            node->fix_fail_udf = true; (*i)++; continue;
        }
        else if (_stricmp(t->val, "FIX_FAIL_COMPONENT") == 0) {
            node->fix_fail_component = true; (*i)++; continue;
        }

        /* unknown token means options phase ended */
        break;
    }

    /* ---- nested commands until END_CATCH_ERROR ---- */
    size_t cap = 4;
    node->commands = (CommandNode**)malloc(cap * sizeof(CommandNode*));
    if (!node->commands) {
        ProPrintfChar("Error: Memory allocation failed in BEGIN_CATCH_ERROR\n");
        return -1;
    }
    node->command_count = 0;

    TokenData* tok = NULL;
    while ((tok = current_token(lexer, i)) != NULL) {
        if (tok->type == tok_keyword && strcmp(tok->val, "END_CATCH_ERROR") == 0) {
            break;
        }
        CommandNode* inner = parse_command(lexer, i, NULL);
        if (inner) {
            if (node->command_count >= cap) {
                cap = cap ? cap * 2 : 4;
                CommandNode** tmp = (CommandNode**)realloc(node->commands, cap * sizeof(CommandNode*));
                if (!tmp) {
                    ProPrintfChar("Error: Realloc failed in BEGIN_CATCH_ERROR body\n");
                    free_catch_error_node(node);
                    return -1;
                }
                node->commands = tmp;
            }
            node->commands[node->command_count++] = inner;
        }
        else {
            /* make forward progress if a bad token slips in */
            ProPrintfChar("Warning: Skipping invalid token in BEGIN_CATCH_ERROR body\n");
            (*i)++;
        }
    }

    /* require END_CATCH_ERROR (same spirit as END_IF requirement) */
    tok = current_token(lexer, i);
    if (!tok || tok->type != tok_keyword || strcmp(tok->val, "END_CATCH_ERROR") != 0) {
        ProPrintfChar("Error: Expected END_CATCH_ERROR to close BEGIN_CATCH_ERROR block\n");
        free_catch_error_node(node);
        return -1;
    }
    (*i)++; /* consume END_CATCH_ERROR */

    LogOnlyPrintfChar("BEGIN_CATCH_ERROR: udf=%d, comp=%d, nested=%zu\n",
        node->fix_fail_udf ? 1 : 0, node->fix_fail_component ? 1 : 0, node->command_count);

    return 0;
}


/*=================================================*\
* 
* // Helper to add a branch to IfNode's branches array (dynamic growth)
* // Helper to add a command to a branch's or else's commands array 
* // Parse IF command and build IfNode
*
\*=================================================*/
static int add_if_branch(IfNode* if_node, IfBranch* branch) {
    if (!branch) return -1;
    IfBranch** new_branches = realloc(if_node->branches, (if_node->branch_count + 1) * sizeof(IfBranch*));
    if (!new_branches) {
        return -1;  // Memory failure
    }
    if_node->branches = new_branches;
    if_node->branches[if_node->branch_count++] = branch;
    return 0;
}

static int add_command_to_list(CommandNode*** commands_ptr, size_t* count, size_t* capacity, CommandNode* cmd) {
    if (!cmd) return -1;
    if (*count >= *capacity) {
        *capacity = *capacity ? *capacity * 2 : 4;
        CommandNode** new_commands = realloc(*commands_ptr, *capacity * sizeof(CommandNode*));
        if (!new_commands) {
            return -1;
        }
        *commands_ptr = new_commands;
    }
    (*commands_ptr)[(*count)++] = cmd;
    return 0;
}

CommandNode* parse_if_command(Lexer* lexer, size_t* i, SymbolTable* st) {
    TokenData* tok = current_token(lexer, i);
    if (!tok || tok->type != tok_keyword || strcmp(tok->val, "IF") != 0) {
        return NULL; /* not an IF */
    }
    (*i)++; /* consume IF */

    /* allocate IfNode */
    IfNode* if_node = (IfNode*)malloc(sizeof(IfNode));
    if (!if_node) {
        ProPrintfChar("Error: Memory allocation failed for IfNode\n");
        return NULL;
    }
    if_node->branches = NULL;
    if_node->branch_count = 0;
    if_node->else_commands = NULL;
    if_node->else_command_count = 0;

    /* assign a unique id for later tracking */
    if_node->id = ++s_if_id_counter;
    LogOnlyPrintfChar("IfNode: assigned id=%d at line %zu\n",
        if_node->id, tok->loc.line);

    /* parse initial IF condition */
    ExpressionNode* condition = parse_expression(lexer, i, st);
    if (!condition) {
        ProPrintfChar("Error: Expected condition after IF\n");
        free(if_node);
        return NULL;
    }
    {
        char* cond_str = expression_to_string(condition);
        LogOnlyPrintfChar("IfNode[%d] initial IF condition: %s\n",
            if_node->id, cond_str ? cond_str : "NULL");
        free(cond_str);
    }

    /* first branch */
    IfBranch* branch = (IfBranch*)malloc(sizeof(IfBranch));
    if (!branch) {
        free_expression(condition);
        free(if_node);
        return NULL;
    }
    branch->condition = condition;
    branch->commands = NULL;
    branch->command_count = 0;
    size_t branch_capacity = 4;
    branch->commands = (CommandNode**)malloc(branch_capacity * sizeof(CommandNode*));
    if (!branch->commands) {
        free_expression(condition);
        free(branch);
        free(if_node);
        return NULL;
    }

    /* parse commands for the first branch until ELSE_IF/ELSE/END_IF */
    while ((tok = current_token(lexer, i)) != NULL) {
        if (tok->type == tok_keyword &&
            (strcmp(tok->val, "ELSE_IF") == 0 ||
                strcmp(tok->val, "ELSE") == 0 ||
                strcmp(tok->val, "END_IF") == 0)) {
            break;
        }
        CommandNode* inner = parse_command(lexer, i, st);
        if (inner) {
            if (add_command_to_list(&branch->commands, &branch->command_count, &branch_capacity, inner) != 0) {
                free_command_node(inner);
                goto cleanup_if;
            }
        }
        else {
            ProPrintfChar("Warning: Skipping invalid token in IF branch\n");
            (*i)++;
        }
    }
    if (add_if_branch(if_node, branch) != 0) {
        goto cleanup_if;
    }

    /* parse zero or more ELSE_IF branches */
    while ((tok = current_token(lexer, i)) != NULL &&
        tok->type == tok_keyword &&
        strcmp(tok->val, "ELSE_IF") == 0) {
        (*i)++; /* consume ELSE_IF */
        condition = parse_expression(lexer, i, st);
        if (!condition) {
            ProPrintfChar("Error: Expected condition after ELSE_IF\n");
            goto cleanup_if;
        }
        {
            char* cond_str = expression_to_string(condition);
            LogOnlyPrintfChar("IfNode[%d] ELSE_IF condition: %s\n",
                if_node->id, cond_str ? cond_str : "NULL");
            free(cond_str);
        }

        branch = (IfBranch*)malloc(sizeof(IfBranch));
        if (!branch) {
            free_expression(condition);
            goto cleanup_if;
        }
        branch->condition = condition;
        branch->commands = NULL;
        branch->command_count = 0;
        branch_capacity = 4;
        branch->commands = (CommandNode**)malloc(branch_capacity * sizeof(CommandNode*));
        if (!branch->commands) {
            free_expression(condition);
            free(branch);
            goto cleanup_if;
        }

        /* parse commands for this ELSE_IF until next ELSE_IF/ELSE/END_IF */
        while ((tok = current_token(lexer, i)) != NULL) {
            if (tok->type == tok_keyword &&
                (strcmp(tok->val, "ELSE_IF") == 0 ||
                    strcmp(tok->val, "ELSE") == 0 ||
                    strcmp(tok->val, "END_IF") == 0)) {
                break;
            }
            CommandNode* inner = parse_command(lexer, i, st);
            if (inner) {
                if (add_command_to_list(&branch->commands, &branch->command_count, &branch_capacity, inner) != 0) {
                    free_command_node(inner);
                    free_expression(condition);
                    free(branch->commands);
                    free(branch);
                    goto cleanup_if;
                }
            }
            else {
                ProPrintfChar("Warning: Skipping invalid token in ELSE_IF branch\n");
                (*i)++;
            }
        }
        if (add_if_branch(if_node, branch) != 0) {
            goto cleanup_if;
        }
    }

    /* optional ELSE block */
    if ((tok = current_token(lexer, i)) != NULL &&
        tok->type == tok_keyword && strcmp(tok->val, "ELSE") == 0) {
        (*i)++; /* consume ELSE */
        size_t else_capacity = 4;
        if_node->else_commands = (CommandNode**)malloc(else_capacity * sizeof(CommandNode*));
        if (!if_node->else_commands) {
            goto cleanup_if;
        }
        if_node->else_command_count = 0;

        while ((tok = current_token(lexer, i)) != NULL) {
            if (tok->type == tok_keyword && strcmp(tok->val, "END_IF") == 0) {
                break;
            }
            CommandNode* inner = parse_command(lexer, i, st);
            if (inner) {
                if (add_command_to_list(&if_node->else_commands, &if_node->else_command_count, &else_capacity, inner) != 0) {
                    free_command_node(inner);
                    goto cleanup_if;
                }
            }
            else {
                ProPrintfChar("Warning: Skipping invalid token in ELSE branch\n");
                (*i)++;
            }
        }
    }

    /* require END_IF */
    if ((tok = current_token(lexer, i)) == NULL ||
        tok->type != tok_keyword || strcmp(tok->val, "END_IF") != 0) {
        ProPrintfChar("Error: Expected END_IF to close IF block\n");
        goto cleanup_if;
    }
    (*i)++; /* consume END_IF */

    /* wrap into CommandNode */
    CommandNode* cmd_node = (CommandNode*)malloc(sizeof(CommandNode));
    if (!cmd_node) {
        goto cleanup_if;
    }
    cmd_node->type = COMMAND_IF;
    cmd_node->data = (CommandData*)malloc(sizeof(CommandData));
    if (!cmd_node->data) {
        free(cmd_node);
        goto cleanup_if;
    }
    /* copy the fully built IfNode into the union */
    cmd_node->data->ifcommand = *if_node;
    free(if_node);

    LogOnlyPrintfChar("IfNode[%d]: branch_count=%zu, else_command_count=%zu\n",
        cmd_node->data->ifcommand.id,
        cmd_node->data->ifcommand.branch_count,
        cmd_node->data->ifcommand.else_command_count);

    return cmd_node;

cleanup_if:
    /* free partial IfNode on error */
    if (if_node) {
        for (size_t b = 0; b < if_node->branch_count; ++b) {
            IfBranch* br = if_node->branches[b];
            if (br) {
                free_expression(br->condition);
                for (size_t c = 0; c < br->command_count; ++c) {
                    free_command_node(br->commands[c]);
                }
                free(br->commands);
                free(br);
            }
        }
        free(if_node->branches);
        for (size_t c = 0; c < if_node->else_command_count; ++c) {
            free_command_node(if_node->else_commands[c]);
        }
        free(if_node->else_commands);
        free(if_node);
    }
    return NULL;
}

// Command table
CommandEntry command_table[] = {
    {"DECLARE_VARIABLE", COMMAND_DECLARE_VARIABLE, parse_declare_variable},
    {"GLOBAL_PICTURE", COMMAND_GLOBAL_PICTURE, parse_global_picture},
    {"SUB_PICTURE", COMMAND_SUB_PICTURE, parse_sub_picture},
    {"CONFIG_ELEM", COMMAND_CONFIG_ELEM, parse_config_elem},
    {"SHOW_PARAM", COMMAND_SHOW_PARAM, parse_show_param},
    {"CHECKBOX_PARAM", COMMAND_CHECKBOX_PARAM, parse_checkbox_param},
    {"USER_INPUT_PARAM", COMMAND_USER_INPUT_PARAM, parse_user_input_param},
    {"RADIOBUTTON_PARAM", COMMAND_RADIOBUTTON_PARAM, parse_radiobutton_param},
    {"USER_SELECT", COMMAND_USER_SELECT, parse_user_select},
    {"USER_SELECT_MULTIPLE", COMMAND_USER_SELECT_MULTIPLE, parse_user_select_multiple},
    {"USER_SELECT_MULTIPLE_OPTIONAL", COMMAND_USER_SELECT_MULTIPLE_OPTIONAL, parse_user_select_multiple_optional},
    {"USER_SELECT_OPTIONAL", COMMAND_USER_SELECT_OPTIONAL, parse_user_select_optional},
    {"INVALIDATE_PARAM", COMMAND_INVALIDATE_PARAM, parse_invlaidate_param},
    {"BEGIN_TABLE", COMMAND_BEGIN_TABLE, parse_begin_table},
    {"MEASURE_DISTANCE", COMMAND_MEASURE_DISTANCE, parse_measure_distance},
    {"MEASURE_LENGTH", COMMAND_MEASURE_LENGTH, parse_measure_length},
    {"SEARCH_MDL_REFS", COMMAND_SEARCH_MDL_REFS, parse_search_mdl_refs},
    {"SEARCH_MDL_REF", COMMAND_SEARCH_MDL_REF, parse_search_mdl_ref},
    {"BEGIN_CATCH_ERROR", COMMAND_BEGIN_CATCH_ERROR, parse_begin_catch_error},


};

// Allocate data based on CommandType
CommandData* allocate_data(CommandType type) {
    switch (type) {
    case COMMAND_CONFIG_ELEM:
        return malloc(sizeof(CommandData));
    case COMMAND_GLOBAL_PICTURE :
        return malloc(sizeof(CommandData));
    case COMMAND_SUB_PICTURE:
        return malloc(sizeof(CommandData));
    case COMMAND_DECLARE_VARIABLE:
        return malloc(sizeof(CommandData));
    case COMMAND_SHOW_PARAM:
        return malloc(sizeof(CommandData));
    case COMMAND_CHECKBOX_PARAM:
        return malloc(sizeof(CommandData));
    case COMMAND_USER_INPUT_PARAM:
        return malloc(sizeof(CommandData));
    case COMMAND_RADIOBUTTON_PARAM:
        return malloc(sizeof(CommandData));
    case COMMAND_USER_SELECT:
        return malloc(sizeof(CommandData));
    case COMMAND_USER_SELECT_OPTIONAL:
        return malloc(sizeof(CommandData));
    case COMMAND_USER_SELECT_MULTIPLE:
        return malloc(sizeof(CommandData));
    case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL:
        return malloc(sizeof(CommandData));
    case COMMAND_INVALIDATE_PARAM:
        return malloc(sizeof(CommandData));
    case COMMAND_BEGIN_TABLE:
        return malloc(sizeof(CommandData));
    case COMMAND_MEASURE_DISTANCE:
        return malloc(sizeof(CommandData));
    case COMMAND_MEASURE_LENGTH:
        return malloc(sizeof(CommandData));
    case COMMAND_SEARCH_MDL_REFS:
        return malloc(sizeof(CommandData));
    case COMMAND_SEARCH_MDL_REF:
        return malloc(sizeof(CommandData));
    case COMMAND_BEGIN_CATCH_ERROR:
        return malloc(sizeof(CommandData));
    default:
        ProPrintf(L"Error: Unknown CommandType in allocate_data\n");
        return NULL;
    }
}

CommandNode* parse_command(Lexer* lexer, size_t* i, SymbolTable* st) {
    if (*i >= lexer->token_count) return NULL;

    /* IF-family still has priority */
    CommandNode* if_cmd = parse_if_command(lexer, i, st);
    if (if_cmd) return if_cmd;

    /* Keyword-driven commands */
    if (lexer->tokens[*i].type == tok_keyword) {
        const char* keyword = lexer->tokens[*i].val;

        /* No special-casing for BEGIN_TABLE here.
           It must be registered in command_table with its parser. */
        CommandEntry* entry = NULL;
        size_t table_size = sizeof(command_table) / sizeof(command_table[0]);
        for (size_t j = 0; j < table_size; j++) {
            if (strcmp(keyword, command_table[j].command_name) == 0) {
                entry = &command_table[j];
                break;
            }
        }

        if (!entry) {
            printf("Warning: Unknown command '%s' at line %zu\n",
                keyword, lexer->tokens[*i].loc.line);
            (*i)++; /* consume the unknown keyword to make progress */
            return NULL;
        }

        /* consume the keyword token */
        (*i)++;

        CommandNode* node = (CommandNode*)malloc(sizeof(CommandNode));
        if (!node) {
            printf("Memory allocation failed for CommandNode\n");
            return NULL;
        }
        node->type = entry->type;
        node->data = allocate_data(node->type);
        if (!node->data) {
            printf("Memory allocation failed for command data\n");
            free(node);
            return NULL;
        }

        int result = entry->parser(lexer, i, node->data);
        if (result != 0) {
            printf("Error parsing '%s' at line %zu\n",
                keyword, lexer->tokens[*i - 1].loc.line);
            free_command_node(node);
            return NULL;
        }

        return node;
    }
    /* Expression / assignment handling stays exactly the same */
/* Expression / assignment handling stays exactly the same */
    else if (lexer->tokens[*i].type == tok_identifier ||
        lexer->tokens[*i].type == tok_number ||
        lexer->tokens[*i].type == tok_lparen ||
        lexer->tokens[*i].type == tok_minus ||
        lexer->tokens[*i].type == tok_string) {

        size_t start = *i;
        ExpressionNode* expr = parse_expression(lexer, i, st);
        if (!expr) {
            ProPrintfChar("Error: Failed to parse expression at line %zu\n",
                lexer->tokens[start].loc.line);
            return NULL;
        }

        TokenData* tok = current_token(lexer, i);
        if (tok && tok->type == tok_equal) {
            (*i)++; /* consume '=' */
            ExpressionNode* rhs = parse_expression(lexer, i, st);
            if (!rhs) {
                ProPrintfChar("Error: Failed to parse RHS in assignment at line %zu\n",
                    lexer->tokens[start].loc.line);
                free_expression(expr);
                return NULL;
            }

            CommandNode* node = (CommandNode*)malloc(sizeof(CommandNode));
            if (!node) {
                free_expression(expr);
                free_expression(rhs);
                return NULL;
            }
            node->type = COMMAND_ASSIGNMENT;
            node->data = (CommandData*)malloc(sizeof(CommandData));
            if (!node->data) {
                free(node);
                free_expression(expr);
                free_expression(rhs);
                return NULL;
            }
            node->semantic_valid = true;

            /* fill assignment payload */
            node->data->assignment.lhs = expr;
            node->data->assignment.rhs = rhs;
            node->data->assignment.assign_id = ++s_assign_id_counter; /* new id */

            /* logging */
            char* lhs_str = expression_to_string(expr);
            char* rhs_str = expression_to_string(rhs);
            LogOnlyPrintfChar("Parsed assignment[%d]: %s = %s",
                node->data->assignment.assign_id,
                lhs_str ? lhs_str : "NULL",
                rhs_str ? rhs_str : "NULL");
            free(lhs_str);
            free(rhs_str);

            return node;
        }
        else {
            CommandNode* node = (CommandNode*)malloc(sizeof(CommandNode));
            if (!node) {
                free_expression(expr);
                return NULL;
            }
            node->type = COMMAND_EXPRESSION;
            node->data = (CommandData*)malloc(sizeof(CommandData));
            if (!node->data) {
                free(node);
                free_expression(expr);
                return NULL;
            }
            node->semantic_valid = true;

            node->data->expression = expr;

            char* expr_str = expression_to_string(node->data->expression);
            LogOnlyPrintfChar("Parsed standalone expression: %s", expr_str ? expr_str : "NULL");
            free(expr_str);
            return node;
        }
    }
    return 0;
}

Block* find_block(BlockList* block_list, BlockType type)
{
    //Check if BlockList or its blocks array is NULL
    if (block_list == NULL || block_list->blocks == NULL)
    {
        return NULL;
    }

    // Iterate through the blocks
    for (size_t i = 0; i < block_list->block_count; i++)
    {
        if (block_list->blocks[i].type == type)
        {
            return &block_list->blocks[i];
        }
    }
    return NULL;
}

// Parse blocks with dynamic memory allocation
BlockList parse_blocks(Lexer* lexer, SymbolTable* st) {
    BlockList block_list = { NULL, 0 };
    size_t block_capacity = 4;
    block_list.blocks = malloc(block_capacity * sizeof(Block));
    if (!block_list.blocks) {
        ProPrintfChar("Memory allocation failed for block list\n");
        return (BlockList) { NULL, 0 };
    }

    size_t i = 0;
    while (i < lexer->token_count) {
        if (lexer->tokens[i].type != tok_keyword || !lexer->tokens[i].val) {
            i++;
            continue;
        }

        BlockType current_block_type = -1;
        const char* end_keyword = NULL;
        if (strcmp(lexer->tokens[i].val, "BEGIN_ASM_DESCR") == 0) {
            current_block_type = BLOCK_ASM;
            end_keyword = "END_ASM_DESCR";
        }
        else if (strcmp(lexer->tokens[i].val, "BEGIN_GUI_DESCR") == 0) {
            current_block_type = BLOCK_GUI;
            end_keyword = "END_GUI_DESCR";
        }
        else if (strcmp(lexer->tokens[i].val, "BEGIN_TAB_DESCR") == 0) {
            current_block_type = BLOCK_TAB;
            end_keyword = "END_TAB_DESCR";
        }
        else {
            i++;
            continue;
        }

        i++; // Skip BEGIN_ token

        CommandNode** commands = malloc(4 * sizeof(CommandNode*));
        size_t cmd_count = 0;
        size_t cmd_capacity = 4;
        if (!commands) {
            ProPrintfChar("Memory allocation failed for commands\n");
            free_block_list(&block_list);
            return (BlockList) { NULL, 0 };
        }

        while (i < lexer->token_count) {
            if (lexer->tokens[i].type == tok_keyword &&
                strcmp(lexer->tokens[i].val, end_keyword) == 0) {
                break;
            }
            CommandNode* cmd = parse_command(lexer, &i, st);
            if (cmd) {
                if (cmd_count >= cmd_capacity) {
                    cmd_capacity *= 2;
                    CommandNode** new_commands = realloc(commands, cmd_capacity * sizeof(CommandNode*));
                    if (!new_commands) {
                        ProPrintfChar("Memory reallocation failed for commands\n");
                        free_command_node(cmd);
                        goto cleanup_commands;
                    }
                    commands = new_commands;
                }
                commands[cmd_count++] = cmd;
            }
            else {
                // Skip to next keyword on error
                while (i < lexer->token_count && lexer->tokens[i].type != tok_keyword) {
                    i++;
                }
            }
        }

        Block block = { current_block_type, commands, cmd_count };
        if (block_list.block_count >= block_capacity) {
            block_capacity *= 2;
            Block* new_blocks = realloc(block_list.blocks, block_capacity * sizeof(Block));
            if (!new_blocks) {
                ProPrintfChar("Memory reallocation failed for block list\n");
                goto cleanup_commands;
            }
            block_list.blocks = new_blocks;
        }
        block_list.blocks[block_list.block_count++] = block;
        i++; // Skip END_ token
        continue;

    cleanup_commands:
        for (size_t j = 0; j < cmd_count; j++) {
            free_command_node(commands[j]);
        }
        free(commands);
        free_block_list(&block_list);
        return (BlockList) { NULL, 0 };
    }

    return block_list;
}

// New: Free expression recursively
void free_expression(ExpressionNode* expr) {
    if (!expr) return;
    switch (expr->type) {
    case EXPR_LITERAL_STRING:
    case EXPR_VARIABLE_REF:
        free(expr->data.string_val);
        break;
    case EXPR_ARRAY_INDEX:
        free_expression(expr->data.array_index.base);
        free_expression(expr->data.array_index.index);
        break;
    case EXPR_MAP_LOOKUP:
        free_expression(expr->data.map_lookup.map);
        free(expr->data.map_lookup.key);
        break;
    case EXPR_STRUCT_ACCESS:
        free_expression(expr->data.struct_access.structure);
        free(expr->data.struct_access.member);
        break;
    default:
        break;  // Literals have no alloc
    }
    free(expr);
}

// Free a CommandNode and its data
void free_command_node(CommandNode* node) {
    if (!node) return;
    if (node->data) {
        switch (node->type) {
        case COMMAND_CONFIG_ELEM: {
            ConfigElemNode* cen = (ConfigElemNode*)node->data;
            if (cen->location_option) free(cen->location_option);
            free(cen);
            break;
        }
        case COMMAND_INVALIDATE_PARAM: {
            InvalidateParamNode* ipn = &((CommandData*)node->data)->invalidate_param;
            free(ipn->parameter);
            break;
        }
        case COMMAND_DECLARE_VARIABLE: {
            DeclareVariableNode* dv = (DeclareVariableNode*)node->data;
            free(dv->name);
            switch (dv->var_type) {
            case VAR_PARAMETER:
                free_expression(dv->data.parameter.default_expr);
                break;
            case VAR_REFERENCE:
                free(dv->data.reference.entity_type);
                free_expression(dv->data.reference.default_ref);
                break;
            case VAR_FILE_DESCRIPTOR:
                free(dv->data.file_desc.mode);
                free(dv->data.file_desc.path);
                break;
            case VAR_ARRAY:
                for (size_t j = 0; j < dv->data.array.init_count; j++) {
                    free_expression(dv->data.array.initializers[j]);
                }
                free(dv->data.array.initializers);
                break;
            case VAR_MAP:
                for (size_t j = 0; j < dv->data.map.pair_count; j++) {
                    free(dv->data.map.pairs[j].key);
                    free_expression(dv->data.map.pairs[j].value);
                }
                free(dv->data.map.pairs);
                break;
            case VAR_GENERAL:
                // Recursive free (assuming inner_data is allocated)
                free(dv->data.general.inner_data);
                break;
            case VAR_STRUCTURE:
                for (size_t j = 0; j < dv->data.structure.member_count; j++) {
                    free(dv->data.structure.members[j].member_name);
                    free_expression(dv->data.structure.members[j].default_expr);
                }
                free(dv->data.structure.members);
                break;
            }
            free(dv);
            break;
        }
        case COMMAND_SHOW_PARAM: {
            ShowParamNode* sp = (ShowParamNode*)node->data;
            free(sp->parameter);
            if (sp->tooltip_message) free(sp->tooltip_message);
            if (sp->image_name) free(sp->image_name);
            free(sp);
            break;
        }
        case COMMAND_CHECKBOX_PARAM: {
            CheckboxParamNode* cpn = (CheckboxParamNode*)node->data;
            if (cpn->parameter) free(cpn->parameter);
            if (cpn->tooltip_message) free(cpn->tooltip_message);
            if (cpn->image_name) free(cpn->image_name);
            if (cpn->tag) free(cpn->tag);
            free(cpn);
            break;
        }
        case COMMAND_USER_INPUT_PARAM: {
            UserInputParamNode* uip = &((CommandData*)node->data)->user_input_param;
            free(uip->parameter);
            uip->parameter = NULL;
            free_expression(uip->default_expr);
            uip->default_expr = NULL;
            for (size_t j = 0; j < uip->default_for_count; j++) {
                free(uip->default_for_params[j]);
                uip->default_for_params[j] = NULL;
            }
            free(uip->default_for_params);
            uip->default_for_params = NULL;
            uip->default_for_count = 0;
            free_expression(uip->width);
            uip->width = NULL;
            free_expression(uip->decimal_places);
            uip->decimal_places = NULL;
            free_expression(uip->model);
            uip->model = NULL;
            free_expression(uip->display_order);
            uip->display_order = NULL;
            free_expression(uip->min_value);
            uip->min_value = NULL;
            free_expression(uip->max_value);
            uip->max_value = NULL;
            free_expression(uip->tooltip_message);
            uip->tooltip_message = NULL;
            free_expression(uip->image_name);
            uip->image_name = NULL;
            free_expression(uip->posX);
            uip->posX = NULL;
            free_expression(uip->posY);
            uip->posY = NULL;
            uip->required = false;
            uip->no_update = false;
            uip->on_picture = false;
            break;
        }
        case COMMAND_RADIOBUTTON_PARAM: {
            RadioButtonParamNode* rbn = (RadioButtonParamNode*)node->data;
            free(rbn->parameter);
            for (size_t k = 0; k < rbn->option_count; k++)
            {
                free_expression(rbn->options[k]);
            }
            free(rbn->options);
            free_expression(rbn->display_order);
            free_expression(rbn->tooltip_message);
            free_expression(rbn->image_name);
            free_expression(rbn->posX);
            free_expression(rbn->posY);
            break;
        }
        case COMMAND_GLOBAL_PICTURE: {
            GlobalPictureNode* gpn = (GlobalPictureNode*)node->data;
            free_expression(gpn->picture_expr);
            free(gpn);
            break;
        }
        case COMMAND_SUB_PICTURE: {
            SubPictureNode* spn = (SubPictureNode*)node->data;
            free_expression(spn->picture_expr);
            free_expression(spn->posX_expr);
            free_expression(spn->posY_expr);
            free(spn);
            break;
        }
        case COMMAND_USER_SELECT: {
            UserSelectNode* usn = &((CommandData*)node->data)->user_select;
            for (size_t k = 0; k < usn->type_count; k++) {
                free_expression(usn->types[k]);
            }
            free(usn->types);
            free(usn->reference);
            free_expression(usn->display_order);
            free_expression(usn->filter_mdl);
            free_expression(usn->filter_feat);
            free_expression(usn->filter_geom);
            free_expression(usn->filter_ref);
            free_expression(usn->filter_identifier);
            free_expression(usn->include_multi_cad);
            free_expression(usn->tooltip_message);
            free_expression(usn->image_name);
            free_expression(usn->posX);
            free_expression(usn->posY);
            free_expression(usn->tag);
            break;
        }
        case COMMAND_USER_SELECT_OPTIONAL:
        {
            UserSelectOptionalNode* usn = &((CommandData*)node->data)->user_select_optional;
            for (size_t k = 0; k < usn->type_count; k++) {
                free_expression(usn->types[k]);
            }
            free(usn->types);
            free(usn->reference);
            free_expression(usn->display_order);
            free_expression(usn->filter_mdl);
            free_expression(usn->filter_feat);
            free_expression(usn->filter_geom);
            free_expression(usn->filter_ref);
            free_expression(usn->filter_identifier);
            free_expression(usn->include_multi_cad);
            free_expression(usn->tooltip_message);
            free_expression(usn->image_name);
            free_expression(usn->posX);
            free_expression(usn->posY);
            free_expression(usn->tag);
            break;
        }
        case COMMAND_USER_SELECT_MULTIPLE: {
            UserSelectMultipleNode* n = &node->data->user_select_multiple;

            if (n->types) {
                for (size_t k = 0; k < n->type_count; k++) free_expression(n->types[k]);
                free(n->types);
            }
            free_expression(n->max_sel);
            free(n->array);

            free_expression(n->display_order);
            free_expression(n->filter_mdl);
            free_expression(n->filter_feat);
            free_expression(n->filter_geom);
            free_expression(n->filter_ref);
            free_expression(n->filter_identifier);
            free_expression(n->include_multi_cad);
            free_expression(n->tooltip_message);
            free_expression(n->image_name);
            free_expression(n->posX);
            free_expression(n->posY);
            free_expression(n->tag);
        } break;
        case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL: {
            UserSelectMultipleOptionalNode* n = &node->data->user_select_multiple_optional;

            if (n->types) {
                for (size_t k = 0; k < n->type_count; k++) free_expression(n->types[k]);
                free(n->types);
            }
            free_expression(n->max_sel);
            free(n->array);

            free_expression(n->display_order);
            free_expression(n->filter_mdl);
            free_expression(n->filter_feat);
            free_expression(n->filter_geom);
            free_expression(n->filter_ref);
            free_expression(n->filter_identifier);
            free_expression(n->include_multi_cad);
            free_expression(n->tooltip_message);
            free_expression(n->image_name);
            free_expression(n->posX);
            free_expression(n->posY);
            free_expression(n->tag);
        } break;
        case COMMAND_BEGIN_TABLE: {
            TableNode* tn = &((CommandData*)node->data)->begin_table;

            free(tn->identifier);
            free_expression(tn->name);

            for (size_t k = 0; k < tn->option_count; k++) {
                free_expression(tn->options[k]);
            }
            free(tn->options);

            for (size_t k = 0; k < tn->sel_string_count; k++) {
                free_expression(tn->sel_strings[k]);
            }
            free(tn->sel_strings);

            for (size_t k = 0; k < tn->data_type_count; k++) {
                free_expression(tn->data_types[k]);
            }
            free(tn->data_types);

            for (size_t r = 0; r < tn->row_count; r++) {
                for (size_t c = 0; c < tn->column_count; c++) {
                    free_expression(tn->rows[r][c]);
                }
                free(tn->rows[r]);
            }
            free(tn->rows);

            free(node->data);  // Free the entire CommandData allocation after cleaning internals
            break;
        }
        case COMMAND_MEASURE_DISTANCE: {
            MeasureDistanceNode* md = (MeasureDistanceNode*)node->data;
            if (md) {
                free_expression(md->parameterResult);
                free(md);
            }
            break;
        }
        case COMMAND_MEASURE_LENGTH: {
            MeasureLengthNode* ml = &((CommandData*)node->data)->measure_length;
            free_expression(ml->reference1);
            free_expression(ml->parameterResult);
            free(node->data);
        } break;

        case COMMAND_SEARCH_MDL_REFS: {
            SearchMdlRefsNode* n = &((CommandData*)node->data)->search_mdl_refs;
            /* use the same helper we wrote for the parser */
            free_search_mdl_refs_node(n);
            free(node->data);
        } break;
        case COMMAND_SEARCH_MDL_REF: {
            SearchMdlRefNode* n = &((CommandData*)node->data)->search_mdl_ref;
            /* use the same helper we wrote for the parser */
            free_search_mdl_ref_node(n);
            free(node->data);
        } break;
        case COMMAND_BEGIN_CATCH_ERROR: {
            CatchErrorNode* cen = &((CommandData*)node->data)->begin_catch_error;
            if (cen->commands) {
                for (size_t k = 0; k < cen->command_count; ++k) {
                    free_command_node(cen->commands[k]);
                }
                free(cen->commands);
            }
            free(node->data);
            break;
        }
        case COMMAND_ASSIGNMENT: {
            /* fix: node->data is CommandData*, not AssignmentNode* */
            AssignmentNode* an = &((CommandData*)node->data)->assignment;
            free_expression(an->lhs);
            free_expression(an->rhs);
            free(node->data);
            break;
        }
        case COMMAND_EXPRESSION: {
            free_expression(((CommandData*)node->data)->expression);
            free(node->data);
            break;
        }
        case COMMAND_IF: {
            IfNode* ifn = &((CommandData*)node->data)->ifcommand;
            for (size_t b = 0; b < ifn->branch_count; b++) {
                IfBranch* branch = ifn->branches[b];
                free_expression(branch->condition);
                for (size_t c = 0; c < branch->command_count; c++)
                    free_command_node(branch->commands[c]);
                free(branch->commands);
                free(branch);
            }
            free(ifn->branches);
            for (size_t c = 0; c < ifn->else_command_count; c++)
                free_command_node(ifn->else_commands[c]);
            free(ifn->else_commands);
            free(node->data);
            break;
        }
        default:
            ProPrintf(L"Warning: Unknown CommandType in free_command_node\n");
            free(node->data);
            break;
        }
    }
    free(node);
}

// Free the BlockList and all associated memory
void free_block_list(BlockList* block_list) {
    if (!block_list || !block_list->blocks) return;

    for (size_t i = 0; i < block_list->block_count; i++) {
        Block* block = &block_list->blocks[i];
        if (block->commands) {
            for (size_t j = 0; j < block->command_count; j++) {
                free_command_node(block->commands[j]);
            }
            free(block->commands);
        }
    }
    free(block_list->blocks);
    block_list->blocks = NULL;
    block_list->block_count = 0;
}





