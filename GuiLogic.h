#ifndef GUI_LOGIC_H
#define GUI_LOGIC_H

#include "utility.h"
#include "symboltable.h"	


/* Keep the user-select pushbutton fitted during repaints/resizes */
typedef struct {
    char draw_area[128];
    char button_id[128];
} ButtonFitData;

/* NEW: select helpers */
ProError require_select(SymbolTable* st, const char* reference);
ProBoolean is_select_satisfied(SymbolTable* st, const char* reference);
ProError paint_user_select_area(char* dialog, const char* draw_area_id, SymbolTable* st, const char* reference);


ProError UserSelectResizeCallback(char* dialog, char* component, ProAppData app_data);
ProError fit_pushbutton_to_drawingarea(char* dialog, const char* draw_area_name, const char* button_name);

ProError refresh_required_input_highlights(char* dialog, SymbolTable* st);
ProError require_input(SymbolTable* st, const char* param_name);


ProBoolean update_show_param_label_text(char* dialog, const char* parameter, const Variable* var); 
void debug_print_symbol_update(const char* param_name, const Variable* var);


ProError rebuild_sub_pictures_only(Block* gui_block, SymbolTable* st);




#endif // !GUI_LOGIC_H
