#ifndef SCRIPT_EXECUTOR_H
#define SCRIPT_EXECUTOR_

#include "symboltable.h"
#include "syntaxanalysis.h"
#include "utility.h"

#define MAX_SUBTABLE_LEVELS 20


typedef void(*ExecuteFunction)(void* data, SymbolTable* st, BlockList* block_list);


enum {
    SLOT_SHOW_PARAM = 0,
    SLOT_CHECKBOX_PARAM = 1,
    SLOT_USER_INPUT_PARAM = 2,
    SLOT_RADIOBUTTON_PARAM = 3,
    SLOT_USER_SELECT = 4,
    SLOT_COUNT = 5
};

typedef struct {
    int computed;                 /* 0/1 */
    unsigned present_mask;        /* bit i set => slot i present off-picture */
    int slot_to_dense[SLOT_COUNT];/* -1 if absent; otherwise 0..dense_count-1 */
    int dense_count;              /* number of occupied columns */
} ColumnPlan;

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
    ProBoolean dirty;
    ColumnPlan column_plan;
    char* root_drawarea_id;
    char* root_table_id;




    struct { char** refs; size_t count; size_t capacity; } required_user_select;
    struct { int initialized; int row; char name[100]; } show_param_layout;
    struct { int initialized; int row; char name[100]; } user_input_layout;
    struct { int initialized; int row; char name[100]; } radiobutton_layout;
    struct { int initialized; int row; char name[100]; } checkbox_layout;
    struct { int initialized; char name[100]; int s_us_grid_initialized; int s_us_gridO_initialize; int s_us_grid1_initialized; int s_us_gridM_initialized; int row; } user_select_layout;
    struct { int initialized; char name[100]; int column; int row; } indvidual_table;
} DialogState;


/* -------------------- NEW: unified IF execution context -------------------- */
typedef struct {
    SymbolTable* st;
    BlockList* block_list;
    DialogState* ui;
    int reactive;  // 0 for initial execution (build UI + logic), 1 for reactive (logic only, skip UI creation)
} ExecContext;




/* Unified walkers */
ProError exec_command_in_context(CommandNode* node, ExecContext* ctx);
ProError execute_if_ctx(IfNode* node, ExecContext* ctx);
/* -------------------------------------------------------------------------- */

ProError execute_command(CommandNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_declare_variable(DeclareVariableNode* node, SymbolTable* st);
ProError execute_assignment(AssignmentNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_sub_picture(SubPictureNode* node, SymbolTable* st);
int if_gate_id_of(IfNode* n, SymbolTable* st);
int st_get_int(SymbolTable* st, const char* key, int* out);
void st_put_int(SymbolTable* st, const char* key, int value);
void EPA_ReactiveRefresh();

#endif // !SCRIPT_EXECUTOR_H
