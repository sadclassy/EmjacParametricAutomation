#include "utility.h"
#include "symboltable.h"
#include "guicomponent.h"
#include "syntaxanalysis.h"
#include "ScriptExecutor.h"	
#include "GuiLogic.h"
#include "semantic_analysis.h"


ProError require_select(SymbolTable* st, const char* reference)
{
    if (!st || !reference) return PRO_TK_BAD_INPUTS;

    Variable* req = get_symbol(st, "REQUIRED_SELECTS");
    if (!req) {
        req = (Variable*)calloc(1, sizeof(Variable));
        if (!req) return PRO_TK_GENERAL_ERROR;
        req->type = TYPE_ARRAY;
        req->data.array.size = 0;
        req->data.array.elements = NULL;
        set_symbol(st, "REQUIRED_SELECTS", req);
    }
    else if (req->type != TYPE_ARRAY) {
        ProPrintfChar("Error: REQUIRED_SELECTS is not an array\n");
        return PRO_TK_GENERAL_ERROR;
    }

    /* prevent duplicates */
    for (size_t i = 0; i < req->data.array.size; ++i) {
        Variable* it = req->data.array.elements[i];
        if (it && it->type == TYPE_STRING && it->data.string_value &&
            strcmp(it->data.string_value, reference) == 0) {
            return PRO_TK_NO_ERROR;
        }
    }

    Variable* namev = (Variable*)calloc(1, sizeof(Variable));
    if (!namev) return PRO_TK_GENERAL_ERROR;
    namev->type = TYPE_STRING;
    namev->data.string_value = _strdup(reference);
    if (!namev->data.string_value) { free(namev); return PRO_TK_GENERAL_ERROR; }

    size_t n = req->data.array.size + 1;
    Variable** grown = (Variable**)realloc(req->data.array.elements, n * sizeof(*grown));
    if (!grown) { free(namev->data.string_value); free(namev); return PRO_TK_GENERAL_ERROR; }
    req->data.array.elements = grown;
    req->data.array.elements[n - 1] = namev;
    req->data.array.size = n;

    return PRO_TK_NO_ERROR;
}

ProBoolean is_select_satisfied(SymbolTable* st, const char* reference)
{
    if (!st || !reference) return PRO_B_FALSE;
    Variable* v = get_symbol(st, reference);
    if (!v) return PRO_B_FALSE;

    if (v->type == TYPE_ARRAY) {
        return (v->data.array.size > 0) ? PRO_B_TRUE : PRO_B_FALSE;
    }
    if (v->type == TYPE_MAP) {
        Variable* refv = hash_table_lookup(v->data.map, "reference_value");
        if (refv && refv->type == TYPE_REFERENCE && refv->data.reference.reference_value) {
            return PRO_B_TRUE;
        }
    }
    return PRO_B_FALSE;
}

ProError paint_user_select_area(char* dialog, const char* draw_area_id, SymbolTable* st, const char* reference)
{
    if (!dialog || !draw_area_id || !st || !reference) return PRO_TK_BAD_INPUTS;

    size_t count = 0;
    Variable* v = get_symbol(st, reference);
    if (v && v->type == TYPE_ARRAY) count = v->data.array.size;

    ProBoolean ok = is_select_satisfied(st, reference);
    ProUIColor color = ok ? PRO_UI_COLOR_WHITE : PRO_UI_COLOR_RED;

    ProError s = ProUIDrawingareaBackgroundcolorSet(dialog, (char*)draw_area_id, color);  /* existing line */
    if (s == PRO_TK_NO_ERROR) {
        (void)ProUIDrawingareaClear(dialog, (char*)draw_area_id);  /* <-- force repaint */
    }
    return s;
}


ProError fit_pushbutton_to_drawingarea(char* dialog, const char* draw_area_name, const char* button_name)
{
    if (!dialog || !draw_area_name || !button_name)
        return PRO_TK_BAD_INPUTS;

    ProError status;
    int da_w = 0, da_h = 0;

    // Get current drawing area size
    status = ProUIDrawingareaDrawingwidthGet(dialog, (char*)draw_area_name, &da_w);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not get width of drawing area '%s'\n", draw_area_name);
        return status;
    }
    status = ProUIDrawingareaDrawingheightGet(dialog, (char*)draw_area_name, &da_h);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not get height of drawing area '%s'\n", draw_area_name);
        return status;
    }

    // Leave a 1px border on each side => subtract 2 from W/H
    int target_w = (da_w > 2) ? (da_w - 2) : da_w;
    int target_h = (da_h > 2) ? (da_h - 2) : da_h;

    // Respect pushbutton minimum size
    int min_w = 0, min_h = 0;
    status = ProUIPushbuttonMinimumsizeGet(dialog, (char*)button_name, &min_w, &min_h);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not get minimum size for '%s'\n", button_name);
        return status;
    }
    if (target_w < min_w) target_w = min_w;
    if (target_h < min_h) target_h = min_h;

    // Position at (1,1) so the 1px frame of the drawing area remains visible
    status = ProUIPushbuttonPositionSet(dialog, (char*)button_name, 1, 1);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set position for pushbutton '%s'\n", button_name);
        return status;
    }

    // Apply final size
    status = ProUIPushbuttonSizeSet(dialog, (char*)button_name, target_w, target_h);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set size for pushbutton '%s' (w=%d, h=%d)\n",
            button_name, target_w, target_h);
        return status;
    }

    return PRO_TK_NO_ERROR;
}

ProError UserSelectResizeCallback(char* dialog, char* component, ProAppData app_data)
{
    (void)component; // component is the drawing area that fired the callback
    ButtonFitData* data = (ButtonFitData*)app_data;
    if (!dialog || !data) return PRO_TK_BAD_INPUTS;

    // Recompute and reapply sizing each time the DA is repainted/resized
    return fit_pushbutton_to_drawingarea(dialog, data->draw_area, data->button_id);
}


/*=================================================*\
* 
* USER_INPUT_PARAM SECTION FOR LOGICAL FUNCTIONS    
* 
* 
\*=================================================*/

/* Is a parameter name present in REQUIRED_INPUTS? */
ProBoolean is_required_input(SymbolTable* st, const char* param_name) {
    if (!st || !param_name) return PRO_B_FALSE;
    Variable* req_inputs = get_symbol(st, "REQUIRED_INPUTS");
    if (!req_inputs || req_inputs->type != TYPE_ARRAY) return PRO_B_FALSE;

    ArrayData* arr = &req_inputs->data.array;
    for (size_t i = 0; i < arr->size; ++i) {
        Variable* item = arr->elements[i];
        if (item && item->type == TYPE_STRING && item->data.string_value) {
            if (strcmp(item->data.string_value, param_name) == 0) {
                return PRO_B_TRUE;
            }
        }
    }
    return PRO_B_FALSE;
}

/* Is the variable satisfied (by your existing rules)? */
ProBoolean is_input_satisfied(const Variable* var) {
    if (!var) return PRO_B_FALSE;

    switch (var->type) {
    case TYPE_STRING:
        return (var->data.string_value && var->data.string_value[0] != '\0') ? PRO_B_TRUE : PRO_B_FALSE;
    case TYPE_INTEGER:
    case TYPE_BOOL:
        return (var->data.int_value != 0) ? PRO_B_TRUE : PRO_B_FALSE;
    case TYPE_DOUBLE:
        return (var->data.double_value != 0.0) ? PRO_B_TRUE : PRO_B_FALSE;
    default:
        return PRO_B_FALSE;
    }
}

/* Paint one input panel based on satisfied/required status */
ProError paint_one_input(char* dialog, SymbolTable* st, const char* param_name) {
    if (!dialog || !st || !param_name) return PRO_TK_BAD_INPUTS;

    /* Only paint if it's actually required */
    if (is_required_input(st, param_name) == PRO_B_FALSE) {
        return PRO_TK_NO_ERROR;
    }

    /* Find current variable value */
    Variable* var = get_symbol(st, param_name);
    if (!var) return PRO_TK_NO_ERROR; /* treat missing as noop */

    /* Use the INPUT PANEL id, not the drawing area */
    char input_id[128];
    snprintf(input_id, sizeof(input_id), "input_panel_%s", param_name);

    /* Decide color: red if missing, default if satisfied */
    const ProUIColor color = (is_input_satisfied(var) == PRO_B_TRUE)
        ? PRO_UI_COLOR_WHITE
        : PRO_UI_COLOR_RED;

    ProError s = ProUIInputpanelBackgroundcolorSet(dialog, input_id, color);
    if (s != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Could not set background color for '%s'\n", input_id);
    }


    return s;
}

/* Ensure REQUIRED_INPUTS is an array; append a string element if not present yet */
ProError require_input(SymbolTable* st, const char* param_name) {
    if (!st || !param_name) return PRO_TK_BAD_INPUTS;

    Variable* req = get_symbol(st, "REQUIRED_INPUTS");
    if (!req) {
        req = (Variable*)calloc(1, sizeof(Variable));
        if (!req) return PRO_TK_GENERAL_ERROR;
        req->type = TYPE_ARRAY;
        req->data.array.size = 0;
        req->data.array.elements = NULL;
        set_symbol(st, "REQUIRED_INPUTS", req);
    }
    else if (req->type != TYPE_ARRAY) {
        ProPrintfChar("Error: REQUIRED_INPUTS is not an array\n");
        return PRO_TK_GENERAL_ERROR;
    }

    /* Prevent duplicates */
    for (size_t i = 0; i < req->data.array.size; ++i) {
        Variable* it = req->data.array.elements[i];
        if (it && it->type == TYPE_STRING && it->data.string_value &&
            strcmp(it->data.string_value, param_name) == 0) {
            return PRO_TK_NO_ERROR;
        }
    }

    Variable* namev = (Variable*)calloc(1, sizeof(Variable));
    if (!namev) return PRO_TK_GENERAL_ERROR;
    namev->type = TYPE_STRING;
    namev->data.string_value = _strdup(param_name);
    if (!namev->data.string_value) { free(namev); return PRO_TK_GENERAL_ERROR; }

    size_t n = req->data.array.size + 1;
    Variable** grown = (Variable**)realloc(req->data.array.elements, n * sizeof(*grown));
    if (!grown) { free(namev->data.string_value); free(namev); return PRO_TK_GENERAL_ERROR; }
    req->data.array.elements = grown;
    req->data.array.elements[n - 1] = namev;
    req->data.array.size = n;

    return PRO_TK_NO_ERROR;
}

/* Repaint ALL required inputs (call whenever anything changes) */
ProError refresh_required_input_highlights(char* dialog, SymbolTable* st) {
    if (!dialog || !st) return PRO_TK_BAD_INPUTS;

    Variable* req_inputs = get_symbol(st, "REQUIRED_INPUTS");
    if (!req_inputs || req_inputs->type != TYPE_ARRAY) return PRO_TK_NO_ERROR;

    ProError final_status = PRO_TK_NO_ERROR;
    ArrayData* arr = &req_inputs->data.array;
    for (size_t i = 0; i < arr->size; ++i) {
        Variable* item = arr->elements[i];
        if (!item || item->type != TYPE_STRING || !item->data.string_value) continue;
        ProError s = paint_one_input(dialog, st, item->data.string_value);
        if (s != PRO_TK_NO_ERROR) final_status = s;
    }
    return final_status;
}


/*=================================================*\
* 
* SHOW_PARAM GUI LOGIC
* 
* 
\*=================================================*/
// Centralized SHOW_PARAM label refresh (tries both on-layout and on-picture IDs)
/*=================================================*
 *
 * SHOW_PARAM GUI LOGIC
 *
 *=================================================*/
ProBoolean update_show_param_label_text(char* dialog, const char* parameter, const Variable* var)
{
    if (!dialog || !parameter || !var) return PRO_B_FALSE;

    wchar_t* w_parameter = char_to_wchar(parameter);
    wchar_t* w_value = variable_value_to_wstring(var);
    if (!w_parameter || !w_value) {
        free(w_parameter);
        free(w_value);
        return PRO_B_FALSE;
    }

    wchar_t label_text[200];
    swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls (%ls)", w_parameter, w_value);

    /* Support both potential IDs so we don't miss existing dialogs */
    const char* ids[2] = { "show_label_%s", "label_%s" }; /* legacy fallback */
    ProBoolean updated = PRO_B_FALSE;

    for (int i = 0; i < 2; ++i) {
        char idbuf[128];
        snprintf(idbuf, sizeof(idbuf), ids[i], parameter);

        wchar_t* existing = NULL;
        if (ProUILabelTextGet(dialog, idbuf, &existing) == PRO_TK_NO_ERROR) {
            (void)ProUILabelTextSet(dialog, idbuf, label_text);
            if (existing) ProWstringFree(existing);
            updated = PRO_B_TRUE;
        }
    }

    /* Print only if we actually refreshed a SHOW_PARAM label */
    if (updated == PRO_B_TRUE) {
        ProPrintfChar("SHOW_PARAM refresh: %s -> ", parameter);
        ProPrintf(L"%ls\n", label_text);
    }

    free(w_parameter);
    free(w_value);
    return updated;
}

 // Print the canonical "param := value" after an update.
 void debug_print_symbol_update(const char* param_name, const Variable* var)
 {
     if (!param_name || !var) return;

     switch (var->type) {
     case TYPE_INTEGER:
         ProPrintfChar("Updated '%s' := %d", param_name, var->data.int_value);
         break;
     case TYPE_BOOL:
         ProPrintfChar("Updated '%s' := %d", param_name, var->data.int_value ? 1 : 0);
         break;
     case TYPE_DOUBLE: {
         // Use a compact, precise format
         char buf[64];
         snprintf(buf, sizeof(buf), "%.15g", var->data.double_value);
         ProPrintfChar("Updated '%s' := %s", param_name, buf);
         break;
     }
     case TYPE_STRING:
         ProPrintfChar("Updated '%s' := \"%s\"", param_name,
             var->data.string_value ? var->data.string_value : "");
         break;
     default:
         ProPrintfChar("Updated '%s' (unsupported type %d)", param_name, (int)var->type);
         break;
     }
 }




 /*=================================================*\
 * 
 * SUB_PICTURE GUI_LOGIC
 * (walking the IF command in real-time)
 * 
 * 
 \*=================================================*/

 // Walk GUI and rebuild only SUB_PICTURES based on current symbols
 ProError rebuild_sub_pictures_only(Block* gui_block, SymbolTable* st)
 {
     if (!gui_block) return PRO_TK_NO_ERROR;

     for (size_t i = 0; i < gui_block->command_count; ++i) {
         CommandNode* cmd = gui_block->commands[i];
         switch (cmd->type)
         {
         case COMMAND_DECLARE_VARIABLE: {
             DeclareVariableNode* dv = (DeclareVariableNode*)cmd->data;
             if (dv && dv->name) {
                 /* force reset to declared default each rebuild */
                 remove_symbol(st, dv->name);
                 (void)execute_declare_variable(dv, st);
             }
         } break;

         case COMMAND_ASSIGNMENT:
             (void)execute_assignment((AssignmentNode*)cmd->data, st, NULL);
             break;

         case COMMAND_SUB_PICTURE:
             (void)execute_sub_picture((SubPictureNode*)cmd->data, st);
             break;

         case COMMAND_IF: {
             IfNode* ifn = (IfNode*)cmd->data;
             bool matched = false;
             for (size_t b = 0; b < ifn->branch_count; ++b) {
                 IfBranch* br = ifn->branches[b];
                 Variable* cond_val = NULL;
                 if (evaluate_expression(br->condition, st, &cond_val) == 0 && cond_val) {
                     bool cond_true = false;
                     if (cond_val->type == TYPE_BOOL || cond_val->type == TYPE_INTEGER)
                         cond_true = (cond_val->data.int_value != 0);
                     else if (cond_val->type == TYPE_DOUBLE)
                         cond_true = (cond_val->data.double_value != 0.0);
                     free_variable(cond_val);

                     if (cond_true) {
                         Block temp = { 0 };
                         temp.command_count = br->command_count;
                         temp.commands = br->commands;
                         (void)rebuild_sub_pictures_only(&temp, st);
                         matched = true;
                         break;
                     }
                 }
             }
             if (!matched && ifn->else_command_count > 0) {
                 Block temp = { 0 };
                 temp.command_count = ifn->else_command_count;
                 temp.commands = ifn->else_commands;
                 (void)rebuild_sub_pictures_only(&temp, st);
             }
         } break;

         default:
             break;
         }
     }
     return PRO_TK_NO_ERROR;
 }

