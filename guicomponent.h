#ifndef GUI_COMPONENT_H
#define GUI_COMPONENT_H


#include "utility.h"
#include "symboltable.h"
#include "syntaxanalysis.h"




typedef struct {
    SymbolTable* st;
    char* param_name;
} CheckboxData;

typedef struct {
    ParameterSubType subtype;
    char* last_valid;  // Dynamically allocated string holding the last valid input
    ProBoolean in_callback;  // Reentrancy guard for input changes
    ProBoolean in_activate;  // Reentrancy guard for activation (submission)
    SymbolTable* st;         // Pointer to symbol table for updates
    char* parameter;         // Parameter name for variable lookup
} InputFilterData;

typedef struct {
    SymbolTable* st;
    char* filter;  // Existing
    UserSelectNode* node;  // Existing
    CreoReferenceType* allowed_types;  // New: Array of allowed enums (e.g., CREO_SURFACE, CREO_CURVE)
    size_t type_count;  // New: Number of allowed types
    char draw_area_id[128];
    char button_id[128];
} UserSelectData;

typedef struct {
    SymbolTable* st;
    char reference[128];  // Sufficient size for reference strings
} UpdateData;

typedef struct {
    SymbolTable* st;
    char* filter;  // Existing
    UserSelectOptionalNode* node;  // Existing
    CreoReferenceType* allowed_types;  // New: Array of allowed enums (e.g., CREO_SURFACE, CREO_CURVE)
    size_t type_count;  // New: Number of allowed types
    char draw_area_id[128];
    char button_id[128];
} UserSelectOptionalData;

typedef struct {
    SymbolTable* st;
    char* filter;  // Existing
    UserSelectMultipleNode* node;  // Existing
    CreoReferenceType* allowed_types;  // New: Array of allowed enums (e.g., CREO_SURFACE, CREO_CURVE)
    size_t type_count;  // New: Number of allowed types
    char draw_area_id[128];
    char button_id[128];
} UserSelectMultipleData;

typedef struct {
    SymbolTable* st;
    char* filter;  // Existing
    UserSelectMultipleOptionalNode* node;  // Existing
    CreoReferenceType* allowed_types;  // New: Array of allowed enums (e.g., CREO_SURFACE, CREO_CURVE)
    size_t type_count;  // New: Number of allowed types
    char draw_area_id[128];
    char button_id[128];
} UserSelectMultipleOptionalData;

typedef struct {
    SymbolTable* st;     // Pointer to the symbol table for variable updates
    char* parameter;     // Parameter name for the variable to update
    RadioButtonParamNode node;
} RadioSelectData;


ProError draw_global_picture(char* dialog, char* component, ProAppData app_data);
ProError draw_sub_pictures(char* dialog, char* component, ProAppData app_data);
ProError OnPictureShowParam(char* dialog, char* draw_area_name, ShowParamNode* node, SymbolTable* st);
ProError addShowParam(char* dialog_name, char* parent_layout_name, ShowParamNode* node, int* current_row, int column, SymbolTable* st);
ProError OnPictureCheckboxParam(char* dialog, char* draw_area_name, CheckboxParamNode* node, SymbolTable* st);
ProError addCheckboxParam(char* dialog_name, char* parent_layout_name, CheckboxParamNode* cbNode, int* current_row, int column, SymbolTable* st);
ProError addUserInputParam(char* dialog_name, char* parent_layout_name, UserInputParamNode* node, int* current_row, int column, SymbolTable* st);
ProError addRadioButtonParam(char* dialog_name, char* parent_layout_name, RadioButtonParamNode* node, int* current_row, int column, SymbolTable* st);
ProError addUserSelect(char* dialog_name, char* parent_layout_name, UserSelectNode* node, int* current_row, int column, SymbolTable* st);
ProError addUserSelectOptional(char* dialog_name, char* parent_layout_name, UserSelectOptionalNode* node, int* current_row, int column, SymbolTable* st);
ProError addUserSelectMultipleOptional(char* dialog_name, char* parent_layout_name, UserSelectMultipleOptionalNode* node, int* current_row, int column, SymbolTable* st);
ProError addUserSelectMultiple(char* dialog_name, char* parent_layout_name, UserSelectMultipleNode* node, int* current_row, int column, SymbolTable* st);
ProError addpicture(char* dialog, char* component, ProAppData app_data);
ProError MyPostManageCallback(char* dialog, char* component, ProAppData app_data);
ProError PushButtonAction(char* dialog, char* component, ProAppData app_data);
ProError CloseCallback(char* dialog_name, char* component, ProAppData app_data);
ProError validate_ok_button(char* dialog, SymbolTable* st);
wchar_t* variable_value_to_wstring(const Variable* var);
void set_bool_in_map(HashTable* map, const char* key, int on);
ProError set_user_select_enabled(char* dialog, SymbolTable* st, const char* reference, ProBoolean enabled, ProBoolean required);



#endif // !GUI_COMPONENT_H

