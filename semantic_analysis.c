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
	[COMMAND_BEGIN_TABLE] = "BEGIN_TABLE",
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
int is_valid_identifier(const char* id) {
	if (!id || !id[0] || (!isalpha(id[0]) && id[0] != '_')) return 0;
	for (const char* p = id + 1; *p; ++p) {
		if (!isalnum(*p) && *p != '_') return 0;
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
	if (!result) return -1;
	*result = NULL;

	if (!expr) {
		/* Empty expression -> treat as empty string per spec */
		return 0;
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
		else if (var && var->type == TYPE_SUBTABLE) {
			/* Allow stringification of subtable refs to their target id */
			*result = _strdup(var->data.string_value);
			return *result ? 0 : -1;
		}
		else if (var) {
			/* allow numeric/bool to be stringified */
			char buf[64];
			switch (var->type) {
			case TYPE_INTEGER:
				snprintf(buf, sizeof(buf), "%ld", (long)var->data.int_value);
				*result = _strdup(buf);
				return *result ? 0 : -1;
			case TYPE_DOUBLE:
				snprintf(buf, sizeof(buf), "%.15g", var->data.double_value);
				*result = _strdup(buf);
				return *result ? 0 : -1;
			case TYPE_BOOL:
				*result = _strdup(var->data.int_value ? "1" : "0");
				return *result ? 0 : -1;
			case TYPE_NULL:
				*result = NULL;
				return 0;
			case TYPE_ARRAY:
				/* Treat references to tables (TYPE_ARRAY) as literal identifiers */
				*result = _strdup(expr->data.string_val);
				return *result ? 0 : -1;
			default:
				return -1;
			}
		}
		else {
			/* Undeclared identifier literal string (unquoted filenames, etc.) */
			*result = _strdup(expr->data.string_val);
			return *result ? 0 : -1;
		}
	}

	case EXPR_BINARY_OP: {
		if (expr->data.binary.op != BINOP_ADD) {
			return -1; /* Only + is concatenation */
		}
		char* left_str = NULL;
		if (evaluate_to_string(expr->data.binary.left, st, &left_str) != 0) return -1;

		char* right_str = NULL;
		if (evaluate_to_string(expr->data.binary.right, st, &right_str) != 0) {
			free(left_str);
			return -1;
		}

		size_t len = (left_str ? strlen(left_str) : 0)
			+ (right_str ? strlen(right_str) : 0) + 1;
		*result = (char*)malloc(len);
		if (!*result) {
			free(left_str);
			free(right_str);
			return -1;
		}
		snprintf(*result, len, "%s%s",
			left_str ? left_str : "",
			right_str ? right_str : "");
		free(left_str);
		free(right_str);
		return 0;
	}

	default: {
		long iv;
		if (evaluate_to_int(expr, st, &iv) == 0) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%ld", iv);
			*result = _strdup(buf);
			return *result ? 0 : -1;
		}
		double dv;
		if (evaluate_to_double(expr, st, &dv) == 0) {
			char buf[64];
			snprintf(buf, sizeof(buf), "%.15g", dv);
			*result = _strdup(buf);
			return *result ? 0 : -1;
		}
		*result = NULL;
		return -1;
	}
	}
}

void coerce_empty_string_to_zero(Variable* v) {
	if (v->type == TYPE_STRING && (v->data.string_value == NULL || strlen(v->data.string_value) == 0)) {
		free(v->data.string_value);
		v->type = TYPE_INTEGER;
		v->data.int_value = 0;
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
				if (left_type == TYPE_INTEGER || left_type == TYPE_DOUBLE || left_type == TYPE_STRING) {
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
		if (!src || src->type == TYPE_UNKNOWN) {
			(*result)->type = TYPE_STRING;
			(*result)->data.string_value = _strdup("");
			return 0;
		}
		/* shallow copy + fixup for strings */
		memcpy(*result, src, sizeof(Variable));
		if (src->type == TYPE_STRING && src->data.string_value) {
			(*result)->data.string_value = _strdup(src->data.string_value);
		}
		return 0;
	}

	case EXPR_BINARY_OP: {
		/* logical AND / OR short-circuit */
		if (expr->data.binary.op == BINOP_AND || expr->data.binary.op == BINOP_OR) {
			Variable* l = NULL;
			if (evaluate_expression(expr->data.binary.left, st, &l) != 0 || !l) {
				free(*result);
				return -1;
			}
			coerce_empty_string_to_zero(l);  // Coerce undefined/empty to 0
			/* coerce left to truthiness */
			int l_true = 0;
			if (l->type == TYPE_BOOL || l->type == TYPE_INTEGER) {
				l_true = (l->data.int_value != 0);
			}
			else if (l->type == TYPE_DOUBLE) {
				l_true = (l->data.double_value != 0.0);
			}
			else if (l->type == TYPE_STRING) {
				l_true = (l->data.string_value && strlen(l->data.string_value) > 0);
			}
			else {
				free_variable(l);
				free(*result);
				return -1;
			}
			if (expr->data.binary.op == BINOP_AND && !l_true) {
				(*result)->type = TYPE_BOOL;
				(*result)->data.int_value = 0;
				free_variable(l);
				return 0;
			}
			if (expr->data.binary.op == BINOP_OR && l_true) {
				(*result)->type = TYPE_BOOL;
				(*result)->data.int_value = 1;
				free_variable(l);
				return 0;
			}
			free_variable(l);
			Variable* r = NULL;
			if (evaluate_expression(expr->data.binary.right, st, &r) != 0 || !r) {
				free(*result);
				return -1;
			}
			coerce_empty_string_to_zero(r);  // Coerce undefined/empty to 0
			{
				int r_true = 0;
				if (r->type == TYPE_BOOL || r->type == TYPE_INTEGER) {
					r_true = (r->data.int_value != 0);
				}
				else if (r->type == TYPE_DOUBLE) {
					r_true = (r->data.double_value != 0.0);
				}
				else if (r->type == TYPE_STRING) {
					r_true = (r->data.string_value && strlen(r->data.string_value) > 0);
				}
				else {
					free_variable(r);
					free(*result);
					return -1;
				}
				(*result)->type = TYPE_BOOL;
				(*result)->data.int_value = r_true;
			}
			free_variable(r);
			return 0;
		}

		/* evaluate both sides once */
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

		coerce_empty_string_to_zero(left_val);  // Coerce undefined/empty to 0
		coerce_empty_string_to_zero(right_val); // Coerce undefined/empty to 0

		/* operator categories */
		const int is_arith =
			(expr->data.binary.op >= BINOP_ADD && expr->data.binary.op <= BINOP_DIV);
		const int is_ordered =
			(expr->data.binary.op == BINOP_LT || expr->data.binary.op == BINOP_LE ||
				expr->data.binary.op == BINOP_GT || expr->data.binary.op == BINOP_GE);

		/* numeric coercion ONLY for arithmetic and ordered compares */
		if ((is_arith || is_ordered) && left_val->type != right_val->type) {
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
		if (is_arith) {
			if ((left_val->type != TYPE_INTEGER && left_val->type != TYPE_DOUBLE) ||
				(right_val->type != TYPE_INTEGER && right_val->type != TYPE_DOUBLE)) {
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
			const double eps = 1e-9;

			switch (expr->data.binary.op) {
			case BINOP_EQ:
			case BINOP_NE: {
				int equal = 0;

				/* if either side is a string, compare as text */
				if (left_val->type == TYPE_STRING || right_val->type == TYPE_STRING) {
					const char* ls = NULL;
					const char* rs = NULL;
					char lbuf[64]; lbuf[0] = '\0';
					char rbuf[64]; rbuf[0] = '\0';

					/* left -> text */
					if (left_val->type == TYPE_STRING) {
						ls = left_val->data.string_value ? left_val->data.string_value : "";
					}
					else if (left_val->type == TYPE_INTEGER || left_val->type == TYPE_BOOL) {
						_snprintf_s(lbuf, sizeof(lbuf), _TRUNCATE, "%d", left_val->data.int_value);
						lbuf[sizeof(lbuf) - 1] = '\0';
						ls = lbuf;
					}
					else if (left_val->type == TYPE_DOUBLE) {
						_snprintf_s(lbuf, sizeof(lbuf), _TRUNCATE, "%.15g", left_val->data.double_value);
						lbuf[sizeof(lbuf) - 1] = '\0';
						ls = lbuf;
					}
					else {
						free_variable(left_val); free_variable(right_val); free(*result); return -1;
					}

					/* right -> text */
					if (right_val->type == TYPE_STRING) {
						rs = right_val->data.string_value ? right_val->data.string_value : "";
					}
					else if (right_val->type == TYPE_INTEGER || right_val->type == TYPE_BOOL) {
						_snprintf_s(rbuf, sizeof(rbuf), _TRUNCATE, "%d", right_val->data.int_value);
						rbuf[sizeof(rbuf) - 1] = '\0';
						rs = rbuf;
					}
					else if (right_val->type == TYPE_DOUBLE) {
						_snprintf_s(rbuf, sizeof(rbuf), _TRUNCATE, "%.15g", right_val->data.double_value);
						rbuf[sizeof(rbuf) - 1] = '\0';
						rs = rbuf;
					}
					else {
						free_variable(left_val); free_variable(right_val); free(*result); return -1;
					}

					equal = (strcmp(ls, rs) == 0);
				}
				else {
					/* numeric equality (int/bool/double) */
					if ((left_val->type == TYPE_DOUBLE) || (right_val->type == TYPE_DOUBLE)) {
						double l = (left_val->type == TYPE_DOUBLE ? left_val->data.double_value : (double)left_val->data.int_value);
						double r = (right_val->type == TYPE_DOUBLE ? right_val->data.double_value : (double)right_val->data.int_value);
						equal = (fabs(l - r) <= eps);
					}
					else if ((left_val->type == TYPE_INTEGER || left_val->type == TYPE_BOOL) &&
						(right_val->type == TYPE_INTEGER || right_val->type == TYPE_BOOL)) {
						equal = (left_val->data.int_value == right_val->data.int_value);
					}
					else {
						free_variable(left_val); free_variable(right_val); free(*result); return -1;
					}
				}

				(*result)->data.int_value = (expr->data.binary.op == BINOP_EQ) ? equal : !equal;
				break;
			}

			case BINOP_LT:
				if (left_val->type == TYPE_DOUBLE && right_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value = (left_val->data.double_value < right_val->data.double_value - eps);
				}
				else if (left_val->type == TYPE_INTEGER && right_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value < right_val->data.int_value);
				}
				else {
					free_variable(left_val); free_variable(right_val); free(*result); return -1;
				}
				break;

			case BINOP_GT:
				if (left_val->type == TYPE_DOUBLE && right_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value = (left_val->data.double_value > right_val->data.double_value + eps);
				}
				else if (left_val->type == TYPE_INTEGER && right_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value > right_val->data.int_value);
				}
				else {
					free_variable(left_val); free_variable(right_val); free(*result); return -1;
				}
				break;

			case BINOP_LE:
				if (left_val->type == TYPE_DOUBLE && right_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value = (left_val->data.double_value <= right_val->data.double_value + eps);
				}
				else if (left_val->type == TYPE_INTEGER && right_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value <= right_val->data.int_value);
				}
				else {
					free_variable(left_val); free_variable(right_val); free(*result); return -1;
				}
				break;

			case BINOP_GE:
				if (left_val->type == TYPE_DOUBLE && right_val->type == TYPE_DOUBLE) {
					(*result)->data.int_value = (left_val->data.double_value >= right_val->data.double_value - eps);
				}
				else if (left_val->type == TYPE_INTEGER && right_val->type == TYPE_INTEGER) {
					(*result)->data.int_value = (left_val->data.int_value >= right_val->data.int_value);
				}
				else {
					free_variable(left_val); free_variable(right_val); free(*result); return -1;
				}
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

	LogOnlyPrintfChar("Note: SUB_PICTURE validated (positions allowed to be negative; handled at runtime)\n");
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

	LogOnlyPrintfChar("Note: Stored SHOW_PARAM options map for '%s' under key '%s'\n", node->parameter, key);

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
	st_baseline_remember(st, node->name, var);   // <-keep original value
	existing = var;
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
	LogOnlyPrintfChar("Note: Stored CHECKBOX_PARAM options map for '%s' under key '%s'\n", node->parameter, key);
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
			param_var->data.int_value = 0;  // Default to first selection
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
	LogOnlyPrintfChar("Note: Stored RADIOBUTTON_PARAM options map for '%s' under key '%s'\n", node->parameter, key);
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

	LogOnlyPrintfChar("Note: USER_SELECT_MULTIPLE '%s' registered with %zu types, max_sel=%ld\n",
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

	LogOnlyPrintfChar("Note: USER_SELECT_MULTIPLE '%s' registered with %zu types, max_sel=%ld\n",
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
* BEGIN_TABLE SEMANTICS CHECK
* 
* 
\*=================================================*/
int check_begin_table_semantics(TableNode* node, SymbolTable* st) {
	if (!node || !st) {
		ProPrintfChar("Error: Invalid TableNode or SymbolTable in semantic analysis\n");
		return -1;
	}
	/* 1) Column type header must match */
	if (node->data_type_count != node->column_count) {
		ProPrintfChar("Error: Data type count (%zu) does not match column count (%zu)\n",
			node->data_type_count, node->column_count);
		return -1;
	}
	/* 2) Map declared data types to VariableType */
	VariableType* column_types = NULL;
	if (node->column_count > 0) {
		column_types = (VariableType*)malloc(node->column_count * sizeof(VariableType));
		if (!column_types) {
			ProPrintfChar("Error: Memory allocation failed for column types\n");
			return -1;
		}
	}
	for (size_t c = 0; c < node->column_count; ++c) {
		char* dtype_str = NULL;
		if (evaluate_to_string(node->data_types[c], st, &dtype_str) != 0 || !dtype_str) {
			ProPrintfChar("Error: Failed to evaluate data type for column %zu\n", c);
			free(column_types);
			free(dtype_str);
			return -1;
		}
		if (strcmp(dtype_str, "STRING") == 0) column_types[c] = TYPE_STRING;
		else if (strcmp(dtype_str, "DOUBLE") == 0) column_types[c] = TYPE_DOUBLE;
		else if (strcmp(dtype_str, "INTEGER") == 0) column_types[c] = TYPE_INTEGER;
		else if (strcmp(dtype_str, "BOOL") == 0) column_types[c] = TYPE_BOOL;
		else if (strcmp(dtype_str, "SUBTABLE") == 0) column_types[c] = TYPE_SUBTABLE; /* CHANGED */
		else if (strcmp(dtype_str, "SUBCOMP") == 0) column_types[c] = TYPE_REFERENCE;
		else if (strcmp(dtype_str, "CONFIG_DELETE_IDS") == 0) column_types[c] = TYPE_STRING;
		else if (strcmp(dtype_str, "CONFIG_STATE") == 0) column_types[c] = TYPE_BOOL;
		else {
			ProPrintfChar("Error: Invalid data type '%s' for column %zu\n", dtype_str, c);
			free(column_types);
			free(dtype_str);
			return -1;
		}
		free(dtype_str);
	}
	/* New: Validate and track options by evaluating to strings */
	for (int i = 0; i < node->option_count; ++i) {
		char* opt_str = NULL;
		if (evaluate_to_string(node->options[i], st, &opt_str) != 0 || !opt_str) {
			ProPrintfChar("Error: Failed to evaluate TABLE_OPTION %d\n", i);
			free(column_types);
			free(opt_str);
			return -1;
		}
		free(opt_str);
	}
	/* New: Validate and prepare column keys from SEL_STRING */
	if (node->sel_string_count != node->column_count) {
		ProPrintfChar("Error: SEL_STRING count (%zu) does not match expected %zu\n",
			node->sel_string_count, node->column_count);
		free(column_types);
		return -1;
	}
	char** column_keys = (char**)malloc(node->column_count * sizeof(char*));
	if (!column_keys) {
		ProPrintfChar("Error: Memory allocation failed for column keys\n");
		free(column_types);
		return -1;
	}
	column_keys[0] = _strdup("SEL_STRING");
	if (!column_keys[0]) {
		ProPrintfChar("Error: Failed to duplicate SEL_STRING key\n");
		free(column_keys);
		free(column_types);
		return -1;
	}
	for (size_t c = 1; c < node->column_count; ++c) {
		ExpressionNode* key_expr = node->sel_strings[c];
		char* k = NULL;
		if (key_expr->type == EXPR_VARIABLE_REF) {
			k = _strdup(key_expr->data.string_val);
		}
		else {
			if (evaluate_to_string(key_expr, st, &k) != 0 || !k) {
				ProPrintfChar("Error: Failed to evaluate SEL_STRING key for column %zu\n", c);
				for (size_t i = 0; i < c; ++i) free(column_keys[i]);
				free(column_keys);
				free(column_types);
				return -1;
			}
		}
		if (!k || !is_valid_identifier(k)) {
			ProPrintfChar("Error: Invalid SEL_STRING key '%s' for column %zu (must be a valid identifier)\n", k ? k : "(null)", c);
			free(k);
			for (size_t i = 0; i < c; ++i) free(column_keys[i]);
			free(column_keys);
			free(column_types);
			return -1;
		}
		column_keys[c] = k;
	}
	/* 3) Materialize rows */
	for (size_t r = 0; r < node->row_count; ++r) {
		for (size_t c = 0; c < node->column_count; ++c) {
			ExpressionNode* cell_expr = node->rows[r][c];
			int is_empty = (cell_expr == NULL);
			if (!is_empty) {
				char* probe = NULL;
				if (evaluate_to_string(cell_expr, st, &probe) == 0) {
					if (!probe || probe[0] == '\0' || strcmp(probe, "NO_VALUE") == 0) {
						is_empty = 1;
					}
				}
				free(probe);
			}
			if (is_empty) continue;
			switch (column_types[c]) {
			case TYPE_STRING: {
				char* s = NULL;
				if (evaluate_to_string(cell_expr, st, &s) != 0) {
					ProPrintfChar("Error: STRING cell failed to evaluate in row %zu, column %zu\n", r, c);
					free(column_types);
					for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
					free(column_keys);
					return -1;
				}
				free(s);
			} break;
			case TYPE_INTEGER: {
				long iv;
				if (evaluate_to_int(cell_expr, st, &iv) != 0) {
					ProPrintfChar("Error: INTEGER cell failed to evaluate in row %zu, column %zu\n", r, c);
					free(column_types);
					for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
					free(column_keys);
					return -1;
				}
			} break;
			case TYPE_DOUBLE: {
				double dv;
				if (evaluate_to_double(cell_expr, st, &dv) != 0) {
					ProPrintfChar("Error: DOUBLE cell failed to evaluate in row %zu, column %zu\n", r, c);
					free(column_types);
					for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
					free(column_keys);
					return -1;
				}
				LogOnlyPrintfChar("Note: DOUBLE cell in row %zu, column %zu evaluated to exact value %.15g\n", r, c, dv);
			} break;
			case TYPE_BOOL: {
				long bv;
				if (evaluate_to_int(cell_expr, st, &bv) != 0) {
					ProPrintfChar("Error: BOOL cell failed to evaluate in row %zu, column %zu\n", r, c);
					free(column_types);
					for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
					free(column_keys);
					return -1;
				}
			} break;
			case TYPE_SUBTABLE: {
				char* name = NULL;
				int res = evaluate_to_string(cell_expr, st, &name);
				if (res != 0) {
					if (cell_expr->type == EXPR_VARIABLE_REF && get_symbol(st, cell_expr->data.string_val) == NULL) {
						LogOnlyPrintfChar("Note: Treating undeclared '%s' as forward SUBTABLE ref (row %zu, col %zu)\n",
							cell_expr->data.string_val, r, c);
					}
					else {
						ProPrintfChar("Error: SUBTABLE cell failed to evaluate in row %zu, column %zu\n", r, c);
						free(column_types);
						for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
						free(column_keys);
						return -1;
					}
				}
				if (name && name[0] != '\0' && strcmp(name, "NO_VALUE") != 0) {
					LogOnlyPrintfChar("Note: SUBTABLE reference '%s' recorded (row %zu, col %zu)\n",
						name, r, c);
				}
				free(name);
			} break;
			case TYPE_REFERENCE: {
				char* name = NULL;
				int res = evaluate_to_string(cell_expr, st, &name);
				if (res != 0) {
					if (cell_expr->type == EXPR_VARIABLE_REF && get_symbol(st, cell_expr->data.string_val) == NULL) {
						LogOnlyPrintfChar("Note: Treating undeclared '%s' as forward REFERENCE ref (row %zu, col %zu)\n",
							cell_expr->data.string_val, r, c);
					}
					else {
						ProPrintfChar("Error: REFERENCE cell failed to evaluate in row %zu, column %zu\n", r, c);
						free(column_types);
						for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
						free(column_keys);
						return -1;
					}
				}
				free(name);
			} break;
			default:
				ProPrintfChar("Error: Unsupported column type %d in row %zu, column %zu\n", column_types[c], r, c);
				free(column_types);
				for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
				free(column_keys);
				return -1;
			}
		}
	}
	/* 4) Materialize table data as array (renamed to data_var for wrapping) */
	Variable* data_var = (Variable*)malloc(sizeof(Variable));
	if (!data_var) { ProPrintfChar("Error: Memory allocation failed for table variable\n"); free(column_types); for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]); free(column_keys); return -1; }
	data_var->type = TYPE_ARRAY;
	data_var->data.array.size = node->row_count;
	data_var->data.array.elements = (node->row_count > 0)
		? (Variable**)malloc(node->row_count * sizeof(Variable*))
		: NULL;
	if (node->row_count > 0 && !data_var->data.array.elements) { free(data_var); free(column_types); for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]); free(column_keys); return -1; }
	for (size_t r = 0; r < node->row_count; ++r) {
		Variable* row_var = (Variable*)malloc(sizeof(Variable));
		if (!row_var) {
			for (size_t k = 0; k < r; ++k) free_variable(data_var->data.array.elements[k]);
			free(data_var->data.array.elements);
			free(data_var);
			free(column_types);
			for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
			free(column_keys);
			return -1;
		}
		row_var->type = TYPE_MAP;
		row_var->data.map = create_hash_table(node->column_count);
		if (!row_var->data.map) {
			free(row_var);
			for (size_t k = 0; k < r; ++k) free_variable(data_var->data.array.elements[k]);
			free(data_var->data.array.elements);
			free(data_var);
			free(column_types);
			for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
			free(column_keys);
			return -1;
		}
		for (size_t c = 0; c < node->column_count; ++c) {
			ExpressionNode* cell_expr = node->rows[r][c];
			Variable* v = (Variable*)malloc(sizeof(Variable));
			if (!v) {
				free_hash_table(row_var->data.map);
				free(row_var);
				for (size_t k = 0; k < r; ++k) free_variable(data_var->data.array.elements[k]);
				free(data_var->data.array.elements);
				free(data_var);
				free(column_types);
				for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
				free(column_keys);
				return -1;
			}
			v->type = TYPE_NULL;
			int is_empty = (cell_expr == NULL);
			char* probe = NULL;
			if (!is_empty && evaluate_to_string(cell_expr, st, &probe) == 0) {
				if (!probe || probe[0] == '\0' || strcmp(probe, "NO_VALUE") == 0) {
					is_empty = 1;
				}
			}
			if (!is_empty) {
				switch (column_types[c]) {
				case TYPE_STRING: {
					char* s = NULL;
					if (evaluate_to_string(cell_expr, st, &s) == 0 && s && s[0] != '\0' && strcmp(s, "NO_VALUE") != 0) {
						v->type = TYPE_STRING;
						v->data.string_value = _strdup(s);
						if (!v->data.string_value) { v->type = TYPE_NULL; }
					}
					free(s);
				} break;
				case TYPE_INTEGER: {
					long iv;
					if (evaluate_to_int(cell_expr, st, &iv) == 0) { v->type = TYPE_INTEGER; v->data.int_value = (int)iv; }
				} break;
				case TYPE_DOUBLE: {
					double dv;
					if (evaluate_to_double(cell_expr, st, &dv) == 0) {
						v->type = TYPE_DOUBLE;
						v->data.double_value = dv;
						LogOnlyPrintfChar("Note: Stored exact DOUBLE value %.15g in row %zu, column %zu\n", dv, r, c);
					}
				} break;
				case TYPE_BOOL: {
					long bv;
					if (evaluate_to_int(cell_expr, st, &bv) == 0) { v->type = TYPE_BOOL; v->data.int_value = (bv != 0); }
				} break;
				case TYPE_SUBTABLE: {
					char* name = NULL;
					int res = evaluate_to_string(cell_expr, st, &name);
					if (res != 0) {
						if (cell_expr->type == EXPR_VARIABLE_REF && get_symbol(st, cell_expr->data.string_val) == NULL) {
							name = _strdup(cell_expr->data.string_val);
							LogOnlyPrintfChar("Note: Stored undeclared '%s' as forward SUBTABLE ref\n", name);
						}
					}
					if (name && name[0] != '\0' && strcmp(name, "NO_VALUE") != 0) {
						v->type = TYPE_SUBTABLE;
						v->data.string_value = _strdup(name);
						if (!v->data.string_value) v->type = TYPE_NULL;
						LogOnlyPrintfChar("Note: Stored SUBTABLE ref '%s' as TYPE_SUBTABLE\n", name);
					}
					free(name);
				} break;
				case TYPE_REFERENCE: {
					char* ref = NULL;
					int res = evaluate_to_string(cell_expr, st, &ref);
					if (res != 0) {
						if (cell_expr->type == EXPR_VARIABLE_REF && get_symbol(st, cell_expr->data.string_val) == NULL) {
							ref = _strdup(cell_expr->data.string_val);
							LogOnlyPrintfChar("Note: Stored undeclared '%s' as forward REFERENCE ref\n", ref);
						}
						else {
							ref = NULL;
						}
					}
					if (ref && ref[0] != '\0' && strcmp(ref, "NO_VALUE") != 0) {
						v->type = TYPE_STRING;
						v->data.string_value = _strdup(ref);
						if (!v->data.string_value) v->type = TYPE_NULL;
					}
					free(ref);
				} break;
				default: /* leave TYPE_NULL */ break;
				}
			}
			free(probe);
			hash_table_insert(row_var->data.map, column_keys[c], v);
		}
		data_var->data.array.elements[r] = row_var;
	}

	/* 4.5) Wrap data in a map with options */
	Variable* table_var = (Variable*)malloc(sizeof(Variable));
	if (!table_var) {
		free_variable(data_var);
		free(column_types);
		for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
		free(column_keys);
		ProPrintfChar("Error: Memory allocation failed for table wrapper\n");
		return -1;
	}
	table_var->type = TYPE_MAP;
	table_var->data.map = create_hash_table(6); /* rows, options, columns, filter_column, filter_only_column */
	if (!table_var->data.map) {
		free(table_var);
		free_variable(data_var);
		free(column_types);
		for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
		free(column_keys);
		return -1;
	}
	hash_table_insert(table_var->data.map, "rows", data_var);

	/* --- NEW: persist columns + filter options --- */
	/* 1) columns[] (string keys; 0 = SEL_STRING, 1+ = data columns) */
	{
		Variable* columns_var = (Variable*)malloc(sizeof(Variable));
		if (columns_var) {
			columns_var->type = TYPE_ARRAY;
			columns_var->data.array.size = node->column_count;
			columns_var->data.array.elements = (Variable**)calloc(node->column_count, sizeof(Variable*));
			if (columns_var->data.array.elements) {
				for (size_t i = 0; i < (size_t)node->column_count; ++i) {
					Variable* s = (Variable*)malloc(sizeof(Variable));
					if (!s) continue;
					s->type = TYPE_STRING;
					s->data.string_value = _strdup(column_keys[i]);
					if (!s->data.string_value) { free(s); s = NULL; }
					columns_var->data.array.elements[i] = s;
				}
				hash_table_insert(table_var->data.map, "columns", columns_var);
			}
			else {
				free(columns_var);
				ProPrintfChar("Warning: Failed to allocate columns array for table '%s'\n", node->identifier);
			}
		}
		else {
			ProPrintfChar("Warning: Failed to allocate columns_var for table '%s'\n", node->identifier);
		}
	}
	/* 2) filter_column / filter_only_column (indices after visible col) */
	if (node->filter_column >= 0) {
		Variable* fc = (Variable*)malloc(sizeof(Variable));
		if (fc) {
			fc->type = TYPE_INTEGER;
			fc->data.int_value = node->filter_column; /* 0 == first AFTER visible */
			hash_table_insert(table_var->data.map, "filter_column", fc);
		}
		else {
			ProPrintfChar("Warning: Failed to allocate filter_column for table '%s'\n", node->identifier);
		}
	}
	if (node->filter_only_column >= 0) {
		Variable* foc = (Variable*)malloc(sizeof(Variable));
		if (foc) {
			foc->type = TYPE_INTEGER;
			foc->data.int_value = node->filter_only_column;
			hash_table_insert(table_var->data.map, "filter_only_column", foc);
		}
		else {
			ProPrintfChar("Warning: Failed to allocate filter_only_column for table '%s'\n", node->identifier);
		}
	}
	/* --- END NEW --- */

	/* New: Materialize and store options as array (kept as-is) */
	if (node->option_count > 0) {
		Variable* options_var = (Variable*)malloc(sizeof(Variable));
		if (!options_var) {
			free_hash_table(table_var->data.map);
			free(table_var);
			free(column_types);
			for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
			free(column_keys);
			ProPrintfChar("Error: Memory allocation failed for options array\n");
			return -1;
		}
		options_var->type = TYPE_ARRAY;
		options_var->data.array.size = node->option_count;
		options_var->data.array.elements = (Variable**)malloc(node->option_count * sizeof(Variable*));
		if (!options_var->data.array.elements) {
			free(options_var);
			free_hash_table(table_var->data.map);
			free(table_var);
			free(column_types);
			for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
			free(column_keys);
			return -1;
		}
		for (int i = 0; i < node->option_count; ++i) {
			char* opt_str = NULL;
			if (evaluate_to_string(node->options[i], st, &opt_str) != 0 || !opt_str) {
				ProPrintfChar("Error: Failed to re-evaluate TABLE_OPTION %d during materialization\n", i);
				for (int j = 0; j < i; ++j) free_variable(options_var->data.array.elements[j]);
				free(options_var->data.array.elements);
				free(options_var);
				free_hash_table(table_var->data.map);
				free(table_var);
				free(column_types);
				for (size_t k = 0; k < node->column_count; ++k) free(column_keys[k]);
				free(column_keys);
				free(opt_str);
				return -1;
			}
			Variable* opt_v = (Variable*)malloc(sizeof(Variable));
			if (!opt_v) {
				for (int j = 0; j < i; ++j) free_variable(options_var->data.array.elements[j]);
				free(options_var->data.array.elements);
				free(options_var);
				free_hash_table(table_var->data.map);
				free(table_var);
				free(column_types);
				for (size_t k = 0; k < node->column_count; ++k) free(column_keys[k]);
				free(column_keys);
				free(opt_str);
				return -1;
			}
			opt_v->type = TYPE_STRING;
			opt_v->data.string_value = _strdup(opt_str);
			if (!opt_v->data.string_value) {
				free(opt_v);
				for (int j = 0; j < i; ++j) free_variable(options_var->data.array.elements[j]);
				free(options_var->data.array.elements);
				free(options_var);
				free_hash_table(table_var->data.map);
				free(table_var);
				free(column_types);
				for (size_t k = 0; k < node->column_count; ++k) free(column_keys[k]);
				free(column_keys);
				free(opt_str);
				return -1;
			}
			options_var->data.array.elements[i] = opt_v;
			free(opt_str);
		}
		hash_table_insert(table_var->data.map, "options", options_var);
	}

	/* 5) Publish to symbol table */
	set_symbol(st, node->identifier, table_var);
	free(column_types);
	for (size_t i = 0; i < node->column_count; ++i) free(column_keys[i]);
	free(column_keys);
	return 0;
}

/*=================================================*\
* 
* // Semantic analysis for IF: Validate conditions and recurse on branches
* 
* 
\*=================================================*/
// Helper to initialize an AssignmentList
static void init_assignment_list(AssignmentList* list) {
	list->ids = NULL;
	list->count = 0;
	list->cap = 0;
}

// Helper to add an ID to an AssignmentList
static void add_to_assignment_list(AssignmentList* list, int id) {
	if (list->count == list->cap) {
		size_t new_cap = (list->cap == 0 ? 8 : list->cap * 2);
		int* tmp = (int*)realloc(list->ids, new_cap * sizeof(int));
		if (!tmp) return; // Out-of-memory: best-effort, skip add
		list->ids = tmp;
		list->cap = new_cap;
	}
	list->ids[list->count++] = id;
}

// Helper to free an AssignmentList
static void free_assignment_list(AssignmentList* list) {
	free(list->ids);
	list->ids = NULL;
	list->count = 0;
	list->cap = 0;
}

static void collect_assignment_ids_from_command(CommandNode* cmd, AssignmentList* list)
{
	if (!cmd || !list) return;

	if (cmd->type == COMMAND_ASSIGNMENT) {
		AssignmentNode* an = &((CommandData*)cmd->data)->assignment;
		if (an && an->assign_id > 0) {
			add_to_assignment_list(list, an->assign_id);
		}
		return;
	}

	if (cmd->type == COMMAND_IF) {
		IfNode* in = &((CommandData*)cmd->data)->ifcommand;
		if (in) {
			for (size_t b = 0; b < in->branch_count; ++b) {
				IfBranch* br = in->branches[b];
				if (!br) continue;
				for (size_t c = 0; c < br->command_count; ++c) {
					collect_assignment_ids_from_command(br->commands[c], list);
				}
			}
			for (size_t c = 0; c < in->else_command_count; ++c) {
				collect_assignment_ids_from_command(in->else_commands[c], list);
			}
		}
		return;
	}

	if (cmd->type == COMMAND_FOR) {
		ForNode* fn = &((CommandData*)cmd->data)->forcommand;
		if (fn) {
			for (size_t c = 0; c < fn->command_count; ++c) {
				collect_assignment_ids_from_command(fn->commands[c], list);
			}
		}
		return;
	}

	if (cmd->type == COMMAND_WHILE) {
		WhileNode* wn = &((CommandData*)cmd->data)->whilecommand;
		if (wn) {
			for (size_t c = 0; c < wn->command_count; ++c) {
				collect_assignment_ids_from_command(wn->commands[c], list);
			}
		}
		return;
	}

	/* Other command types have no nested command blocks */
}

int check_if_semantics(IfNode* node, SymbolTable* st) {
	if (!node) {
		ProPrintfChar("Error: Invalid IF node\n");
		return -1;
	}

	/* 0) Ensure the IFS registry exists */
	Variable* ifreg = get_symbol(st, "IFS");
	if (!ifreg) {
		ifreg = (Variable*)malloc(sizeof(Variable));
		if (!ifreg) { ProPrintfChar("Error: OOM creating IFS registry\n"); return -1; }
		ifreg->type = TYPE_MAP;
		ifreg->data.map = create_hash_table(32);
		ifreg->display_options = NULL;
		ifreg->declaration_count = 1;
		if (!ifreg->data.map) { free(ifreg); ProPrintfChar("Error: OOM creating IFS map\n"); return -1; }
		set_symbol(st, "IFS", ifreg);
	}
	else if (ifreg->type != TYPE_MAP || !ifreg->data.map) {
		ProPrintfChar("Error: Symbol 'IFS' exists but is not a map\n");
		return -1;
	}

	/* Prepare per-branch lists (branch_count for IF/ELSE_IF, +1 if ELSE present) */
	size_t total_branches = node->branch_count + (node->else_command_count > 0 ? 1 : 0);
	AssignmentList* branch_lists = (AssignmentList*)calloc(total_branches, sizeof(AssignmentList));
	if (!branch_lists) {
		ProPrintfChar("Error: OOM for branch assignment lists\n");
		return -1;
	}
	for (size_t i = 0; i < total_branches; ++i) {
		init_assignment_list(&branch_lists[i]);
	}

	/* 1) Validate each branch condition; set __CURRENT_IF_ID while recursing */
	/*    We reuse a single integer variable for __CURRENT_IF_ID to avoid churn. */
	Variable* cur_if_sym = get_symbol(st, "__CURRENT_IF_ID");
	if (!cur_if_sym) {
		cur_if_sym = (Variable*)malloc(sizeof(Variable));
		if (!cur_if_sym) {
			for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
			free(branch_lists);
			ProPrintfChar("Error: OOM for __CURRENT_IF_ID\n"); return -1;
		}
		cur_if_sym->type = TYPE_INTEGER;
		cur_if_sym->data.int_value = 0;
		cur_if_sym->display_options = NULL;
		cur_if_sym->declaration_count = 1;
		set_symbol(st, "__CURRENT_IF_ID", cur_if_sym);
	}
	const int saved_if_id = cur_if_sym->data.int_value;

	for (size_t b = 0; b < node->branch_count; b++) {
		IfBranch* branch = node->branches[b];
		VariableType cond_type = get_expression_type(branch->condition, st);
		if (cond_type == -1 ||
			(cond_type != TYPE_BOOL && cond_type != TYPE_INTEGER && cond_type != TYPE_DOUBLE)) {
			ProPrintfChar("Error: IF/ELSE_IF condition must be boolean or coercible (int/double)\n");
			for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
			free(branch_lists);
			return -1;
		}

		/* Set current IF context for anything analyzed under this IF */
		cur_if_sym->data.int_value = node->id;

		/* Recurse */
		for (size_t c = 0; c < branch->command_count; c++) {
			if (analyze_command(branch->commands[c], st) != 0) {
				cur_if_sym->data.int_value = saved_if_id;
				for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
				free(branch_lists);
				return -1;
			}
			/* Collect per branch */
			collect_assignment_ids_from_command(branch->commands[c], &branch_lists[b]);
		}
	}

	/* ELSE block */
	if (node->else_command_count > 0) {
		cur_if_sym->data.int_value = node->id;
		for (size_t c = 0; c < node->else_command_count; c++) {
			if (analyze_command(node->else_commands[c], st) != 0) {
				cur_if_sym->data.int_value = saved_if_id;
				for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
				free(branch_lists);
				return -1;
			}
			collect_assignment_ids_from_command(node->else_commands[c], &branch_lists[node->branch_count]);
		}
		cur_if_sym->data.int_value = saved_if_id;
	}

	/* 2) Build the IF entry map and publish into IFS */
	Variable* entry = (Variable*)malloc(sizeof(Variable));
	if (!entry) {
		for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
		free(branch_lists);
		ProPrintfChar("Error: OOM for IF entry\n"); return -1;
	}
	entry->type = TYPE_MAP;
	entry->data.map = create_hash_table(16);
	entry->display_options = NULL;
	entry->declaration_count = 1;
	if (!entry->data.map) {
		free(entry);
		for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
		free(branch_lists);
		ProPrintfChar("Error: OOM for IF entry map\n"); return -1;
	}

	/* Basic facts */
	(void)add_int_to_map(entry->data.map, "if_id", node->id);
	(void)add_int_to_map(entry->data.map, "branch_count", (int)node->branch_count);
	(void)add_int_to_map(entry->data.map, "else_command_count", (int)node->else_command_count);

	/* Optional pretty print of the first IF condition to aid debugging */
	if (node->branch_count > 0 && node->branches[0] && node->branches[0]->condition) {
		char* cond = expression_to_string(node->branches[0]->condition);
		if (cond) (void)add_string_to_map(entry->data.map, "if_condition", cond);
	}

	/* NEW: branch_assignments as array of arrays (one per branch + else if present) */
	Variable* branch_arr = (Variable*)malloc(sizeof(Variable));
	if (!branch_arr) {
		free_hash_table(entry->data.map); free(entry);
		for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
		free(branch_lists);
		ProPrintfChar("Error: OOM for branch_assignments array\n"); return -1;
	}
	branch_arr->type = TYPE_ARRAY;
	branch_arr->display_options = NULL;
	branch_arr->declaration_count = 1;
	branch_arr->data.array.size = (int)total_branches;
	branch_arr->data.array.elements = (Variable**)calloc(total_branches > 0 ? total_branches : 1, sizeof(Variable*));
	if (!branch_arr->data.array.elements && total_branches > 0) {
		free(branch_arr); free_hash_table(entry->data.map); free(entry);
		for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
		free(branch_lists);
		ProPrintfChar("Error: OOM for branch_assignments elements\n");
		return -1;
	}
	size_t total_assigns = 0;
	for (size_t b = 0; b < total_branches; ++b) {
		AssignmentList* lst = &branch_lists[b];
		Variable* sub_arr = (Variable*)malloc(sizeof(Variable));
		if (!sub_arr) continue;
		sub_arr->type = TYPE_ARRAY;
		sub_arr->display_options = NULL;
		sub_arr->declaration_count = 1;
		sub_arr->data.array.size = (int)lst->count;
		sub_arr->data.array.elements = (Variable**)calloc(lst->count > 0 ? lst->count : 1, sizeof(Variable*));
		if (!sub_arr->data.array.elements && lst->count > 0) {
			free(sub_arr); continue;
		}
		for (size_t i = 0; i < lst->count; ++i) {
			Variable* iv = (Variable*)malloc(sizeof(Variable));
			if (!iv) continue;
			iv->type = TYPE_INTEGER;
			iv->data.int_value = lst->ids[i];
			iv->display_options = NULL;
			iv->declaration_count = 1;
			sub_arr->data.array.elements[i] = iv;
		}
		branch_arr->data.array.elements[b] = sub_arr;
		total_assigns += lst->count;
	}
	(void)add_var_to_map(entry->data.map, "branch_assignments", branch_arr);
	(void)add_int_to_map(entry->data.map, "has_assignments", (int)(total_assigns > 0));

	/* Insert under stable key IF_#### */
	{
		char key[32];
		snprintf(key, sizeof(key), "IF_%04d", node->id);
		(void)add_var_to_map(ifreg->data.map, key, entry);
	}

	/* 3) Back-fill each ASSIGN_#### with its parent IF id and branch_index (if created already) */
	/*    branch_index: 0 for IF, 1.. for ELSE_IF, total_branches-1 for ELSE if present */
	if (total_assigns > 0) {
		Variable* areg = get_symbol(st, "ASSIGNMENTS");
		if (areg && areg->type == TYPE_MAP && areg->data.map) {
			for (size_t b = 0; b < total_branches; ++b) {
				AssignmentList* lst = &branch_lists[b];
				int branch_idx = (int)b;
				if (b == node->branch_count) branch_idx = -1; // ELSE
				for (size_t i = 0; i < lst->count; ++i) {
					char akey[32];
					snprintf(akey, sizeof(akey), "ASSIGN_%04d", lst->ids[i]);
					Variable* aentry = hash_table_lookup(areg->data.map, akey);
					if (aentry && aentry->type == TYPE_MAP && aentry->data.map) {
						(void)add_int_to_map(aentry->data.map, "if_id", node->id);
						(void)add_int_to_map(aentry->data.map, "branch_index", branch_idx);
					}
				}
			}
		}
	}

	/* Cleanup */
	for (size_t i = 0; i < total_branches; ++i) free_assignment_list(&branch_lists[i]);
	free(branch_lists);

	LogOnlyPrintfChar("Note: IF validated; %zu branches analyzed (linked %zu assignment(s))\n",
		node->branch_count, total_assigns);
	return 0;
}

/*=================================================*
 *
 * ASSIGNMENT semantic analysis: validate and register in symbol table
 *
 * The registry lives at top-level key "ASSIGNMENTS" (TYPE_MAP).
 * Each assignment is stored under "ASSIGN_####" with:
 *   - assign_id   : int
 *   - lhs_text    : string (pretty string of LHS)
 *   - rhs_text    : string (pretty string of RHS)
 *   - lhs_type    : int (VariableType)
 *   - rhs_type    : int (VariableType)
 *   - lhs_expr    : TYPE_EXPR (raw AST pointer)
 *   - rhs_expr    : TYPE_EXPR (raw AST pointer)
 *   - lhs_name    : string (only when LHS is EXPR_VARIABLE_REF)
 *   - if_id       : int (added if inside IF)
 *   - branch_index: int (new: 0.. for branches, -1 for else)
 *
\*=================================================*/
int check_assignment_semantics(AssignmentNode* node, SymbolTable* st)
{
	if (!node || !node->lhs || !node->rhs) {
		ProPrintfChar("Error: Invalid assignment node\n");
		return -1;
	}

	/* LHS form check ... (unchanged) */
	if (node->lhs->type != EXPR_VARIABLE_REF &&
		node->lhs->type != EXPR_ARRAY_INDEX &&
		node->lhs->type != EXPR_MAP_LOOKUP &&
		node->lhs->type != EXPR_STRUCT_ACCESS)
	{
		ProPrintfChar("Error: LHS of assignment must be a variable, array index, map lookup, or struct member\n");
		return -1;
	}

	/* Type inference ... (unchanged) */
	VariableType lhs_type = get_expression_type(node->lhs, st);
	if (lhs_type == (VariableType)-1) {
		ProPrintfChar("Error: Invalid or undeclared LHS in assignment\n");
		return -1;
	}
	VariableType rhs_type = get_expression_type(node->rhs, st);
	if (rhs_type == (VariableType)-1) {
		ProPrintfChar("Error: Invalid RHS expression in assignment\n");
		return -1;
	}

	/* Ensure ASSIGNMENTS registry ... (unchanged) */
	Variable* reg = get_symbol(st, "ASSIGNMENTS");
	if (!reg) {
		reg = (Variable*)malloc(sizeof(Variable));
		if (!reg) { ProPrintfChar("Error: OOM for ASSIGNMENTS registry\n"); return -1; }
		reg->type = TYPE_MAP;
		reg->data.map = create_hash_table(32);
		reg->display_options = NULL;
		reg->declaration_count = 1;
		if (!reg->data.map) { free(reg); ProPrintfChar("Error: OOM for ASSIGNMENTS map\n"); return -1; }
		set_symbol(st, "ASSIGNMENTS", reg);
	}
	else if (reg->type != TYPE_MAP || !reg->data.map) {
		ProPrintfChar("Error: Symbol 'ASSIGNMENTS' exists but is not a map\n");
		return -1;
	}

	/* Build entry map ... (unchanged up to 4b) */
	Variable* entry = (Variable*)malloc(sizeof(Variable));
	if (!entry) { ProPrintfChar("Error: OOM for assignment entry\n"); return -1; }
	entry->type = TYPE_MAP;
	entry->data.map = create_hash_table(16);
	entry->display_options = NULL;
	entry->declaration_count = 1;
	if (!entry->data.map) { free(entry); ProPrintfChar("Error: OOM for assignment entry map\n"); return -1; }
	(void)add_int_to_map(entry->data.map, "assign_id", (int)node->assign_id);

	/* 4b) stringified, 4c) raw exprs, 4d) types, 4e) lhs_name ... (unchanged) */
	{
		char* lhs_text = expression_to_string(node->lhs);
		char* rhs_text = expression_to_string(node->rhs);
		if (lhs_text) (void)add_string_to_map(entry->data.map, "lhs_text", lhs_text);
		if (rhs_text) (void)add_string_to_map(entry->data.map, "rhs_text", rhs_text);
	}
	{
		Variable* v_lhs = (Variable*)malloc(sizeof(Variable));
		Variable* v_rhs = (Variable*)malloc(sizeof(Variable));
		if (!v_lhs || !v_rhs) { if (v_lhs) free(v_lhs); if (v_rhs) free(v_rhs); ProPrintfChar("Error: OOM for expr handles\n"); return -1; }
		v_lhs->type = TYPE_EXPR; v_lhs->data.expr = node->lhs; v_lhs->display_options = NULL; v_lhs->declaration_count = 1;
		v_rhs->type = TYPE_EXPR; v_rhs->data.expr = node->rhs; v_rhs->display_options = NULL; v_rhs->declaration_count = 1;
		(void)add_var_to_map(entry->data.map, "lhs_expr", v_lhs);
		(void)add_var_to_map(entry->data.map, "rhs_expr", v_rhs);
	}
	(void)add_int_to_map(entry->data.map, "lhs_type", (int)lhs_type);
	(void)add_int_to_map(entry->data.map, "rhs_type", (int)rhs_type);
	if (node->lhs->type == EXPR_VARIABLE_REF && node->lhs->data.string_val) {
		(void)add_string_to_map(entry->data.map, "lhs_name", _strdup(node->lhs->data.string_val));
	}

	/* NEW: backlink to the IF we are currently inside, if any */
	{
		Variable* cur_if = get_symbol(st, "__CURRENT_IF_ID");
		if (cur_if && cur_if->type == TYPE_INTEGER && cur_if->data.int_value > 0) {
			(void)add_int_to_map(entry->data.map, "if_id", cur_if->data.int_value);
			// branch_index will be back-filled during IF analysis (since it knows the branch)
		}
	}

	/* Insert entry under ASSIGN_#### */
	{
		char key[32];
		snprintf(key, sizeof(key), "ASSIGN_%04d", node->assign_id);
		(void)add_var_to_map(reg->data.map, key, entry);
	}

	LogOnlyPrintfChar("Note: ASSIGNMENT registered with assign_id=%d\n", node->assign_id);
	return 0;
}

/*=================================================*
 *
 * New: Watcher Index Builder
 * Builds a top-level map "WATCHER_INDEX" in the symbol table.
 * Key: variable name (string, from lhs_name if simple var)
 * Value: array of maps, each: {if_id: int, branch_index: int, assign_id: int}
 * Only includes assignments with lhs_name (simple variables) and inside IFs.
 * This allows tracking conditional assignments to variables for re-evaluation.
 *
 * Call this after all semantic analysis, e.g., at the end of perform_semantic_analysis.
 *
\*=================================================*/
int build_watcher_index(SymbolTable* st) {
	Variable* areg = get_symbol(st, "ASSIGNMENTS");
	if (!areg || areg->type != TYPE_MAP || !areg->data.map) {
		ProPrintfChar("Warning: No ASSIGNMENTS registry; skipping watcher index\n");
		return 0; // No error, just no index
	}

	Variable* watcher = (Variable*)malloc(sizeof(Variable));
	if (!watcher) {
		ProPrintfChar("Error: OOM for WATCHER_INDEX\n");
		return -1;
	}
	watcher->type = TYPE_MAP;
	watcher->data.map = create_hash_table(64); // Arbitrary initial size
	watcher->display_options = NULL;
	watcher->declaration_count = 1;
	if (!watcher->data.map) {
		free(watcher);
		ProPrintfChar("Error: OOM for WATCHER_INDEX map\n");
		return -1;
	}

	// Iterate over all ASSIGN_#### entries
	HashTable* assign_map = areg->data.map;
	for (size_t bucket = 0; bucket < assign_map->size; ++bucket) {
		HashEntry* entry = assign_map->buckets[bucket];
		while (entry) {
			if (entry->value && entry->value->type == TYPE_MAP && entry->value->data.map) {
				HashTable* a_map = entry->value->data.map;

				// Check for lhs_name, if_id, branch_index
				Variable* lhs_name_var = hash_table_lookup(a_map, "lhs_name");
				Variable* if_id_var = hash_table_lookup(a_map, "if_id");
				Variable* branch_idx_var = hash_table_lookup(a_map, "branch_index");
				Variable* assign_id_var = hash_table_lookup(a_map, "assign_id");

				if (lhs_name_var && lhs_name_var->type == TYPE_STRING && lhs_name_var->data.string_value &&
					if_id_var && if_id_var->type == TYPE_INTEGER &&
					branch_idx_var && branch_idx_var->type == TYPE_INTEGER &&
					assign_id_var && assign_id_var->type == TYPE_INTEGER) {

					const char* var_name = lhs_name_var->data.string_value;

					// Get or create array for this var_name
					Variable* var_list = hash_table_lookup(watcher->data.map, var_name);
					if (!var_list) {
						var_list = (Variable*)malloc(sizeof(Variable));
						if (!var_list) goto next_entry; // OOM: skip
						var_list->type = TYPE_ARRAY;
						var_list->data.array.size = 0;
						var_list->data.array.elements = NULL;
						var_list->display_options = NULL;
						var_list->declaration_count = 1;
						(void)add_var_to_map(watcher->data.map, var_name, var_list);
					}
					if (var_list->type != TYPE_ARRAY) goto next_entry; // Corrupt: skip

					// Append a map {if_id, branch_index, assign_id}
					Variable* info = (Variable*)malloc(sizeof(Variable));
					if (!info) goto next_entry;
					info->type = TYPE_MAP;
					info->data.map = create_hash_table(4);
					info->display_options = NULL;
					info->declaration_count = 1;
					if (!info->data.map) { free(info); goto next_entry; }

					(void)add_int_to_map(info->data.map, "if_id", if_id_var->data.int_value);
					(void)add_int_to_map(info->data.map, "branch_index", branch_idx_var->data.int_value);
					(void)add_int_to_map(info->data.map, "assign_id", assign_id_var->data.int_value);

					// Append to array
					size_t new_size = var_list->data.array.size + 1;
					Variable** new_elems = (Variable**)realloc(var_list->data.array.elements, new_size * sizeof(Variable*));
					if (!new_elems) { free_hash_table(info->data.map); free(info); goto next_entry; }
					var_list->data.array.elements = new_elems;
					var_list->data.array.elements[var_list->data.array.size] = info;
					var_list->data.array.size = new_size;
				}
			}
		next_entry:
			entry = entry->next;
		}
	}

	set_symbol(st, "WATCHER_INDEX", watcher);
	ProPrintfChar("Note: WATCHER_INDEX built successfully\n");
	return 0;
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
	case COMMAND_BEGIN_TABLE:
		result = check_begin_table_semantics((TableNode*)cmd->data, st);
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
	if (build_watcher_index(st) != 0) {
		ProPrintfChar("Error: Failed to build watcher index\n");
		return -1;
	}
	print_symbol_table(st);
	return 0;  // Always return success to proceed; invalid commands are flagged
}

