#include "utility.h"
#include "syntaxanalysis.h"
#include "semantic_analysis.h"
#include "symboltable.h"

// Forward declaration for recursive helper
static int analyze_command(CommandNode* cmd, SymbolTable* st);
void free_variable(Variable* var);

static const char* command_names[] = {
	[COMMAND_DECLARE_VARIABLE] = "DECLARE_VARIABLE",
	[COMMAND_CONFIG_ELEM] = "CONFIG_ELEM",
	[COMMAND_SHOW_PARAM] = "SHOW_PARAM",
	[COMMAND_GLOBAL_PICTURE] = "GLOBAL_PICTURE",
	[COMMAND_SUB_PICTURE] = "SUB_PICTURE",
	[COMMAND_USER_INPUT_PARAM] = "USER_INPUT_PARAM",
	[COMMAND_CHECKBOX_PARAM] = "CHECKBOX_PARAM",
	[COMMAND_USER_SELECT] = "USER_SELECT",
	[COMMAND_USER_SELECT_MULTIPLE] = "USER_SELECT_MULTIPLE",
	[COMMAND_RADIOBUTTON_PARAM] = "RADIOBUTTON_PARAM",
	[COMMAND_IF] = "IF",
	[COMMAND_FOR] = "FOR",
	[COMMAND_WHILE] = "WHILE",
	[COMMAND_ASSIGNMENT] = "ASSIGNMENT",
	[COMMAND_EXPRESSION] = "EXPRESSION"
	// Extend this array if additional CommandType values are defined in syntaxanalysis.h.
};


// List of valid table options
static const char* valid_options[] = {
	"NO_AUTOSEL", "NO_FILTER", "DEPEND_ON_INPUT", "INVALIDATE_ON_UNSELECT",
	"SHOW_AUTOSEL", "FILTER_RIGID", "FILTER_ONLY_COLUMN", "FILTER_COLUMN",
	"TABLE_HEIGHT", "ARRAY"
};
static size_t num_valid_options = sizeof(valid_options) / sizeof(valid_options[0]);

// Check if an option is valid
int is_valid_option(const char* option) {
	for (size_t i = 0; i < num_valid_options; i++) {
		if (strcmp(option, valid_options[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

// Check if a string is a valid integer
int is_integer(const char* str) {
	// Handle empty string
	if (strlen(str) == 0) {
		return 0;
	}

	char* endptr;
	// Reset errno to detect overflow from this call
	errno = 0;
	// Call strtol; cast to void to suppress warning since we use errno and endptr
	(void)strtol(str, &endptr, 10);

	// Check if the entire string was consumed
	if (*endptr != '\0') {
		return 0; // Invalid if there are leftover characters
	}

	// Check for overflow
	if (errno == ERANGE) {
		return 0; // Out of range is invalid
	}

	return 1; // Valid integer
}

// List of valid data types
static const char* valid_data_types[] = {
	"STRING", "DOUBLE", "INTEGER", "BOOL", "SUBTABLE", "SUBCOMP",
	"CONFIG_DELETE_IDS", "CONFIG_STATE"
};
static size_t num_valid_data_types = sizeof(valid_data_types) / sizeof(valid_data_types[0]);

// Lookup table for valid types and their Creo mappings
static const struct {
	const char* name;
	CreoReferenceType creo_type;
} valid_ref_types[] = {
	{"AXIS", CREO_AXIS},
	{"CURVE", CREO_CURVE},
	{"EDGE", CREO_EDGE},
	{"SURFACE", CREO_SURFACE},
	{"PLANE", CREO_PLANE},
	// End with {NULL, CREO_UNKNOWN}
	{NULL, CREO_UNKNOWN}
};

// Helper: Get Creo type from string (case-insensitive; returns CREO_UNKNOWN if invalid)
static CreoReferenceType get_creo_ref_type(const char* type_str) {
	char* upper_str = _strdup(type_str);
	if (!upper_str) return CREO_UNKNOWN;
	for (char* p = upper_str; *p; p++) {
		*p = (char)toupper((unsigned char)*p);  // Safe cast to avoid data loss and undefined behavior
	}
	for (size_t k = 0; valid_ref_types[k].name; k++) {
		if (strcmp(upper_str, valid_ref_types[k].name) == 0) {
			free(upper_str);
			return valid_ref_types[k].creo_type;
		}
	}
	free(upper_str);
	return CREO_UNKNOWN;
}

// Check if a data type is valid
int is_valid_data_type(const char* type) {
	for (size_t i = 0; i < num_valid_data_types; i++) {
		if (strcmp(type, valid_data_types[i]) == 0) {
			return 1;
		}
	}
	return 0;
}




// New helper to map string to VariableType
static VariableType string_to_type(const char* type_str) {
	if (strcmp(type_str, "STRING") == 0) return TYPE_STRING;
	if (strcmp(type_str, "DOUBLE") == 0) return TYPE_DOUBLE;
	if (strcmp(type_str, "INTEGER") == 0) return TYPE_INTEGER;
	if (strcmp(type_str, "BOOL") == 0) return TYPE_BOOL;
	return -1;  // Invalid type
}

// New helper to set default value based on type
void set_default_value(Variable* var) {
	switch (var->type) {
	case TYPE_INTEGER: var->data.int_value = 0; break;
	case TYPE_DOUBLE: var->data.double_value = 0.0; break;
	case TYPE_STRING: var->data.string_value = _strdup(""); break;
	case TYPE_BOOL: var->data.int_value = 0; break;
	default: break;
	}
	var->display_options = NULL;  // Initialize to NULL
}

// Helper to check if a string is a valid identifier
static int is_valid_identifier(const char* name) {
	if (!name || !*name || isdigit(*name)) return 0;  // Empty or starts with digit
	for (const char* p = name; *p; p++) {
		if (!isalnum(*p) && *p != '_') return 0;  // Alphanumeric + underscore only
	}
	return 1;
}

// Helper to map AST VariableType to symbol table VariableType
VariableType map_variable_type(VariableType vtype, ParameterSubType pstype) {
	switch (vtype) {
	case VAR_PARAMETER:
		switch (pstype) {
		case PARAM_INT: return TYPE_INTEGER;
		case PARAM_DOUBLE: return TYPE_DOUBLE;
		case PARAM_STRING: return TYPE_STRING;
		case PARAM_BOOL: return TYPE_BOOL;
		default: return -1;
		}
	case VAR_REFERENCE: return TYPE_REFERENCE;
	case VAR_FILE_DESCRIPTOR: return TYPE_FILE_DESCRIPTOR;
	case VAR_ARRAY: return TYPE_ARRAY;
	case VAR_MAP: return TYPE_MAP;
	case VAR_STRUCTURE: return TYPE_STRUCTURE;
	case VAR_GENERAL: return -1;  // Handled recursively
	default: return -1;
	}
}

// Helper to validate entity_type for references
static int is_valid_entity_type(const char* entity_type) {
	// Example valid types; extend as needed for Creo entities
	const char* valid_types[] = { "surface", "edge", "quilt", "body", "curve", NULL};
	for (int i = 0; valid_types[i]; i++) {
		if (strcmp(entity_type, valid_types[i]) == 0) return 1;
	}
	return 0;
}

// Helper to create a default Variable* for a given type
static Variable* create_default_variable(VariableType type) {
	Variable* var = malloc(sizeof(Variable));
	if (!var) return NULL;
	var->type = type;
	set_default_value(var);
	return var;
}

// evaluate_to_int (add binary op handling)
int evaluate_to_int(ExpressionNode* expr, SymbolTable* st, long* result) {
	if (!expr) return -1;
	switch (expr->type) {
	case EXPR_LITERAL_INT:
	case EXPR_LITERAL_BOOL:
		*result = expr->data.int_val;
		return 0;
	case EXPR_LITERAL_DOUBLE:  // Coerce double to int (with warning if needed)
		*result = (long)expr->data.double_val;
		return 0;
	case EXPR_VARIABLE_REF: {
		Variable* var = get_symbol(st, expr->data.string_val);
		if (!var) return -1;
		if (var->type == TYPE_INTEGER || var->type == TYPE_BOOL) {
			*result = var->data.int_value;
			return 0;
		}
		else if (var->type == TYPE_DOUBLE) {  // Coerce
			*result = (long)var->data.double_value;
			return 0;
		}
		return -1;
	}
	case EXPR_ARRAY_INDEX: {
		// Existing code...
	}
	case EXPR_UNARY_OP: {
		if (expr->data.unary.op == UNOP_NEG) {
			long v;
			if (evaluate_to_int(expr->data.unary.operand, st, &v) != 0) return -1;
			*result = -v;
			return 0;
		}
		return -1; /* unsupported unary op for int */
	}
	case EXPR_BINARY_OP: {  // New: Handle arithmetic ops recursively
		long left_val, right_val;
		int status = evaluate_to_int(expr->data.binary.left, st, &left_val);
		if (status != 0) return -1;
		status = evaluate_to_int(expr->data.binary.right, st, &right_val);
		if (status != 0) return -1;
		switch (expr->data.binary.op) {
		case BINOP_ADD: *result = left_val + right_val; break;
		case BINOP_SUB: *result = left_val - right_val; break;
		case BINOP_MUL: *result = left_val * right_val; break;
		case BINOP_DIV:
			if (right_val == 0) return -1;  // Div by zero
			*result = left_val / right_val; break;
		default: return -1;  // Unsupported op for int
		}
		return 0;
	}
	default: return -1;
	}
}

// Basic expression evaluator for double (with coercion from int)
int evaluate_to_double(ExpressionNode* expr, SymbolTable* st, double* result) {
	if (!expr) return -1;
	switch (expr->type) {
	case EXPR_LITERAL_DOUBLE:
		*result = expr->data.double_val;
		return 0;
	case EXPR_LITERAL_INT:
		*result = (double)expr->data.int_val;
		return 0;
	case EXPR_VARIABLE_REF: {
		Variable* var = get_symbol(st, expr->data.string_val);
		if (!var) return -1;
		if (var->type == TYPE_DOUBLE) {
			*result = var->data.double_value;
			return 0;
		}
		else if (var->type == TYPE_INTEGER) {
			*result = (double)var->data.int_value;
			return 0;
		}
		return -1;
	}
	case EXPR_UNARY_OP: {
		if (expr->data.unary.op == UNOP_NEG) {
			double v;
			if (evaluate_to_double(expr->data.unary.operand, st, &v) != 0) return -1;
			*result = -v;
			return 0;
		}
		return -1; /* unsupported unary op for double */
	}
		 // ... (add EXPR_ARRAY_INDEX if needed)
	case EXPR_BINARY_OP: {  // New: Handle recursively
		double left_val, right_val;
		int status = evaluate_to_double(expr->data.binary.left, st, &left_val);
		if (status != 0) return -1;
		status = evaluate_to_double(expr->data.binary.right, st, &right_val);
		if (status != 0) return -1;
		switch (expr->data.binary.op) {
		case BINOP_ADD: *result = left_val + right_val; break;
		case BINOP_SUB: *result = left_val - right_val; break;
		case BINOP_MUL: *result = left_val * right_val; break;
		case BINOP_DIV:
			if (right_val == 0.0) return -1;
			*result = left_val / right_val; break;
		default: return -1;
		}
		return 0;
	}
	default: return -1;
	}
}

// Basic expression evaluators (for string)
int evaluate_to_string(ExpressionNode* expr, SymbolTable* st, char** result) {
	if (!expr) {
		*result = NULL;
		return 0;  // Optional (empty string allowed per spec)
	}
	switch (expr->type) {
	case EXPR_LITERAL_STRING:
		*result = _strdup(expr->data.string_val);
		return *result ? 0 : -1;
	case EXPR_VARIABLE_REF: {
		Variable* var = get_symbol(st, expr->data.string_val);
		if (var && var->type == TYPE_STRING) {
			*result = _strdup(var->data.string_value);
			return *result ? 0 : -1;
		}
		else if (!var) {
			// Treat undeclared identifier as literal string (handles unquoted file names)
			*result = _strdup(expr->data.string_val);
			return *result ? 0 : -1;
		}
		return -1;  // Declared but non-string
	}
	case EXPR_BINARY_OP: {
		if (expr->data.binary.op != BINOP_ADD) {
			return -1;  // Only + for concatenation
		}
		char* left_str;
		int status = evaluate_to_string(expr->data.binary.left, st, &left_str);
		if (status != 0) return -1;
		char* right_str;
		status = evaluate_to_string(expr->data.binary.right, st, &right_str);
		if (status != 0) {
			free(left_str);
			return -1;
		}
		// Concatenate (handle NULL as empty)
		size_t len = (left_str ? strlen(left_str) : 0) + (right_str ? strlen(right_str) : 0) + 1;
		*result = malloc(len);
		if (!*result) {
			free(left_str);
			free(right_str);
			return -1;
		}
		snprintf(*result, len, "%s%s", left_str ? left_str : "", right_str ? right_str : "");
		free(left_str);
		free(right_str);
		return 0;
	}
	default:
		*result = NULL;
		return -1;
	}
}

// Infer the static type of an expression (returns VariableType or -1 on error)
VariableType get_expression_type(ExpressionNode* expr, SymbolTable* st) {
	if (!expr) return -1;

	switch (expr->type) {
	case EXPR_LITERAL_INT:
	case EXPR_LITERAL_BOOL:
		return (expr->type == EXPR_LITERAL_BOOL ? TYPE_BOOL : TYPE_INTEGER);
	case EXPR_LITERAL_DOUBLE:
		return TYPE_DOUBLE;
	case EXPR_LITERAL_STRING:
		return TYPE_STRING;
	case EXPR_CONSTANT: {
		// Assume data.double_val is set for PI; extend if more constants
		return TYPE_DOUBLE;  // PI is double
	}
	case EXPR_VARIABLE_REF: {
		Variable* var = get_symbol(st, expr->data.string_val);
		if (var) {
			return var->type;
		}
		else if (strchr(expr->data.string_val, '.')) {  // Heuristic: treat as file name (string)
			return TYPE_STRING;
		}
		else {
			ProPrintfChar("Error: Undeclared variable '%s' in expression\n", expr->data.string_val);
			return -1;
		}
	}
	case EXPR_UNARY_OP: {
		VariableType operand_type = get_expression_type(expr->data.unary.operand, st);
		if (expr->data.unary.op == UNOP_NEG && (operand_type == TYPE_INTEGER || operand_type == TYPE_DOUBLE)) {
			return operand_type;
		}
		ProPrintfChar("Error: Invalid operand type for unary operator\n");
		return -1;
	}
	case EXPR_BINARY_OP: {
		VariableType left_type = get_expression_type(expr->data.binary.left, st);
		VariableType right_type = get_expression_type(expr->data.binary.right, st);
		if (left_type == -1 || right_type == -1) return -1;

		/*  treat + as string concatenation if either side is a string */
		if (expr->data.binary.op == BINOP_ADD &&
			(left_type == TYPE_STRING || right_type == TYPE_STRING)) {
			return TYPE_STRING;
		}

		// Arithmetic: numeric only, promote to double if mixed
		if (expr->data.binary.op >= BINOP_ADD && expr->data.binary.op <= BINOP_DIV) {
			if ((left_type != TYPE_INTEGER && left_type != TYPE_DOUBLE) ||
				(right_type != TYPE_INTEGER && right_type != TYPE_DOUBLE)) {
				ProPrintfChar("Error: Arithmetic operands must be numeric\n");
				return -1;
			}
			return (left_type == TYPE_DOUBLE || right_type == TYPE_DOUBLE) ? TYPE_DOUBLE : TYPE_INTEGER;
		}

		// Comparisons: return bool; numerics or strings for ==/<>
		if (expr->data.binary.op >= BINOP_EQ && expr->data.binary.op <= BINOP_GE) {
			if (left_type != right_type) {
				ProPrintfChar("Error: Incompatible types for comparison\n");
				return -1;
			}
			if (expr->data.binary.op == BINOP_EQ || expr->data.binary.op == BINOP_NE) {
				if (left_type == TYPE_INTEGER || left_type == TYPE_DOUBLE || left_type == TYPE_BOOL || left_type == TYPE_STRING) {
					return TYPE_BOOL;
				}
			}
			else {  // <, >, <=, >==: numerics only
				if (left_type == TYPE_INTEGER || left_type == TYPE_DOUBLE) {
					return TYPE_BOOL;
				}
			}
			ProPrintfChar("Error: Invalid types for comparison\n");
			return -1;
		}

		// Logical: bool or coercible numeric
		if (expr->data.binary.op == BINOP_AND || expr->data.binary.op == BINOP_OR) {
			if ((left_type == TYPE_BOOL || left_type == TYPE_INTEGER || left_type == TYPE_DOUBLE) &&
				(right_type == TYPE_BOOL || right_type == TYPE_INTEGER || right_type == TYPE_DOUBLE)) {
				return TYPE_BOOL;
			}
			ProPrintfChar("Error: Logical operands must be boolean or coercible\n");
			return -1;
		}

		ProPrintfChar("Error: Unknown binary operator\n");
		return -1;
	}
	case EXPR_FUNCTION_CALL: {
		size_t expected_args = 0;
		VariableType return_type = -1;
		VariableType arg_types[3] = { -1, -1, -1 };  // Max 3 args

		switch (expr->data.func_call.func) {
			// Math functions (1 arg, double -> double)
		case FUNC_SIN: case FUNC_ASIN: case FUNC_COS: case FUNC_ACOS:
		case FUNC_TAN: case FUNC_ATAN: case FUNC_SINH: case FUNC_COSH:
		case FUNC_TANH: case FUNC_LOG: case FUNC_LN: case FUNC_EXP:
		case FUNC_CEIL: case FUNC_FLOOR: case FUNC_ABS: case FUNC_SQRT:
		case FUNC_SQR:
			expected_args = 1;
			arg_types[0] = TYPE_DOUBLE;
			return_type = TYPE_DOUBLE;
			break;

			// 2-arg math (double, double -> double)
		case FUNC_POW: case FUNC_MOD:
			expected_args = 2;
			arg_types[0] = TYPE_DOUBLE;
			arg_types[1] = TYPE_DOUBLE;
			return_type = TYPE_DOUBLE;
			break;

			// round (double, int -> double)
		case FUNC_ROUND:
			expected_args = 2;
			arg_types[0] = TYPE_DOUBLE;
			arg_types[1] = TYPE_INTEGER;
			return_type = TYPE_DOUBLE;
			break;

			// String functions
		case FUNC_STRFIND: case FUNC_STRFINDCS: case FUNC_STRCMP: case FUNC_STRCMPCS:
			expected_args = 2;
			arg_types[0] = TYPE_STRING;
			arg_types[1] = TYPE_STRING;
			return_type = TYPE_INTEGER;
			break;
		case FUNC_STRLEN: case FUNC_ASC:
			expected_args = 1;
			arg_types[0] = TYPE_STRING;
			return_type = TYPE_INTEGER;
			break;
		case FUNC_STOF:
			expected_args = 1;
			arg_types[0] = TYPE_STRING;
			return_type = TYPE_DOUBLE;
			break;
		case FUNC_STOI:
			expected_args = 1;
			arg_types[0] = TYPE_STRING;
			return_type = TYPE_INTEGER;
			break;
		case FUNC_STOB:
			expected_args = 1;
			arg_types[0] = TYPE_STRING;
			return_type = TYPE_BOOL;
			break;
		case FUNC_ISNUMBER: case FUNC_ISINTEGER: case FUNC_ISDOUBLE:
			expected_args = 1;
			arg_types[0] = TYPE_STRING;
			return_type = TYPE_BOOL;
			break;

			// Comparison functions (double, double, int -> bool)
		case FUNC_EQUAL: case FUNC_LESS: case FUNC_LESSOREQUAL:
		case FUNC_GREATER: case FUNC_GREATEROREQUAL:
			expected_args = 3;
			arg_types[0] = TYPE_DOUBLE;
			arg_types[1] = TYPE_DOUBLE;
			arg_types[2] = TYPE_INTEGER;
			return_type = TYPE_BOOL;
			break;

		default:
			ProPrintfChar("Error: Unknown function return type\n");
			return -1;
		}

		if (expr->data.func_call.arg_count != expected_args) {
			ProPrintfChar("Error: Function expects %zu args, got %zu\n", expected_args, expr->data.func_call.arg_count);
			return -1;
		}

		for (size_t a = 0; a < expected_args; a++) {
			VariableType arg_type = get_expression_type(expr->data.func_call.args[a], st);
			if (arg_type == -1) return -1;
			if (arg_type != arg_types[a]) {
				// Allow int -> double coercion
				if (arg_types[a] == TYPE_DOUBLE && arg_type == TYPE_INTEGER) {
					continue;
				}
				ProPrintfChar("Error: Arg %zu type mismatch: expected %d, got %d\n", a, arg_types[a], arg_type);
				return -1;
			}
		}

		return return_type;
	}
	case EXPR_ARRAY_INDEX: {
		VariableType base_type = get_expression_type(expr->data.array_index.base, st);
		if (base_type != TYPE_ARRAY) {
			ProPrintfChar("Error: Array index on non-array type\n");
			return -1;
		}
		VariableType index_type = get_expression_type(expr->data.array_index.index, st);
		if (index_type != TYPE_INTEGER) {
			ProPrintfChar("Error: Array index must be integer\n");
			return -1;
		}
		// Infer element type from symbol table (assume base is variable ref)
		if (expr->data.array_index.base->type == EXPR_VARIABLE_REF) {
			Variable* arr_var = get_symbol(st, expr->data.array_index.base->data.string_val);
			if (arr_var && arr_var->type == TYPE_ARRAY && arr_var->data.array.size > 0) {
				return arr_var->data.array.elements[0]->type;  // Assume homogeneous array
			}
		}
		ProPrintfChar("Error: Unable to infer array element type\n");
		return -1;
	}
	case EXPR_MAP_LOOKUP: {
		VariableType map_type = get_expression_type(expr->data.map_lookup.map, st);
		if (map_type != TYPE_MAP) {
			ProPrintfChar("Error: Map lookup on non-map type\n");
			return -1;
		}
		// Values in map can vary; return general or require specific check (for simplicity, assume TYPE_GENERAL)
		return -1;  // Adjust if map values are typed uniformly
	}
	case EXPR_STRUCT_ACCESS: {
		VariableType struct_type = get_expression_type(expr->data.struct_access.structure, st);
		if (struct_type != TYPE_STRUCTURE) {
			ProPrintfChar("Error: Struct access on non-struct type\n");
			return -1;
		}
		// Lookup member type from symbol table
		if (expr->data.struct_access.structure->type == EXPR_VARIABLE_REF) {
			Variable* struct_var = get_symbol(st, expr->data.struct_access.structure->data.string_val);
			if (struct_var && struct_var->type == TYPE_STRUCTURE) {
				Variable* member_var = hash_table_lookup(struct_var->data.structure, expr->data.struct_access.member);
				if (member_var) {
					return member_var->type;
				}
			}
		}
		ProPrintfChar("Error: Unable to infer struct member type\n");
		return -1;
	}
	default:
		ProPrintfChar("Error: Unknown expression type\n");
		return -1;
	}
}

// General evaluator: Returns a new Variable* from expression (updated for binary ops)
int evaluate_expression(ExpressionNode* expr, SymbolTable* st, Variable** result) {
	if (!expr) {
		*result = NULL;
		return -1;
	}
	*result = (Variable*)malloc(sizeof(Variable));
	if (!*result) return -1;

	switch (expr->type) {
	case EXPR_LITERAL_INT:
	case EXPR_LITERAL_BOOL:
		(*result)->type = (expr->type == EXPR_LITERAL_BOOL ? TYPE_BOOL : TYPE_INTEGER);
		(*result)->data.int_value = (int)expr->data.int_val;
		return 0;

	case EXPR_LITERAL_DOUBLE:
		(*result)->type = TYPE_DOUBLE;
		(*result)->data.double_value = expr->data.double_val;
		return 0;

	case EXPR_LITERAL_STRING:
		(*result)->type = TYPE_STRING;
		(*result)->data.string_value = _strdup(expr->data.string_val);
		if (!(*result)->data.string_value) { free(*result); return -1; }
		return 0;

	case EXPR_VARIABLE_REF: {
		Variable* src = get_symbol(st, expr->data.string_val);
		if (!src) { free(*result); return -1; }
		memcpy(*result, src, sizeof(Variable));
		if (src->type == TYPE_STRING) {
			(*result)->data.string_value = _strdup(src->data.string_value);
		}
		/* TODO: deep copy for ARRAY/MAP/STRUCT if needed */
		return 0;
	}

	case EXPR_BINARY_OP: {
		/* ---- NEW: short-circuit logical AND / OR ---- */
		if (expr->data.binary.op == BINOP_AND || expr->data.binary.op == BINOP_OR) {
			Variable* l = NULL;
			if (evaluate_expression(expr->data.binary.left, st, &l) != 0 || !l) {
				free(*result);
				return -1;
			}

			/* coerce left to truthiness: bool/int != 0, double != 0.0 */
			int l_true = 0;
			if (l->type == TYPE_BOOL || l->type == TYPE_INTEGER) {
				l_true = (l->data.int_value != 0);
			}
			else if (l->type == TYPE_DOUBLE) {
				l_true = (l->data.double_value != 0.0);
			}
			else {
				free_variable(l);
				free(*result);
				return -1; /* invalid type for logical op */
			}

			if (expr->data.binary.op == BINOP_AND && !l_true) {
				/* left false => whole expr false (short-circuit) */
				(*result)->type = TYPE_BOOL;
				(*result)->data.int_value = 0;
				free_variable(l);
				return 0;
			}
			if (expr->data.binary.op == BINOP_OR && l_true) {
				/* left true => whole expr true (short-circuit) */
				(*result)->type = TYPE_BOOL;
				(*result)->data.int_value = 1;
				free_variable(l);
				return 0;
			}

			/* must evaluate right side */
			free_variable(l);
			Variable* r = NULL;
			if (evaluate_expression(expr->data.binary.right, st, &r) != 0 || !r) {
				free(*result);
				return -1;
			}
			int r_true = 0;
			if (r->type == TYPE_BOOL || r->type == TYPE_INTEGER) {
				r_true = (r->data.int_value != 0);
			}
			else if (r->type == TYPE_DOUBLE) {
				r_true = (r->data.double_value != 0.0);
			}
			else {
				free_variable(r);
				free(*result);
				return -1;
			}
			(*result)->type = TYPE_BOOL;
			(*result)->data.int_value = r_true;
			free_variable(r);
			return 0;
		}
		/* ---- END NEW LOGICALS ---- */

		/* existing: evaluate both sides once for arithmetic/comparisons */
		Variable* left_val = NULL;
		Variable* right_val = NULL;
		if (evaluate_expression(expr->data.binary.left, st, &left_val) != 0 || !left_val) {
			free(*result);
			return -1;
		}
		if (evaluate_expression(expr->data.binary.right, st, &right_val) != 0 || !right_val) {
			free_variable(left_val);
			free(*result);
			return -1;
		}

		/* numeric coercion (int <-> double) for arithmetic/compare */
		if (left_val->type != right_val->type) {
			if (left_val->type == TYPE_INTEGER && right_val->type == TYPE_DOUBLE) {
				left_val->data.double_value = (double)left_val->data.int_value;
				left_val->type = TYPE_DOUBLE;
			}
			else if (left_val->type == TYPE_DOUBLE && right_val->type == TYPE_INTEGER) {
				right_val->data.double_value = (double)right_val->data.int_value;
				right_val->type = TYPE_DOUBLE;
			}
			else {
				free_variable(left_val);
				free_variable(right_val);
				free(*result);
				return -1;
			}
		}

		/* arithmetic */
		if (expr->data.binary.op >= BINOP_ADD && expr->data.binary.op <= BINOP_DIV) {
			if (left_val->type != TYPE_INTEGER && left_val->type != TYPE_DOUBLE) {
				free_variable(left_val); free_variable(right_val); free(*result); return -1;
			}
			double l = (left_val->type == TYPE_DOUBLE ? left_val->data.double_value : (double)left_val->data.int_value);
			double r = (right_val->type == TYPE_DOUBLE ? right_val->data.double_value : (double)right_val->data.int_value);
			double dres = 0.0;
			switch (expr->data.binary.op) {
			case BINOP_ADD: dres = l + r; break;
			case BINOP_SUB: dres = l - r; break;
			case BINOP_MUL: dres = l * r; break;
			case BINOP_DIV:
				if (r == 0.0) { free_variable(left_val); free_variable(right_val); free(*result); return -1; }
				dres = l / r; break;
			default: free_variable(left_val); free_variable(right_val); free(*result); return -1;
			}
			/* keep double if any side was double or if division; else int */
			if (left_val->type == TYPE_DOUBLE || right_val->type == TYPE_DOUBLE || expr->data.binary.op == BINOP_DIV) {
				(*result)->type = TYPE_DOUBLE;
				(*result)->data.double_value = dres;
			}
			else {
				(*result)->type = TYPE_INTEGER;
				(*result)->data.int_value = (int)dres;
			}
			free_variable(left_val);
			free_variable(right_val);
			return 0;
		}

		/* comparisons -> boolean */
		(*result)->type = TYPE_BOOL;
		{
			const double eps = 1e-9; /* tolerance for doubles */
			switch (expr->data.binary.op) {
			case BINOP_EQ:
				if (left_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value =
						(fabs(left_val->data.double_value - right_val->data.double_value) <= eps);
				}
				else if (left_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value == right_val->data.int_value);
				}
				else if (left_val->type == TYPE_STRING) {
					(*result)->data.int_value = (strcmp(left_val->data.string_value,
						right_val->data.string_value) == 0);
				}
				else { free_variable(left_val); free_variable(right_val); free(*result); return -1; }
				break;

			case BINOP_NE:
				if (left_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value =
						(fabs(left_val->data.double_value - right_val->data.double_value) > eps);
				}
				else if (left_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value != right_val->data.int_value);
				}
				else if (left_val->type == TYPE_STRING) {
					(*result)->data.int_value = (strcmp(left_val->data.string_value,
						right_val->data.string_value) != 0);
				}
				else { free_variable(left_val); free_variable(right_val); free(*result); return -1; }
				break;

			case BINOP_LT:
				if (left_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value =
						(left_val->data.double_value < right_val->data.double_value - eps);
				}
				else if (left_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value < right_val->data.int_value);
				}
				else { free_variable(left_val); free_variable(right_val); free(*result); return -1; }
				break;

			case BINOP_GT:
				if (left_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value =
						(left_val->data.double_value > right_val->data.double_value + eps);
				}
				else if (left_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value > right_val->data.int_value);
				}
				else { free_variable(left_val); free_variable(right_val); free(*result); return -1; }
				break;

			case BINOP_LE:
				if (left_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value =
						(left_val->data.double_value <= right_val->data.double_value + eps);
				}
				else if (left_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value <= right_val->data.int_value);
				}
				else { free_variable(left_val); free_variable(right_val); free(*result); return -1; }
				break;

			case BINOP_GE:
				if (left_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value =
						(left_val->data.double_value >= right_val->data.double_value - eps);
				}
				else if (left_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value >= right_val->data.int_value);
				}
				else { free_variable(left_val); free_variable(right_val); free(*result); return -1; }
				break;

			default:
				free_variable(left_val); free_variable(right_val); free(*result); return -1;
			}
		}

		free_variable(left_val);
		free_variable(right_val);
		return 0;
	}

	default:
		free(*result);
		return -1;
	}
}

/*=================================================*\
* 
* GLOBAL_PICTURE semantic analysis: Validate and store picture file name
* 
* 
\*=================================================*/
int check_global_picture_semantics(GlobalPictureNode* node, SymbolTable* st)
{
	ProError status;
	if (!node || !node->picture_expr)
	{
		ProPrintfChar("Error: Invalid GLOBAL_PICTURE node\n");
		return -1;
	}

	// Check for duplicate GLOBAL_PICTURE
	if (get_symbol(st, "GLOBAL_PICTURE"))
	{
		ProPrintfChar("Error: Multiple GLOBAL_PICTURE commands detected; only one allowed\n");
		return -1;
	}

	// Evaluate expression to string
	char* file_name;
	status = evaluate_to_string(node->picture_expr, st, &file_name);
	if (status != 0 || !file_name || strlen(file_name) == 0)
	{
		free(file_name);
		ProPrintfChar("Error: Failed to evaluate or empty picture file name\n");
		return -1;
	}

	// Create and store variable in symbol table
	Variable* pic_var = malloc(sizeof(Variable));
	if (!pic_var)
	{
		free(file_name);
		ProPrintfChar("Error: Memeory allocation failed for GLOBAL_PICTURE variable\n");
		return -1;
	}

	// Inserted snippet: Prepend GIF_DIR to form full path
	Variable* gif_dir = get_symbol(st, "GIF_DIR");
	if (gif_dir && gif_dir->type == TYPE_STRING &&
		(file_name[0] != '\\' || file_name[1] != '\\') &&  // Not UNC
		!(isalpha(file_name[0]) && file_name[1] == ':')) {  // Not drive letter
		size_t len = strlen(gif_dir->data.string_value) + strlen(file_name) + 1;
		char* full_path = malloc(len);
		if (!full_path) {
			free(pic_var);
			free(file_name);
			ProPrintfChar("Error: Memory allocation failed for full path in GLOBAL_PICTURE\n");
			return -1;
		}
		snprintf(full_path, len, "%s%s", gif_dir->data.string_value, file_name);
		free(file_name);
		file_name = full_path;
	}

	pic_var->type = TYPE_STRING;
	pic_var->data.string_value = file_name;

	set_symbol(st, "GLOBAL_PICTURE", pic_var);

	LogOnlyPrintfChar("NODE: GLOBAL_PICTURE validated and stored as '%s' \n", file_name);

	return 0;
}

/*=================================================*\
* 
* SUB_PICTURE semantic analysis: validate and append to array in symbol table
* 
* 
\*=================================================*/
int check_sub_picture_semantic(SubPictureNode* node, SymbolTable* st) {
	if (!node || !node->picture_expr || !node->posX_expr || !node->posY_expr) {
		ProPrintfChar("Error: Invalid SUB_PICTURE node\n");
		return -1;
	}

	/* Require GLOBAL_PICTURE to have been validated first */
	if (!get_symbol(st, "GLOBAL_PICTURE")) {
		ProPrintfChar("Error: SUB_PICTURE requires prior GLOBAL_PICTURE\n");
		return -1;
	}

	/* Positions must be integer-typed expressions (keep this) */
	if (get_expression_type(node->posX_expr, st) != TYPE_INTEGER ||
		get_expression_type(node->posY_expr, st) != TYPE_INTEGER) {
		ProPrintfChar("Error: Positions must evaluate to integers\n");
		return -1;
	}

	/* Validate picture expression and normalize path (unchanged pattern) */
	char* file_name;
	int status = evaluate_to_string(node->picture_expr, st, &file_name);
	if (status != 0 || !file_name || strlen(file_name) == 0) {
		free(file_name);
		ProPrintfChar("Error: Failed to evaluate or empty picture file name\n");
		return -1;
	}

	/* Prepend GIF_DIR if relative */
	Variable* gif_dir = get_symbol(st, "GIF_DIR");
	if (gif_dir && gif_dir->type == TYPE_STRING &&
		(file_name[0] != '\\' || file_name[1] != '\\') && /* not UNC */
		!(isalpha(file_name[0]) && file_name[1] == ':'))  /* not drive letter */
	{
		size_t len = strlen(gif_dir->data.string_value) + strlen(file_name) + 1;
		char* full_path = (char*)malloc(len);
		if (!full_path) {
			free(file_name);
			ProPrintfChar("Error: Memory allocation failed for full path in SUB_PICTURE\n");
			return -1;
		}
		snprintf(full_path, len, "%s%s", gif_dir->data.string_value, file_name);
		free(file_name);
		file_name = full_path;
	}

	/* Extension warning (unchanged) */
	const char* ext = strrchr(file_name, '.');
	if (!ext || (strcmp(ext, ".gif") != 0 && strcmp(ext, ".bmp") != 0 &&
		strcmp(ext, ".jpeg") != 0 && strcmp(ext, ".png") != 0)) {
		ProPrintfChar("Warning: Unsupported image format in SUB_PICTURE\n");
	}

	free(file_name);

	ProPrintfChar("Note: SUB_PICTURE validated (positions allowed to be negative; handled at runtime)\n");
	return 0;
}

/*=================================================*\
* 
* SHOW_PARAM semantic analysis: Validate and store parameter with display options
* 
* 
\*=================================================*/
int check_show_param_semantics(ShowParamNode* node, SymbolTable* st) {
	if (!node->parameter || strlen(node->parameter) == 0) {
		ProPrintfChar("Error: Missing or empty parameter name in SHOW_PARAM\n");
		return 1;  // Error
	}

	if (!is_valid_identifier(node->parameter)) {
		ProPrintfChar("Error: Invalid parameter name '%s' in SHOW_PARAM\n", node->parameter);
		return 1;
	}

	// Map subtype to VariableType
	VariableType declared_type;
	switch (node->subtype) {
	case PARAM_INT: declared_type = TYPE_INTEGER; break;
	case PARAM_DOUBLE: declared_type = TYPE_DOUBLE; break;
	case PARAM_STRING: declared_type = TYPE_STRING; break;
	case PARAM_BOOL: declared_type = TYPE_BOOL; break;
	default:
		ProPrintfChar("Error: Invalid parameter subtype for '%s' in SHOW_PARAM\n", node->parameter);
		return 1;
	}

	Variable* param_var = get_symbol(st, node->parameter);
	if (param_var) {
		if (param_var->type != declared_type) {
			ProPrintfChar("Error: Type mismatch for existing parameter '%s': Expected %d, but found %d\n",
				node->parameter, declared_type, param_var->type);
			return 1;
		}
		ProPrintfChar("Note: Parameter '%s' already exists; SHOW_PARAM options map will be (re)created if options are present\n", node->parameter);
	}
	else {
		param_var = malloc(sizeof(Variable));
		if (!param_var) {
			ProPrintfChar("Memory allocation failed for parameter '%s'\n", node->parameter);
			return 1;
		}
		param_var->type = declared_type;
		set_default_value(param_var);
		set_symbol(st, node->parameter, param_var);
	}

	// Check if any display options are present
	bool has_options = node->tooltip_message != NULL || node->image_name != NULL || node->on_picture;
	if (!has_options) {
		return 0;  // No options; success after variable handling
	}

	// Create map for options (mirroring CONFIG_ELEM storage)
	HashTable* options_map = create_hash_table(16);
	if (!options_map) {
		ProPrintfChar("Error: Failed to create hash table for options of '%s'\n", node->parameter);
		return 1;
	}

	int status = 0;
	char* tooltip = NULL;
	if (node->tooltip_message) {
		status = evaluate_to_string(node->tooltip_message, st, &tooltip);
		if (status != 0 || !tooltip || strlen(tooltip) == 0) {
			free(tooltip);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or empty tooltip message for '%s'\n", node->parameter);
			return 1;
		}
		Variable* tooltip_var = malloc(sizeof(Variable));
		if (!tooltip_var) {
			free(tooltip);
			free_hash_table(options_map);
			ProPrintfChar("Memory allocation failed for tooltip of '%s'\n", node->parameter);
			return 1;
		}
		tooltip_var->type = TYPE_STRING;
		tooltip_var->data.string_value = tooltip;  // Transfer ownership
		hash_table_insert(options_map, "tooltip", tooltip_var);
	}

	if (node->image_name) {
		if (!node->tooltip_message) {
			free_hash_table(options_map);
			ProPrintfChar("Error: IMAGE_NAME requires TOOLTIP_MESSAGE in SHOW_PARAM for '%s'\n", node->parameter);
			return 1;
		}
		char* image;
		status = evaluate_to_string(node->image_name, st, &image);
		if (status != 0 || !image || strlen(image) == 0) {
			free(image);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or empty image name for '%s'\n", node->parameter);
			return 1;
		}
		Variable* image_var = malloc(sizeof(Variable));
		if (!image_var) {
			free(image);
			free_hash_table(options_map);
			ProPrintfChar("Memory allocation failed for image of '%s'\n", node->parameter);
			return 1;
		}
		image_var->type = TYPE_STRING;
		image_var->data.string_value = image;
		hash_table_insert(options_map, "image", image_var);
	}

	if (node->on_picture) {
		Variable* on_picture_var = malloc(sizeof(Variable));
		if (!on_picture_var) {
			free_hash_table(options_map);
			ProPrintfChar("Memory allocation failed for on_picture flag of '%s'\n", node->parameter);
			return 1;
		}
		on_picture_var->type = TYPE_BOOL;
		on_picture_var->data.int_value = 1;  // True
		hash_table_insert(options_map, "on_picture", on_picture_var);

		long posx_val;
		status = evaluate_to_int(node->posX, st, &posx_val);
		if (status != 0 || posx_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posX for '%s'\n", node->parameter);
			return 1;
		}
		Variable* posx_var = malloc(sizeof(Variable));
		if (!posx_var) {
			free_hash_table(options_map);
			ProPrintfChar("Memory allocation failed for posX of '%s'\n", node->parameter);
			return 1;
		}
		posx_var->type = TYPE_INTEGER;
		posx_var->data.int_value = (int)posx_val;
		hash_table_insert(options_map, "posX", posx_var);

		long posy_val;
		status = evaluate_to_int(node->posY, st, &posy_val);
		if (status != 0 || posy_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posY for '%s'\n", node->parameter);
			return 1;
		}
		Variable* posy_var = malloc(sizeof(Variable));
		if (!posy_var) {
			free_hash_table(options_map);
			ProPrintfChar("Memory allocation failed for posY of '%s'\n", node->parameter);
			return 1;
		}
		posy_var->type = TYPE_INTEGER;
		posy_var->data.int_value = (int)posy_val;
		hash_table_insert(options_map, "posY", posy_var);
	}

	// Create a dedicated Variable for the options map and store it in the symbol table
	Variable* options_var = malloc(sizeof(Variable));
	if (!options_var) {
		free_hash_table(options_map);
		ProPrintfChar("Memory allocation failed for SHOW_PARAM options variable of '%s'\n", node->parameter);
		return 1;
	}
	options_var->type = TYPE_MAP;
	options_var->data.map = options_map;

	// Generate unique key for this SHOW_PARAM's options
	char key[256];
	snprintf(key, sizeof(key), "SHOW_PARAM_OPTIONS_%s", node->parameter);
	set_symbol(st, key, options_var);

	ProPrintfChar("Note: Stored SHOW_PARAM options map for '%s' under key '%s'\n", node->parameter, key);

	return 0;  // Success
}

/*=================================================*\
* 
* CONFIG_ELEM semantic analysis
* 
* 
\*=================================================*/
int check_config_elem_semantics(ConfigElemNode* node, SymbolTable* st) {
	if (!node) {
		ProPrintfChar("Error: Invalid CONFIG_ELEM node\n");
		return -1;
	}

	// Evaluate width if present (optional double expression)
	double width_val = -1.0;  // Default invalid to indicate absence
	if (node->width) {
		int status = evaluate_to_double(node->width, st, &width_val);
		if (status != 0) {
			ProPrintfChar("Error: Failed to evaluate width expression in CONFIG_ELEM\n");
			return -1;
		}
		if (width_val <= 0.0) {
			ProPrintfChar("Error: Width must be greater than 0 in CONFIG_ELEM\n");
			return -1;
		}
		// Note on interpretation
		if (width_val > 0.0 && width_val < 1.0) {
			ProPrintfChar("Note: Width %.2f interpreted as perceptual value (%% of screen)\n", width_val);
		}
		else if (width_val >= 1.0) {
			ProPrintfChar("Note: Width %.2f interpreted as absolute value\n", width_val);
		}
	}

	// Evaluate height if present (optional, but requires width)
	double height_val = -1.0;  // Default invalid
	if (node->height) {
		int status = evaluate_to_double(node->height, st, &height_val);
		if (status != 0) {
			ProPrintfChar("Error: Failed to evaluate height expression in CONFIG_ELEM\n");
			return -1;
		}
		if (height_val <= 0.0) {
			ProPrintfChar("Error: Height must be greater than 0 in CONFIG_ELEM\n");
			return -1;
		}
		if (width_val < 0.0) {  // Height without width
			ProPrintfChar("Error: Height specified without width in CONFIG_ELEM\n");
			return -1;
		}
		// Note on interpretation
		if (height_val > 0.0 && height_val < 1.0) {
			ProPrintfChar("Note: Height %.2f interpreted as perceptual value (%% of screen)\n", height_val);
		}
		else if (height_val >= 1.0) {
			ProPrintfChar("Note: Height %.2f interpreted as absolute value\n", height_val);
		}
	}

	// Validate SCREEN_LOCATION if present
	char* location_str = NULL;
	if (node->has_screen_location) {
		if (!node->location_option) {
			ProPrintfChar("Error: SCREEN_LOCATION requires a location option in CONFIG_ELEM\n");
			return -1;
		}
		int status = evaluate_to_string(node->location_option, st, &location_str);
		if (status != 0 || !location_str || strlen(location_str) == 0) {
			free(location_str);
			ProPrintfChar("Error: Failed to evaluate or invalid location option for SCREEN_LOCATION in CONFIG_ELEM\n");
			return -1;
		}
		// Validate against allowed locations
		const char* valid_locations[] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT", "CENTER", NULL };
		int is_valid = 0;
		for (int k = 0; valid_locations[k]; k++) {
			if (strcmp(location_str, valid_locations[k]) == 0) {
				is_valid = 1;
				break;
			}
		}
		if (!is_valid) {
			ProPrintfChar("Error: Invalid location option '%s' for SCREEN_LOCATION in CONFIG_ELEM\n", location_str);
			free(location_str);
			return -1;
		}
	}

	// Store in symbol table as a map (only one CONFIG_ELEM allowed; checked in caller)
	HashTable* config_map = create_hash_table(16);
	if (!config_map) {
		free(location_str);
		ProPrintfChar("Error: Failed to create hash table for CONFIG_ELEM\n");
		return -1;
	}

	// Populate map with boolean flags for options
	if (!add_bool_to_map(config_map, "no_tables", node->no_tables) ||
		!add_bool_to_map(config_map, "no_gui", node->no_gui) ||
		!add_bool_to_map(config_map, "auto_commit", node->auto_commit) ||
		!add_bool_to_map(config_map, "auto_close", node->auto_close) ||
		!add_bool_to_map(config_map, "show_gui_for_existing", node->show_gui_for_existing) ||
		!add_bool_to_map(config_map, "no_auto_update", node->no_auto_update) ||
		!add_bool_to_map(config_map, "continue_on_cancel", node->continue_on_cancel) ||
		!add_bool_to_map(config_map, "has_screen_location", node->has_screen_location)) {
		free_hash_table(config_map);
		free(location_str);
		ProPrintfChar("Error: Failed to add boolean options to CONFIG_ELEM map\n");
		return -1;
	}

	// Add evaluated strings/doubles
	if (node->has_screen_location) {
		if (!add_string_to_map(config_map, "location_option", location_str)) {
			free_hash_table(config_map);
			free(location_str);
			ProPrintfChar("Error: Failed to add location_option to CONFIG_ELEM map\n");
			return -1;
		}
	}
	if (width_val >= 0.0) {
		if (!add_double_to_map(config_map, "width", width_val)) {
			free_hash_table(config_map);
			free(location_str);
			ProPrintfChar("Error: Failed to add width to CONFIG_ELEM map\n");
			return -1;
		}
	}
	if (height_val >= 0.0) {
		if (!add_double_to_map(config_map, "height", height_val)) {
			free_hash_table(config_map);
			free(location_str);
			ProPrintfChar("Error: Failed to add height to CONFIG_ELEM map\n");
			return -1;
		}
	}

	// Create and insert Variable for the map
	Variable* config_var = malloc(sizeof(Variable));
	if (!config_var) {
		free_hash_table(config_map);
		free(location_str);
		ProPrintfChar("Error: Failed to create variable for CONFIG_ELEM\n");
		return -1;
	}
	config_var->type = TYPE_MAP;
	config_var->data.map = config_map;
	set_symbol(st, "CONFIG_ELEM", config_var);


	free(location_str);  // Clean up evaluated string
	return 0;  // Success
}

/*=================================================*\
* 
* DECLARE_VARIABLE semantic anlysis
* 
* 
\*=================================================*/
int check_declare_variable_semantics(DeclareVariableNode* node, SymbolTable* st) {
	if (!node || !node->name || strlen(node->name) == 0) {
		ProPrintfChar("Error: Missing or empty variable name in DECLARE_VARIABLE\n");
		return 1;  // Error
	}

	if (!is_valid_identifier(node->name)) {
		ProPrintfChar("Error: Invalid variable name '%s' in DECLARE_VARIABLE\n", node->name);
		return 1;
	}

	VariableType mapped_type = map_variable_type(node->var_type, (node->var_type == VAR_PARAMETER ? node->data.parameter.subtype : PARAM_INT));
	if (mapped_type == -1) {
		ProPrintfChar("Error: Invalid variable type for '%s' in DECLARE_VARIABLE\n", node->name);
		return 1;
	}

	Variable* existing = get_symbol(st, node->name);
	if (existing != NULL) {
		// New: Flag as duplicate by incrementing count (original value preserved)
		existing->declaration_count++;
		ProPrintfChar("Note: Variable '%s' redeclared (count now %d); INVALIDATE_PARAM check for REDECLARATION handled at runtime\n",
			node->name, existing->declaration_count);
		return 0;  // Process as success, but flagged for runtime
	}

	// New declaration: Proceed with creation and storage (as in original)
	Variable* var = malloc(sizeof(Variable));
	if (!var) {
		ProPrintfChar("Memory allocation failed for variable '%s'\n", node->name);
		return 1;
	}
	var->type = mapped_type;
	var->declaration_count = 1;  // New: Initialize count

	int status = 0;
	switch (node->var_type) {
	case VAR_PARAMETER:
		if (node->data.parameter.default_expr) {
			switch (var->type) {
			case TYPE_INTEGER:
			case TYPE_BOOL: {
				long ival;
				status = evaluate_to_int(node->data.parameter.default_expr, st, &ival);
				if (status != 0) {
					free(var);
					ProPrintfChar("Error: Failed to evaluate default integer/bool for '%s'\n", node->name);
					return 1;
				}
				var->data.int_value = (int)ival;
				break;
			}
			case TYPE_DOUBLE: {
				double dval;
				status = evaluate_to_double(node->data.parameter.default_expr, st, &dval);
				if (status != 0) {
					free(var);
					ProPrintfChar("Error: Failed to evaluate default double for '%s'\n", node->name);
					return 1;
				}
				var->data.double_value = dval;
				break;
			}
			case TYPE_STRING: {
				char* sval;
				status = evaluate_to_string(node->data.parameter.default_expr, st, &sval);
				if (status != 0 || !sval) {
					free(var);
					ProPrintfChar("Error: Failed to evaluate default string for '%s'\n", node->name);
					return 1;
				}
				var->data.string_value = sval;  // Transfer ownership
				break;
			}
			default:
				free(var);
				ProPrintfChar("Error: Unsupported parameter subtype for '%s'\n", node->name);
				return 1;
			}
		}
		else {
			set_default_value(var);
		}
		break;
	case VAR_REFERENCE:
		var->data.reference.reference_value = NULL;
		break;
	case VAR_FILE_DESCRIPTOR:
		var->data.file_descriptor = NULL;
		break;
	case VAR_ARRAY:
		var->data.array.elements = NULL;
		var->data.array.size = 0;
		break;
	case VAR_MAP:
		var->data.map = create_hash_table(16);
		if (!var->data.map) {
			free(var);
			return 1;
		}
		break;
	case VAR_STRUCTURE:
		var->data.structure = create_hash_table(16);
		if (!var->data.structure) {
			free(var);
			return 1;
		}
		break;
	default:
		free(var);
		ProPrintfChar("Error: Unsupported variable type for '%s'\n", node->name);
		return 1;
	}

	set_symbol(st, node->name, var);
	return 0;
}

/*=================================================*\
* 
* CHECKBOX_PARAM semantic analysis: Validate and store parameter with display options
* 
* 
\*=================================================*/
int check_checkbox_param_semantics(CheckboxParamNode* node, SymbolTable* st) {
	if (!node->parameter || strlen(node->parameter) == 0) {
		ProPrintfChar("Error: Missing or empty parameter name in CHECKBOX_PARAM\n");
		return 1; // Error
	}
	if (!is_valid_identifier(node->parameter)) {
		ProPrintfChar("Error: Invalid parameter name '%s' in CHECKBOX_PARAM\n", node->parameter);
		return 1;
	}
	// Map subtype to VariableType (restrict to INTEGER or BOOL for checkbox semantics)
	VariableType declared_type;
	switch (node->subtype) {
	case PARAM_INT: declared_type = TYPE_INTEGER; break;
	case PARAM_BOOL: declared_type = TYPE_BOOL; break;
	default:
		ProPrintfChar("Error: Invalid parameter subtype for '%s' in CHECKBOX_PARAM; must be INTEGER or BOOL\n", node->parameter);
		return 1;
	}
	Variable* param_var = get_symbol(st, node->parameter);
	if (param_var) {
		if (param_var->type != declared_type) {
			ProPrintfChar("Error: Type mismatch for existing parameter '%s': Expected %d, but found %d\n",
				node->parameter, declared_type, param_var->type);
			return 1;
		}
		ProPrintfChar("Note: Parameter '%s' already exists; CHECKBOX_PARAM options map will be (re)created if options are present\n", node->parameter);
	}
	else {
		param_var = malloc(sizeof(Variable));
		if (!param_var) {
			ProPrintfChar("Memory allocation failed for parameter '%s'\n", node->parameter);
			return 1;
		}
		param_var->type = declared_type;
		set_default_value(param_var); // Defaults to 0 (unchecked)
		param_var->display_options = NULL; // Not used; options stored separately
		set_symbol(st, node->parameter, param_var);
	}
	// Check if any options are present
	bool has_options = node->required || node->display_order != NULL || node->tooltip_message != NULL ||
		node->image_name != NULL || node->on_picture || node->tag != NULL;
	if (!has_options) {
		return 0; // No options; success after variable handling
	}
	// Create map for options
	HashTable* options_map = create_hash_table(16);
	if (!options_map) {
		ProPrintfChar("Error: Failed to create hash table for options of '%s'\n", node->parameter);
		return 1;
	}
	int status = 0;
	// Add required (bool)
	if (!add_bool_to_map(options_map, "required", node->required)) {
		free_hash_table(options_map);
		ProPrintfChar("Error: Failed to add required to options map for '%s'\n", node->parameter);
		return 1;
	}
	// Evaluate and add display_order if present (int >= 0)
	if (node->display_order) {
		long display_order_val;
		status = evaluate_to_int(node->display_order, st, &display_order_val);
		if (status != 0 || display_order_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) display_order for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_int_to_map(options_map, "display_order", (int)display_order_val)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add display_order to options map for '%s'\n", node->parameter);
			return 1;
		}
	}
	// Evaluate and add tooltip_message if present (string)
	char* tooltip = NULL;
	if (node->tooltip_message) {
		status = evaluate_to_string(node->tooltip_message, st, &tooltip);
		if (status != 0 || !tooltip || strlen(tooltip) == 0) {
			free(tooltip);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or empty tooltip message for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_string_to_map(options_map, "tooltip", tooltip)) {
			free(tooltip);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add tooltip to options map for '%s'\n", node->parameter);
			return 1;
		}
		free(tooltip); // add_string_to_map duplicates
	}
	// Evaluate and add image_name if present (string, requires tooltip)
	if (node->image_name) {
		if (!node->tooltip_message) {
			free_hash_table(options_map);
			ProPrintfChar("Error: IMAGE requires TOOLTIP in CHECKBOX_PARAM for '%s'\n", node->parameter);
			return 1;
		}
		char* image;
		status = evaluate_to_string(node->image_name, st, &image);
		if (status != 0 || !image || strlen(image) == 0) {
			free(image);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or empty image name for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_string_to_map(options_map, "image", image)) {
			free(image);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add image to options map for '%s'\n", node->parameter);
			return 1;
		}
		free(image);
	}
	// Add on_picture and positions if set
	if (node->on_picture) {
		if (!add_bool_to_map(options_map, "on_picture", true)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add on_picture to options map for '%s'\n", node->parameter);
			return 1;
		}
		long posx_val;
		status = evaluate_to_int(node->posX, st, &posx_val);
		if (status != 0 || posx_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posX for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_int_to_map(options_map, "posX", (int)posx_val)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add posX to options map for '%s'\n", node->parameter);
			return 1;
		}
		long posy_val;
		status = evaluate_to_int(node->posY, st, &posy_val);
		if (status != 0 || posy_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posY for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_int_to_map(options_map, "posY", (int)posy_val)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add posY to options map for '%s'\n", node->parameter);
			return 1;
		}
	}
	// Evaluate and add tag if present (string)
	char* tag = NULL;
	if (node->tag) {
		status = evaluate_to_string(node->tag, st, &tag);
		if (status != 0 || !tag || strlen(tag) == 0) {
			free(tag);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or empty tag for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_string_to_map(options_map, "tag", tag)) {
			free(tag);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add tag to options map for '%s'\n", node->parameter);
			return 1;
		}
		free(tag);
	}
	// Create a dedicated Variable for the options map and store it in the symbol table
	Variable* options_var = malloc(sizeof(Variable));
	if (!options_var) {
		free_hash_table(options_map);
		ProPrintfChar("Memory allocation failed for CHECKBOX_PARAM options variable of '%s'\n", node->parameter);
		return 1;
	}
	options_var->type = TYPE_MAP;
	options_var->data.map = options_map;
	// Generate unique key for this CHECKBOX_PARAM's options
	char key[256];
	snprintf(key, sizeof(key), "CHECKBOX_PARAM_OPTIONS_%s", node->parameter);
	set_symbol(st, key, options_var);
	ProPrintfChar("Note: Stored CHECKBOX_PARAM options map for '%s' under key '%s'\n", node->parameter, key);
	return 0; // Success
}

/*=================================================*\
* 
* USER_INPUT_PARAM semantic analysis: Validate and store parameter with display options
* 
* 
\*=================================================*/
int check_user_input_param_semantics(UserInputParamNode * node, SymbolTable * st)
{
	if (!node->parameter || strlen(node->parameter) == 0)
	{
		ProPrintfChar("Error: missing or empty parameter name in USER_INPUT_PARAM\n");
		return -1;
	}
	if (!is_valid_identifier(node->parameter))
	{
		ProPrintfChar("Error: Invalid parameter name '%s' in USER_INPUT_PARAM\n", node->parameter);
		return -1;
	}
	VariableType declared_type;
	switch (node->subtype)
	{
	case PARAM_INT: declared_type = TYPE_INTEGER; break;
	case PARAM_DOUBLE: declared_type = TYPE_DOUBLE; break;
	case PARAM_STRING: declared_type = TYPE_STRING; break;
	case PARAM_BOOL: declared_type = TYPE_BOOL; break;
	default:
		ProPrintfChar("Error: Invalid parameter subtype for '%s' in USER_INPUT_PARAM\n", node->parameter);
		return -1;
	}
	Variable* existing = get_symbol(st, node->parameter);
	if (existing)
	{
		if (existing->type != declared_type)
		{
			ProPrintfChar("Error: Type mismatch for existing parameter '%s': Expected %d, but found %d",
				node->parameter, declared_type, existing->type);
			return -1;
		}
		ProPrintfChar("NOTE: Parameter '%s' already exists; USER_INPUT_PARAM details noted but no change to variable\n", node->parameter);
	}
	else
	{
		Variable* var = malloc(sizeof(Variable));
		if (!var)
		{
			ProPrintfChar("Memory allocation failed for parameter '%s'\n", node->parameter);
			return -1;
		}
		var->type = declared_type;
		if (node->default_expr)
		{
			int status = 0;
			switch (var->type)
			{
			case TYPE_INTEGER:
			case TYPE_BOOL:
			{
				long ival;
				status = evaluate_to_int(node->default_expr, st, &ival);
				if (status != 0)
				{
					free(var);
					ProPrintfChar("Error: Failed to evaluate default integer/bool for '%s'\n", node->parameter);
					return -1;
				}
				var->data.int_value = (int)ival;
				break;
			}
			case TYPE_DOUBLE:
			{
				double dval;
				status = evaluate_to_double(node->default_expr, st, &dval);
				if (status != 0)
				{
					free(var);
					ProPrintfChar("Error: Failed to evaluate default double for '%s'\n", node->parameter);
					return -1;
				}
				var->data.double_value = dval;
				break;
			}
			case TYPE_STRING:
			{
				char* sval;
				status = evaluate_to_string(node->default_expr, st, &sval);
				if (status != 0)
				{
					free(var);
					ProPrintfChar("Error: Failed to evaluate default string for '%s'\n", node->parameter);
					return -1;
				}
				var->data.string_value = sval;
				break;
			}
			default:
				free(var);
				ProPrintfChar("Error: Unsupported parameter subtype for '%s'\n", node->parameter);
				return -1;
			}
		}
		else
		{
			set_default_value(var);
		}
		set_symbol(st, node->parameter, var);
	}
	// Check if any display options are present
	bool has_option = node->width != NULL || node->decimal_places != NULL || node->model != NULL ||
		node->display_order != NULL || node->min_value != NULL || node->max_value != NULL ||
		node->tooltip_message != NULL || node->image_name != NULL || node->on_picture ||
		node->required || node->no_update || node->default_for_count > 0;
	if (!has_option)
	{
		return 0; // No options: success after variable handling
	}
	// Create map for display options
	HashTable* display_map = create_hash_table(16);
	if (!display_map)
	{
		ProPrintfChar("Error: Failed to create hash table for display options of '%s'\n", node->parameter);
		return -1;
	}
	int status = 0;
	// Add required (bool)
	if (!add_bool_to_map(display_map, "required", node->required))
	{
		free_hash_table(display_map);
		ProPrintfChar("Error: Failed to add required to display map for '%s'\n", node->parameter);
		return -1;
	}
	// Add no_update (bool)
	if (!add_bool_to_map(display_map, "no_update", node->no_update))
	{
		free_hash_table(display_map);
		ProPrintfChar("Error: Failed to add no_update to display_map for '%s'\n", node->parameter);
		return -1;
	}
	// Add default_for_params if present (string array)
	if (node->default_for_count > 0)
	{
		char** params = malloc(node->default_for_count * sizeof(char*));
		if (!params)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Memory allocation failed for default_for_params in '%s'\n", node->parameter);
			return -1;
		}
		for (size_t k = 0; k < node->default_for_count; k++)
		{
			if (!is_valid_identifier(node->default_for_params[k]))
			{
				for (size_t j = 0; j < k; j++) free(params[j]);
				free(params);
				free_hash_table(display_map);
				ProPrintfChar("Error: Invalid parameter name in DEFAULT_FOR for '%s'\n", node->parameter);
				return -1;
			}
			// Check if referenced param exists (warning if not)
			if (!get_symbol(st, node->default_for_params[k]))
			{
				ProPrintfChar("Warning: DEFAULT_FOR references undeclared parameter '%s' in '%s'\n",
					node->default_for_params[k], node->parameter);
			}
			params[k] = _strdup(node->default_for_params[k]);
			if (!params[k])
			{
				for (size_t j = 0; j < k; j++) free(params[j]);
				free(params);
				free_hash_table(display_map);
				ProPrintfChar("Error: Failed to duplicate default_for_param in '%s'\n", node->parameter);
				return -1;
			}
		}
		if (!add_string_array_to_map(display_map, "default_for", params, node->default_for_count))
		{
			for (size_t k = 0; k < node->default_for_count; k++) free(params[k]);
			free(params);
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add default_for to display map for '%s'\n", node->parameter);
			return -1;
		}
		for (size_t k = 0; k < node->default_for_count; k++) free(params[k]);
		free(params);
	}
	// Evaluate and add width if present (numeric)
	if (node->width)
	{
		double width_val;
		status = evaluate_to_double(node->width, st, &width_val);
		if (status != 0 || width_val <= 0.0)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (non-positive) width for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_double_to_map(display_map, "width", width_val))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add width to display map for '%s'\n", node->parameter);
			return -1;
		}
	}
	// Evaluate and add decimal_places if present (numeric)
	if (node->decimal_places)
	{
		if (node->subtype != PARAM_DOUBLE)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: DECIMAL_PLACES only applicable for DOUBLE in '%s'\n", node->parameter);
			return -1;
		}
		double dec_val;
		status = evaluate_to_double(node->decimal_places, st, &dec_val);
		if (status != 0 || dec_val < 0.0)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) decimal_places for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_double_to_map(display_map, "decimal_places", dec_val))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add decimal_places to display map for '%s'\n", node->parameter);
			return -1;
		}
	}
	// Evaluate and add model if present (string)
	char* model_str = NULL;
	if (node->model)
	{
		status = evaluate_to_string(node->model, st, &model_str);
		if (status != 0 || !model_str || strlen(model_str) == 0)
		{
			free(model_str);
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or empty model for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_string_to_map(display_map, "model", model_str))
		{
			free(model_str);
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add model to display map for '%s'\n", node->parameter);
			return -1;
		}
		free(model_str);
	}
	// Evaluate and add display_order if present (int)
	if (node->display_order)
	{
		long display_order_val;
		status = evaluate_to_int(node->display_order, st, &display_order_val);
		if (status != 0 || display_order_val < 0)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) DISPLAY_ORDER for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_int_to_map(display_map, "display_order", (int)display_order_val))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add DISPLAY_ORDER to display map for '%s'\n", node->parameter);
			return -1;
		}
	}
	// Evaluate and add min_value if present (numeric)
	if (node->min_value)
	{
		if (node->subtype != PARAM_DOUBLE && node->subtype != PARAM_INT)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: MIN_VALUE only applicable for numeric type in '%s'\n", node->parameter);
			return -1;
		}
		double min_val;
		status = evaluate_to_double(node->min_value, st, &min_val);
		if (status != 0)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate min_value for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_double_to_map(display_map, "min_value", min_val))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add min_value to display map for '%s'\n", node->parameter);
			return -1;
		}
	}
	// Evaluate and add max_value if present (numeric)
	if (node->max_value)
	{
		if (node->subtype != PARAM_DOUBLE && node->subtype != PARAM_INT)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: MAX_VALUE only applicable for numeric types in '%s'\n", node->parameter);
			return -1;
		}
		double max_val;
		status = evaluate_to_double(node->max_value, st, &max_val);
		if (status != 0)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate MAX_VALUE for '%s'\n", node->parameter);
			return -1;
		}
		if (node->min_value)
		{
			double min_val;
			evaluate_to_double(node->min_value, st, &min_val);
			if (max_val < min_val)
			{
				free_hash_table(display_map);
				ProPrintfChar("Error: MAX_VALUE less than MIN_VALUE in '%s'\n", node->parameter);
				return -1;
			}
		}
		if (!add_double_to_map(display_map, "max_value", max_val))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add MAX_VALUE to display map for '%s'\n", node->parameter);
			return -1;
		}
	}
	// Evaluate and add tooltip_message if present (string)
	char* tooltip = NULL;
	if (node->tooltip_message)
	{
		status = evaluate_to_string(node->tooltip_message, st, &tooltip);
		if (status != 0 || !tooltip || strlen(tooltip) == 0)
		{
			free(tooltip);
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or empty tooltip message for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_string_to_map(display_map, "tooltip", tooltip))
		{
			free(tooltip);
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add tooltip to display map for '%s'\n", node->parameter);
			return -1;
		}
		free(tooltip);
	}
	// Evaluate and add image_name if present (string, requires tooltip)
	if (node->image_name)
	{
		if (!node->tooltip_message)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: IMAGE requires TOOLTIP in USER_INPUT_PARAM for '%s'\n", node->parameter);
			return -1;
		}
		char* image;
		status = evaluate_to_string(node->image_name, st, &image);
		if (status != 0 || !image || strlen(image) == 0)
		{
			free(image);
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or empty image name for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_string_to_map(display_map, "image", image))
		{
			free(image);
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add image to display map for '%s'\n", node->parameter);
			return -1;
		}
		free(image);
	}
	// Add on_picture and position if set
	if (node->on_picture)
	{
		if (!add_bool_to_map(display_map, "on_picture", true))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add on_picture to display map for '%s'\n", node->parameter);
			return -1;
		}
		// Require posX and posY if on_picture
		if (!node->posX || !node->posY)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: ON_PICTURE requires POS_X and POS_Y for '%s'\n", node->parameter);
			return -1;
		}
		long posx_val;
		status = evaluate_to_int(node->posX, st, &posx_val);
		if (status != 0 || posx_val < 0)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posX for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_int_to_map(display_map, "posX", (int)posx_val))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add posX to display map for '%s'\n", node->parameter);
			return -1;
		}
		long posy_val;
		status = evaluate_to_int(node->posY, st, &posy_val);
		if (status != 0 || posy_val < 0)
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posY for '%s'\n", node->parameter);
			return -1;
		}
		if (!add_int_to_map(display_map, "posY", (int)posy_val))
		{
			free_hash_table(display_map);
			ProPrintfChar("Error: Failed to add posY to display map for '%s'\n", node->parameter);
			return -1;
		}
	}
	// Create Variable for the display map
	Variable* display_var = malloc(sizeof(Variable));
	if (!display_var)
	{
		free_hash_table(display_map);
		ProPrintfChar("Memory allocation failed for display variable of '%s'\n", node->parameter);
		return -1;
	}
	display_var->type = TYPE_MAP;
	display_var->data.map = display_map;
	// Generate key for symbol table (e.g., "USER_INPUT:%s")
	char display_key[256];
	snprintf(display_key, sizeof(display_key), "USER_INPUT:%s", node->parameter);
	set_symbol(st, display_key, display_var);
	return 0;
}

/*=================================================*\
* 
* RADIOBUTTON_PARAM semantic analysis: Validate and store parameter with display options
* 
* 
\*=================================================*/
int check_radiobutton_param_semantics(RadioButtonParamNode * node, SymbolTable * st) {
	if (!node->parameter || strlen(node->parameter) == 0) {
		ProPrintfChar("Error: Missing or empty parameter name in RADIOBUTTON_PARAM\n");
		return 1;  // Error
	}
	if (!is_valid_identifier(node->parameter)) {
		ProPrintfChar("Error: Invalid parameter name '%s' in RADIOBUTTON_PARAM\n", node->parameter);
		return 1;
	}
	// Validate parsed subtype (parameter_type from syntax: must be INTEGER or BOOL; others invalid)
	VariableType declared_type;
	if (node->subtype == PARAM_INT) {
		declared_type = TYPE_INTEGER;
	}
	else if (node->subtype == PARAM_BOOL) {
		if (node->option_count > 2) {
			ProPrintfChar("Error: BOOL parameter in RADIOBUTTON_PARAM cannot have more than 2 options for '%s'\n", node->parameter);
			return 1;
		}
		declared_type = TYPE_BOOL;
	}
	else {
		// STRING or DOUBLE not supported (parameter gets index, not option label/value)
		ProPrintfChar("Error: Invalid parameter type for RADIOBUTTON_PARAM '%s'; must be INTEGER or BOOL\n", node->parameter);
		return 1;
	}
	Variable* param_var = get_symbol(st, node->parameter);
	if (param_var) {
		if (param_var->type != declared_type) {
			ProPrintfChar("Error: Type mismatch for existing parameter '%s': Expected %d, but found %d\n",
				node->parameter, declared_type, param_var->type);
			return 1;
		}
		ProPrintfChar("Note: Parameter '%s' already exists; RADIOBUTTON options map will be (re)created if options are present\n", node->parameter);
	}
	else {
		param_var = malloc(sizeof(Variable));
		if (!param_var) {
			ProPrintfChar("Memory allocation failed for parameter '%s'\n", node->parameter);
			return 1;
		}
		param_var->type = declared_type;
		if (declared_type == TYPE_INTEGER) {
			param_var->data.int_value = -1;  // Default to no selection
		}
		else {  // TYPE_BOOL
			param_var->data.int_value = 0;
		}
		set_symbol(st, node->parameter, param_var);
	}
	// Validate and evaluate options (must be non-empty strings if present)
	if (node->option_count < 2) {
		ProPrintfChar("Warning: RADIOBUTTON_PARAM '%s' has fewer than 2 options; typically requires multiple choices\n", node->parameter);
	}
	// Check if any display options are present (including the options list)
	bool has_options = node->required || node->display_order != NULL || node->tooltip_message != NULL ||
		node->image_name != NULL || node->on_picture || node->option_count > 0;
	if (!has_options) {
		return 0;  // No options; success after variable handling
	}
	// Create map for options
	HashTable* options_map = create_hash_table(16);
	if (!options_map) {
		ProPrintfChar("Error: Failed to create hash table for options of '%s'\n", node->parameter);
		return 1;
	}
	int status = 0;
	// Add options array if present
	if (node->option_count > 0) {
		Variable* options_array = malloc(sizeof(Variable));
		if (!options_array) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Memory allocation failed for options array in '%s'\n", node->parameter);
			return 1;
		}
		options_array->type = TYPE_ARRAY;
		options_array->data.array.size = node->option_count;
		options_array->data.array.elements = malloc(node->option_count * sizeof(Variable*));
		if (!options_array->data.array.elements && node->option_count > 0) {
			free(options_array);
			free_hash_table(options_map);
			ProPrintfChar("Error: Memory allocation failed for options elements in '%s'\n", node->parameter);
			return 1;
		}
		for (size_t k = 0; k < node->option_count; k++) {
			char* opt_str;
			status = evaluate_to_string(node->options[k], st, &opt_str);
			if (status != 0 || !opt_str || strlen(opt_str) == 0) {
				free(opt_str);
				// Clean up partial array
				for (size_t j = 0; j < k; j++) {
					free_variable(options_array->data.array.elements[j]);
				}
				free(options_array->data.array.elements);
				free(options_array);
				free_hash_table(options_map);
				ProPrintfChar("Error: Failed to evaluate or empty option %zu for '%s'\n", k, node->parameter);
				return 1;
			}
			Variable* str_var = malloc(sizeof(Variable));
			if (!str_var) {
				free(opt_str);
				// Clean up partial array
				for (size_t j = 0; j < k; j++) {
					free_variable(options_array->data.array.elements[j]);
				}
				free(options_array->data.array.elements);
				free(options_array);
				free_hash_table(options_map);
				ProPrintfChar("Error: Memory allocation failed for option string %zu in '%s'\n", k, node->parameter);
				return 1;
			}
			str_var->type = TYPE_STRING;
			str_var->data.string_value = opt_str;  // Transfer ownership
			options_array->data.array.elements[k] = str_var;
		}
		hash_table_insert(options_map, "options", options_array);  // Transfer ownership
	}
	// Add required (bool)
	if (!add_bool_to_map(options_map, "required", node->required)) {
		free_hash_table(options_map);
		ProPrintfChar("Error: Failed to add required to options map for '%s'\n", node->parameter);
		return 1;
	}
	// If required, add to global required radios list for easy runtime validation
	if (node->required) {
		Variable* req_radios = get_symbol(st, "REQUIRED_RADIOS");
		if (!req_radios) {
			req_radios = malloc(sizeof(Variable));
			if (!req_radios) {
				free_hash_table(options_map);
				ProPrintfChar("Error: Memory allocation failed for REQUIRED_RADIOS list\n");
				return 1;
			}
			req_radios->type = TYPE_ARRAY;
			req_radios->data.array.size = 0;
			req_radios->data.array.elements = NULL;
			set_symbol(st, "REQUIRED_RADIOS", req_radios);
		}
		else if (req_radios->type != TYPE_ARRAY) {
			free_hash_table(options_map);
			ProPrintfChar("Error: REQUIRED_RADIOS in symbol table is not an array\n");
			return 1;
		}
		Variable* param_name_var = malloc(sizeof(Variable));
		if (!param_name_var) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Memory allocation failed for required radio entry\n");
			return 1;
		}
		param_name_var->type = TYPE_STRING;
		param_name_var->data.string_value = _strdup(node->parameter);
		if (!param_name_var->data.string_value) {
			free(param_name_var);
			free_hash_table(options_map);
			ProPrintfChar("Error: String duplication failed for required radio\n");
			return 1;
		}
		size_t new_size = req_radios->data.array.size + 1;
		Variable** new_elements = realloc(req_radios->data.array.elements, new_size * sizeof(Variable*));
		if (!new_elements) {
			free_variable(param_name_var);
			free_hash_table(options_map);
			ProPrintfChar("Error: Reallocation failed for REQUIRED_RADIOS array\n");
			return 1;
		}
		new_elements[new_size - 1] = param_name_var;
		req_radios->data.array.elements = new_elements;
		req_radios->data.array.size = new_size;
		ProPrintfChar("Note: RADIOBUTTON_PARAM '%s' marked as required and added to validation list\n", node->parameter);
	}
	// Evaluate and add display_order if present (int >= 0)
	if (node->display_order) {
		long display_order_val;
		status = evaluate_to_int(node->display_order, st, &display_order_val);
		if (status != 0 || display_order_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) display_order for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_int_to_map(options_map, "display_order", (int)display_order_val)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add display_order to options map for '%s'\n", node->parameter);
			return 1;
		}
	}
	// Evaluate and add tooltip_message if present (string)
	char* tooltip = NULL;
	if (node->tooltip_message) {
		status = evaluate_to_string(node->tooltip_message, st, &tooltip);
		if (status != 0 || !tooltip || strlen(tooltip) == 0) {
			free(tooltip);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or empty tooltip message for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_string_to_map(options_map, "tooltip", tooltip)) {
			free(tooltip);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add tooltip to options map for '%s'\n", node->parameter);
			return 1;
		}
		free(tooltip);  // add_string_to_map duplicates
	}
	// Evaluate and add image_name if present (string, requires tooltip)
	if (node->image_name) {
		if (!node->tooltip_message) {
			free_hash_table(options_map);
			ProPrintfChar("Error: IMAGE requires TOOLTIP in RADIOBUTTON_PARAM for '%s'\n", node->parameter);
			return 1;
		}
		char* image;
		status = evaluate_to_string(node->image_name, st, &image);
		if (status != 0 || !image || strlen(image) == 0) {
			free(image);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or empty image name for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_string_to_map(options_map, "image", image)) {
			free(image);
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add image to options map for '%s'\n", node->parameter);
			return 1;
		}
		free(image);
	}
	// Add on_picture and positions if set
	if (node->on_picture) {
		if (!add_bool_to_map(options_map, "on_picture", true)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add on_picture to options map for '%s'\n", node->parameter);
			return 1;
		}
		long posx_val;
		status = evaluate_to_int(node->posX, st, &posx_val);
		if (status != 0 || posx_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posX for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_int_to_map(options_map, "posX", (int)posx_val)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add posX to options map for '%s'\n", node->parameter);
			return 1;
		}
		long posy_val;
		status = evaluate_to_int(node->posY, st, &posy_val);
		if (status != 0 || posy_val < 0) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to evaluate or invalid (negative) posY for '%s'\n", node->parameter);
			return 1;
		}
		if (!add_int_to_map(options_map, "posY", (int)posy_val)) {
			free_hash_table(options_map);
			ProPrintfChar("Error: Failed to add posY to options map for '%s'\n", node->parameter);
			return 1;
		}
	}
	// Create a dedicated Variable for the options map and store it in the symbol table
	Variable* options_var = malloc(sizeof(Variable));
	if (!options_var) {
		free_hash_table(options_map);
		ProPrintfChar("Memory allocation failed for RADIOBUTTON_PARAM options variable of '%s'\n", node->parameter);
		return 1;
	}
	options_var->type = TYPE_MAP;
	options_var->data.map = options_map;
	// Generate unique key for this RADIOBUTTON_PARAM's options (consistent with USER_INPUT)
	char key[256];
	snprintf(key, sizeof(key), "RADIOBUTTON:%s", node->parameter);
	set_symbol(st, key, options_var);
	ProPrintfChar("Note: Stored RADIOBUTTON_PARAM options map for '%s' under key '%s'\n", node->parameter, key);
	return 0;  // Success
}

/*=================================================*\
*
* USER_SELECT semantic analysis: Validate and store reference with display options
*
*
\*=================================================*/
int check_user_select_semantics(UserSelectNode* node, SymbolTable* st) 
{
	if (!node || !st) return -1;

	// Validate reference identifier
	if (!is_valid_identifier(node->reference))
	{
		ProPrintfChar("Error: Invalid reference identifier '%s' in USER_SELECT\n", node->reference);
		return 1;
	}

	// Check for duplicate
	if (get_symbol(st, node->reference))
	{
		ProPrintfChar("Error: Reference '%s' already declared\n", node->reference);
		return -1;
	}
	
	// Validate type count 
	if (node->type_count == 0)
	{
		ProPrintfChar("Error: No Types specified in USER_SELECT for '%s'\n", node->reference);
		return 1;
	}

	// Create top-level map variable to hold all USER_SELECT options/details
	Variable* user_var = malloc(sizeof(Variable));
	if (!user_var) return -1;
	user_var->type = TYPE_MAP;
	HashTable* map = create_hash_table(32);
	if (!map)
	{
		free(user_var);
		return -1;
	}
	user_var->data.map = map;
	user_var->declaration_count = 1;

	int status = 0;

	// Types (strings) + ALLOWED_TYPES (mapped ints)
	Variable* types_array = malloc(sizeof(Variable));
	if (!types_array) goto cleanup;
	types_array->type = TYPE_ARRAY;
	types_array->data.array.size = node->type_count;
	types_array->data.array.elements = malloc(node->type_count * sizeof(Variable*));
	if (!types_array->data.array.elements)
	{
		free_variable(types_array);
		goto cleanup;
	}

	Variable* allowed_types_array = malloc(sizeof(Variable));
	if (!allowed_types_array)
	{
		free_variable(types_array);
		goto cleanup;
	}
	allowed_types_array->type = TYPE_ARRAY;
	allowed_types_array->data.array.size = node->type_count;
	allowed_types_array->data.array.elements = malloc(node->type_count * sizeof(Variable*));
	if (!allowed_types_array->data.array.elements)
	{
		free_variable(types_array);
		free_variable(allowed_types_array);
		goto cleanup;
	}
	for (size_t k = 0; k < node->type_count; k++) {
		// Each type expr must evaluate to STRING (e.g., "AXIS", "SURFACE")
		VariableType expr_type = get_expression_type(node->types[k], st);
		if (expr_type != TYPE_STRING) {
			ProPrintfChar("Error: Type %zu must evaluate to STRING in USER_SELECT '%s'\n", k, node->reference);
			free_variable(types_array);
			free_variable(allowed_types_array);
			goto cleanup;
		}

		char* type_str = NULL;
		status = evaluate_to_string(node->types[k], st, &type_str);
		if (status != 0 || !type_str) {
			ProPrintfChar("Error: Failed to evaluate type %zu in USER_SELECT '%s'\n", k, node->reference);
			free_variable(types_array);
			free_variable(allowed_types_array);
			goto cleanup;
		}

		// Store string in types_array
		Variable* str_var = malloc(sizeof(Variable));
		if (!str_var) { free(type_str); free_variable(types_array); free_variable(allowed_types_array); goto cleanup; }
		str_var->type = TYPE_STRING;
		str_var->data.string_value = type_str;
		types_array->data.array.elements[k] = str_var;

		CreoReferenceType creo_t = get_creo_ref_type(type_str);  // returns CREO_* or CREO_UNKNOWN
		Variable* map_var = malloc(sizeof(Variable));
		if (!map_var) { free_variable(types_array); free_variable(allowed_types_array); goto cleanup; }
		map_var->type = TYPE_INTEGER;
		map_var->data.int_value = (int)creo_t;
		allowed_types_array->data.array.elements[k] = map_var;
	}

	// Put arrays into the map
	hash_table_insert(map, "types", types_array);
	hash_table_insert(map, "allowed_types", allowed_types_array);

	if (node->display_order) {
		long order_val;
		status = evaluate_to_int(node->display_order, st, &order_val);
		if (status != 0 || order_val < 0) {
			ProPrintfChar("Error: display_order must be a non-negative integer for USER_SELECT '%s'\n", node->reference);
			goto cleanup;
		}
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_INTEGER;
		v->data.int_value = (int)order_val;
		hash_table_insert(map, "display_order", v);
	}

	if (node->allow_reselect) {
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_BOOL;
		v->data.int_value = 1;
		hash_table_insert(map, "allow_reselect", v);
	}
	if (node->select_by_box) {
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_BOOL;
		v->data.int_value = 1;
		hash_table_insert(map, "select_by_box", v);
	}
	if (node->select_by_menu) {
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_BOOL;
		v->data.int_value = 1;
		hash_table_insert(map, "select_by_menu", v);
	}

	if (node->tooltip_message) {
		char* s = NULL;
		status = evaluate_to_string(node->tooltip_message, st, &s);
		if (status != 0 || !s || s[0] == '\0') { free(s); ProPrintfChar("Error: Empty TOOLTIP in USER_SELECT '%s'\n", node->reference); goto cleanup; }
		Variable* v = malloc(sizeof(Variable)); if (!v) { free(s); goto cleanup; }
		v->type = TYPE_STRING; v->data.string_value = s;
		hash_table_insert(map, "tooltip", v);
	}
	if (node->image_name) {
		char* s = NULL;
		status = evaluate_to_string(node->image_name, st, &s);
		if (status != 0 || !s || s[0] == '\0') { free(s); ProPrintfChar("Error: Empty IMAGE in USER_SELECT '%s'\n", node->reference); goto cleanup; }
		Variable* v = malloc(sizeof(Variable)); if (!v) { free(s); goto cleanup; }
		v->type = TYPE_STRING; v->data.string_value = s;
		hash_table_insert(map, "image", v);
	}

	if (node->on_picture) {
		Variable* onv = malloc(sizeof(Variable)); if (!onv) goto cleanup;
		onv->type = TYPE_BOOL; onv->data.int_value = 1;
		hash_table_insert(map, "on_picture", onv);

		long x, y;
		if (evaluate_to_int(node->posX, st, &x) != 0 || x < 0) { ProPrintfChar("Error: Invalid posX in USER_SELECT '%s'\n", node->reference); goto cleanup; }
		if (evaluate_to_int(node->posY, st, &y) != 0 || y < 0) { ProPrintfChar("Error: Invalid posY in USER_SELECT '%s'\n", node->reference); goto cleanup; }

		Variable* vx = malloc(sizeof(Variable)); if (!vx) goto cleanup;
		vx->type = TYPE_INTEGER; vx->data.int_value = (int)x;
		hash_table_insert(map, "posX", vx);

		Variable* vy = malloc(sizeof(Variable)); if (!vy) goto cleanup;
		vy->type = TYPE_INTEGER; vy->data.int_value = (int)y;
		hash_table_insert(map, "posY", vy);
	}
	if (node->tag) {
		char* tag = NULL;
		status = evaluate_to_string(node->tag, st, &tag);
		if (status != 0 || !tag || tag[0] == '\0') {
			free(tag);
			ProPrintfChar("Error: Failed to evaluate or empty tag for USER_SELECT '%s'\n", node->reference);
			goto cleanup;
		}
		Variable* tv = malloc(sizeof(Variable));
		if (!tv) { free(tag); goto cleanup; }
		tv->type = TYPE_STRING;
		tv->data.string_value = tag;
		hash_table_insert(map, "tag", tv);
	}

	if (node->is_required == PRO_B_TRUE) {
		// Keep same storage pattern as before, but key off is_required
		Variable* req_list = get_symbol(st, "REQUIRED_SELECTS");
		if (!req_list) {
			req_list = malloc(sizeof(Variable));
			if (!req_list) goto cleanup;
			req_list->type = TYPE_ARRAY;
			req_list->data.array.size = 0;
			req_list->data.array.elements = NULL;
			set_symbol(st, "REQUIRED_SELECTS", req_list);
		}
		else if (req_list->type != TYPE_ARRAY) {
			ProPrintfChar("Error: REQUIRED_SELECTS in symbol table is not an array\n");
			goto cleanup;
		}

		Variable* ref_var = malloc(sizeof(Variable));
		if (!ref_var) goto cleanup;
		ref_var->type = TYPE_STRING;
		ref_var->data.string_value = _strdup(node->reference);
		if (!ref_var->data.string_value) { free(ref_var); goto cleanup; }

		size_t new_size = req_list->data.array.size + 1;
		Variable** new_elems = realloc(req_list->data.array.elements, new_size * sizeof(Variable*));
		if (!new_elems) { free_variable(ref_var); goto cleanup; }
		new_elems[new_size - 1] = ref_var;
		req_list->data.array.elements = new_elems;
		req_list->data.array.size = new_size;

		ProPrintfChar("Note: USER_SELECT '%s' marked as required\n", node->reference);
	}

	// Finally store the whole USER_SELECT map under the reference name
	set_symbol(st, node->reference, user_var);
	return 0;

cleanup:
	free_variable(user_var);  // Frees map and any children we already inserted
	return -1;

}

int check_user_select_optional_semantics(UserSelectOptionalNode* node, SymbolTable* st)
{
	if (!node || !st) return -1;

	// Validate reference identifier
	if (!is_valid_identifier(node->reference))
	{
		ProPrintfChar("Error: Invalid reference identifier '%s' in USER_SELECT\n", node->reference);
		return 1;
	}

	// Check for duplicate
	if (get_symbol(st, node->reference))
	{
		ProPrintfChar("Error: Reference '%s' already declared\n", node->reference);
		return -1;
	}

	// Validate type count 
	if (node->type_count == 0)
	{
		ProPrintfChar("Error: No Types specified in USER_SELECT for '%s'\n", node->reference);
		return 1;
	}

	// Create top-level map variable to hold all USER_SELECT options/details
	Variable* user_var = malloc(sizeof(Variable));
	if (!user_var) return -1;
	user_var->type = TYPE_MAP;
	HashTable* map = create_hash_table(32);
	if (!map)
	{
		free(user_var);
		return -1;
	}
	user_var->data.map = map;
	user_var->declaration_count = 1;

	int status = 0;

	// Types (strings) + ALLOWED_TYPES (mapped ints)
	Variable* types_array = malloc(sizeof(Variable));
	if (!types_array) goto cleanup;
	types_array->type = TYPE_ARRAY;
	types_array->data.array.size = node->type_count;
	types_array->data.array.elements = malloc(node->type_count * sizeof(Variable*));
	if (!types_array->data.array.elements)
	{
		free_variable(types_array);
		goto cleanup;
	}

	Variable* allowed_types_array = malloc(sizeof(Variable));
	if (!allowed_types_array)
	{
		free_variable(types_array);
		goto cleanup;
	}
	allowed_types_array->type = TYPE_ARRAY;
	allowed_types_array->data.array.size = node->type_count;
	allowed_types_array->data.array.elements = malloc(node->type_count * sizeof(Variable*));
	if (!allowed_types_array->data.array.elements)
	{
		free_variable(types_array);
		free_variable(allowed_types_array);
		goto cleanup;
	}
	for (size_t k = 0; k < node->type_count; k++) {
		// Each type expr must evaluate to STRING (e.g., "AXIS", "SURFACE")
		VariableType expr_type = get_expression_type(node->types[k], st);
		if (expr_type != TYPE_STRING) {
			ProPrintfChar("Error: Type %zu must evaluate to STRING in USER_SELECT '%s'\n", k, node->reference);
			free_variable(types_array);
			free_variable(allowed_types_array);
			goto cleanup;
		}

		char* type_str = NULL;
		status = evaluate_to_string(node->types[k], st, &type_str);
		if (status != 0 || !type_str) {
			ProPrintfChar("Error: Failed to evaluate type %zu in USER_SELECT '%s'\n", k, node->reference);
			free_variable(types_array);
			free_variable(allowed_types_array);
			goto cleanup;
		}

		// Store string in types_array
		Variable* str_var = malloc(sizeof(Variable));
		if (!str_var) { free(type_str); free_variable(types_array); free_variable(allowed_types_array); goto cleanup; }
		str_var->type = TYPE_STRING;
		str_var->data.string_value = type_str;
		types_array->data.array.elements[k] = str_var;

		CreoReferenceType creo_t = get_creo_ref_type(type_str);  // returns CREO_* or CREO_UNKNOWN
		Variable* map_var = malloc(sizeof(Variable));
		if (!map_var) { free_variable(types_array); free_variable(allowed_types_array); goto cleanup; }
		map_var->type = TYPE_INTEGER;
		map_var->data.int_value = (int)creo_t;
		allowed_types_array->data.array.elements[k] = map_var;
	}

	// Put arrays into the map
	hash_table_insert(map, "types", types_array);
	hash_table_insert(map, "allowed_types", allowed_types_array);

	if (node->display_order) {
		long order_val;
		status = evaluate_to_int(node->display_order, st, &order_val);
		if (status != 0 || order_val < 0) {
			ProPrintfChar("Error: display_order must be a non-negative integer for USER_SELECT '%s'\n", node->reference);
			goto cleanup;
		}
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_INTEGER;
		v->data.int_value = (int)order_val;
		hash_table_insert(map, "display_order", v);
	}

	if (node->allow_reselect) {
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_BOOL;
		v->data.int_value = 1;
		hash_table_insert(map, "allow_reselect", v);
	}
	if (node->select_by_box) {
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_BOOL;
		v->data.int_value = 1;
		hash_table_insert(map, "select_by_box", v);
	}
	if (node->select_by_menu) {
		Variable* v = malloc(sizeof(Variable));
		if (!v) goto cleanup;
		v->type = TYPE_BOOL;
		v->data.int_value = 1;
		hash_table_insert(map, "select_by_menu", v);
	}

	if (node->tooltip_message) {
		char* s = NULL;
		status = evaluate_to_string(node->tooltip_message, st, &s);
		if (status != 0 || !s || s[0] == '\0') { free(s); ProPrintfChar("Error: Empty TOOLTIP in USER_SELECT '%s'\n", node->reference); goto cleanup; }
		Variable* v = malloc(sizeof(Variable)); if (!v) { free(s); goto cleanup; }
		v->type = TYPE_STRING; v->data.string_value = s;
		hash_table_insert(map, "tooltip", v);
	}
	if (node->image_name) {
		char* s = NULL;
		status = evaluate_to_string(node->image_name, st, &s);
		if (status != 0 || !s || s[0] == '\0') { free(s); ProPrintfChar("Error: Empty IMAGE in USER_SELECT '%s'\n", node->reference); goto cleanup; }
		Variable* v = malloc(sizeof(Variable)); if (!v) { free(s); goto cleanup; }
		v->type = TYPE_STRING; v->data.string_value = s;
		hash_table_insert(map, "image", v);
	}

	if (node->on_picture) {
		Variable* onv = malloc(sizeof(Variable)); if (!onv) goto cleanup;
		onv->type = TYPE_BOOL; onv->data.int_value = 1;
		hash_table_insert(map, "on_picture", onv);

		long x, y;
		if (evaluate_to_int(node->posX, st, &x) != 0 || x < 0) { ProPrintfChar("Error: Invalid posX in USER_SELECT '%s'\n", node->reference); goto cleanup; }
		if (evaluate_to_int(node->posY, st, &y) != 0 || y < 0) { ProPrintfChar("Error: Invalid posY in USER_SELECT '%s'\n", node->reference); goto cleanup; }

		Variable* vx = malloc(sizeof(Variable)); if (!vx) goto cleanup;
		vx->type = TYPE_INTEGER; vx->data.int_value = (int)x;
		hash_table_insert(map, "posX", vx);

		Variable* vy = malloc(sizeof(Variable)); if (!vy) goto cleanup;
		vy->type = TYPE_INTEGER; vy->data.int_value = (int)y;
		hash_table_insert(map, "posY", vy);
	}
	if (node->tag) {
		char* tag = NULL;
		status = evaluate_to_string(node->tag, st, &tag);
		if (status != 0 || !tag || tag[0] == '\0') {
			free(tag);
			ProPrintfChar("Error: Failed to evaluate or empty tag for USER_SELECT '%s'\n", node->reference);
			goto cleanup;
		}
		Variable* tv = malloc(sizeof(Variable));
		if (!tv) { free(tag); goto cleanup; }
		tv->type = TYPE_STRING;
		tv->data.string_value = tag;
		hash_table_insert(map, "tag", tv);
	}

	if (node->is_required == PRO_B_TRUE) {
		// Keep same storage pattern as before, but key off is_required
		Variable* req_list = get_symbol(st, "REQUIRED_SELECTS");
		if (!req_list) {
			req_list = malloc(sizeof(Variable));
			if (!req_list) goto cleanup;
			req_list->type = TYPE_ARRAY;
			req_list->data.array.size = 0;
			req_list->data.array.elements = NULL;
			set_symbol(st, "REQUIRED_SELECTS", req_list);
		}
		else if (req_list->type != TYPE_ARRAY) {
			ProPrintfChar("Error: REQUIRED_SELECTS in symbol table is not an array\n");
			goto cleanup;
		}

		Variable* ref_var = malloc(sizeof(Variable));
		if (!ref_var) goto cleanup;
		ref_var->type = TYPE_STRING;
		ref_var->data.string_value = _strdup(node->reference);
		if (!ref_var->data.string_value) { free(ref_var); goto cleanup; }

		size_t new_size = req_list->data.array.size + 1;
		Variable** new_elems = realloc(req_list->data.array.elements, new_size * sizeof(Variable*));
		if (!new_elems) { free_variable(ref_var); goto cleanup; }
		new_elems[new_size - 1] = ref_var;
		req_list->data.array.elements = new_elems;
		req_list->data.array.size = new_size;

		ProPrintfChar("Note: USER_SELECT '%s' marked as required\n", node->reference);
	}

	// Finally store the whole USER_SELECT map under the reference name
	set_symbol(st, node->reference, user_var);
	return 0;

cleanup:
	free_variable(user_var);  // Frees map and any children we already inserted
	return -1;

}

int check_user_select_multiple_semantics(UserSelectMultipleNode* node, SymbolTable* st)
{
	if (!node) {
		ProPrintfChar("Error: Invalid USER_SELECT_MULTIPLE node\n");
		return -1;
	}

	/* ---- validate array identifier ---- */
	if (!node->array || !*node->array) {
		ProPrintfChar("Error: USER_SELECT_MULTIPLE requires a target array name\n");
		return -1;
	}
	/* same identifier rules used elsewhere */
	/* local helper exists in this file: is_valid_identifier */
	if (!is_valid_identifier(node->array)) {  /* uses [a-zA-Z_][a-zA-Z0-9_]* */
		ProPrintfChar("Error: Invalid array identifier '%s'\n", node->array);
		return -1;
	}
	/* Disallow clobbering an existing symbol of a different shape */
	if (get_symbol(st, node->array)) {
		ProPrintfChar("Error: Symbol '%s' already exists\n", node->array);
		return -1;
	}

	/* ---- validate type list ---- */
	if (node->type_count == 0) {
		ProPrintfChar("Error: USER_SELECT_MULTIPLE must specify at least one type\n");
		return -1;
	}

	/* Build strings[] and allowed_types[] */
	char** type_names = (char**)calloc(node->type_count, sizeof(char*));
	if (!type_names) return -1;

	Variable* allowed_arr = (Variable*)malloc(sizeof(Variable));
	if (!allowed_arr) { free(type_names); return -1; }
	allowed_arr->type = TYPE_ARRAY;
	allowed_arr->data.array.size = 0;
	allowed_arr->data.array.elements = (Variable**)calloc(node->type_count, sizeof(Variable*));
	if (!allowed_arr->data.array.elements) { free(type_names); free(allowed_arr); return -1; }

	for (size_t i = 0; i < node->type_count; ++i) {
		VariableType t = get_expression_type(node->types[i], st);
		if (t != TYPE_STRING) {
			ProPrintfChar("Error: USER_SELECT_MULTIPLE type %zu must be string\n", i);
			/* cleanup */
			for (size_t k = 0; k < i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			return -1;
		}
		char* tname = NULL;
		if (evaluate_to_string(node->types[i], st, &tname) != 0 || !tname || !*tname) {
			ProPrintfChar("Error: Failed to evaluate type %zu\n", i);
			for (size_t k = 0; k < i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			free(tname);
			return -1;
		}
		type_names[i] = tname;

		/* Map to Creo enum (AXIS, CURVE, EDGE, SURFACE, PLANE...) */
		CreoReferenceType rt = get_creo_ref_type(tname);  /* maps strings to PRO_* codes */
		if (rt == CREO_UNKNOWN) {
			ProPrintfChar("Error: Unsupported reference type '%s'\n", tname);
			for (size_t k = 0; k <= i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			return -1;
		}
		Variable* iv = (Variable*)malloc(sizeof(Variable));
		if (!iv) {
			for (size_t k = 0; k <= i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			return -1;
		}
		iv->type = TYPE_INTEGER;
		iv->data.int_value = (int)rt;
		allowed_arr->data.array.elements[allowed_arr->data.array.size++] = iv;
	}

	/* ---- validate max_sel ---- */
	long max_sel_val = -1;
	if (!node->max_sel) {
		ProPrintfChar("Error: USER_SELECT_MULTIPLE requires max_sel\n");
		for (size_t k = 0; k < node->type_count; ++k) free(type_names[k]);
		free(type_names);
		for (size_t k = 0; k < allowed_arr->data.array.size; ++k) free(allowed_arr->data.array.elements[k]);
		free(allowed_arr->data.array.elements);
		free(allowed_arr);
		return -1;
	}
	if (evaluate_to_int(node->max_sel, st, &max_sel_val) != 0) {
		ProPrintfChar("Error: max_sel must be an integer (negative => unlimited)\n");
		for (size_t k = 0; k < node->type_count; ++k) free(type_names[k]);
		free(type_names);
		for (size_t k = 0; k < allowed_arr->data.array.size; ++k) free(allowed_arr->data.array.elements[k]);
		free(allowed_arr->data.array.elements);
		free(allowed_arr);
		return -1;
	}

	/* ---- build config map under the array name ---- */
	Variable* cfg = (Variable*)malloc(sizeof(Variable));
	if (!cfg) {
		for (size_t k = 0; k < node->type_count; ++k) free(type_names[k]);
		free(type_names);
		for (size_t k = 0; k < allowed_arr->data.array.size; ++k) free(allowed_arr->data.array.elements[k]);
		free(allowed_arr->data.array.elements);
		free(allowed_arr);
		return -1;
	}
	cfg->type = TYPE_MAP;
	cfg->data.map = create_hash_table(32);

	/* types[] and allowed_types[] */
	(void)add_string_array_to_map(cfg->data.map, "types", type_names, node->type_count);
	/* type_names memory becomes owned by the map helpers; if your helper copies, free here instead */
	hash_table_insert(cfg->data.map, "allowed_types", allowed_arr);

	/* max_sel */
	(void)add_int_to_map(cfg->data.map, "max_sel", (int)max_sel_val);

	/* display_order (optional) */
	if (node->display_order) {
		long ord = 0;
		if (evaluate_to_int(node->display_order, st, &ord) == 0) {
			(void)add_int_to_map(cfg->data.map, "display_order", (int)ord);
		}
		else {
			ProPrintfChar("Error: DISPLAY_ORDER must be numeric\n");
		}
	}

	/* flags */
	(void)add_bool_to_map(cfg->data.map, "allow_reselect", node->allow_reselect ? true : false);
	(void)add_bool_to_map(cfg->data.map, "select_by_box", node->select_by_box ? true : false);
	(void)add_bool_to_map(cfg->data.map, "select_by_menu", node->select_by_menu ? true : false);

	/* include_multi_cad (TRUE/FALSE identifier or string) -> bool */
	if (node->include_multi_cad) {
		char* inc = NULL;
		if (evaluate_to_string(node->include_multi_cad, st, &inc) == 0 && inc) {
			int v = (_stricmp(inc, "TRUE") == 0) ? 1 : 0;
			(void)add_bool_to_map(cfg->data.map, "include_multi_cad", v ? true : false);
			free(inc);
		}
		else {
			ProPrintfChar("Error: INCLUDE_MULTI_CAD must be TRUE or FALSE\n");
		}
	}

	/* filters: keep as stringified expressions so runtime can resolve variables/arrays */
	if (node->filter_mdl) { char* s = expression_to_string(node->filter_mdl);  if (s) (void)add_string_to_map(cfg->data.map, "filter_mdl", s); }
	if (node->filter_feat) { char* s = expression_to_string(node->filter_feat); if (s) (void)add_string_to_map(cfg->data.map, "filter_feat", s); }
	if (node->filter_geom) { char* s = expression_to_string(node->filter_geom); if (s) (void)add_string_to_map(cfg->data.map, "filter_geom", s); }
	if (node->filter_ref) { char* s = expression_to_string(node->filter_ref);  if (s) (void)add_string_to_map(cfg->data.map, "filter_ref", s); }
	if (node->filter_identifier) {
		char* s = NULL;
		if (evaluate_to_string(node->filter_identifier, st, &s) == 0 && s) {
			(void)add_string_to_map(cfg->data.map, "filter_identifier", s);
		}
		else {
			ProPrintfChar("Error: FILTER_IDENTIFIER must be a string\n");
		}
	}

	/* tooltip/image */
	if (node->tooltip_message) {
		char* msg = NULL;
		if (evaluate_to_string(node->tooltip_message, st, &msg) == 0 && msg) {
			(void)add_string_to_map(cfg->data.map, "tooltip", msg);
		}
		else {
			ProPrintfChar("Error: TOOLTIP message must be a string\n");
		}
	}
	if (node->image_name) {
		char* img = NULL;
		if (evaluate_to_string(node->image_name, st, &img) == 0 && img) {
			(void)add_string_to_map(cfg->data.map, "image_name", img);
		}
		else {
			ProPrintfChar("Error: IMAGE name must be a string\n");
		}
	}

	/* ON_PICTURE */
	(void)add_bool_to_map(cfg->data.map, "on_picture", node->on_picture ? true : false);
	if (node->on_picture) {
		long x = 0, y = 0;
		if (node->posX && evaluate_to_int(node->posX, st, &x) == 0) {
			(void)add_int_to_map(cfg->data.map, "posX", (int)x);
		}
		else if (node->posX) {
			ProPrintfChar("Error: ON_PICTURE posX must be integer\n");
		}
		if (node->posY && evaluate_to_int(node->posY, st, &y) == 0) {
			(void)add_int_to_map(cfg->data.map, "posY", (int)y);
		}
		else if (node->posY) {
			ProPrintfChar("Error: ON_PICTURE posY must be integer\n");
		}
	}

	/* tag (optional) */
	if (node->tag) {
		char* tag = NULL;
		if (evaluate_to_string(node->tag, st, &tag) == 0 && tag) {
			(void)add_string_to_map(cfg->data.map, "tag", tag);
		}
		else {
			ProPrintfChar("Error: 'tag' must be a string\n");
		}
	}

	/* finally, publish the config under the array name */
	set_symbol(st, node->array, cfg);

	ProPrintfChar("Note: USER_SELECT_MULTIPLE '%s' registered with %zu types, max_sel=%ld\n",
		node->array, node->type_count, max_sel_val);
	return 0;
}

int check_user_select_multiple_optional_semantics(UserSelectMultipleOptionalNode* node, SymbolTable* st)
{
	if (!node) {
		ProPrintfChar("Error: Invalid USER_SELECT_MULTIPLE node\n");
		return -1;
	}

	/* ---- validate array identifier ---- */
	if (!node->array || !*node->array) {
		ProPrintfChar("Error: USER_SELECT_MULTIPLE requires a target array name\n");
		return -1;
	}
	/* same identifier rules used elsewhere */
	/* local helper exists in this file: is_valid_identifier */
	if (!is_valid_identifier(node->array)) {  /* uses [a-zA-Z_][a-zA-Z0-9_]* */
		ProPrintfChar("Error: Invalid array identifier '%s'\n", node->array);
		return -1;
	}
	/* Disallow clobbering an existing symbol of a different shape */
	if (get_symbol(st, node->array)) {
		ProPrintfChar("Error: Symbol '%s' already exists\n", node->array);
		return -1;
	}

	/* ---- validate type list ---- */
	if (node->type_count == 0) {
		ProPrintfChar("Error: USER_SELECT_MULTIPLE must specify at least one type\n");
		return -1;
	}

	/* Build strings[] and allowed_types[] */
	char** type_names = (char**)calloc(node->type_count, sizeof(char*));
	if (!type_names) return -1;

	Variable* allowed_arr = (Variable*)malloc(sizeof(Variable));
	if (!allowed_arr) { free(type_names); return -1; }
	allowed_arr->type = TYPE_ARRAY;
	allowed_arr->data.array.size = 0;
	allowed_arr->data.array.elements = (Variable**)calloc(node->type_count, sizeof(Variable*));
	if (!allowed_arr->data.array.elements) { free(type_names); free(allowed_arr); return -1; }

	for (size_t i = 0; i < node->type_count; ++i) {
		VariableType t = get_expression_type(node->types[i], st);
		if (t != TYPE_STRING) {
			ProPrintfChar("Error: USER_SELECT_MULTIPLE type %zu must be string\n", i);
			/* cleanup */
			for (size_t k = 0; k < i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			return -1;
		}
		char* tname = NULL;
		if (evaluate_to_string(node->types[i], st, &tname) != 0 || !tname || !*tname) {
			ProPrintfChar("Error: Failed to evaluate type %zu\n", i);
			for (size_t k = 0; k < i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			free(tname);
			return -1;
		}
		type_names[i] = tname;

		/* Map to Creo enum (AXIS, CURVE, EDGE, SURFACE, PLANE...) */
		CreoReferenceType rt = get_creo_ref_type(tname);  /* maps strings to PRO_* codes */
		if (rt == CREO_UNKNOWN) {
			ProPrintfChar("Error: Unsupported reference type '%s'\n", tname);
			for (size_t k = 0; k <= i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			return -1;
		}
		Variable* iv = (Variable*)malloc(sizeof(Variable));
		if (!iv) {
			for (size_t k = 0; k <= i; ++k) { free(type_names[k]); }
			free(type_names);
			free(allowed_arr->data.array.elements);
			free(allowed_arr);
			return -1;
		}
		iv->type = TYPE_INTEGER;
		iv->data.int_value = (int)rt;
		allowed_arr->data.array.elements[allowed_arr->data.array.size++] = iv;
	}

	/* ---- validate max_sel ---- */
	long max_sel_val = -1;
	if (!node->max_sel) {
		ProPrintfChar("Error: USER_SELECT_MULTIPLE requires max_sel\n");
		for (size_t k = 0; k < node->type_count; ++k) free(type_names[k]);
		free(type_names);
		for (size_t k = 0; k < allowed_arr->data.array.size; ++k) free(allowed_arr->data.array.elements[k]);
		free(allowed_arr->data.array.elements);
		free(allowed_arr);
		return -1;
	}
	if (evaluate_to_int(node->max_sel, st, &max_sel_val) != 0) {
		ProPrintfChar("Error: max_sel must be an integer (negative => unlimited)\n");
		for (size_t k = 0; k < node->type_count; ++k) free(type_names[k]);
		free(type_names);
		for (size_t k = 0; k < allowed_arr->data.array.size; ++k) free(allowed_arr->data.array.elements[k]);
		free(allowed_arr->data.array.elements);
		free(allowed_arr);
		return -1;
	}

	/* ---- build config map under the array name ---- */
	Variable* cfg = (Variable*)malloc(sizeof(Variable));
	if (!cfg) {
		for (size_t k = 0; k < node->type_count; ++k) free(type_names[k]);
		free(type_names);
		for (size_t k = 0; k < allowed_arr->data.array.size; ++k) free(allowed_arr->data.array.elements[k]);
		free(allowed_arr->data.array.elements);
		free(allowed_arr);
		return -1;
	}
	cfg->type = TYPE_MAP;
	cfg->data.map = create_hash_table(32);

	/* types[] and allowed_types[] */
	(void)add_string_array_to_map(cfg->data.map, "types", type_names, node->type_count);
	/* type_names memory becomes owned by the map helpers; if your helper copies, free here instead */
	hash_table_insert(cfg->data.map, "allowed_types", allowed_arr);

	/* max_sel */
	(void)add_int_to_map(cfg->data.map, "max_sel", (int)max_sel_val);

	/* display_order (optional) */
	if (node->display_order) {
		long ord = 0;
		if (evaluate_to_int(node->display_order, st, &ord) == 0) {
			(void)add_int_to_map(cfg->data.map, "display_order", (int)ord);
		}
		else {
			ProPrintfChar("Error: DISPLAY_ORDER must be numeric\n");
		}
	}

	/* flags */
	(void)add_bool_to_map(cfg->data.map, "allow_reselect", node->allow_reselect ? true : false);
	(void)add_bool_to_map(cfg->data.map, "select_by_box", node->select_by_box ? true : false);
	(void)add_bool_to_map(cfg->data.map, "select_by_menu", node->select_by_menu ? true : false);

	/* include_multi_cad (TRUE/FALSE identifier or string) -> bool */
	if (node->include_multi_cad) {
		char* inc = NULL;
		if (evaluate_to_string(node->include_multi_cad, st, &inc) == 0 && inc) {
			int v = (_stricmp(inc, "TRUE") == 0) ? 1 : 0;
			(void)add_bool_to_map(cfg->data.map, "include_multi_cad", v ? true : false);
			free(inc);
		}
		else {
			ProPrintfChar("Error: INCLUDE_MULTI_CAD must be TRUE or FALSE\n");
		}
	}

	/* filters: keep as stringified expressions so runtime can resolve variables/arrays */
	if (node->filter_mdl) { char* s = expression_to_string(node->filter_mdl);  if (s) (void)add_string_to_map(cfg->data.map, "filter_mdl", s); }
	if (node->filter_feat) { char* s = expression_to_string(node->filter_feat); if (s) (void)add_string_to_map(cfg->data.map, "filter_feat", s); }
	if (node->filter_geom) { char* s = expression_to_string(node->filter_geom); if (s) (void)add_string_to_map(cfg->data.map, "filter_geom", s); }
	if (node->filter_ref) { char* s = expression_to_string(node->filter_ref);  if (s) (void)add_string_to_map(cfg->data.map, "filter_ref", s); }
	if (node->filter_identifier) {
		char* s = NULL;
		if (evaluate_to_string(node->filter_identifier, st, &s) == 0 && s) {
			(void)add_string_to_map(cfg->data.map, "filter_identifier", s);
		}
		else {
			ProPrintfChar("Error: FILTER_IDENTIFIER must be a string\n");
		}
	}

	/* tooltip/image */
	if (node->tooltip_message) {
		char* msg = NULL;
		if (evaluate_to_string(node->tooltip_message, st, &msg) == 0 && msg) {
			(void)add_string_to_map(cfg->data.map, "tooltip", msg);
		}
		else {
			ProPrintfChar("Error: TOOLTIP message must be a string\n");
		}
	}
	if (node->image_name) {
		char* img = NULL;
		if (evaluate_to_string(node->image_name, st, &img) == 0 && img) {
			(void)add_string_to_map(cfg->data.map, "image_name", img);
		}
		else {
			ProPrintfChar("Error: IMAGE name must be a string\n");
		}
	}

	/* ON_PICTURE */
	(void)add_bool_to_map(cfg->data.map, "on_picture", node->on_picture ? true : false);
	if (node->on_picture) {
		long x = 0, y = 0;
		if (node->posX && evaluate_to_int(node->posX, st, &x) == 0) {
			(void)add_int_to_map(cfg->data.map, "posX", (int)x);
		}
		else if (node->posX) {
			ProPrintfChar("Error: ON_PICTURE posX must be integer\n");
		}
		if (node->posY && evaluate_to_int(node->posY, st, &y) == 0) {
			(void)add_int_to_map(cfg->data.map, "posY", (int)y);
		}
		else if (node->posY) {
			ProPrintfChar("Error: ON_PICTURE posY must be integer\n");
		}
	}

	/* tag (optional) */
	if (node->tag) {
		char* tag = NULL;
		if (evaluate_to_string(node->tag, st, &tag) == 0 && tag) {
			(void)add_string_to_map(cfg->data.map, "tag", tag);
		}
		else {
			ProPrintfChar("Error: 'tag' must be a string\n");
		}
	}

	/* finally, publish the config under the array name */
	set_symbol(st, node->array, cfg);

	ProPrintfChar("Note: USER_SELECT_MULTIPLE '%s' registered with %zu types, max_sel=%ld\n",
		node->array, node->type_count, max_sel_val);
	return 0;
}

/*=================================================*\
* 
* INVALIDATE_PARAM semantic analysis
* 
* 
\*=================================================*/
int check_invalidate_param_semantics(InvalidateParamNode* node, SymbolTable* st) {
	if (!node || !node->parameter || strlen(node->parameter) == 0) {
		ProPrintfChar("Error: Invalid or missing parameter in INVALIDATE_PARAM\n");
		return -1;
	}

	if (!is_valid_identifier(node->parameter)) {
		ProPrintfChar("Error: Invalid parameter name '%s' in INVALIDATE_PARAM\n", node->parameter);
		return -1;
	}

	Variable* var = get_symbol(st, node->parameter);
	if (!var) {
		ProPrintfChar("Warning: Parameter '%s' not declared; INVALIDATE_PARAM has no effect but is valid\n", node->parameter);
		// Proceed to store even for undeclared, as runtime may handle dynamically
	}
	else {
		// Restrict to basic parameter types
		if (var->type != TYPE_INTEGER && var->type != TYPE_DOUBLE && var->type != TYPE_STRING && var->type != TYPE_BOOL) {
			ProPrintfChar("Error: INVALIDATE_PARAM can only invalidate parameter types (int, double, string, bool) for '%s'\n", node->parameter);
			return -1;
		}
	}

	// Store the invalidated parameter in symbol table for tracking (array of strings)
	Variable* inv_list = get_symbol(st, "INVALIDATED_PARAMS");
	if (!inv_list) {
		inv_list = malloc(sizeof(Variable));
		if (!inv_list) {
			ProPrintfChar("Error: Memory allocation failed for INVALIDATED_PARAMS list\n");
			return -1;
		}
		inv_list->type = TYPE_ARRAY;
		inv_list->data.array.size = 0;
		inv_list->data.array.elements = NULL;
		set_symbol(st, "INVALIDATED_PARAMS", inv_list);
	}
	else if (inv_list->type != TYPE_ARRAY) {
		ProPrintfChar("Error: INVALIDATED_PARAMS in symbol table is not an array\n");
		return -1;
	}

	Variable* param_var = malloc(sizeof(Variable));
	if (!param_var) {
		ProPrintfChar("Error: Memory allocation failed for invalidated parameter entry\n");
		return -1;
	}
	param_var->type = TYPE_STRING;
	param_var->data.string_value = _strdup(node->parameter);
	if (!param_var->data.string_value) {
		free(param_var);
		ProPrintfChar("Error: String duplication failed for invalidated parameter\n");
		return -1;
	}

	size_t new_size = inv_list->data.array.size + 1;
	Variable** new_elements = realloc(inv_list->data.array.elements, new_size * sizeof(Variable*));
	if (!new_elements) {
		free_variable(param_var);
		ProPrintfChar("Error: Reallocation failed for INVALIDATED_PARAMS array\n");
		return -1;
	}
	new_elements[new_size - 1] = param_var;
	inv_list->data.array.elements = new_elements;
	inv_list->data.array.size = new_size;

	// Success: Log without modifying symbol table entry for the variable itself
	LogOnlyPrintfChar("INVALIDATE_PARAM: Parameter '%s' validated (type: %d); removal deferred to runtime\n", node->parameter, var ? var->type : -1);
	return 0;
}

/*=================================================*\
* 
* // Semantic analysis for IF: Validate conditions and recurse on branches
* 
* 
\*=================================================*/
int check_if_semantics(IfNode* node, SymbolTable* st) {
	if (!node) {
		ProPrintfChar("Error: Invalid IF node\n");
		return -1;
	}

	// Validate each branch condition (must be boolean or coercible)
	for (size_t b = 0; b < node->branch_count; b++) {
		IfBranch* branch = node->branches[b];
		VariableType cond_type = get_expression_type(branch->condition, st);
		if (cond_type == -1 ||
			(cond_type != TYPE_BOOL && cond_type != TYPE_INTEGER && cond_type != TYPE_DOUBLE)) {
			ProPrintfChar("Error: IF/ELSE_IF condition must be boolean or coercible (int/double)\n");
			return -1;
		}

		// Recurse into branch commands
		for (size_t c = 0; c < branch->command_count; c++) {
			if (analyze_command(branch->commands[c], st) != 0) {
				return -1;
			}
		}
	}

	// Recurse into ELSE commands
	for (size_t c = 0; c < node->else_command_count; c++) {
		if (analyze_command(node->else_commands[c], st) != 0) {
			return -1;
		}
	}

	// Optional: Log success for debugging
	ProPrintfChar("Note: IF validated; % zu branches analyzed\n", node->branch_count);
	return 0;
}

/*=================================================*\
* 
* // New function in semantic_analysis.c (revised with debug messages)
* 
* 
\*=================================================*/
int check_assignment_semantics(AssignmentNode* node, SymbolTable* st) {
	if (!node || !node->lhs || !node->rhs) {
		ProPrintfChar("Error: Invalid assignment node\n");
		return -1;
	}

	// Debug: Log the assignment being checked (basic info; extend for full stringification if needed)
	ProPrintfChar("Debug: Checking assignment - LHS type: %d, RHS type: %d\n",
		node->lhs->type, node->rhs->type);

	// Validate LHS is an lvalue
	ExpressionType lhs_expr_type = node->lhs->type;
	if (lhs_expr_type != EXPR_VARIABLE_REF && lhs_expr_type != EXPR_ARRAY_INDEX &&
		lhs_expr_type != EXPR_MAP_LOOKUP && lhs_expr_type != EXPR_STRUCT_ACCESS) {
		ProPrintfChar("Error: LHS of assignment must be a variable, array index, map lookup, or struct member\n");
		return -1;
	}

	// Get LHS type (infers and checks declaration/validity)
	VariableType lhs_type = get_expression_type(node->lhs, st);
	if (lhs_type == -1) {
		ProPrintfChar("Error: Invalid or undeclared LHS in assignment\n");
		return -1;
	}

	// Debug: If LHS is a variable ref, check and print symbol table details
	if (node->lhs->type == EXPR_VARIABLE_REF) {
		const char* lhs_var_name = node->lhs->data.string_val;
		Variable* lhs_var = get_symbol(st, lhs_var_name);
		if (lhs_var) {
			ProPrintfChar("Debug: LHS variable '%s' found in symbol table. Type: %d\n", lhs_var_name, lhs_var->type);
			// Print value based on type (extend for more types as needed)
			switch (lhs_var->type) {
			case TYPE_INTEGER:
			case TYPE_BOOL:
				ProPrintfChar("Debug: LHS value: %d\n", lhs_var->data.int_value);
				break;
			case TYPE_DOUBLE:
				ProPrintfChar("Debug: LHS value: %.2f\n", lhs_var->data.double_value);
				break;
			case TYPE_STRING:
				ProPrintfChar("Debug: LHS value: %s\n", lhs_var->data.string_value ? lhs_var->data.string_value : "(null)");
				break;
			default:
				ProPrintfChar("Debug: LHS value: (complex type, not printed)\n");
				break;
			}
		}
		else {
			ProPrintfChar("Debug: LHS variable '%s' NOT found in symbol table\n", lhs_var_name);
		}
	}

	// Get RHS type
	VariableType rhs_type = get_expression_type(node->rhs, st);
	if (rhs_type == -1) {
		ProPrintfChar("Error: Invalid RHS expression in assignment\n");
		return -1;
	}

	// Debug: If RHS is a variable ref, check and print symbol table details
	if (node->rhs->type == EXPR_VARIABLE_REF) {
		const char* rhs_var_name = node->rhs->data.string_val;
		Variable* rhs_var = get_symbol(st, rhs_var_name);
		if (rhs_var) {
			ProPrintfChar("Debug: RHS variable '%s' found in symbol table. Type: %d\n", rhs_var_name, rhs_var->type);
			// Print value based on type
			switch (rhs_var->type) {
			case TYPE_INTEGER:
			case TYPE_BOOL:
				ProPrintfChar("Debug: RHS value: %d\n", rhs_var->data.int_value);
				break;
			case TYPE_DOUBLE:
				ProPrintfChar("Debug: RHS value: %.2f\n", rhs_var->data.double_value);
				break;
			case TYPE_STRING:
				ProPrintfChar("Debug: RHS value: %s\n", rhs_var->data.string_value ? rhs_var->data.string_value : "(null)");
				break;
			default:
				ProPrintfChar("Debug: RHS value: (complex type, not printed)\n");
				break;
			}
		}
		else {
			ProPrintfChar("Debug: RHS variable '%s' NOT found in symbol table\n", rhs_var_name);
		}
	}

	// Check type compatibility (with coercion)
	if (lhs_type != rhs_type) {
		if (lhs_type == TYPE_DOUBLE && rhs_type == TYPE_INTEGER) {
			// Allow int -> double
		}
		else if (lhs_type == TYPE_INTEGER && rhs_type == TYPE_DOUBLE) {
			ProPrintfChar("Warning: Potential precision loss assigning double to int\n");
		}
		else {
			ProPrintfChar("Error: Type mismatch in assignment: LHS type %d, RHS type %d\n", lhs_type, rhs_type);
			return -1;
		}
	}

	// Optional: If LHS is variable ref, confirm it's not const (if you add const support later)

	LogOnlyPrintfChar("Note: Assignment validated (LHS type %d, RHS type %d)\n", lhs_type, rhs_type);
	return 0;  // Success
}

// Recursive helper to analyze a single CommandNode (handles nesting)
static int analyze_command(CommandNode* cmd, SymbolTable* st) {
	if (!cmd) return 0;  // Skip null nodes

	int result = 0;
	switch (cmd->type) {
	case COMMAND_CONFIG_ELEM:
		result = check_config_elem_semantics((ConfigElemNode*)cmd->data, st);  // Pass st for storage
		break;
	case COMMAND_GLOBAL_PICTURE:
		result = check_global_picture_semantics((GlobalPictureNode*)cmd->data, st);
		break;
	case COMMAND_SUB_PICTURE:
		result = check_sub_picture_semantic((SubPictureNode*)cmd->data, st);
		break;
	case COMMAND_CHECKBOX_PARAM:
		result = check_checkbox_param_semantics((CheckboxParamNode*)cmd->data, st);
		break;  // Add this break to prevent fall-through
	case COMMAND_IF: {
		result = check_if_semantics(&((CommandData*)cmd->data)->ifcommand, st);
		break;
	}
	case COMMAND_DECLARE_VARIABLE:
		result = check_declare_variable_semantics((DeclareVariableNode*)cmd->data, st);
		break;
	case COMMAND_INVALIDATE_PARAM:
		result = check_invalidate_param_semantics((InvalidateParamNode*)cmd->data, st);
		break;
	case COMMAND_SHOW_PARAM:
		result = check_show_param_semantics((ShowParamNode*)cmd->data, st);
		break;
	case COMMAND_USER_INPUT_PARAM:
		result = check_user_input_param_semantics((UserInputParamNode*)cmd->data, st);
		break;
	case COMMAND_RADIOBUTTON_PARAM:
		result = check_radiobutton_param_semantics((RadioButtonParamNode*)cmd->data, st);
		break;
	case COMMAND_USER_SELECT:
		result = check_user_select_semantics((UserSelectNode*)cmd->data, st);
		break;
	case COMMAND_USER_SELECT_OPTIONAL:
		result = check_user_select_optional_semantics((UserSelectOptionalNode*)cmd->data, st);
		break;
	case COMMAND_USER_SELECT_MULTIPLE:
		result = check_user_select_multiple_semantics((UserSelectMultipleNode*)cmd->data, st);
		break;
	case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL:
		result = check_user_select_multiple_optional_semantics((UserSelectMultipleOptionalNode*)cmd->data, st);
		break;
	case COMMAND_ASSIGNMENT:
		result = check_assignment_semantics(&((CommandData*)cmd->data)->assignment, st);
		break;
		// Optional: For COMMAND_EXPRESSION (bare expressions, e.g., for side effects like function calls)
	case COMMAND_EXPRESSION: {
		VariableType expr_type = get_expression_type(((CommandData*)cmd->data)->expression, st);
		if (expr_type == -1) {
			ProPrintfChar("Error: Invalid bare expression\n");
			result = -1;
		}
		else {
			result = 0;  // Just type-check; no assignment
		}
		break;
	}
	default: {
		const char* cmd_name = (cmd->type >= 0 && cmd->type < sizeof(command_names) / sizeof(command_names[0]) && command_names[cmd->type])
			? command_names[cmd->type] : "Unknown";
		ProPrintfChar("Warning: No semantic analysis available for command '%s' (type %d); skipping analysis\n", cmd_name, cmd->type);
		result = 0;  // Treat as success to continue without error propagation
	} break;
	}
	if (result != 0) {
		cmd->semantic_valid = false;  // Mark as invalid if analysis failed
	}

	return result;
}

// Minor update to perform_semantic_analysis for better cleanup
int perform_semantic_analysis(BlockList* block_list, SymbolTable* st) {
	if (!block_list) {
		ProPrintfChar("Error: No block list provided for semantic analysis\n");
		return -1;
	}

	// Define ordered block types for processing: ASM first, then GUI, then TAB
	BlockType order[] = { BLOCK_ASM, BLOCK_GUI, BLOCK_TAB };
	size_t order_count = sizeof(order) / sizeof(order[0]);

	for (size_t ord = 0; ord < order_count; ord++) {
		Block* block = find_block(block_list, order[ord]);
		if (!block) continue;  // Skip if block type not present

		for (size_t j = 0; j < block->command_count; j++) {
			int cmd_result = analyze_command(block->commands[j], st);
			if (cmd_result != 0) {
				ProPrintfChar("Semantic error in block type %d, command %zu\n", order[ord], j);
			}
		}
	}
	print_symbol_table(st);
	return 0;  // Always return success to proceed; invalid commands are flagged
}

