#include "symboltable.h"
#include "utility.h"
#include "syntaxanalysis.h"


/* Forward declaration to allow mutual recursion */
void free_variable(Variable* var);


// Static hash function for strings using djb2 algoithm
static unsigned long hash_function(const char* str)
{
	unsigned long hash = 5381;
	int c;
	while ((c = *str++) != 0)
	{
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

// hash table creation
HashTable* create_hash_table(size_t initial_size)
{
	HashTable* ht = malloc(sizeof(HashTable));
	if (!ht) return NULL;

	ht->buckets = calloc(initial_size, sizeof(HashEntry*));
	if (!ht->buckets)
	{
		free(ht);
		return NULL;
	}
	ht->size = initial_size;
	ht->count = 0;

	// New: Initialize key_order similarly to SymbolTable
	ht->key_order = malloc(initial_size * sizeof(char*));
	if (!ht->key_order)
	{
		free(ht->buckets);
		free(ht);
		return NULL;
	}
	ht->key_count = 0;
	ht->key_capacity = initial_size;

	return ht;
}

// Insert or update a key-value pair in the hash table
void hash_table_insert(HashTable* ht, const char* key, Variable* value)
{
	if (!ht || !key || !value) return;

	unsigned long hash = hash_function(key);
	size_t index = hash % ht->size;
	HashEntry* entry = ht->buckets[index];

	// Check if key already exists
	while (entry != NULL)
	{
		if (strcmp(entry->key, key) == 0)
		{
			// Key found, update value (old value must be freed by caller if needed)
			entry->value = value;
			return;
		}
		entry = entry->next;
	}
	// Key not found, create new entry
	HashEntry* new_entry = malloc(sizeof(HashEntry));
	if (!new_entry) return;

	new_entry->key = _strdup(key);
	if (!new_entry->key)
	{
		free(new_entry);
		return;
	}

	new_entry->value = value;
	new_entry->next = ht->buckets[index];
	ht->buckets[index] = new_entry;
	ht->count++;

	// New: Add to key_order only if new key
	if (ht->key_count >= ht->key_capacity) {
		size_t new_capacity = ht->key_capacity * 2;
		char** new_key_order = realloc(ht->key_order, new_capacity * sizeof(char*));
		if (!new_key_order) {
			// Non-fatal: Proceed without updating order
			return;
		}
		ht->key_order = new_key_order;
		ht->key_capacity = new_capacity;
	}
	ht->key_order[ht->key_count] = _strdup(key);
	if (ht->key_order[ht->key_count]) {
		ht->key_count++;
	}
}

// Look up a value by key in the hash table
Variable* hash_table_lookup(HashTable* ht, const char* key)
{
	if (!ht || !key) return NULL;

	unsigned long hash = hash_function(key);
	size_t index = hash % ht->size;
	HashEntry* entry = ht->buckets[index];

	while (entry != NULL)
	{
		if (strcmp(entry->key, key) == 0)
		{
			return entry->value;
		}
		entry = entry->next;
	}
	return NULL;
}

// Remove a key-value pair from the hash table;
void hash_table_remove(HashTable* ht, const char* key)
{
	if (!ht || !key) return;

	unsigned long hash = hash_function(key);
	size_t index = hash % ht->size;
	HashEntry* entry = ht->buckets[index];
	HashEntry* prev = NULL;

	while (entry != NULL)
	{
		if (strcmp(entry->key, key) == 0)
		{
			if (prev == NULL)
			{
				ht->buckets[index] = entry->next;
			}
			else
			{
				prev->next = entry->next;
			}
			free(entry->key);
			free_variable(entry->value);
			free(entry);
			ht->count--;

			// New: Remove from key_order
			for (size_t i = 0; i < ht->key_count; i++) {
				if (strcmp(ht->key_order[i], key) == 0) {
					free(ht->key_order[i]);
					for (size_t j = i; j < ht->key_count - 1; j++) {
						ht->key_order[j] = ht->key_order[j + 1];
					}
					ht->key_count--;
					break;
				}
			}
			return;
		}
		prev = entry;
		entry = entry->next;
	}
}

// Remove a symbol from the symbol table; frees the Variable if found
void remove_symbol(SymbolTable* st, const char* name) {
	if (!st || !name) return;

	// Remove from hash table
	hash_table_remove(st->table, name);

	// Remove from key_order (linear search and shift)
	for (size_t i = 0; i < st->key_count; i++) {
		if (strcmp(st->key_order[i], name) == 0) {
			free(st->key_order[i]);  // Free duplicated key
			// Shift remaining keys
			for (size_t j = i; j < st->key_count - 1; j++) {
				st->key_order[j] = st->key_order[j + 1];
			}
			st->key_count--;
			break;
		}
	}
}

int add_var_to_map(HashTable* ht, const char* key, Variable* var)
{
	if (!ht || !key || !var)
	{
		return 0;
	}

	// Insert the variable into the hash table
	hash_table_insert(ht, key, var);

	// Since hash_table_insert is void and handles allocation failures internally by return early,
	// assume success unless enhanced error checking is added to hash_table_insert in the future.
	return 1;

}

// Free the hash table and its contents
void free_hash_table(HashTable* ht)
{
	if (!ht) return;

	for (size_t i = 0; i < ht->size; i++)
	{
		HashEntry* entry = ht->buckets[i];
		while (entry != NULL)
		{
			HashEntry* next = entry->next;
			free(entry->key);
			free_variable(entry->value);
			free(entry);
			entry = next;
		}
	}
	free(ht->buckets);

	// New: Free key_order
	if (ht->key_order) {
		for (size_t i = 0; i < ht->key_count; i++) {
			free(ht->key_order[i]);
		}
		free(ht->key_order);
	}
	free(ht);
}

void free_variable(Variable* var) {
	if (var == NULL) return;

	switch (var->type) {
	case TYPE_STRING:
	case TYPE_SUBTABLE:  /* free underlying string for subtable refs */
		if (var->data.string_value != NULL) {
			free(var->data.string_value);
		}
		break;

	case TYPE_REFERENCE:
		if (var->data.reference.reference_value) {
			ProSelection sel = (ProSelection)var->data.reference.reference_value;
			ProSelectionFree(&sel);
			var->data.reference.reference_value = (void*)sel;
		}
		break;

	case TYPE_FILE_DESCRIPTOR:
		if (var->data.file_descriptor != NULL) {
			fclose(var->data.file_descriptor);
		}
		break;

	case TYPE_ARRAY:
		if (var->data.array.elements != NULL) {
			for (size_t i = 0; i < var->data.array.size; i++) {
				free_variable(var->data.array.elements[i]);
			}
			free(var->data.array.elements);
		}
		break;

	case TYPE_MAP:
		if (var->data.map != NULL) {
			free_hash_table(var->data.map);
		}
		break;

	case TYPE_STRUCTURE:
		if (var->data.structure != NULL) {
			free_hash_table(var->data.structure);
		}
		break;

	default:
		break;
	}

	free(var);
}

// Create a new Symbol Table
SymbolTable* create_symbol_table(void) {
	SymbolTable* st = malloc(sizeof(SymbolTable));
	if (!st) return NULL;

	st->table = create_hash_table(16);
	if (!st->table) {
		free(st);
		return NULL;
	}

	st->key_order = malloc(16 * sizeof(char*));
	if (!st->key_order) {
		free_hash_table(st->table);
		free(st);
		return NULL;
	}
	st->key_count = 0;
	st->key_capacity = 16;

	// Predefine GIF_DIR as a string variable 
	Variable* gif_dir_var = malloc(sizeof(Variable));
	if (gif_dir_var)
	{
		gif_dir_var->type = TYPE_STRING;
		gif_dir_var->data.string_value = _strdup("C:\\GlobalPicture\\");
		set_symbol(st, "GIF_DIR", gif_dir_var);
	}

	return st;
}

// Set a variable in the symbol table, overwriting and freeing old value if it exists
void set_symbol(SymbolTable* st, const char* name, Variable* var) {
	if (!st || !st->table || !name || !var) return;

	// Check if the key already exists
	Variable* old_var = hash_table_lookup(st->table, name);

	// If the key is new, add it to key_order
	if (!old_var) {
		// Resize key_order if necessary
		if (st->key_count >= st->key_capacity) {
			size_t new_capacity = st->key_capacity * 2;
			char** new_key_order = realloc(st->key_order, new_capacity * sizeof(char*));
			if (!new_key_order) {
				// On failure, proceed without adding to key_order (non-fatal)
				goto insert;
			}
			st->key_order = new_key_order;
			st->key_capacity = new_capacity;
		}
		// Duplicate the key and add it
		st->key_order[st->key_count] = _strdup(name);
		if (!st->key_order[st->key_count]) {
			// On failure, proceed without adding (non-fatal)
			goto insert;
		}
		st->key_count++;
	}

insert:
	// Insert or update the variable in the hash table
	hash_table_insert(st->table, name, var);

	// Free the old variable if it existed
	if (old_var) {
		free_variable(old_var);
	}
}

// Retrieve a variable from the symbol table
Variable *get_symbol(SymbolTable* st, const char* name)
{
	if (!st || !st->table || !name) return NULL;
	return hash_table_lookup(st->table, name);
}


// Free the symbol table and its contents
void free_symbol_table(SymbolTable* st) {
	if (!st) return;

	// Free the hash table
	free_hash_table(st->table);

	// Free the key_order array
	if (st->key_order) {
		for (size_t i = 0; i < st->key_count; i++) {
			free(st->key_order[i]); // Free duplicated strings
		}
		free(st->key_order);
	}

	free(st);
}


// Helper function to print indentation for nested output
static char* get_indent(int indent) {
	static char indent_str[100];
	int spaces = indent * 2;
	for (int i = 0; i < spaces; i++) {
		indent_str[i] = ' ';
	}
	indent_str[spaces] = '\0';
	return indent_str;
}

static void print_variable(const Variable* var, int indent) {
	if (!var) {
		LogOnlyPrintfChar("%sValue: NULL\n", get_indent(indent));
		return;
	}
	switch (var->type) {
	case TYPE_INTEGER:
		LogOnlyPrintfChar("%sType: INTEGER, Value: %d\n", get_indent(indent), var->data.int_value);
		break;
	case TYPE_DOUBLE:
		// Revised: Use %.15g for higher precision to avoid rounding in output
		LogOnlyPrintfChar("%sType: DOUBLE, Value: %.15g\n", get_indent(indent), var->data.double_value);
		break;
	case TYPE_STRING:
		LogOnlyPrintfChar("%sType: STRING, Value: %s\n", get_indent(indent),
			var->data.string_value ? var->data.string_value : "NULL");
		break;
	case TYPE_SUBTABLE:
		LogOnlyPrintfChar("%sType: SUBTABLE, Target: %s\n", get_indent(indent),
			var->data.string_value ? var->data.string_value : "NULL");
		break;
	case TYPE_REFERENCE:
		LogOnlyPrintfChar("%sType: REFERENCE, Allowed Types Count: %zu\n",
			get_indent(indent), var->data.reference.allowed_count);
		for (size_t i = 0; i < var->data.reference.allowed_count; i++) {
			LogOnlyPrintfChar("%s  Allowed Type %zu: %d\n", get_indent(indent + 1), i,
				var->data.reference.allowed_types[i]);
		}
		break;
	case TYPE_FILE_DESCRIPTOR:  // New case for file descriptors
		LogOnlyPrintfChar("%sType: FILE_DESCRIPTOR, Value: %s\n", get_indent(indent),
			var->data.file_descriptor ? "Open file handle" : "NULL");
		break;
	case TYPE_ARRAY: {
		LogOnlyPrintfChar("%sType: ARRAY\n", get_indent(indent));
		ArrayData array = var->data.array;
		for (size_t j = 0; j < array.size; j++) {
			LogOnlyPrintfChar("%sElement %zu:\n", get_indent(indent + 1), j);
			print_variable(array.elements[j], indent + 2);
		}
		break;
	}
	case TYPE_MAP: {
		LogOnlyPrintfChar("%sType: MAP\n", get_indent(indent));
		HashTable* map = var->data.map;
		if (!map || map->key_count == 0) {
			LogOnlyPrintfChar("%sMap is empty or not initialized\n", get_indent(indent + 1));
			return;
		}
		// New: Iterate key_order for insertion-order printing
		for (size_t i = 0; i < map->key_count; i++) {
			const char* key = map->key_order[i];
			Variable* value = hash_table_lookup(map, key);
			if (value) {
				LogOnlyPrintfChar("%sKey: %s\n", get_indent(indent + 1), key);
				print_variable(value, indent + 2);
			}
		}
		break;
	}
	case TYPE_STRUCTURE: {  // New case for structures, treated similarly to maps
		LogOnlyPrintfChar("%sType: STRUCTURE\n", get_indent(indent));
		HashTable* structure = var->data.structure;
		if (!structure || structure->key_count == 0) {
			LogOnlyPrintfChar("%sStructure is empty or not initialized\n", get_indent(indent + 1));
			return;
		}
		for (size_t i = 0; i < structure->key_count; i++) {
			const char* key = structure->key_order[i];
			Variable* value = hash_table_lookup(structure, key);
			if (value) {
				LogOnlyPrintfChar("%sField: %s\n", get_indent(indent + 1), key);
				print_variable(value, indent + 2);
			}
		}
		break;
	}
	default:
		LogOnlyPrintfChar("%sType: UNKNOWN\n", get_indent(indent));
		break;
	}
}

// Function to print the entire symbol table
void print_symbol_table(const SymbolTable* st) {
	if (!st || !st->table) {
		ProPrintf(L"Symbol table is empty or not initialized.\n");
		return;
	}

	ProPrintf(L"Symbol Table Contents:\n");
	for (size_t i = 0; i < st->key_count; i++) {
		const char* key = st->key_order[i];
		Variable* var = hash_table_lookup(st->table, key);
		if (var) {
			LogOnlyPrintfChar("Key: %s\n", key);
			print_variable(var, 1);
		}
	}
}
