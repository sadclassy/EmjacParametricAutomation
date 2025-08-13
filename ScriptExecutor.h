#ifndef SCRIPT_EXECUTOR_H
#define SCRIPT_EXECUTOR_

#include "symboltable.h"
#include "syntaxanalysis.h"
#include "utility.h"


typedef void(*ExecuteFunction)(void* data, SymbolTable* st, BlockList* block_list);

// Dialog structure
typedef struct
{
    char* dialog_name;
    char* main_layout_name;
    char* table_layout_name;
    char* confirmation_layout_name;
    int global_row_counter;
    Block* gui_block;  // Added for access during updates
    SymbolTable* st;

    struct
    {
        char** refs;
        size_t count;
        size_t capacity;
    } required_user_select;

    struct
    {
        int initialized;
        int row;
        char name[100];
    } show_param_layout;
    struct
    {
        int initialized;
        int row;
        char name[100];
    } user_input_layout;
    struct
    {
        int initialized;
        int row;
        char name[100];
    } radiobutton_layout;
    struct
    {
        int initialized;
        int row;
        char name[100];
    } checkbox_layout;
    struct
    {
        int initialized;
        char name[100];
        int s_us_grid_initialized;
        int row;
    } user_select_layout;

} DialogState;


ProError execute_command(CommandNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_declare_variable(DeclareVariableNode* node, SymbolTable* st);
ProError execute_assignment(AssignmentNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_sub_picture(SubPictureNode* node, SymbolTable* st);

void EPA_ReactiveRefresh(void);

#endif // !SCRIPT_EXECUTOR_H
