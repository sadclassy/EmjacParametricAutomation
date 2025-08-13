#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "utility.h"


// Forward declaration of Variable to resolve circular dependency
typedef struct Variable Variable;
typedef struct ExpressionNode ExpressionNode;

// Named struct for array data
typedef struct {
    struct Variable** elements;
    size_t size;
} ArrayData;

// Hash table entry structure for key-value pairs
typedef struct HashEntry {
    char* key;           // String key
    Variable* value;     // Pointer to Variable
    struct HashEntry* next; // Next entry for collision handling
} HashEntry;

// Custom hash table structure
typedef struct HashTable {
    HashEntry** buckets; // Array of pointers to entries
    size_t size;         // Number of buckets
    size_t count;        // Number of stored entries
} HashTable;

// Variable type enumeration
typedef enum {
    TYPE_INTEGER,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_REFERENCE,
    TYPE_FILE_DESCRIPTOR,
    TYPE_ARRAY,
    TYPE_MAP,
    TYPE_STRUCTURE,
    TYPE_EXPR
} VariableType;

// Variable structure with union for different types
typedef struct Variable {
    VariableType type;
    union {
        int int_value;          // For TYPE_INTEGER, TYPE_BOOL
        double double_value;    // For TYPE_DOUBLE
        char* string_value;     // For TYPE_STRING
        struct {                // For TYPE_REFERENCE (expanded for Creo ProType support)
            ProType* allowed_types;  // Array of allowed ProType (e.g., PRO_AXIS) from USER_SELECT
            size_t allowed_count;    // Number of allowed types (supports multi-type USER_SELECT)
            void* reference_value;   // Creo reference handle (e.g., ProSelection; set at runtime)
        } reference;
        FILE* file_descriptor;  // For TYPE_FILE_DESCRIPTOR
        ArrayData array;        // For TYPE_ARRAY
        HashTable* map;         // For TYPE_MAP
        HashTable* structure;   // For TYPE_STRUCTURE
        ExpressionNode* expr;
    } data;
    HashTable* display_options; // New: Optional display options map (NULL if none)
    int declaration_count;
} Variable;

// Symbol table structure
typedef struct {
    HashTable* table;
    char** key_order;      // Array to store keys in order
    size_t key_count;      // Number of keys in key_order
    size_t key_capacity;   // Allocated size of key_order
} SymbolTable;

// Function prototypes for hash table operations
int add_var_to_map(HashTable* ht, const char* key, Variable* var);
HashTable* create_hash_table(size_t initial_size);
void hash_table_insert(HashTable* ht, const char* key, Variable* value);
Variable* hash_table_lookup(HashTable* ht, const char* key);
void free_hash_table(HashTable* ht);
void free_variable(Variable* var);

// Function prototypes for symbol table operations
SymbolTable* create_symbol_table(void);
void set_symbol(SymbolTable* st, const char* name, Variable* var);
Variable* get_symbol(SymbolTable* st, const char* name);
void remove_symbol(SymbolTable* st, const char* name);
void free_symbol_table(SymbolTable* st);
void print_symbol_table(const SymbolTable* st);
static void print_variable(const Variable* var, int indent);
static void print_indent(int indent);

#endif // !SYMBOL_TABLE_H