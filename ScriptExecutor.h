#ifndef SCRIPT_EXECUTOR_H
#define SCRIPT_EXECUTOR_

#include "symboltable.h"
#include "syntaxanalysis.h"
#include "utility.h"

#define MAX_SUBTABLE_LEVELS 20


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
    Block* tab_block;
    SymbolTable* st;
    int  root_table_built;           /* 0/1 */
    char root_identifier[128];       /* optional: which table to build as root */
    bool needs_show_param;
    bool needs_checkbox;
    bool needs_user_input;
    bool needs_radiobutton;
    bool needs_user_select;
    bool needs_picture;
    int num_param_sections;



    struct { char** refs; size_t count; size_t capacity; } required_user_select;
    struct { int initialized; int row; char name[100]; } show_param_layout;
    struct { int initialized; int row; char name[100]; } user_input_layout;
    struct { int initialized; int row; char name[100]; } radiobutton_layout;
    struct { int initialized; int row; char name[100]; } checkbox_layout;
    struct { int initialized; char name[100]; int s_us_grid_initialized; int s_us_gridO_initialize; int s_us_grid1_initialized; int s_us_gridM_initialized; int row; } user_select_layout;
    struct { int initialized; char name[100]; int column; int row; } indvidual_table;
} DialogState;


/* -------------------- NEW: unified IF execution context -------------------- */
typedef struct ExecContext {
    SymbolTable* st;
    BlockList* block_list;  /* may be NULL */
    DialogState* ui;          /* NULL for non-GUI */
} ExecContext;

/* Unified walkers */
ProError exec_command_in_context(CommandNode* node, ExecContext* ctx);
ProError execute_if_ctx(IfNode* node, ExecContext* ctx);
/* -------------------------------------------------------------------------- */

ProError execute_command(CommandNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_declare_variable(DeclareVariableNode* node, SymbolTable* st);
ProError execute_assignment(AssignmentNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_sub_picture(SubPictureNode* node, SymbolTable* st);

void EPA_ReactiveRefresh(void);

#endif // !SCRIPT_EXECUTOR_H
