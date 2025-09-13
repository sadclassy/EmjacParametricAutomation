#ifndef SYNTAX_ANALYSIS_H
#define SYNTAX_ANALYSIS_H

#pragma warning(push)
#pragma warning(disable: 6001)

#include "utility.h"
#include "LexicalAnalysis.h"
#include "SymbolTable.h"

typedef struct CommandNode CommandNode;



// Enum for Creo reference types (mapped from strings)
typedef enum {
    CREO_ASSEMBLY = PRO_ASSEMBLY,
    CREO_AXIS = PRO_AXIS,
    CREO_CURVE = PRO_CURVE,
    CREO_EDGE = PRO_EDGE,
    CREO_SURFACE = PRO_SURFACE,
    CREO_PLANE = PRO_DATUM_PLANE,
    CREO_UNKNOWN = -1  // Invalid
} CreoReferenceType;

// Enum for variable types based on scripting language overview
typedef enum {
    VAR_PARAMETER,      // int, double, string; booleans as int (0/1)
    VAR_REFERENCE,      // Creo entities (e.g., surfaces, edges); stores reference descriptor
    VAR_FILE_DESCRIPTOR,// File I/O descriptors
    VAR_ARRAY,          // Indexed collection; supports sub-arrays for multi-dimensionality
    VAR_MAP,            // Key-value container with overwrite semantics
    VAR_GENERAL,        // Polymorphic: parameter, reference, file, or array (for nesting)
    VAR_STRUCTURE       // Named members with dot notation; nests any type
} VariableType;

// Sub-enum for parameter subtypes (within VAR_PARAMETER)
typedef enum {
    PARAM_INT,
    PARAM_DOUBLE,
    PARAM_STRING,
    PARAM_BOOL  // Handled as int (0/1)
} ParameterSubType;

// Forward declaration for recursive expressions (e.g., nested arrays)
typedef struct ExpressionNode ExpressionNode;

// Named struct for map pairs to ensure type compatibility
typedef struct {
    char* key;
    ExpressionNode* value;
} MapPair;

// Named struct for structure members to ensure type compatibility
typedef struct {
    char* member_name;
    VariableType member_type;
    ExpressionNode* default_expr;
} StructMember;

// Forward declaration for recursive nesting in VAR_GENERAL
typedef struct VariableDataStruct VariableDataStruct;

// Type-specific data for variables (union in DeclareVariableNode)
typedef union {
    struct {  // For VAR_PARAMETER
        ParameterSubType subtype;
        ExpressionNode* default_expr;  // Expression for default (e.g., literal or computed)
    } parameter;
    struct {  // For VAR_REFERENCE
        char* entity_type;             // e.g., "surface", "edge"
        ExpressionNode* default_ref;   // Optional default reference expression
    } reference;
    struct {  // For VAR_FILE_DESCRIPTOR
        char* mode;                    // e.g., "read", "write"
        char* path;                    // File path string
    } file_desc;
    struct {  // For VAR_ARRAY
        VariableType element_type;     // Type of elements (recursive for sub-arrays)
        ExpressionNode** initializers; // Array of expressions for init list
        size_t init_count;
    } array;
    struct {  // For VAR_MAP
        MapPair* pairs;                // Key-value pairs for initialization
        size_t pair_count;
    } map;
    struct {  // For VAR_GENERAL (polymorphic)
        VariableType inner_type;       // Delegated type
        VariableDataStruct* inner_data; // Typed pointer to nested data (replaces void*)
    } general;
    struct {  // For VAR_STRUCTURE
        StructMember* members;         // Members: name, type, optional default
        size_t member_count;
    } structure;
} VariableData;

// Wrapper struct to enable recursion
struct VariableDataStruct {
    VariableData data;  // Contains the union
};

// Enum for binary operators (arithmetic and comparisons)
typedef enum {
    BINOP_ADD,      // +
    BINOP_SUB,      // -
    BINOP_MUL,      // *
    BINOP_DIV,      // /
    BINOP_EQ,       // ==
    BINOP_NE,       // <>
    BINOP_LT,       // <
    BINOP_GT,       // >
    BINOP_LE,       // <=
    BINOP_GE,      // >=
    BINOP_AND,
    BINOP_OR
} BinaryOpType;

// Enum for unary operators
typedef enum {
    UNOP_NEG        // - (negation)
} UnaryOpType;

// Enum for built-in functions (math, string, conversion, etc.)
typedef enum {
    FUNC_SIN,
    FUNC_ASIN,
    FUNC_COS,
    FUNC_ACOS,
    FUNC_TAN,
    FUNC_ATAN,
    FUNC_SINH,
    FUNC_COSH,
    FUNC_TANH,
    FUNC_LOG,
    FUNC_LN,
    FUNC_EXP,
    FUNC_CEIL,
    FUNC_FLOOR,
    FUNC_ABS,
    FUNC_SQRT,
    FUNC_SQR,
    FUNC_POW,
    FUNC_MOD,
    FUNC_ROUND,
    FUNC_STRFIND,
    FUNC_STRFINDCS,
    FUNC_STRLEN,
    FUNC_STRCMP,
    FUNC_STRCMPCS,
    FUNC_STOF,
    FUNC_STOI,
    FUNC_STOB,
    FUNC_ASC,
    FUNC_ISNUMBER,
    FUNC_ISINTEGER,
    FUNC_ISDOUBLE,
    FUNC_EQUAL,
    FUNC_LESS,
    FUNC_LESSOREQUAL,
    FUNC_GREATER,
    FUNC_GREATEROREQUAL
} FunctionType;

// Enum for expression types (expanded for math expressions)
typedef enum {
    EXPR_LITERAL_INT,
    EXPR_LITERAL_DOUBLE,
    EXPR_LITERAL_STRING,
    EXPR_LITERAL_BOOL,
    EXPR_CONSTANT,         // e.g., PI
    EXPR_VARIABLE_REF,     // Reference to a variable name
    EXPR_UNARY_OP,         // Unary operations (e.g., -expr)
    EXPR_BINARY_OP,        // Binary operations (e.g., expr + expr)
    EXPR_FUNCTION_CALL,    // Function calls (e.g., sin(expr))
    EXPR_ARRAY_INDEX,      // base[index]
    EXPR_MAP_LOOKUP,       // map.key
    EXPR_STRUCT_ACCESS,     // struct.member
} ExpressionType;

// Expression node for values, accesses, and computations (expanded)
typedef struct ExpressionNode {
    ExpressionType type;
    union {
        long int_val;                // EXPR_LITERAL_INT or EXPR_LITERAL_BOOL (0/1)
        double double_val;           // EXPR_LITERAL_DOUBLE or EXPR_CONSTANT (e.g., PI)
        char* string_val;            // EXPR_LITERAL_STRING or EXPR_VARIABLE_REF (name)
        bool bool_val;
        struct {                     // EXPR_UNARY_OP
            UnaryOpType op;
            ExpressionNode* operand;
        } unary;
        struct {                     // EXPR_BINARY_OP
            BinaryOpType op;
            ExpressionNode* left;
            ExpressionNode* right;
        } binary;
        struct {                     // EXPR_FUNCTION_CALL
            FunctionType func;       // Built-in function enum
            ExpressionNode** args;   // Array of arguments (supports varying arity)
            size_t arg_count;
        } func_call;
        struct {                     // EXPR_ARRAY_INDEX
            ExpressionNode* base;
            ExpressionNode* index;
        } array_index;
        struct {                     // EXPR_MAP_LOOKUP
            ExpressionNode* map;
            char* key;
        } map_lookup;
        struct {                     // EXPR_STRUCT_ACCESS
            ExpressionNode* structure;
            char* member;
        } struct_access;
    } data;
} ExpressionNode;

// Updated DeclareVariableNode with type enum and union
typedef struct {
    VariableType var_type;
    char* name;              // Variable name (identifier)
    VariableData data;       // Type-specific details
} DeclareVariableNode;

// Existing structs for parsed data
typedef struct {
    bool no_tables;
    bool no_gui;
    bool auto_commit;
    bool auto_close;
    bool show_gui_for_existing;
    bool no_auto_update;
    bool continue_on_cancel;
    bool has_screen_location;      // Flag for SCREEN_LOCATION
    ExpressionNode* location_option;  // Expression for location string (e.g., literal string)
    ExpressionNode* width;            // Expression for width (e.g., literal number)
    ExpressionNode* height;           // Expression for height (e.g., literal number)
} ConfigElemNode;

typedef struct {
    VariableType var_type;    // Variable type (e.g., VAR_PARAMETER)
    ParameterSubType subtype; // Parameter subtype (e.g., PARAM_DOUBLE)
    char* parameter;          // Storing the parameter (static identifier)
    ExpressionNode* tooltip_message;  // Expression for tooltip (e.g., string literal)
    ExpressionNode* image_name;       // Expression for image name (e.g., string literal)
    bool on_picture;          // true or false for ON_PICTURE option
    ExpressionNode* posX;     // Expression for posX (e.g., numeric literal)
    ExpressionNode* posY;     // Expression for posY (e.g., numeric literal)
} ShowParamNode;

typedef struct {
    ExpressionNode* picture_expr;
}GlobalPictureNode;

typedef struct
{
    ExpressionNode* picture_expr;  // The file name (single or concatenated)
    ExpressionNode* posX_expr;           // X-position as a string (number or identifier)
    ExpressionNode* posY_expr;           // Y-position as a string (number or identifier)
} SubPictureNode;

// Update UserInputParamNode struct
typedef struct {
    ParameterSubType subtype;
    char* parameter;
    ExpressionNode* default_expr;
    char** default_for_params;
    size_t default_for_count;
    ExpressionNode* width;
    ExpressionNode* decimal_places;
    ExpressionNode* model;
    bool required;
    bool no_update;
    ExpressionNode* display_order;
    ExpressionNode* min_value;
    ExpressionNode* max_value;
    ExpressionNode* tooltip_message;
    ExpressionNode* image_name;
    bool on_picture;
    ExpressionNode* posX;
    ExpressionNode* posY;
} UserInputParamNode;

typedef struct {
    ParameterSubType subtype;
    char* parameter;
    bool required;
    ExpressionNode* display_order;
    ExpressionNode* tooltip_message;
    ExpressionNode* image_name;
    bool on_picture;
    ExpressionNode* posX;
    ExpressionNode* posY;
    ExpressionNode* tag;
} CheckboxParamNode;

typedef struct {
    ExpressionNode** types;         // Array of expressions for types (e.g., literals like "AXIS" or variables like "&myType")
    size_t type_count;
    char* reference;                // Reference name (identifier string)
    ExpressionNode* display_order;  // Expression for display_order (numeric)
    bool allow_reselect;            // Flag (no value)
    ExpressionNode* filter_mdl;     // Expression for filter_mdl (variable or array)
    ExpressionNode* filter_feat;    // Expression for filter_feat (variable or array)
    ExpressionNode* filter_geom;    // Expression for filter_geom (variable or array)
    ExpressionNode* filter_ref;     // Expression for filter_ref (variable or array)
    ExpressionNode* filter_identifier; // Expression for filter_identifier (string literal)
    bool select_by_box;             // Flag (no value)
    bool select_by_menu;            // Flag (no value)
    ExpressionNode* include_multi_cad; // Expression for include_multi_cad (e.g., identifier "TRUE" or "FALSE")
    ExpressionNode* tooltip_message;   // Expression for tooltip_message (string literal)
    ExpressionNode* image_name;     // Expression for image_name (string literal)
    bool on_picture;                // Flag (set if ON_PICTURE present)
    ExpressionNode* posX;           // Expression for posX (numeric)
    ExpressionNode* posY;           // Expression for posY (numeric)
    ExpressionNode* tag;            // Expression for tag (numeric, optional)
    ProBoolean is_required;
} UserSelectNode;

typedef struct {
    ExpressionNode** types;         // Array of expressions for types (e.g., literals like "AXIS" or variables like "&myType")
    size_t type_count;
    char* reference;                // Reference name (identifier string)
    ExpressionNode* display_order;  // Expression for display_order (numeric)
    bool allow_reselect;            // Flag (no value)
    ExpressionNode* filter_mdl;     // Expression for filter_mdl (variable or array)
    ExpressionNode* filter_feat;    // Expression for filter_feat (variable or array)
    ExpressionNode* filter_geom;    // Expression for filter_geom (variable or array)
    ExpressionNode* filter_ref;     // Expression for filter_ref (variable or array)
    ExpressionNode* filter_identifier; // Expression for filter_identifier (string literal)
    bool select_by_box;             // Flag (no value)
    bool select_by_menu;            // Flag (no value)
    ExpressionNode* include_multi_cad; // Expression for include_multi_cad (e.g., identifier "TRUE" or "FALSE")
    ExpressionNode* tooltip_message;   // Expression for tooltip_message (string literal)
    ExpressionNode* image_name;     // Expression for image_name (string literal)
    bool on_picture;                // Flag (set if ON_PICTURE present)
    ExpressionNode* posX;           // Expression for posX (numeric)
    ExpressionNode* posY;           // Expression for posY (numeric)
    ExpressionNode* tag;            // Expression for tag (numeric, optional)
    ProBoolean is_required;
} UserSelectOptionalNode;

typedef struct {

    ExpressionNode** types;
    size_t type_count;
    ExpressionNode* max_sel;
    char* array;
    ExpressionNode* display_order;
    bool allow_reselect;
    ExpressionNode* filter_mdl;
    ExpressionNode* filter_feat;
    ExpressionNode* filter_geom;
    ExpressionNode* filter_ref;
    ExpressionNode* filter_identifier;
    bool select_by_box;
    bool select_by_menu;
    ExpressionNode* include_multi_cad;
    ExpressionNode* tooltip_message;
    ExpressionNode* image_name;
    bool on_picture;
    ExpressionNode* posX;
    ExpressionNode* posY;
    ExpressionNode* tag;
}UserSelectMultipleNode;

typedef struct {

    ExpressionNode** types;
    size_t type_count;
    ExpressionNode* max_sel;
    char* array;
    ExpressionNode* display_order;
    bool allow_reselect;
    ExpressionNode* filter_mdl;
    ExpressionNode* filter_feat;
    ExpressionNode* filter_geom;
    ExpressionNode* filter_ref;
    ExpressionNode* filter_identifier;
    bool select_by_box;
    bool select_by_menu;
    ExpressionNode* include_multi_cad;
    ExpressionNode* tooltip_message;
    ExpressionNode* image_name;
    bool on_picture;
    ExpressionNode* posX;
    ExpressionNode* posY;
    ExpressionNode* tag;
}UserSelectMultipleOptionalNode;

typedef struct {
    ParameterSubType subtype;
    char* parameter;
    ExpressionNode** options;
    size_t option_count;
    bool required;
    ExpressionNode* display_order;
    ExpressionNode* tooltip_message;
    ExpressionNode* image_name;
    bool on_picture;
    ExpressionNode* posX;
    ExpressionNode* posY;
} RadioButtonParamNode;

typedef struct
{
    char* parameter;
}InvalidateParamNode;

typedef struct {
    char* identifier; // TABLE_IDENTIFIER (remains char* as a fixed lexical token)
    ExpressionNode* name;            // Optional table name (now an expression, e.g., string literal or variable)
    ExpressionNode** options;        // TABLE_OPTION options (array of expressions for dynamic options)
    int option_count;             // Number of options
    ExpressionNode** sel_strings;    // SEL_STRING parameters (array of expressions for selection strings)
    int sel_string_count;         // Number of SEL_STRING parameters
    ExpressionNode** data_types;     // Data types for columns (array of expressions, e.g., "int" or "&myTypeVar")
    int data_type_count;          // Number of data types
    ExpressionNode*** rows;          // 2D array of row data (outer array for rows, inner arrays for column expressions)
    int row_count;                // Number of rows
    int column_count;             // Number of columns (should match sel_string_count for consistency)
    bool no_autosel;              /* NO_AUTOSEL */
    bool no_filter;               /* NO_FILTER */
    bool depend_on_input;         /* DEPEND_ON_INPUT */
    bool invalidate_on_unselect;  /* INVALIDATE_ON_UNSELECT */
    bool show_autosel;            /* SHOW_AUTOSEL */
    bool filter_rigid;            /* FILTER_RIGID */
    bool array;
    int  filter_only_column;      /* FILTER_ONLY_COLUMN <int>, -1 if not specified */
    int  filter_column;           /* FILTER_COLUMN <int>,      -1 if not specified */
    int  table_height;            /* TABLE_HEIGHT <int>, default 12 */
    bool table_height_set;        /* tracks explicit presence vs. default */

} TableNode;

typedef enum {
    FOR_INTERF_MDL,       // INTERF_MDL model [SOLID_ONLY]
    FOR_INTERF_BODY,      // INTERF_BODY refBody [SOLID_ONLY]
    FOR_INTERF_SURF,      // INTERF_SURF refSurface
    FOR_INTERF_QUILT,     // INTERF_QUILT refQuilt
    FOR_INTERF_QUILT_SOLID, // INTERF_QUILT_SOLID refQuilt
    FOR_OTHER_REFS_IN_FEAT, // OTHER_REFS_IN_FEAT reference [ASK_USER]
    FOR_ALL_REFS_IN_FEAT, // ALL_REFS_IN_FEAT reference
    FOR_OTHER_INSTANCES,  // OTHER_INSTANCES reference
    FOR_ALL_INSTANCES,    // ALL_INSTANCES reference
    FOR_ARRAY,            // ARRAY refArray
    FOR_REVERSE_ARRAY,    // REVERSE_ARRAY refArray
    FOR_MAP,              // MAP refMap
    FOR_REVERSE_MAP,      // REVERSE_MAP refMap
    FOR_FAMINSTANCES,     // FAMINSTANCES refGeneric
    FOR_LIST              // LIST reference1 reference2 ...
} ForOptionType;

typedef struct {
    char* loop_var;             // Reference variable name (e.g., "CUTTED_PLATE<:out>")
    ForOptionType option;       // Iteration type
    ExpressionNode** args;      // Arguments (e.g., model for INTERF_MDL, refArray for ARRAY)
    size_t arg_count;           // Varies by option (e.g., 1 for ARRAY, variable for LIST)
    ExpressionNode** excludes;  // Optional EXCLUDE models (for INTERF_* options)
    size_t exclude_count;
    CommandNode** commands;     // Nested commands in the loop body
    size_t command_count;
} ForNode;

typedef struct {
    ExpressionNode* condition;  // Condition expression (e.g., "a > 10 AND a < 20")
    CommandNode** commands;     // Nested commands for this branch
    size_t command_count;
} IfBranch;

typedef struct {
    IfBranch** branches;        // Array of branches (IF and ELSE_IF)
    size_t branch_count;
    CommandNode** else_commands; // Optional ELSE block
    size_t else_command_count;
    int id;
} IfNode;


typedef struct {
    ExpressionNode* condition;  // Loop condition (e.g., "number <> 0")
    CommandNode** commands;     // Nested commands in the loop body
    size_t command_count;
} WhileNode;

// New struct for assignment node
typedef struct {
    ExpressionNode* lhs;              // Left-hand side (variable name)
    ExpressionNode* rhs;    // Right-hand side expression
    int assign_id;
} AssignmentNode;


typedef union
{
    DeclareVariableNode declare_variable;
    ConfigElemNode config_elem;
    ShowParamNode show_param;
    GlobalPictureNode global_picture;
    SubPictureNode sub_picture;
    UserInputParamNode user_input_param;
    CheckboxParamNode checkbox_param;
    UserSelectNode user_select;
    UserSelectOptionalNode user_select_optional;
    UserSelectMultipleOptionalNode user_select_multiple_optional;
    UserSelectMultipleNode user_select_multiple;
    RadioButtonParamNode radiobutton_param;
    InvalidateParamNode invalidate_param;
    TableNode begin_table;
    WhileNode whilecommand;
    ForNode forcommand;
    AssignmentNode assignment;
    IfNode ifcommand;
    ExpressionNode* expression;
}CommandData;

// Enum for command types
typedef enum {

    COMMAND_DECLARE_VARIABLE,
    COMMAND_CONFIG_ELEM,
    COMMAND_SHOW_PARAM,
    COMMAND_GLOBAL_PICTURE,
    COMMAND_SUB_PICTURE,
    COMMAND_USER_INPUT_PARAM,
    COMMAND_CHECKBOX_PARAM,
    COMMAND_USER_SELECT,
    COMMAND_USER_SELECT_OPTIONAL,
    COMMAND_USER_SELECT_MULTIPLE,
    COMMAND_USER_SELECT_MULTIPLE_OPTIONAL,
    COMMAND_RADIOBUTTON_PARAM,
    COMMAND_BEGIN_TABLE,
    COMMAND_IF,
    COMMAND_FOR,
    COMMAND_WHILE,
    COMMAND_ASSIGNMENT,
    COMMAND_EXPRESSION,
    COMMAND_INVALIDATE_PARAM
    // Add other command types as needed
} CommandType;

// AST node for a parsed command
typedef struct CommandNode {
    CommandType type;
    CommandData* data; // Points to ConfigElemNode, ShowParamNode, etc.
    bool semantic_valid;  // New: Flag to indicate if semantic analysis passed (default true)
} CommandNode;


// Block types
typedef enum {
    BLOCK_ASM,
    BLOCK_GUI,
    BLOCK_TAB
} BlockType;

// Updated Block struct to hold CommandNode pointers
typedef struct {
    BlockType type;
    CommandNode** commands; // Array of pointers to CommandNode
    size_t command_count;
} Block;

typedef struct {
    Block* blocks;
    size_t block_count;
} BlockList;


typedef int (*CommandParser)(Lexer* lexer, size_t* i, CommandData* parsed_data);

typedef struct {
    const char* command_name;
    CommandType type;      // Added to associate with CommandType
    CommandParser parser;
} CommandEntry;

// Function declarations
BlockList parse_blocks(Lexer* lexer, SymbolTable* st);
void free_block_list(BlockList* block_list);
CommandNode* parse_command(Lexer* lexer, size_t* i, SymbolTable* st);
Block* find_block(BlockList* block_list, BlockType type);
bool add_bool_to_map(HashTable* map, const char* key, bool value);
bool add_double_to_map(HashTable* map, const char* key, double value);
bool add_int_to_map(HashTable* map, const char* key, int value);
bool add_string_to_map(HashTable* map, const char* key, char* value);
bool add_string_array_to_map(HashTable* map, const char* key, char** values, size_t count);
void free_expression(ExpressionNode* expr);
char* expression_to_string(ExpressionNode* expr);
ExpressionNode* parse_comparison(Lexer* lexer, size_t* i, SymbolTable* st);




#endif // !SYNTAX_ANALYSIS_H
