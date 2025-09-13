#ifndef GUI_LOGIC_H
#define GUI_LOGIC_H

#include "utility.h"
#include "symboltable.h"	


/* Keep the user-select pushbutton fitted during repaints/resizes */
typedef struct {
    char draw_area[128];
    char button_id[128];
} ButtonFitData;

typedef struct {
    ProBoolean do_declares;
    ProBoolean do_assigns;
    ProBoolean do_subpics;
} GuiRebuildOps;

ProError set_checkbox_param_enabled(char* dialog, SymbolTable* st, const char* param, ProBoolean enabled);



/* NEW: select helpers */
ProBoolean is_selection_equal(ProSelection sel1, ProSelection sel2);
ProError UserSelectCallback(char* dialog, char* component, ProAppData app_data);
ProError UserSelectUpdateCallback(char* dialog, char* component, ProAppData app_data);
ProError UserSelectMultipleCallback(char* dialog, char* component, ProAppData app_data);
ProError UserSelectMultipleOptionalCallback(char* dialog, char* component, ProAppData app_data);
ProError require_select(SymbolTable* st, const char* reference);
ProError unrequire_select(SymbolTable* st, const char* reference);
ProError UserSelectResizeCallback(char* dialog, char* component, ProAppData app_data);
ProError fit_pushbutton_to_drawingarea(char* dialog, const char* draw_area_name, const char* button_name);
ProBoolean is_select_satisfied(SymbolTable* st, char* reference);
ProError UserSelectUpdateCallback(char* dialog, char* component, ProAppData app_data);
ProError UserSelectOptionalUpdateCallback(char* dialog, char* component, ProAppData app_data);
ProError set_user_select_optional_enabled(char* dialog, SymbolTable* st, const char* reference, ProBoolean enabled, ProBoolean required);
int var_to_bool(const Variable* v, int dflt);



/*=================================================*\
* 
* USER_INPUT_PARAM logical headers
* 
* 
\*=================================================*/
ProError refresh_required_input_highlights(char* dialog, SymbolTable* st);
ProError require_input(SymbolTable* st, const char* param_name);
ProError set_inputpanel_param_enabled(char* dialog, SymbolTable* st, const char* param, ProBoolean enabled);


/*=================================================*\
* 
* SHOW_PARAM logical headers
* 
* 
\*=================================================*/
ProError set_show_param_enabled(char* dialog, SymbolTable* st, const char* parameter, ProBoolean enabled);
ProBoolean OnPicture_update_show_param_label_text(char* dialog, const char* parameter, const Variable* var);
ProBoolean update_show_param_label_text(char* dialog, const char* parameter, const Variable* var); 
ProError update_show_param_label(char* dialog, const char* param_name, const Variable* var, ProBoolean on_picture);
void debug_print_symbol_update(const char* param_name, const Variable* var);
ProError refresh_all_show_params(Block* blk, char* dialog_name, SymbolTable* st);


/*=================================================*\
* 
* RADIOBUTTON LOGICAL HEADERS
* 
* 
\*=================================================*/
ProError set_radiobutton_param_enabled(char* dialog, SymbolTable* st, const char* param, ProBoolean enabled);

/*=================================================*\
* 
* SUB_PICTURE LOGICAL HEADERS
* 
* 
\*=================================================*/
ProError rebuild_sub_pictures_only(Block* gui_block, SymbolTable* st);

/* NEW: UI param helpers */
ProError track_ui_param(SymbolTable* st, const char* param_name);
ProBoolean is_ui_param(SymbolTable* st, const char* param_name);

//helpers

ProError update_assignments_only_impl(Block* blk, SymbolTable* st, int target_if_id, int in_winner);
ProError update_assignments_only(Block* gui_block, SymbolTable* st);





#endif // !GUI_LOGIC_H
