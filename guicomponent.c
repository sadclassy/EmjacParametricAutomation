#include "guicomponent.h"
#include "symboltable.h"
#include "syntaxanalysis.h"
#include "semantic_analysis.h"
#include "ScriptExecutor.h"
#include "GuiLogic.h"




// Helper function to get a string representation of the variable type
char* get_variable_type_string(VariableType vtype, ParameterSubType psubtype) {
    switch (vtype) {
    case VAR_PARAMETER:
        switch (psubtype) {
        case PARAM_INT: return "int";
        case PARAM_DOUBLE: return "double";
        case PARAM_STRING: return "string";
        case PARAM_BOOL: return "bool";
        }
        break;
    case VAR_REFERENCE: return "reference";
    case VAR_FILE_DESCRIPTOR: return "file_descriptor";
    case VAR_ARRAY: return "array";
    case VAR_MAP: return "map";
    case VAR_GENERAL: return "general";
    case VAR_STRUCTURE: return "structure";
    }
    return "unknown";
}

// Define the helper function outside epaProSelect (static to limit scope to this file)
static const char* get_item_type_str(ProType type) {
    switch (type) {
    case PRO_EDGE: return "Edge";
    case PRO_SURFACE: return "Surface";
    case PRO_AXIS: return "Axis";
    case PRO_CURVE: return "Curve";
    case PRO_DATUM_PLANE: return "Plane";
        // Add more types as needed based on ProType enum
    default: return "Unknown";
    }
}

// Helper to convert Variable value to wchar_t* (allocated, caller must free)
wchar_t* variable_value_to_wstring(const Variable* var) {
    if (!var) return _wcsdup(L"undefined");

    wchar_t buf[64];  // Sufficient for most values
    switch (var->type) {
    case TYPE_INTEGER:
    case TYPE_BOOL:
        swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%d", var->data.int_value);
        return _wcsdup(buf);
    case TYPE_DOUBLE:
        swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.2f", var->data.double_value);
        return _wcsdup(buf);
    case TYPE_STRING:
        // No quotes, per example
        return char_to_wchar(var->data.string_value ? var->data.string_value : "");
    default:
        return _wcsdup(L"unsupported");
    }
}


/*=================================================*
* 
* Validation helper to enable/disable OK button based on required options
* from components
* 
*=================================================*/
ProError validate_ok_button(char* dialog, SymbolTable* st) {
    if (!dialog || !st) return PRO_TK_BAD_INPUTS;

    bool all_selected = true;

    // Check required radios (existing code)
    Variable* req_radios = get_symbol(st, "REQUIRED_RADIOS");
    if (req_radios && req_radios->type == TYPE_ARRAY && req_radios->data.array.size > 0) {
        ArrayData* array = &req_radios->data.array;
        for (size_t i = 0; i < array->size; i++) {
            Variable* item = array->elements[i];
            if (item->type != TYPE_STRING || !item->data.string_value) continue;

            Variable* radio_var = get_symbol(st, item->data.string_value);
            if (!radio_var) {
                all_selected = false;
                break;
            }
            if (radio_var->type == TYPE_STRING) {
                if (!radio_var->data.string_value || strlen(radio_var->data.string_value) == 0) {
                    all_selected = false;
                    break;
                }
            }
            else if (radio_var->type == TYPE_INTEGER) {
                if (radio_var->data.int_value < 0) {  // Assuming <0 means unselected
                    all_selected = false;
                    break;
                }
            }
            else {
                all_selected = false;
                break;
            }
        }
    }

    // Check required selects (existing code)
    if (all_selected) {  // Only proceed if radios are OK
        Variable* req_selects = get_symbol(st, "REQUIRED_SELECTS");
        if (req_selects && req_selects->type == TYPE_ARRAY && req_selects->data.array.size > 0) {
            ArrayData* array = &req_selects->data.array;
            for (size_t i = 0; i < array->size; i++) {
                Variable* item = array->elements[i];
                if (item->type != TYPE_STRING || !item->data.string_value) continue;

                Variable* select_var = get_symbol(st, item->data.string_value);
                if (!select_var) {
                    all_selected = false;
                    break;
                }
                // Check for valid selection based on storage type
                if (select_var->type == TYPE_MAP) {
                    Variable* ref_val = hash_table_lookup(select_var->data.map, "reference_value");
                    if (!ref_val || ref_val->type != TYPE_REFERENCE || !ref_val->data.reference.reference_value) {
                        all_selected = false;
                        break;
                    }
                }
                else if (select_var->type == TYPE_ARRAY) {
                    if (select_var->data.array.size == 0) {
                        all_selected = false;
                        break;
                    }
                    // Optional: Deep-check array elements are valid references
                    for (size_t j = 0; j < select_var->data.array.size; j++) {
                        Variable* elem = select_var->data.array.elements[j];
                        if (elem->type != TYPE_REFERENCE || !elem->data.reference.reference_value) {
                            all_selected = false;
                            break;
                        }
                    }
                    if (!all_selected) break;
                }
                else {
                    all_selected = false;
                    break;
                }
            }
        }
    }

    // Check required checkboxes (new)
    if (all_selected) {  // Only proceed if previous checks passed
        Variable* req_checkboxes = get_symbol(st, "REQUIRED_CHECKBOXES");
        if (req_checkboxes && req_checkboxes->type == TYPE_ARRAY && req_checkboxes->data.array.size > 0) {
            ArrayData* array = &req_checkboxes->data.array;
            for (size_t i = 0; i < array->size; i++) {
                Variable* item = array->elements[i];
                if (item->type != TYPE_STRING || !item->data.string_value) continue;

                Variable* check_var = get_symbol(st, item->data.string_value);
                if (!check_var) {
                    all_selected = false;
                    break;
                }
                // Validate type and value (checked if != 0)
                if ((check_var->type != TYPE_INTEGER && check_var->type != TYPE_BOOL) || check_var->data.int_value == 0) {
                    all_selected = false;
                    break;
                }
            }
        }
    }

    // Check required inputs (new)
    if (all_selected) {  // Only proceed if previous checks passed
        Variable* req_inputs = get_symbol(st, "REQUIRED_INPUTS");
        if (req_inputs && req_inputs->type == TYPE_ARRAY && req_inputs->data.array.size > 0) {
            ArrayData* array = &req_inputs->data.array;
            for (size_t i = 0; i < array->size; i++) {
                Variable* item = array->elements[i];
                if (item->type != TYPE_STRING || !item->data.string_value) continue;

                Variable* input_var = get_symbol(st, item->data.string_value);
                if (!input_var) {
                    all_selected = false;
                    break;
                }
                bool is_valid = true;
                switch (input_var->type) {
                case TYPE_STRING:
                    if (!input_var->data.string_value || strlen(input_var->data.string_value) == 0) {
                        is_valid = false;
                    }
                    break;
                case TYPE_INTEGER:
                case TYPE_BOOL:
                    if (input_var->data.int_value == 0) {
                        is_valid = false;
                    }
                    break;
                case TYPE_DOUBLE:
                    if (input_var->data.double_value == 0.0) {
                        is_valid = false;
                    }
                    break;
                default:
                    is_valid = false;
                    break;
                }
                if (!is_valid) {
                    all_selected = false;
                    break;
                }
            }
        }
    }

    if (all_selected) {
        return ProUIPushbuttonEnable(dialog, "ok_button");
    }
    else {
        return ProUIPushbuttonDisable(dialog, "ok_button");
    }
}

/*=================================================*\
* 
* GlobalPicture command diplay executable 
* --this displays the global picture within 
* the dialog--
* 
\*=================================================*/
ProError draw_global_picture(char* dialog, char* component, ProAppData app_data) {
    SymbolTable* st = (SymbolTable*)app_data;
    if (!st) return PRO_TK_BAD_INPUTS;
    Variable* pic_var = get_symbol(st, "GLOBAL_PICTURE");
    if (!pic_var || pic_var->type != TYPE_STRING || !pic_var->data.string_value) {
        return PRO_TK_GENERAL_ERROR;
    }
    ProUIPoint mainPoint = { 0, 0 };
    return ProUIDrawingareaImageDraw(dialog, component, pic_var->data.string_value, &mainPoint);
}

ProError draw_sub_pictures(char* dialog, char* component, ProAppData app_data) {
    SymbolTable* st = (SymbolTable*)app_data;
    if (!st) return PRO_TK_BAD_INPUTS;
    Variable* sub_pics_var = get_symbol(st, "SUB_PICTURES");
    if (!sub_pics_var || sub_pics_var->type != TYPE_ARRAY) return PRO_TK_NO_ERROR;  // No sub-pictures: silent success
    ArrayData* array = &sub_pics_var->data.array;
    for (size_t i = 0; i < array->size; i++) {
        Variable* sub_var = array->elements[i];
        if (!sub_var || sub_var->type != TYPE_MAP) continue;
        HashTable* sub_map = sub_var->data.map;

        // Evaluate filename expression
        Variable* fname_expr_var = hash_table_lookup(sub_map, "filename_expr");
        if (!fname_expr_var || fname_expr_var->type != TYPE_EXPR) continue;
        char* file_name = NULL;
        ProError status = evaluate_to_string(fname_expr_var->data.expr, st, &file_name);
        if (status != PRO_TK_NO_ERROR || !file_name) continue;

        // Evaluate posX expression
        Variable* posx_expr_var = hash_table_lookup(sub_map, "posX_expr");
        int pos_x = 0;
        if (posx_expr_var && posx_expr_var->type == TYPE_EXPR) {
            long temp_x;
            status = evaluate_to_int(posx_expr_var->data.expr, st, &temp_x);
            if (status == PRO_TK_NO_ERROR) pos_x = (int)temp_x;
        }

        // Evaluate posY expression
        Variable* posy_expr_var = hash_table_lookup(sub_map, "posY_expr");
        int pos_y = 0;
        if (posy_expr_var && posy_expr_var->type == TYPE_EXPR) {
            long temp_y;
            status = evaluate_to_int(posy_expr_var->data.expr, st, &temp_y);
            if (status == PRO_TK_NO_ERROR) pos_y = (int)temp_y;
        }

        // Draw using evaluated values */
        if (pos_x < 0) pos_x = 0;
        if (pos_y < 0) pos_y = 0;
        ProUIPoint subPoint = (ProUIPoint){ pos_x, pos_y };
        status = ProUIDrawingareaImageDraw(dialog, component, file_name, &subPoint);
        free(file_name);  // Clean up allocated string
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Failed to draw SUB_PICTURE at index %zu\n", i);
            return status;
        }
    }
    return PRO_TK_NO_ERROR;
}

ProError addpicture(char* dialog, char* component, ProAppData app_data) {
    ProError status = ProUIDrawingareaClear(dialog, component);
    if (status != PRO_TK_NO_ERROR) {
        ProGenericMsg(L"Error: Could not clear drawing area");
        return status;
    }
    status = draw_global_picture(dialog, component, app_data);
    if (status != PRO_TK_NO_ERROR) return status;
    return draw_sub_pictures(dialog, component, app_data);
}

/*=================================================*\
* 
* SHOW_PARAM command display executable
* --This displays the SHOW_PARAM command within 
* the creo dialog--
* 
\*=================================================*/

/* Conservative text->size estimate for on-picture labels. */
void onpic_label_size_for_text(const wchar_t* txt, int* out_w, int* out_h)
{
    size_t n = (txt ? wcslen(txt) : 0);
    int char_w = 8;      /* dialog-unit approximation per glyph */
    int pad = 10;     /* breathing room */
    int min_w = 40;     /* never smaller than this */
    if (out_w) *out_w = (int)(min_w + (int)n * char_w + pad);
    if (out_h) *out_h = 16; /* matches your existing ~15 */
}

ProError OnPictureShowParam(char* dialog, char* draw_area_name, ShowParamNode* node, SymbolTable* st) {
    ProError status;
    if (!dialog || !draw_area_name || !node || !node->parameter || !st) {
        ProPrintfChar("Error: Invalid inputs in CreateOnPictureShowParam");
        return PRO_TK_BAD_INPUTS;
    }

    Variable* var = get_symbol(st, node->parameter);
    if (!var) {
        ProPrintfChar("Error: Variable '%s' not found for ON_PICTURE", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    int x_pos = 0, y_pos = 0;
    if (node->posX) { long tx; if (evaluate_to_int(node->posX, st, &tx) == PRO_TK_NO_ERROR) x_pos = (int)tx; }
    if (node->posY) { long ty; if (evaluate_to_int(node->posY, st, &ty) == PRO_TK_NO_ERROR) y_pos = (int)ty; }

    // IMPORTANT: unify ID with addShowParam
    char label_name[100];
    snprintf(label_name, sizeof(label_name), "show_label_%s", node->parameter);

    wchar_t* name_w = char_to_wchar(node->parameter);
    /* NEW: try friendly */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->parameter, &w_friendly)) {
            free(name_w);
            name_w = w_friendly;
        }
    }
    wchar_t* value_w = variable_value_to_wstring(var);
    if (!name_w || !value_w) {
        free(name_w); free(value_w);
        ProPrintfChar("Error: Failed to format text for '%s'", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    wchar_t label_text[256];
    if (wcslen(value_w) > 0)
        swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls", value_w);
    else
        swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls: (undefined)", name_w);
    free(name_w);
    free(value_w);

    status = ProUIDrawingareaLabelAdd(dialog, draw_area_name, label_name);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add label '%s' to drawing area '%s'", label_name, draw_area_name);
        return status;
    }

    int lw = 0, lh = 0;
    onpic_label_size_for_text(label_text, &lw, &lh);
    status = ProUILabelSizeSet(dialog, label_name, lw, lh);
    if (status != PRO_TK_NO_ERROR)
    {
        ProPrintfChar("Could not set label size");
        return status;
    }

    status = ProUILabelTextSet(dialog, label_name, label_text);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set text for label '%s'", label_name);
        return status;
    }

    status = ProUILabelPositionSet(dialog, label_name, x_pos, y_pos);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set position for label '%s'", label_name);
        return status;
    }

    return PRO_TK_NO_ERROR;
}

ProError addShowParam(char* dialog_name, char* parent_layout_name, ShowParamNode* node, int* current_row, int column, SymbolTable* st) {
    ProError status;

    // Lookup the variable from symbol table
    Variable* var = get_symbol(st, node->parameter);
    if (!var) {
        ProPrintfChar("Error: Parameter '%s' not found in symbol table\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;  // Or continue with "(undefined)"
    }

    // Verify type matches (map subtype to VariableType)
    VariableType expected_type;
    switch (node->subtype) {
    case PARAM_INT: expected_type = TYPE_INTEGER; break;
    case PARAM_DOUBLE: expected_type = TYPE_DOUBLE; break;
    case PARAM_STRING: expected_type = TYPE_STRING; break;
    case PARAM_BOOL: expected_type = TYPE_BOOL; break;
    default:
        ProPrintfChar("Error: Invalid subtype for '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    if (var->type != expected_type) {
        ProPrintfChar("Error: Type mismatch for '%s': Expected %d, found %d\n", node->parameter, expected_type, var->type);
        return PRO_TK_GENERAL_ERROR;
    }

    // Initialize grid options (unchanged)
    ProUIGridopts label_grid = { 0 };
    label_grid.column = column;
    label_grid.row = *current_row;
    (*current_row)++;  // Increment row for sequential placement
    label_grid.horz_cells = 1;
    label_grid.vert_cells = 1;
    label_grid.top_offset = 20;
    label_grid.horz_resize = PRO_B_TRUE;
    label_grid.attach_bottom = PRO_B_TRUE;
    label_grid.attach_left = PRO_B_TRUE;
    label_grid.attach_right = PRO_B_TRUE;
    label_grid.attach_top = PRO_B_TRUE;

    // Create label ID (unchanged)
    char label_id[512];
    snprintf(label_id, sizeof(label_id), "show_label_%s", node->parameter);

    // Add label to layout (unchanged)
    status = ProUILayoutLabelAdd(dialog_name, parent_layout_name, label_id, &label_grid);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not add label for '%s'\n", node->parameter);
        return status;
    }

    // Get type string (unchanged)
    char* param_type_str = get_variable_type_string(node->var_type, node->subtype);

    wchar_t* w_parameter = char_to_wchar(node->parameter);
    wchar_t* w_type = char_to_wchar(param_type_str);  /* unchanged */
    if (!w_parameter || !w_type) { /* unchanged error path */ }

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->parameter, &w_friendly)) {
            free(w_parameter);
            w_parameter = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }

    if (!w_parameter || !w_type) {
        free(w_parameter);
        free(w_type);
        ProPrintf(L"Conversion to wide string failed\n");
        return PRO_TK_GENERAL_ERROR;
    }

    // Get value as wide string
    wchar_t* w_value = variable_value_to_wstring(var);
    if (!w_value) {
        free(w_parameter);
        free(w_type);
        ProPrintf(L"Failed to format value for '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
   
    // Format label text: "DE_MASTER (1.10)" - exclude type per requirement/example
    wchar_t label_text[200];
    swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls:%ls", w_parameter, w_value);

    // Set label text (unchanged)
    status = ProUILabelTextSet(dialog_name, label_id, label_text);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintf(L"Could not set text for label '%s'\n", label_id);
        free(w_parameter);
        free(w_type);
        free(w_value);
        return status;
    }

    // Free allocations
    free(w_parameter);
    free(w_type);
    free(w_value);

    return PRO_TK_NO_ERROR;
}

/*=================================================*\
* 
* CHECKBOX_PARAM command display executable
* --This displays the checkbox param within
* the creo dialog--
* 
\*=================================================*/
ProError CheckboxCallback(char* dialog, char* component, ProAppData app_data) {
    if (!app_data) return PRO_TK_BAD_INPUTS;
    
    CheckboxData* data = (CheckboxData*)app_data;
    SymbolTable* st = data->st;
    const char* param = data->param_name;
    
    if (!st || !param) {
        ProPrintfChar("Error: Invalid data in CheckboxCallback for '%s'", param ? param : "unknown");
        return PRO_TK_BAD_INPUTS;
    }
    
    ProBoolean state = PRO_B_FALSE;
    ProError status = ProUICheckbuttonGetState(dialog, component, &state);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Failed to get state for checkbox '%s'", component);
        return status;
    }
    
    Variable* var = get_symbol(st, param);
    if (!var || (var->type != TYPE_BOOL && var->type != TYPE_INTEGER)) {
        ProPrintfChar("Error: Variable '%s' not found or invalid type for checkbox update", param);
        return PRO_TK_GENERAL_ERROR;
    }
    
    var->data.int_value = (state == PRO_B_TRUE) ? 1 : 0;
    ProPrintfChar("Updated '%s' to %d (selected: %s)", param, var->data.int_value, state ? "true" : "false");
    
    // Optional: Re-validate OK button if this checkbox affects requirements
    validate_ok_button(dialog, st);


    EPA_ReactiveRefresh();
    
    return PRO_TK_NO_ERROR;
}

ProError OnPictureCheckboxParam(char* dialog, char* draw_area_name, CheckboxParamNode* node, SymbolTable* st) {
    ProError status;
    if (!dialog || !draw_area_name || !node || !node->parameter || !st) {
        ProPrintfChar("Error: Invalid inputs in CreateOnPictureCheckboxParam");
        return PRO_TK_BAD_INPUTS;
    }

    // Retrieve the variable for the parameter (to set initial state)
    Variable* var = get_symbol(st, node->parameter);
    if (!var) {
        ProPrintfChar("Error: Variable '%s' not found for ON_PICTURE checkbox", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Evaluate positions from expressions (fallback to 0 if missing or evaluation fails)
    int x_pos = 0;
    if (node->posX) {
        long temp_x;
        int eval_status = evaluate_to_int(node->posX, st, &temp_x);
        if (eval_status == PRO_TK_NO_ERROR) {
            x_pos = (int)temp_x;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posX for '%s'; using x=0", node->parameter);
        }
    }
    else {
        ProPrintfChar("Warning: posX missing in ON_PICTURE for '%s'; using x=0", node->parameter);
    }

    int y_pos = 0;
    if (node->posY) {
        long temp_y;
        int eval_status = evaluate_to_int(node->posY, st, &temp_y);
        if (eval_status == PRO_TK_NO_ERROR) {
            y_pos = (int)temp_y;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posY for '%s'; using y=0", node->parameter);
        }
    }
    else {
        ProPrintfChar("Warning: posY missing in ON_PICTURE for '%s'; using y=0", node->parameter);
    }


    // Create unique checkbox ID
    char checkbox_id[100];
    snprintf(checkbox_id, sizeof(checkbox_id), "checkbox_%s", node->parameter);

    // Evaluate tag expression to string (if present)
    char* tag_str = NULL;
    if (node->tag) {
        int eval_status = evaluate_to_string(node->tag, st, &tag_str);
        if (eval_status != PRO_TK_NO_ERROR || !tag_str) {
            ProPrintfChar("Warning: Failed to evaluate tag for '%s'; using empty tag", node->parameter);
            tag_str = _strdup("");
        }
    }
    else {
        tag_str = _strdup("");
    }

    // Convert parameter and tag to wchar_t* for label text
    wchar_t* param_w = char_to_wchar(node->parameter);
    if (!param_w) {
        free(tag_str);
        ProPrintfChar("Error: Failed to convert parameter name '%s' to wchar_t", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->parameter, &w_friendly)) {
            free(param_w);
            param_w = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }

    wchar_t* tag_w = char_to_wchar(tag_str ? tag_str : "");
    free(tag_str);
    if (!tag_w) {
        free(param_w);
        ProPrintfChar("Error: Failed to convert tag for '%s' to wchar_t", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Combine into label text (e.g., "param (tag)")
    wchar_t label_text[256];
    swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls (%ls)", param_w, tag_w);
    free(param_w);
    free(tag_w);

    // Add the checkbox to the drawing area
    status = ProUIDrawingareaCheckbuttonAdd(dialog, draw_area_name, checkbox_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add checkbox '%s' to drawing area '%s'", checkbox_id, draw_area_name);
        return status;
    }

    // Set the checkbox text
    status = ProUICheckbuttonTextSet(dialog, checkbox_id, label_text);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set text for checkbox '%s'", checkbox_id);
        return status;
    }

    // Set the checkbox position
    status = ProUICheckbuttonPositionSet(dialog, checkbox_id, x_pos, y_pos);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set position for checkbox '%s'", checkbox_id);
        return status;
    }

    // Set initial state based on variable value
    ProBoolean initial_state = PRO_B_FALSE;
    if (var->type == TYPE_BOOL || var->type == TYPE_INTEGER) {
        initial_state = (var->data.int_value != 0) ? PRO_B_TRUE : PRO_B_FALSE;
    }
    status = ProUICheckbuttonUnset(dialog, checkbox_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Could not set initial state for checkbox '%s'", checkbox_id);
    }

    // Set tooltip if present
    if (node->tooltip_message) {
        char* tooltip_str = NULL;
        int eval_status = evaluate_to_string(node->tooltip_message, st, &tooltip_str);
        if (eval_status == PRO_TK_NO_ERROR && tooltip_str) {
            wchar_t* tooltip_w = char_to_wchar(tooltip_str);
            if (tooltip_w) {
                ProUICheckbuttonHelptextSet(dialog, checkbox_id, tooltip_w);
                free(tooltip_w);
            }
            free(tooltip_str);
        }
    }

    // Allocate callback data and set activate action
    CheckboxData* data = malloc(sizeof(CheckboxData));
    if (!data) {
        ProPrintfChar("Error: Memory allocation failed for CheckboxData");
        return PRO_TK_GENERAL_ERROR;
    }
    data->st = st;
    data->param_name = _strdup(node->parameter);
    if (!data->param_name) {
        free(data);
        ProPrintfChar("Error: Failed to duplicate parameter name for callback");
        return PRO_TK_GENERAL_ERROR;
    }

    status = ProUICheckbuttonActivateActionSet(dialog, checkbox_id, CheckboxCallback, (ProAppData)data);
    if (status != PRO_TK_NO_ERROR) {
        free(data->param_name);
        free(data);
        ProPrintfChar("Error: Could not set activate action for checkbox '%s'", checkbox_id);
        return status;
    }

    return PRO_TK_NO_ERROR;
}

ProError addCheckboxParam(char* dialog_name, char* parent_layout_name, CheckboxParamNode* cbNode, int* current_row, int column, SymbolTable* st)
{
    ProError status;

    // Validate inputs
    if (!dialog_name || !parent_layout_name || !cbNode) {
        ProGenericMsg(L"Error: Invalid input to addCheckboxParam.\n");
        return PRO_TK_BAD_INPUTS;
    }

    // Lookup the variable from symbol table
    Variable* var = get_symbol(st, cbNode->parameter);
    if (!var) {
        ProPrintfChar("Error: Parameter '%s' not found in symbol table\n", cbNode->parameter);
        return PRO_TK_GENERAL_ERROR;  // Or continue with "(undefined)"
    }

    // Verify type matches (map subtype to VariableType)
    VariableType expected_type;
    switch (cbNode->subtype) {
    case PARAM_INT: expected_type = TYPE_INTEGER; break;
    case PARAM_DOUBLE: expected_type = TYPE_DOUBLE; break;
    case PARAM_STRING: expected_type = TYPE_STRING; break;
    case PARAM_BOOL: expected_type = TYPE_BOOL; break;
    default:
        ProPrintfChar("Error: Invalid subtype for '%s'\n", cbNode->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    if (var->type != expected_type) {
        ProPrintfChar("Error: Type mismatch for '%s': Expected %d, found %d\n", cbNode->parameter, expected_type, var->type);
        return PRO_TK_GENERAL_ERROR;
    }

    // Create label ID
    char cbp_label_id[100];
    snprintf(cbp_label_id, sizeof(cbp_label_id), "checkbox_%s", cbNode->parameter);

    // Initialize grid options
    ProUIGridopts grid_opts_cbp = { 0 };
    grid_opts_cbp.attach_bottom = PRO_B_TRUE;
    grid_opts_cbp.attach_left = PRO_B_TRUE;
    grid_opts_cbp.attach_right = PRO_B_TRUE;
    grid_opts_cbp.attach_top = PRO_B_TRUE;
    grid_opts_cbp.horz_cells = 1;
    grid_opts_cbp.vert_cells = 1;
    grid_opts_cbp.column = column + 1;
    grid_opts_cbp.row = *current_row;
    (*current_row)++;  // Increment row for sequential placement
    

    status = ProUILayoutCheckbuttonAdd(dialog_name, parent_layout_name, cbp_label_id, &grid_opts_cbp);
    if (status != PRO_TK_NO_ERROR)
    {
        ProGenericMsg(L"Error: Could not set checkbox in dialog");
        return status;
    }

    // Convert parameter to wide string
    wchar_t* w_parameter = char_to_wchar(cbNode->parameter);
    if (!w_parameter) {
        ProPrintf(L"Conversion to wide string failed for parameter\n");
        return PRO_TK_GENERAL_ERROR;
    }

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(cbNode->parameter, &w_friendly)) {
            free(w_parameter);
            w_parameter = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }

    // Evaluate and convert tag if present
    wchar_t* w_tag = NULL;
    if (cbNode->tag) {
        char* tag_str = NULL;
        status = evaluate_to_string(cbNode->tag, st, &tag_str);
        if (status != PRO_TK_NO_ERROR || !tag_str) {
            free(w_parameter);
            ProPrintf(L"Failed to evaluate tag for '%s'\n", cbNode->parameter);
            return status ? status : PRO_TK_BAD_INPUTS;
        }
        w_tag = char_to_wchar(tag_str);
        free(tag_str);  // Free evaluated string
        if (!w_tag) {
            free(w_parameter);
            ProPrintf(L"Conversion to wide string failed for tag\n");
            return PRO_TK_GENERAL_ERROR;
        }
    }

    // Format label text: "ALL_SKELS (Y/N)" or "ALL_SKELS" if no tag
    wchar_t cbp_label_text[200];
    if (w_tag) {
        swprintf(cbp_label_text, sizeof(cbp_label_text) / sizeof(wchar_t), L"(%ls) %ls", w_tag, w_parameter);
    }
    else {
        swprintf(cbp_label_text, sizeof(cbp_label_text) / sizeof(wchar_t), L"%ls", w_parameter);
    }

    status = ProUICheckbuttonTextSet(dialog_name, cbp_label_id, cbp_label_text);
    if (status != PRO_TK_NO_ERROR)
    {
        free(w_parameter);
        free(w_tag);
        ProGenericMsg(L"Error: Could not set text for CHECKBOX_PARAM");
        return status;
    }


    // Set initial state based on variable value
    ProBoolean initial_state = PRO_B_FALSE;
    if (var->type == TYPE_BOOL || var->type == TYPE_INTEGER) {
        initial_state = (var->data.int_value != 0) ? PRO_B_TRUE : PRO_B_FALSE;
    }
    status = ProUICheckbuttonUnset(dialog_name, cbp_label_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Could not set initial state for checkbox '%s'", cbp_label_id);
    }

    // Allocate callback data and set activate action
    CheckboxData* data = malloc(sizeof(CheckboxData));
    if (!data) {
        ProPrintfChar("Error: Memory allocation failed for CheckboxData");
        return PRO_TK_GENERAL_ERROR;
    }
    data->st = st;
    data->param_name = _strdup(cbNode->parameter);
    if (!data->param_name) {
        free(data);
        ProPrintfChar("Error: Failed to duplicate parameter name for callback");
        return PRO_TK_GENERAL_ERROR;
    }

    status = ProUICheckbuttonActivateActionSet(dialog_name, cbp_label_id, CheckboxCallback, (ProAppData)data);
    if (status != PRO_TK_NO_ERROR) {
        free(data->param_name);
        free(data);
        ProPrintfChar("Error: Could not set activate action for checkbox '%s'", cbp_label_id);
        return status;
    }

    // Free allocations
    free(w_parameter);
    free(w_tag);

    return PRO_TK_NO_ERROR;
}

/*=================================================*\
* 
* USER_INPUT Commands (text filtering, and display exectuable)
* --This handles the text filtering for inputs as well as 
* displays the InputPanel within the creo dialog--
* 
\*=================================================*/
ProError InputFilterCallback(char* dialog, char* component, ProAppData app_data) {
    InputFilterData* filter_data = (InputFilterData*)app_data;
    if (!filter_data) return PRO_TK_BAD_INPUTS;
    if (filter_data->in_callback == PRO_B_TRUE) return PRO_TK_NO_ERROR;
    filter_data->in_callback = PRO_B_TRUE;

    char* current_str = NULL;
    ProError status = ProUIInputpanelStringGet(dialog, component, &current_str);
    if (status != PRO_TK_NO_ERROR || !current_str) {
        filter_data->in_callback = PRO_B_FALSE;
        return status;
    }

    // validate 
    int is_valid = 0; char* endptr = NULL; errno = 0;
    switch (filter_data->subtype) {
    case PARAM_STRING: is_valid = 1; break;
    case PARAM_INT:    (void)strtol(current_str, &endptr, 10);
        is_valid = (endptr == current_str + strlen(current_str)) && (errno != ERANGE); break;
    case PARAM_BOOL: { long v = strtol(current_str, &endptr, 10);
        is_valid = (endptr == current_str + strlen(current_str)) && (errno != ERANGE) && (v == 0 || v == 1); } break;
    case PARAM_DOUBLE: (void)strtod(current_str, &endptr);
        is_valid = (endptr == current_str + strlen(current_str)) && (errno != ERANGE); break;
    default:           is_valid = 0; break;
    }

    if (is_valid) {
         //persist last valid 
        free(filter_data->last_valid);
        filter_data->last_valid = _strdup(current_str);

        // write symbol (type-aware) 
        Variable* var = get_symbol(filter_data->st, filter_data->parameter);
        if (var) {
            switch (filter_data->subtype) {
            case PARAM_DOUBLE: {
                double dv = strtod(current_str, NULL);
                if (var->type == TYPE_DOUBLE) {
                    var->data.double_value = dv;
                }
                else if (var->type == TYPE_INTEGER || var->type == TYPE_BOOL) {
                    var->data.int_value = (int)dv;
                }
                else if (var->type == TYPE_STRING) {
                    free(var->data.string_value);
                    var->data.string_value = _strdup(current_str);
                }
                break;
            }
            case PARAM_INT:
            case PARAM_BOOL: {
                int iv = (int)strtol(current_str, NULL, 10);
                if (var->type == TYPE_INTEGER || var->type == TYPE_BOOL) {
                    var->data.int_value = iv;
                }
                else if (var->type == TYPE_DOUBLE) {
                    // critical: keep active slot in sync for DOUBLE backing 
                    var->data.double_value = (double)iv;
                }
                else if (var->type == TYPE_STRING) {
                    free(var->data.string_value);
                    var->data.string_value = _strdup(current_str);
                }
                break;
            }
            case PARAM_STRING:
                if (var->type == TYPE_STRING) {
                    free(var->data.string_value);
                    var->data.string_value = _strdup(current_str);
                }
                break;
            default: break;
            }
        }


        // repaint + reactive IF rebuild 
        (void)ProUIDrawingareaClear(dialog, "draw_area");
        (void)addpicture(dialog, "draw_area", (ProAppData)filter_data->st);
    }
    else {
        // revert to last valid 
        if (filter_data->last_valid) (void)ProUIInputpanelStringSet(dialog, component, filter_data->last_valid);
        else                         (void)ProUIInputpanelStringSet(dialog, component, "");
    }

    ProStringFree(current_str);
    filter_data->in_callback = PRO_B_FALSE;
    return status;
}

ProError ActivateCallback(char* dialog, char* component, ProAppData app_data) {
    InputFilterData* filter_data = (InputFilterData*)app_data;
    if (!filter_data) {
        return PRO_TK_BAD_INPUTS;
    }

    if (filter_data->in_activate == PRO_B_TRUE) {
        return PRO_TK_NO_ERROR;  // Prevent recursion
    }
    filter_data->in_activate = PRO_B_TRUE;

    Variable* var = get_symbol(filter_data->st, filter_data->parameter);
    if (!var) {
        ProPrintfChar("Error: Parameter '%s' not found during activation\n", filter_data->parameter);
        filter_data->in_activate = PRO_B_FALSE;
        return PRO_TK_GENERAL_ERROR;
    }

    ProError status = PRO_TK_NO_ERROR;
    switch (filter_data->subtype) {
    case PARAM_DOUBLE: {
        double val; status = ProUIInputpanelDoubleGet(dialog, component, &val);
        if (status == PRO_TK_NO_ERROR) {
            if (var->type == TYPE_DOUBLE) var->data.double_value = val;
            else if (var->type == TYPE_INTEGER || var->type == TYPE_BOOL) var->data.int_value = (int)val;
        }
        break;
    }
    case PARAM_INT:
    case PARAM_BOOL: {
        int val; status = ProUIInputpanelIntegerGet(dialog, component, &val);
        if (status == PRO_TK_NO_ERROR) {
            if (var->type == TYPE_INTEGER || var->type == TYPE_BOOL) var->data.int_value = val;
            else if (var->type == TYPE_DOUBLE) var->data.double_value = (double)val; /* critical sync */
        }
        break;
    }
    case PARAM_STRING: {
        char* sval = NULL; status = ProUIInputpanelStringGet(dialog, component, &sval);
        if (status == PRO_TK_NO_ERROR && sval) {
            if (var->type == TYPE_STRING) {
                free(var->data.string_value);
                var->data.string_value = _strdup(sval);
            }
            ProStringFree(sval);
        }
        break;
    }
    default: break;
    }

    // debug + SHOW_PARAM label update (if present) 
    Variable* after = get_symbol(filter_data->st, filter_data->parameter);
    if (after) {
        debug_print_symbol_update(filter_data->parameter, after);
        update_show_param_label(dialog, filter_data->parameter, after, PRO_B_FALSE);
    }

    refresh_required_input_highlights(dialog, filter_data->st);
    EPA_ReactiveRefresh();  // makes IF branches/picture choice/button gating react now

    filter_data->in_activate = PRO_B_FALSE;
    return status;
}

ProError OnPictureActivateCallback(char* dialog, char* component, ProAppData app_data) {
    InputFilterData* filter_data = (InputFilterData*)app_data;
    if (!filter_data) {
        return PRO_TK_BAD_INPUTS;
    }

    if (filter_data->in_activate == PRO_B_TRUE) {
        return PRO_TK_NO_ERROR;  // Prevent recursion
    }
    filter_data->in_activate = PRO_B_TRUE;

    Variable* var = get_symbol(filter_data->st, filter_data->parameter);
    if (!var) {
        ProPrintfChar("Error: Parameter '%s' not found during activation\n", filter_data->parameter);
        filter_data->in_activate = PRO_B_FALSE;
        return PRO_TK_GENERAL_ERROR;
    }

    ProError status = PRO_TK_NO_ERROR;
    switch (filter_data->subtype) {
    case PARAM_DOUBLE: {
        double val; status = ProUIInputpanelDoubleGet(dialog, component, &val);
        if (status == PRO_TK_NO_ERROR) {
            if (var->type == TYPE_DOUBLE) var->data.double_value = val;
            else if (var->type == TYPE_INTEGER || var->type == TYPE_BOOL) var->data.int_value = (int)val;
        }
        break;
    }
    case PARAM_INT:
    case PARAM_BOOL: {
        int val; status = ProUIInputpanelIntegerGet(dialog, component, &val);
        if (status == PRO_TK_NO_ERROR) {
            if (var->type == TYPE_INTEGER || var->type == TYPE_BOOL) var->data.int_value = val;
            else if (var->type == TYPE_DOUBLE) var->data.double_value = (double)val; /* critical sync */
        }
        break;
    }
    case PARAM_STRING: {
        char* sval = NULL; status = ProUIInputpanelStringGet(dialog, component, &sval);
        if (status == PRO_TK_NO_ERROR && sval) {
            if (var->type == TYPE_STRING) {
                free(var->data.string_value);
                var->data.string_value = _strdup(sval);
            }
            ProStringFree(sval);
        }
        break;
    }
    default: break;
    }

    // debug + SHOW_PARAM label update (if present) 
    Variable* after = get_symbol(filter_data->st, filter_data->parameter);
    if (after) {
        debug_print_symbol_update(filter_data->parameter, after);
        update_show_param_label(dialog, filter_data->parameter, after, PRO_B_TRUE);
    }

    refresh_required_input_highlights(dialog, filter_data->st);
    EPA_ReactiveRefresh();  // makes IF branches/picture choice/button gating react now

    filter_data->in_activate = PRO_B_FALSE;
    return status;
}

ProError OnPictureUserInputParam(char* dialog, char* draw_area_name, UserInputParamNode* node, SymbolTable* st) {
    ProError status;
    if (!dialog || !draw_area_name || !node || !node->parameter || !st) {
        ProPrintfChar("Error: Invalid inputs in OnPictureUserInputParam");
        return PRO_TK_BAD_INPUTS;
    }

    // Retrieve the variable for the parameter (to set initial value)
    Variable* var = get_symbol(st, node->parameter);
    if (!var) {
        ProPrintfChar("Error: Parameter '%s' not found in symbol table\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Verify type matches (map subtype to VariableType)
    VariableType expected_type;
    switch (node->subtype) {
    case PARAM_INT: expected_type = TYPE_INTEGER; break;
    case PARAM_DOUBLE: expected_type = TYPE_DOUBLE; break;
    case PARAM_STRING: expected_type = TYPE_STRING; break;
    case PARAM_BOOL: expected_type = TYPE_BOOL; break;
    default:
        ProPrintfChar("Error: Invalid subtype for '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    if (var->type != expected_type) {
        ProPrintfChar("Error: Type mismatch for '%s': Expected %d, found %d\n", node->parameter, expected_type, var->type);
        return PRO_TK_GENERAL_ERROR;
    }

    // Evaluate positions from expressions (fallback to 0 if missing or evaluation fails)
    int x_pos = 0;
    if (node->posX) {
        long temp_x;
        status = evaluate_to_int(node->posX, st, &temp_x);
        if (status == PRO_TK_NO_ERROR) {
            x_pos = (int)temp_x;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posX for '%s'; using x=0", node->parameter);
        }
    }
    else {
        ProPrintfChar("Warning: posX missing in ON_PICTURE for '%s'; using x=0", node->parameter);
    }

    int y_pos = 0;
    if (node->posY) {
        long temp_y;
        status = evaluate_to_int(node->posY, st, &temp_y);
        if (status == PRO_TK_NO_ERROR) {
            y_pos = (int)temp_y;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posY for '%s'; using y=0", node->parameter);
        }
    }
    else {
        ProPrintfChar("Warning: posY missing in ON_PICTURE for '%s'; using y=0", node->parameter);
    }


    char input_id[100];
    snprintf(input_id, sizeof(input_id), "input_panel_%s", node->parameter);


    // Add input panel to sub-drawing area
    status = ProUIDrawingareaInputpanelAdd(dialog, "draw_area", input_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not add input panel for '%s'\n", node->parameter);
        return status;
    }

    status = ProUIInputpanelColumnsSet(dialog, input_id, 5);

    // Set input panel position (relative to sub-drawing area)
    status = ProUIInputpanelPositionSet(dialog, input_id, x_pos, y_pos);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set input panel position for '%s'\n", node->parameter);
        return status;
    }

    if (node->required) {
        ProError rs = require_input(st, node->parameter);
        if (rs != PRO_TK_NO_ERROR) {
            ProPrintfChar("Error: failed to register required input '%s'\n", node->parameter);
            return rs;
        }
    }

    status = ProUIInputpanelAutohighlightEnable(dialog, input_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not highlight TextBox");
        return status;
    }

    // Set input type to restrict input based on subtype
    ProUIInputtype input_type;
    switch (node->subtype) {
    case PARAM_STRING:
        input_type = PROUIINPUTTYPE_STRING;
        break;
    case PARAM_INT:
    case PARAM_BOOL:
        input_type = PROUIINPUTTYPE_INTEGER;
        break;
    case PARAM_DOUBLE:
        input_type = PROUIINPUTTYPE_DOUBLE;
        break;
    default:
        input_type = PROUIINPUTTYPE_STRING;  // Fallback
    }
    status = ProUIInputpanelInputtypeSet(dialog, input_id, input_type);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set input type for '%s'\n", node->parameter);
        return status;
    }

    // Set initial value based on type
    switch (node->subtype) {
    case PARAM_DOUBLE:
        status = ProUIInputpanelDoubleSet(dialog, input_id, var->data.double_value);
        break;
    case PARAM_INT:
    case PARAM_BOOL:
        status = ProUIInputpanelIntegerSet(dialog, input_id, var->data.int_value);
        break;
    case PARAM_STRING:
        status = ProUIInputpanelStringSet(dialog, input_id, var->data.string_value);
        break;
    }
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set initial value for '%s'\n", node->parameter);
        return status;
    }

    // Allocate and initialize filter data
    InputFilterData* filter_data = (InputFilterData*)malloc(sizeof(InputFilterData));
    if (!filter_data) {
        ProPrintfChar("Error: Memory allocation failed for filter data of '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    filter_data->subtype = node->subtype;
    filter_data->in_callback = PRO_B_FALSE;
    filter_data->in_activate = PRO_B_FALSE;
    filter_data->st = st;
    filter_data->parameter = _strdup(node->parameter);  // Duplicate for ownership
    if (!filter_data->parameter) {
        free(filter_data);
        ProPrintfChar("Error: Memory allocation failed for parameter name '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Set initial last_valid based on subtype
    char buf[64] = { 0 };  // Buffer for formatted numeric strings
    char* init_str = NULL;
    switch (node->subtype) {
    case PARAM_DOUBLE: {
        double val;
        status = ProUIInputpanelDoubleGet(dialog, input_id, &val);
        if (status == PRO_TK_NO_ERROR) {
            snprintf(buf, sizeof(buf), "%.2f", val);  // Adjust precision as needed
            init_str = buf;
        }
        break;
    }
    case PARAM_INT:
    case PARAM_BOOL: {
        int val;
        status = ProUIInputpanelIntegerGet(dialog, input_id, &val);
        if (status == PRO_TK_NO_ERROR) {
            snprintf(buf, sizeof(buf), "%d", val);
            init_str = buf;
        }
        break;
    }
    case PARAM_STRING: {
        status = ProUIInputpanelStringGet(dialog, input_id, &init_str);
        if (status != PRO_TK_NO_ERROR) {
            init_str = NULL;
        }
        break;
    }
    }
    filter_data->last_valid = _strdup(init_str ? init_str : "");
    if (node->subtype == PARAM_STRING && init_str) {
        ProStringFree(init_str);  // Free toolkit-allocated string
    }

    // Register the callback
    status = ProUIInputpanelInputActionSet(dialog, input_id, InputFilterCallback, (ProAppData)filter_data);
    if (status != PRO_TK_NO_ERROR) {
        free(filter_data->last_valid);
        free(filter_data);
        ProPrintfChar("Could not set input action for '%s'\n", node->parameter);
        return status;
    }

    // Register the activation (submit) callback
    status = ProUIInputpanelActivateActionSet(dialog, input_id, OnPictureActivateCallback, (ProAppData)filter_data);
    if (status != PRO_TK_NO_ERROR) {
        free(filter_data->last_valid);
        free(filter_data->parameter);
        free(filter_data);
        ProPrintfChar("Could not set activation action for '%s'\n", node->parameter);
        return status;
    }

    // Handle min_value if present
    if (node->min_value) {
        if (node->subtype == PARAM_DOUBLE) {
            double min_val;
            status = evaluate_to_double(node->min_value, st, &min_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMindoubleSet(dialog, input_id, min_val);
            }
        }
        else if (node->subtype == PARAM_INT || node->subtype == PARAM_BOOL) {
            long min_val;
            status = evaluate_to_int(node->min_value, st, &min_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMinintegerSet(dialog, input_id, (int)min_val);
            }
        }
    }

    // Handle max_value if present
    if (node->max_value) {
        if (node->subtype == PARAM_DOUBLE) {
            double max_val;
            status = evaluate_to_double(node->max_value, st, &max_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMaxdoubleSet(dialog, input_id, max_val);
            }
        }
        else if (node->subtype == PARAM_INT || node->subtype == PARAM_BOOL) {
            long max_val;
            status = evaluate_to_int(node->max_value, st, &max_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMaxintegerSet(dialog, input_id, (int)max_val);
            }
        }
    }

    // Handle decimal_places for doubles
    if (node->subtype == PARAM_DOUBLE && node->decimal_places) {
        long digits;
        status = evaluate_to_int(node->decimal_places, st, &digits);
        if (status == PRO_TK_NO_ERROR && digits >= 0) {
            ProUIInputpanelDigitsSet(dialog, input_id, (int)digits);
        }
    }

    // Handle tooltip_message if present
    if (node->tooltip_message) {
        char* tooltip_str = NULL;
        status = evaluate_to_string(node->tooltip_message, st, &tooltip_str);
        if (status == PRO_TK_NO_ERROR && tooltip_str) {
            wchar_t* w_tooltip = char_to_wchar(tooltip_str);
            if (w_tooltip) {
                ProUIInputpanelHelptextSet(dialog, input_id, w_tooltip);
                free(w_tooltip);
            }
            free(tooltip_str);
        }
    }


    (void)refresh_required_input_highlights(dialog, st);

    (void)track_ui_param(st, node->parameter);

    return PRO_TK_NO_ERROR;
}

ProError addUserInputParam(char* dialog_name, char* parent_layout_name, UserInputParamNode* node, int* current_row, int column, SymbolTable* st) {
    ProError status;

    // Lookup the variable from symbol table
    Variable* var = get_symbol(st, node->parameter);
    if (!var) {
        ProPrintfChar("Error: Parameter '%s' not found in symbol table\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Verify type matches (map subtype to VariableType)
    VariableType expected_type;
    switch (node->subtype) {
    case PARAM_INT: expected_type = TYPE_INTEGER; break;
    case PARAM_DOUBLE: expected_type = TYPE_DOUBLE; break;
    case PARAM_STRING: expected_type = TYPE_STRING; break;
    case PARAM_BOOL: expected_type = TYPE_BOOL; break;
    default:
        ProPrintfChar("Error: Invalid subtype for '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    if (var->type != expected_type) {
        ProPrintfChar("Error: Type mismatch for '%s': Expected %d, found %d\n", node->parameter, expected_type, var->type);
        return PRO_TK_GENERAL_ERROR;
    }

    // Generate unique IDs
    char label_id[100];
    char area_id[100];
    char input_id[100];
    snprintf(label_id, sizeof(label_id), "input_label_%s", node->parameter);
    snprintf(area_id, sizeof(area_id), "input_area_%s", node->parameter);
    snprintf(input_id, sizeof(input_id), "input_panel_%s", node->parameter);

    // Initialize grid options for label
    ProUIGridopts label_grid = { 0 };
    label_grid.horz_cells = 1;
    label_grid.vert_cells = 1;
    label_grid.attach_bottom = PRO_B_TRUE;
    label_grid.attach_left = PRO_B_TRUE;
    label_grid.attach_right = PRO_B_TRUE;
    label_grid.attach_top = PRO_B_TRUE;
    label_grid.column = column;
    label_grid.row = *current_row;
    

    // Add label to layout
    status = ProUILayoutLabelAdd(dialog_name, parent_layout_name, label_id, &label_grid);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not add label for '%s'\n", node->parameter);
        return status;
    }

    // Set label text
    wchar_t* w_parameter = char_to_wchar(node->parameter);
    if (!w_parameter) {
        ProPrintf(L"Conversion to wide string failed\n");
        return PRO_TK_GENERAL_ERROR;
    }

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->parameter, &w_friendly)) {
            free(w_parameter);
            w_parameter = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }

    status = ProUILabelTextSet(dialog_name, label_id, w_parameter);
    free(w_parameter);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set text for label '%s'\n", label_id);
        return status;
    }

    // Initialize grid options for drawing area (next column if not on_picture)
    ProUIGridopts area_grid = label_grid;
    if (!node->on_picture) {
        area_grid.column += 1;
        area_grid.horz_resize = PRO_B_FALSE;
        area_grid.vert_cells = PRO_B_TRUE;
        area_grid.horz_cells = 0;
        area_grid.vert_cells = 1;
        area_grid.bottom_offset = 5;
    }

    // Add drawing area
    status = ProUILayoutDrawingareaAdd(dialog_name, parent_layout_name, area_id, &area_grid);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not add drawing area for '%s'\n", node->parameter);
        return status;
    }

    // Decorate drawing area
    status = ProUIDrawingareaDecorate(dialog_name, area_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not decorate drawing area for '%s'\n", node->parameter);
        return status;
    }

    // Set drawing area height (fixed, or based on option)
    int height = 25;
    int width = 93;
    status = ProUIDrawingareaDrawingheightSet(dialog_name, area_id, height);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set drawing area height for '%s'\n", node->parameter);
        return status;
    }

    status = ProUIDrawingareaDrawingwidthSet(dialog_name, area_id, width);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set drawing area width for '%s'\n", node->parameter);
        return status;
    }

    // Add input panel
    status = ProUIDrawingareaInputpanelAdd(dialog_name, area_id, input_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not add input panel for '%s'\n", node->parameter);
        return status;
    }

    status = ProUIInputpanelColumnsSet(dialog_name, input_id, 7);

    // Set input panel position
    status = ProUIInputpanelPositionSet(dialog_name, input_id, 0, 1);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set input panel position for '%s'\n", node->parameter);
        return status;
    }


    if (node->required) {
        ProError rs = require_input(st, node->parameter);
        if (rs != PRO_TK_NO_ERROR) {
            ProPrintfChar("Error: failed to register required input '%s'\n", node->parameter);
            return rs;
        }
    }


    status = ProUIInputpanelAutohighlightEnable(dialog_name, input_id);
    if (status != PRO_TK_NO_ERROR)
    {
        ProPrintfChar("Could not highlight TextBox");
        return status;
    }


    // Set input type to restrict input based on subtype
    ProUIInputtype input_type;
    switch (node->subtype) {
    case PARAM_STRING:
        input_type = PROUIINPUTTYPE_STRING;
        break;
    case PARAM_INT:
    case PARAM_BOOL:
        input_type = PROUIINPUTTYPE_INTEGER;
        break;
    case PARAM_DOUBLE:
        input_type = PROUIINPUTTYPE_DOUBLE;
        break;
    default:
        input_type = PROUIINPUTTYPE_STRING;  // Fallback
    }
    status = ProUIInputpanelInputtypeSet(dialog_name, input_id, input_type);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set input type for '%s'\n", node->parameter);
        return status;
    }

    // Set initial value based on type
    switch (node->subtype) {
    case PARAM_DOUBLE:
        status = ProUIInputpanelDoubleSet(dialog_name, input_id, var->data.double_value);
        break;
    case PARAM_INT:
    case PARAM_BOOL:
        status = ProUIInputpanelIntegerSet(dialog_name, input_id, var->data.int_value);
        break;
    case PARAM_STRING:
        status = ProUIInputpanelStringSet(dialog_name, input_id, var->data.string_value);
        break;
    }
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not set initial value for '%s'\n", node->parameter);
        return status;
    }

    // Allocate and initialize filter data
    InputFilterData* filter_data = (InputFilterData*)malloc(sizeof(InputFilterData));
    if (!filter_data) {
        ProPrintfChar("Error: Memory allocation failed for filter data of '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    filter_data->subtype = node->subtype;
    filter_data->in_callback = PRO_B_FALSE;
    filter_data->in_activate = PRO_B_FALSE;
    filter_data->st = st;
    filter_data->parameter = _strdup(node->parameter);  // Duplicate for ownership
    if (!filter_data->parameter) {
        free(filter_data);
        ProPrintfChar("Error: Memory allocation failed for parameter name '%s'\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Set initial last_valid based on subtype
    char buf[64] = { 0 };  // Buffer for formatted numeric strings
    char* init_str = NULL;
    switch (node->subtype) {
    case PARAM_DOUBLE: {
        double val;
        status = ProUIInputpanelDoubleGet(dialog_name, input_id, &val);
        if (status == PRO_TK_NO_ERROR) {
            snprintf(buf, sizeof(buf), "%.2f", val);  // Adjust precision as needed
            init_str = buf;
        }
        break;
    }
    case PARAM_INT:
    case PARAM_BOOL: {
        int val;
        status = ProUIInputpanelIntegerGet(dialog_name, input_id, &val);
        if (status == PRO_TK_NO_ERROR) {
            snprintf(buf, sizeof(buf), "%d", val);
            init_str = buf;
        }
        break;
    }
    case PARAM_STRING: {
        status = ProUIInputpanelStringGet(dialog_name, input_id, &init_str);
        if (status != PRO_TK_NO_ERROR) {
            init_str = NULL;
        }
        break;
    }
    }
    filter_data->last_valid = _strdup(init_str ? init_str : "");
    if (node->subtype == PARAM_STRING && init_str) {
        ProStringFree(init_str);  // Free toolkit-allocated string
    }

    // Register the callback
    status = ProUIInputpanelInputActionSet(dialog_name, input_id, InputFilterCallback, (ProAppData)filter_data);
    if (status != PRO_TK_NO_ERROR) {
        free(filter_data->last_valid);
        free(filter_data);
        ProPrintfChar("Could not set input action for '%s'\n", node->parameter);
        return status;
    }

    // Register the activation (submit) callback
    status = ProUIInputpanelActivateActionSet(dialog_name, input_id, ActivateCallback, (ProAppData)filter_data);
    if (status != PRO_TK_NO_ERROR) {
        free(filter_data->last_valid);
        free(filter_data->parameter);
        free(filter_data);
        ProPrintfChar("Could not set activation action for '%s'\n", node->parameter);
        return status;
    }

    // Handle min_value if present
    if (node->min_value) {
        if (node->subtype == PARAM_DOUBLE) {
            double min_val;
            status = evaluate_to_double(node->min_value, st, &min_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMindoubleSet(dialog_name, input_id, min_val);
            }
        }
        else if (node->subtype == PARAM_INT || node->subtype == PARAM_BOOL) {
            long min_val;
            status = evaluate_to_int(node->min_value, st, &min_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMinintegerSet(dialog_name, input_id, (int)min_val);
            }
        }
    }

    // Handle max_value if present
    if (node->max_value) {
        if (node->subtype == PARAM_DOUBLE) {
            double max_val;
            status = evaluate_to_double(node->max_value, st, &max_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMaxdoubleSet(dialog_name, input_id, max_val);
            }
        }
        else if (node->subtype == PARAM_INT || node->subtype == PARAM_BOOL) {
            long max_val;
            status = evaluate_to_int(node->max_value, st, &max_val);
            if (status == PRO_TK_NO_ERROR) {
                ProUIInputpanelMaxintegerSet(dialog_name, input_id, (int)max_val);
            }
        }
    }

    // Handle decimal_places for doubles
    if (node->subtype == PARAM_DOUBLE && node->decimal_places) {
        long digits;
        status = evaluate_to_int(node->decimal_places, st, &digits);
        if (status == PRO_TK_NO_ERROR && digits >= 0) {
            ProUIInputpanelDigitsSet(dialog_name, input_id, (int)digits);
        }
    }

    // Handle tooltip_message if present
    if (node->tooltip_message) {
        char* tooltip_str = NULL;
        status = evaluate_to_string(node->tooltip_message, st, &tooltip_str);
        if (status == PRO_TK_NO_ERROR && tooltip_str) {
            wchar_t* w_tooltip = char_to_wchar(tooltip_str);
            if (w_tooltip) {
                ProUIInputpanelHelptextSet(dialog_name, input_id, w_tooltip);
                free(w_tooltip);
            }
            free(tooltip_str);
        }
    }

    // Handle WIDTH if present (for display width)
    if (node->width) {
        double width_val;
        status = evaluate_to_double(node->width, st, &width_val);
        if (status == PRO_TK_NO_ERROR && width_val > 0) {
            int width_pixels = (int)width_val;
            status = ProUIDrawingareaDrawingwidthSet(dialog_name, area_id, width_pixels);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Warning: Could not set width for '%s'\n", node->parameter);
            }
        }
    }


    (void)refresh_required_input_highlights(dialog_name, st);

    if (!node->on_picture) {
        (*current_row)++;
    }

    (void)track_ui_param(st, node->parameter);


    return PRO_TK_NO_ERROR;
}

/*=================================================*\
* 
* RADIOBUTTON_PARAM command display execution
* --this displays the radiobutton within the dialog--
* 
\*=================================================*/
static void ensure_radio_options_map(SymbolTable* st, const char* param, char** names, int count, int required_flag)
{
    if (!st || !param) return;
    if (count < 0) count = 0;

    char key[256];
    snprintf(key, sizeof(key), "RADIOBUTTON:%s", param);

    /* Already present and well-formed? nothing to do. */
    Variable* existing = get_symbol(st, key);
    if (existing && existing->type == TYPE_MAP && existing->data.map) {
        return;
    }

    /* Build fresh map: { options: [string...], required: int } */
    HashTable* map = create_hash_table(8);
    if (!map) {
        ProPrintfChar("Error: ensure_radio_options_map: create_hash_table failed for %s\n", param);
        return;
    }

    Variable* opts = (Variable*)calloc(1, sizeof(Variable));
    if (!opts) { free_hash_table(map); return; }
    opts->type = TYPE_ARRAY;
    opts->data.array.size = (size_t)count;
    opts->data.array.elements = NULL;

    if (count > 0) {
        opts->data.array.elements = (Variable**)calloc((size_t)count, sizeof(Variable*));
        if (!opts->data.array.elements) {
            free(opts);
            free_hash_table(map);
            return;
        }
    }

    /* Fill options array; bail out safely on any failure. */
    for (int i = 0; i < count; ++i) {
        Variable* sv = (Variable*)calloc(1, sizeof(Variable));
        if (!sv) goto fail;
        sv->type = TYPE_STRING;

        const char* src = (names && names[i]) ? names[i] : "";
        sv->data.string_value = _strdup(src);
        if (!sv->data.string_value) { free(sv); goto fail; }

        opts->data.array.elements[i] = sv;
    }

    Variable* req = (Variable*)calloc(1, sizeof(Variable));
    if (!req) goto fail;
    req->type = TYPE_INTEGER;
    req->data.int_value = required_flag ? 1 : 0;

    Variable* mv = (Variable*)calloc(1, sizeof(Variable));
    if (!mv) { free_variable(req); goto fail; }
    mv->type = TYPE_MAP;
    mv->data.map = map;

    /* Populate map only after all pieces are valid. */
    add_var_to_map(map, "options", opts);
    add_var_to_map(map, "required", req);

    set_symbol(st, key, mv);  /* ownership of mv/map/children transfers to the table */
    return;

fail:
    /* Clean up partial 'opts' array (strings + elements buffer) */
    if (opts) {
        if (opts->type == TYPE_ARRAY && opts->data.array.elements) {
            for (int j = 0; j < count; ++j) {
                if (opts->data.array.elements[j]) {
                    free_variable(opts->data.array.elements[j]);
                }
            }
            free(opts->data.array.elements);
        }
        free(opts);
    }
    /* No entries were inserted into 'map' yet, so a plain free is safe. */
    free_hash_table(map);
}

ProError RadioSelectCallback(char* dialog, char* component, ProAppData app_data) {
    RadioSelectData* data = (RadioSelectData*)app_data;
    if (!data || !data->st || !data->parameter) {
        ProPrintfChar("Error: Invalid data in RadioSelectCallback");
        return PRO_TK_BAD_INPUTS;
    }

    // Retrieve selected names
    int n_names = 0;
    char** names = NULL;
    ProError status = ProUIRadiogroupSelectednamesGet(dialog, component, &n_names, &names);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not get selected names for radio group '%s'", data->parameter);
        return status;
    }
    LogOnlyPrintfChar("Selected radiobutton:", names);

    char* sel_name = NULL;
    if (n_names > 0) {
        sel_name = _strdup(names[0]);  // Assume single selection
        if (!sel_name) {
            ProStringarrayFree(names, n_names);
            return PRO_TK_GENERAL_ERROR;
        }
    }
    else {
        sel_name = _strdup("");  // No selection
    }
    ProStringarrayFree(names, n_names);

    // Get variable
    Variable* var = get_symbol(data->st, data->parameter);
    if (!var) {
        free(sel_name);
        ProPrintfChar("Error: Radio parameter '%s' not found", data->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Retrieve options map for index mapping (if needed)
    char map_key[256];
    snprintf(map_key, sizeof(map_key), "RADIOBUTTON:%s", data->parameter);
    Variable* options_map_var = get_symbol(data->st, map_key);
    if (!options_map_var || options_map_var->type != TYPE_MAP) {
        free(sel_name);
        ProPrintfChar("Error: Options map not found for '%s'", data->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    HashTable* options_map = options_map_var->data.map;
    Variable* options_arr_var = hash_table_lookup(options_map, "options");
    if (!options_arr_var || options_arr_var->type != TYPE_ARRAY) {
        free(sel_name);
        ProPrintfChar("Error: Options array not found for '%s'", data->parameter);
        return PRO_TK_GENERAL_ERROR;
    }
    ArrayData* options_arr = &options_arr_var->data.array;

    // Update based on type
    if (var->type == TYPE_INTEGER) {
        int index = -1;  // Default unselected
        if (sel_name && strlen(sel_name) > 0) {
            for (size_t i = 0; i < options_arr->size; i++) {
                Variable* opt = options_arr->elements[i];
                if (opt->type == TYPE_STRING && opt->data.string_value && strcmp(opt->data.string_value, sel_name) == 0) {
                    index = (int)i;
                    break;
                }
            }
            if (index == -1) {
                free(sel_name);
                ProPrintfChar("Error: Selected name '%s' not found in options for '%s'", sel_name, data->parameter);
                return PRO_TK_GENERAL_ERROR;
            }
        }
        var->data.int_value = index;
        ProPrintfChar("Selected radio index: %d for parameter %s\n", index, data->parameter);
    }
    else if (var->type == TYPE_STRING) {
        free(var->data.string_value);
        var->data.string_value = sel_name;
        if (!var->data.string_value) {
            ProPrintfChar("Error: Memory allocation failed for selected value in '%s'", data->parameter);
            return PRO_TK_GENERAL_ERROR;
        }
        ProPrintfChar("Selected radio: %s for parameter %s\n", var->data.string_value, data->parameter);
    }
    else {
        free(sel_name);
        ProPrintfChar("Error: Unsupported type %d for radio parameter '%s'", var->type, data->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Validate OK button
    status = validate_ok_button(dialog, data->st);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Failed to validate OK button after radio selection in '%s'", data->parameter);
    }
    EPA_ReactiveRefresh(dialog);

    return PRO_TK_NO_ERROR;
}

/* Clamp a radiogroup to its minimum size (plus optional padding). */
static ProError radiogroup_shrink_to_fit(char* dialog, char* rg_id, int width_hint, int height_hint)  /* <=0 means "use min" */
{
    if (!dialog || !rg_id) return PRO_TK_BAD_INPUTS;

    int min_w = 0, min_h = 0;
    ProError st = ProUIRadiogroupMinimumsizeGet(dialog, rg_id, &min_w, &min_h);
    if (st != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: MinimumsizeGet failed for radiogroup '%s'\n", rg_id);
        return st;
    }

    /* Hints are allowed, but clamp to >= minimums per API contract. */
    int w = (width_hint > 0 ? width_hint : min_w);
    int h = (height_hint > 0 ? height_hint : min_h);
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;

    st = ProUIRadiogroupSizeSet(dialog, rg_id, w, h);
    if (st != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: SizeSet(%d x %d) failed for radiogroup '%s'\n", w, h, rg_id);
    }
    return st;
}

ProError OnPictureRadioButtonParam(char* dialog, char* draw_area_name, RadioButtonParamNode* node, SymbolTable* st)
 {
    ProError status;
    if (!dialog || !draw_area_name || !node || !node->parameter || !st) {
        ProPrintfChar("Error: Invalid inputs in OnPictureRadioButtonParam");
        return PRO_TK_BAD_INPUTS;
    }

    // Check if there are options available for the radio buttons
    if (node->option_count == 0) {
        ProPrintfChar("Error: No options provided for radio button group '%s'.\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Evaluate positions from expressions (fallback to 0 if missing or evaluation fails)
    int x_pos = 0;
    if (node->posX) {
        long temp_x;
        status = evaluate_to_int(node->posX, st, &temp_x);
        if (status == PRO_TK_NO_ERROR) {
            x_pos = (int)temp_x;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posX for '%s'; using x=0", node->parameter);
        }
    }
    else {
        ProPrintfChar("Warning: posX missing in ON_PICTURE for '%s'; using x=0", node->parameter);
    }

    int y_pos = 0;
    if (node->posY) {
        long temp_y;
        status = evaluate_to_int(node->posY, st, &temp_y);
        if (status == PRO_TK_NO_ERROR) {
            y_pos = (int)temp_y;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posY for '%s'; using y=0", node->parameter);
        }
    }
    else {
        ProPrintfChar("Warning: posY missing in ON_PICTURE for '%s'; using y=0", node->parameter);
    }

    // Generate unique names for label and radio group component
    char label_id[100];
    snprintf(label_id, sizeof(label_id), "radio_label_%s", node->parameter);
    static int radio_count = 0;
    char rb_component_name[100];
    snprintf(rb_component_name, sizeof(rb_component_name), "radio_group_%s", node->parameter);

    // Convert parameter to wide string
    wchar_t* w_parameter = char_to_wchar(node->parameter);
    if (!w_parameter) {
        ProPrintf(L"Conversion to wide string failed for parameter\n");
        return PRO_TK_GENERAL_ERROR;
    }

    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->parameter, &w_friendly)) {
            /* swap: caller must free the active pointer later */
            free(w_parameter);
            w_parameter = w_friendly;  /* e.g., "ALL SKELS" instead of "ALL_SKELS" */
        }
    }


    // Determine if the required option is present and format the label accordingly
    wchar_t label_text[200];
    if (node->required) {
        swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls (REQUIRED)", w_parameter);

        // Add to REQUIRE_RADIOS array in symbol table
        Variable* req_var = get_symbol(st, "REQUIRE_RADIOS");
        if (!req_var) {
            req_var = malloc(sizeof(Variable));
            if (!req_var) return PRO_TK_GENERAL_ERROR;
            req_var->type = TYPE_ARRAY;
            req_var->data.array.size = 0;
            req_var->data.array.elements = NULL;
            set_symbol(st, "REQUIRE_RADIOS", req_var);
        }
        else if (req_var->type != TYPE_ARRAY) {
            return PRO_TK_GENERAL_ERROR;
        }

        Variable* param_name_var = malloc(sizeof(Variable));
        if (!param_name_var) return PRO_TK_GENERAL_ERROR;
        param_name_var->type = TYPE_STRING;
        param_name_var->data.string_value = _strdup(node->parameter);
        if (!param_name_var->data.string_value) {
            free(param_name_var);
            return PRO_TK_GENERAL_ERROR;
        }

        size_t new_size = req_var->data.array.size + 1;
        Variable** new_elements = realloc(req_var->data.array.elements, new_size * sizeof(Variable*));
        if (!new_elements) {
            free(param_name_var->data.string_value);
            free(param_name_var);
            return PRO_TK_GENERAL_ERROR;
        }
        req_var->data.array.elements = new_elements;
        req_var->data.array.elements[new_size - 1] = param_name_var;
        req_var->data.array.size = new_size;
    }
    else {
        swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls", w_parameter);
    }
    free(w_parameter);

    // Add label to drawing area
    status = ProUIDrawingareaLabelAdd(dialog, draw_area_name, label_id);
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    // Set label text
    status = ProUILabelTextSet(dialog, label_id, label_text);
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    // Set label position
    status = ProUILabelPositionSet(dialog, label_id, x_pos, y_pos);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set position for label '%s'\n", label_id);
        return status;
    }

    // Add the radio group to the drawing area
    status = ProUIDrawingareaRadiogroupAdd(dialog, draw_area_name, rb_component_name);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add radio group '%s' to drawing area.\n", node->parameter);
        return status;
    }

    (void)radiogroup_shrink_to_fit(dialog, rb_component_name, 4, 2);

    // Set the radio group position (below the label for vertical stacking)
    status = ProUIRadiogroupPositionSet(dialog, rb_component_name, x_pos, y_pos + 20);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set position for radio group '%s'\n", rb_component_name);
        return status;
    }

    // Set the radio group orientation to vertical
    status = ProUIRadiogroupOrientationSet(dialog, rb_component_name, 1);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set orientation for radio group '%s'.\n", node->parameter);
        return status;
    }

    // Prepare arrays for button names and labels by evaluating options
    int option_count = (int)node->option_count;
    char** button_names = (char**)malloc(option_count * sizeof(char*));
    wchar_t** button_labels = (wchar_t**)malloc(option_count * sizeof(wchar_t*));
    if (!button_names || !button_labels) {
        ProPrintfChar("Error: Memory allocation failed for radio group '%s'.\n", node->parameter);
        free(button_names);
        free(button_labels);
        return PRO_TK_GENERAL_ERROR;
    }

    // Evaluate each option expression to string and populate names/labels
    for (int i = 0; i < option_count; i++) {
        char* option_str = NULL;
        status = evaluate_to_string(node->options[i], st, &option_str);
        if (status != PRO_TK_NO_ERROR || !option_str) {
            ProPrintfChar("Error: Failed to evaluate option %d for '%s'.\n", i, node->parameter);
            for (int j = 0; j < i; j++) {
                free(button_names[j]);
                free(button_labels[j]);
            }
            free(button_names);
            free(button_labels);
            return status;
        }

        button_labels[i] = char_to_wchar(option_str);
        if (!button_labels[i]) {
            ProPrintfChar("Error: Wide string conversion failed for option %d.\n", i);
            free(option_str);
            for (int j = 0; j < i; j++) {
                free(button_names[j]);
                free(button_labels[j]);
            }
            free(button_names);
            free(button_labels);
            return PRO_TK_GENERAL_ERROR;
        }

        button_names[i] = option_str;  // Use the evaluated string as name (caller frees later)
    }

    // Set the radio group names
    status = ProUIRadiogroupNamesSet(dialog, rb_component_name, option_count, button_names);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set names for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    // Set the radio group labels
    status = ProUIRadiogroupLabelsSet(dialog, rb_component_name, option_count, button_labels);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set labels for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    // Set initial selection to first option if TYPE_INTEGER and options exist
    Variable* var = get_symbol(st, node->parameter);
    if (var && var->type == TYPE_INTEGER && option_count > 0) {
        char* initial_name = button_names[0];  // First option name
        int n_names = 1;
        char* names[1] = { initial_name };
        status = ProUIRadiogroupSelectednamesSet(dialog, rb_component_name, n_names, names);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Error: Could not set initial selection for radio group '%s'.\n", node->parameter);
            goto cleanup;
        }
        var->data.int_value = 0;  // Ensure variable matches UI

        // Log the default selection
        LogOnlyPrintfChar("Default Selection Index: %d ", var->data.int_value);
    }

    // Allocate and initialize callback data
    RadioSelectData* select_data = (RadioSelectData*)malloc(sizeof(RadioSelectData));
    if (!select_data) {
        ProPrintfChar("Error: Memory allocation failed for RadioSelectData of '%s'\n", node->parameter);
        goto cleanup;
    }
    select_data->st = st;
    select_data->parameter = _strdup(node->parameter);
    if (!select_data->parameter) {
        free(select_data);
        ProPrintfChar("Error: Memory allocation failed for parameter name '%s'\n", node->parameter);
        goto cleanup;
    }
    ensure_radio_options_map(st, node->parameter, button_names, option_count, node->required ? 1 : 0);


    // Register the selection callback
    status = ProUIRadiogroupSelectActionSet(dialog, rb_component_name, RadioSelectCallback, (ProAppData)select_data);
    if (status != PRO_TK_NO_ERROR) {
        free(select_data->parameter);
        free(select_data);
        ProPrintfChar("Error: Could not set selection action for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    // Handle tooltip_message if present
    if (node->tooltip_message) {
        char* tooltip_str = NULL;
        status = evaluate_to_string(node->tooltip_message, st, &tooltip_str);
        if (status == PRO_TK_NO_ERROR && tooltip_str) {
            wchar_t* w_tooltip = char_to_wchar(tooltip_str);
            if (w_tooltip) {
                ProUIRadiogroupHelptextSet(dialog, rb_component_name, w_tooltip);
                free(w_tooltip);
            }
            free(tooltip_str);
        }
    }

cleanup:
    // Free allocated memory
    for (int i = 0; i < option_count; i++) {
        free(button_names[i]);
        free(button_labels[i]);
    }
    free(button_names);
    free(button_labels);

    return status;
}

ProError addRadioButtonParam(char* dialog_name, char* parent_layout_name, RadioButtonParamNode* node, int* current_row, int column, SymbolTable* st)
{
    ProError status;

    if (node->option_count == 0) {
        ProPrintfChar("Error: No options provided for radio button group '%s'.\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    static int sub_layout_count = 0;
    char sub_layout_name[100];
    snprintf(sub_layout_name, sizeof(sub_layout_name), "radio_sub_layout_%d", sub_layout_count++);

    ProUIGridopts grid_opts_sub = { 0 };
    grid_opts_sub.horz_cells = 1;
    grid_opts_sub.vert_cells = 1;
    grid_opts_sub.attach_bottom = PRO_B_TRUE;
    grid_opts_sub.attach_left = PRO_B_TRUE;
    grid_opts_sub.attach_right = PRO_B_TRUE;
    grid_opts_sub.attach_top = PRO_B_TRUE;
    grid_opts_sub.column = column;
    grid_opts_sub.row = *current_row;
    (*current_row)++;

    status = ProUILayoutLayoutAdd(dialog_name, parent_layout_name, sub_layout_name, &grid_opts_sub);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add sub-layout for radio group '%s'.\n", node->parameter);
        return status;
    }

    status = ProUILayoutDecorate(dialog_name, sub_layout_name);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not decorate sub-layout for radio group '%s'.\n", node->parameter);
        return status;
    }

    /* Build group title */
    wchar_t* w_parameter = char_to_wchar(node->parameter);
    if (!w_parameter) {
        ProPrintf(L"Conversion to wide string failed for parameter\n");
        return PRO_TK_GENERAL_ERROR;
    }

    /* === NEW: replace with friendly name from sel_list.txt if available === */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->parameter, &w_friendly)) {
            /* swap: caller must free the active pointer later */
            free(w_parameter);
            w_parameter = w_friendly;  /* e.g., "ALL SKELS" instead of "ALL_SKELS" */
        }
    }
    /* === END NEW === */

    wchar_t layout_text[200];
    if (node->required) {
        swprintf(layout_text, sizeof(layout_text) / sizeof(wchar_t), L"%ls (REQUIRED)", w_parameter);

        /* Track required radio groups in symbol table (unchanged) */
        Variable* req_var = get_symbol(st, "REQUIRE_RADIOS");
        if (!req_var) {
            req_var = (Variable*)malloc(sizeof(Variable));
            if (!req_var) { free(w_parameter); return PRO_TK_GENERAL_ERROR; }
            req_var->type = TYPE_ARRAY;
            req_var->data.array.size = 0;
            req_var->data.array.elements = NULL;
            set_symbol(st, "REQUIRE_RADIOS", req_var);
        }
        else if (req_var->type != TYPE_ARRAY) {
            free(w_parameter);
            return PRO_TK_GENERAL_ERROR;
        }

        Variable* param_name_var = (Variable*)malloc(sizeof(Variable));
        if (!param_name_var) { free(w_parameter); return PRO_TK_GENERAL_ERROR; }
        param_name_var->type = TYPE_STRING;
        param_name_var->data.string_value = _strdup(node->parameter);
        if (!param_name_var->data.string_value) { free(param_name_var); free(w_parameter); return PRO_TK_GENERAL_ERROR; }

        size_t new_size = req_var->data.array.size + 1;
        Variable** new_elements = (Variable**)realloc(req_var->data.array.elements, new_size * sizeof(Variable*));
        if (!new_elements) {
            free(param_name_var->data.string_value);
            free(param_name_var);
            free(w_parameter);
            return PRO_TK_GENERAL_ERROR;
        }
        req_var->data.array.elements = new_elements;
        req_var->data.array.elements[new_size - 1] = param_name_var;
        req_var->data.array.size = new_size;
    }
    else {
        swprintf(layout_text, sizeof(layout_text) / sizeof(wchar_t), L"%ls", w_parameter);
    }

    status = ProUILayoutTextSet(dialog_name, sub_layout_name, layout_text);
    free(w_parameter);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set text for sub-layout '%s'.\n", node->parameter);
        return status;
    }

    char rb_component_name[100];
    snprintf(rb_component_name, sizeof(rb_component_name), "radio_group_%s", node->parameter);

    ProUIGridopts grid_opts_rb = { 0 };
    grid_opts_rb.column = 0;
    grid_opts_rb.row = 0;
    grid_opts_rb.horz_cells = 1;
    grid_opts_rb.vert_cells = 1;

    status = ProUILayoutRadiogroupAdd(dialog_name, sub_layout_name, rb_component_name, &grid_opts_rb);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add radio group '%s' to sub-layout.\n", node->parameter);
        return status;
    }

    status = ProUIRadiogroupOrientationSet(dialog_name, rb_component_name, 1);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set orientation for radio group '%s'.\n", node->parameter);
        return status;
    }

    int option_count = (int)node->option_count;
    char** button_names = (char**)malloc(option_count * sizeof(char*));
    wchar_t** button_labels = (wchar_t**)malloc(option_count * sizeof(wchar_t*));
    if (!button_names || !button_labels) {
        ProPrintfChar("Error: Memory allocation failed for radio group '%s'.\n", node->parameter);
        free(button_names);
        free(button_labels);
        return PRO_TK_GENERAL_ERROR;
    }

    for (int i = 0; i < option_count; i++) {
        char* option_str = NULL;
        status = evaluate_to_string(node->options[i], st, &option_str);
        if (status != PRO_TK_NO_ERROR || !option_str) {
            ProPrintfChar("Error: Failed to evaluate option %d for '%s'.\n", i, node->parameter);
            for (int j = 0; j < i; j++) {
                free(button_names[j]);
                free(button_labels[j]);
            }
            free(button_names);
            free(button_labels);
            return status;
        }

        button_labels[i] = char_to_wchar(option_str);
        if (!button_labels[i]) {
            ProPrintfChar("Error: Wide string conversion failed for option %d.\n", i);
            free(option_str);
            for (int j = 0; j < i; j++) {
                free(button_names[j]);
                free(button_labels[j]);
            }
            free(button_names);
            free(button_labels);
            return PRO_TK_GENERAL_ERROR;
        }

        button_names[i] = option_str; /* keep; freed in cleanup */
    }

    status = ProUIRadiogroupNamesSet(dialog_name, rb_component_name, option_count, button_names);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set names for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    status = ProUIRadiogroupLabelsSet(dialog_name, rb_component_name, option_count, button_labels);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set labels for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    Variable* var = get_symbol(st, node->parameter);
    if (var && var->type == TYPE_INTEGER && option_count > 0) {
        char* initial_name = button_names[0];
        int n_names = 1;
        char* names[1] = { initial_name };
        status = ProUIRadiogroupSelectednamesSet(dialog_name, rb_component_name, n_names, names);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Error: Could not set initial selection for radio group '%s'.\n", node->parameter);
            goto cleanup;
        }
        var->data.int_value = 0;
        LogOnlyPrintfChar("Default Selection Index: %d ", var->data.int_value);
    }

    RadioSelectData* select_data = (RadioSelectData*)malloc(sizeof(RadioSelectData));
    if (!select_data) {
        ProPrintfChar("Error: Memory allocation failed for RadioSelectData of '%s'\n", node->parameter);
        goto cleanup;
    }
    select_data->st = st;
    select_data->parameter = _strdup(node->parameter);
    if (!select_data->parameter) {
        free(select_data);
        ProPrintfChar("Error: Memory allocation failed for parameter name '%s'\n", node->parameter);
        goto cleanup;
    }

    ensure_radio_options_map(st, node->parameter, button_names, option_count, node->required ? 1 : 0);

    status = ProUIRadiogroupSelectActionSet(dialog_name, rb_component_name, RadioSelectCallback, (ProAppData)select_data);
    if (status != PRO_TK_NO_ERROR) {
        free(select_data->parameter);
        free(select_data);
        ProPrintfChar("Error: Could not set selection action for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    if (node->tooltip_message) {
        char* tooltip_str = NULL;
        status = evaluate_to_string(node->tooltip_message, st, &tooltip_str);
        if (status == PRO_TK_NO_ERROR && tooltip_str) {
            wchar_t* w_tooltip = char_to_wchar(tooltip_str);
            if (w_tooltip) {
                ProUIRadiogroupHelptextSet(dialog_name, rb_component_name, w_tooltip);
                free(w_tooltip);
            }
            free(tooltip_str);
        }
    }

cleanup:
    for (int i = 0; i < option_count; i++) {
        free(button_names[i]);
        free(button_labels[i]);
    }
    free(button_names);
    free(button_labels);

    return status;
}

/*=================================================*\
* 
* USER_SELECT command to display the push buttons
* -- This displays the pushbutton within the gui but no logic 
* to make the pushbutton do an action--
* 
\*=================================================*/
/* static helper: set or create a TYPE_BOOL entry inside a select's map */
void set_bool_in_map(HashTable* map, const char* key, int on)
{
    if (!map || !key) return;
    Variable* v = hash_table_lookup(map, key);
    if (!v) {
        v = (Variable*)calloc(1, sizeof(Variable));
        if (!v) return;
        v->type = TYPE_BOOL;
        v->data.int_value = on ? 1 : 0;
        hash_table_insert(map, key, v);
    }
    else {
        // normalize type; keep it simple and robust 
        v->type = TYPE_BOOL;
        v->data.int_value = on ? 1 : 0;
    }
}

ProError OnPictureUserSelect(char* dialog, char* draw_area_name, UserSelectNode* node, SymbolTable* st)
{
    if (!dialog || !draw_area_name || !node || !node->reference || !st)
        return PRO_TK_BAD_INPUTS;
    LogOnlyPrintfChar("Entering OnPIctureUserSelect");
    ProError status;

    // Idempotence guard: if we already created this ref's widgets, do nothing
    {
        Variable* guard = get_symbol(st, node->reference);
        if (guard && guard->type == TYPE_MAP) {
            Variable* existed = hash_table_lookup(guard->data.map, "button_id");
            if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
                ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->reference);
                return PRO_TK_NO_ERROR;
            }
        }
    }

    // Unique ids (reference-based)
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s", node->reference);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s", node->reference);

    // Persist control IDs and initialize flags if absent (pre-tagged IF flags are preserved)
    {
        Variable* sel_var = get_symbol(st, node->reference);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* vbtn = (Variable*)malloc(sizeof(Variable));
            Variable* vdar = (Variable*)malloc(sizeof(Variable));
            if (vbtn && vdar) {
                vbtn->type = TYPE_STRING; vbtn->data.string_value = _strdup(button_id);
                vdar->type = TYPE_STRING; vdar->data.string_value = _strdup(button_draw);
                hash_table_insert(sel_var->data.map, "button_id", vbtn);
                hash_table_insert(sel_var->data.map, "draw_area_id", vdar);
            }

            if (!hash_table_lookup(sel_var->data.map, "ui_enabled"))
                set_bool_in_map(sel_var->data.map, "ui_enabled", 1);
            if (!hash_table_lookup(sel_var->data.map, "ui_required"))
                set_bool_in_map(sel_var->data.map, "ui_required", 1);
        }
    }

    // Read flags after persistence (respect any IF pre-tagging)
    int ui_enabled = 1;
    int ui_required = 1;
    {
        Variable* sel_var = get_symbol(st, node->reference);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* en = hash_table_lookup(sel_var->data.map, "ui_enabled");
            Variable* rq = hash_table_lookup(sel_var->data.map, "ui_required");
            ui_enabled = (en && (en->type == TYPE_BOOL || en->type == TYPE_INTEGER)) ? (en->data.int_value != 0) : 1;
            ui_required = (rq && (rq->type == TYPE_BOOL || rq->type == TYPE_INTEGER)) ? (rq->data.int_value != 0) : 1;
        }
    }

    // Evaluate positions from expressions (fallback to 0 if missing or evaluation fails)
    int x_pos = 0;
    if (node->posX) {
        long temp_x;
        status = evaluate_to_int(node->posX, st, &temp_x);
        if (status == PRO_TK_NO_ERROR) {
            x_pos = (int)temp_x;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posX for '%s'; using x=0", node->reference);
        }
    }
    else {
        ProPrintfChar("Warning: posX missing in ON_PICTURE for '%s'; using x=0", node->reference);
    }

    int y_pos = 0;
    if (node->posY) {
        long temp_y;
        status = evaluate_to_int(node->posY, st, &temp_y);
        if (status == PRO_TK_NO_ERROR) {
            y_pos = (int)temp_y;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posY for '%s'; using y=0", node->reference);
        }
    }
    else {
        ProPrintfChar("Warning: posY missing in ON_PICTURE for '%s'; using y=0", node->reference);
    }

    // Container drawing area
    status = ProUIDrawingareaDrawingareaAdd(dialog, draw_area_name, button_draw);
    if (status != PRO_TK_NO_ERROR) return status;

    if (ui_required) {
        ProUILayoutTextSet(dialog, button_draw, L"REQUIRED");
    }

    status = ProUIDrawingareaDrawingheightSet(dialog, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;

    ProUIDrawingareaPositionSet(dialog, button_draw, x_pos, y_pos);

    // Pushbutton inside the child drawing area
    status = ProUIDrawingareaPushbuttonAdd(dialog, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    // Place the button; final fit later
    ProUIPushbuttonPositionSet(dialog, button_id, 0, 1);

    // Keep REQUIRED_SELECTS in sync with ui_required
    if (ui_required) require_select(st, node->reference);
    else             unrequire_select(st, node->reference);

    // Button text
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->reference);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    {
        Variable* select_var = get_symbol(st, node->reference);
        if (select_var && select_var->type == TYPE_MAP) {
            Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
            if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
                snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
            }
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->reference, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }


    status = ProUIPushbuttonTextSet(dialog, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    // Tooltip (optional)
    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // Wire collector callback
    {
        UserSelectData* sel_data = (UserSelectData*)calloc(1, sizeof(UserSelectData));
        if (!sel_data) return PRO_TK_GENERAL_ERROR;
        sel_data->st = st;
        sel_data->node = node;
        snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
        snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

        status = ProUIPushbuttonActivateActionSet(dialog, button_id, UserSelectCallback, (ProAppData)sel_data);
        if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }
    }

    // Auto-fit the button inside its drawing area
    {
        status = fit_pushbutton_to_drawingarea(dialog, button_draw, button_id);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
        }

        ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
        if (fit_data) {
            snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
            snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
            status = ProUIDrawingareaPostmanagenotifyActionSet(dialog, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
                free(fit_data);
            }
        }
    }

    // Now apply initial enabled/disabled state to actual widgets
    if (!ui_enabled) {
        ProUIPushbuttonDisable(dialog, button_id);
        ProUIDrawingareaDisable(dialog, button_draw);
    }
    else {
        ProUIPushbuttonEnable(dialog, button_id);
        ProUIDrawingareaEnable(dialog, button_draw);
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->reference);
        status = ProUIDrawingareaUpdateActionSet(dialog, button_draw, UserSelectUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }

    /* Force initial repaint to apply color via callback */
    (void)ProUIDrawingareaClear(dialog, button_draw);  /* Triggers update callback for redraw */

    return PRO_TK_NO_ERROR;
}

ProError addUserSelect(char* dialog_name, char* parent_layout_name, UserSelectNode* node, int* current_row, int column, SymbolTable* st)
{
    if (!dialog_name || !parent_layout_name || !node || !node->reference || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    // Idempotence guard: if we already created this ref's widgets, do nothing
    {
        Variable* guard = get_symbol(st, node->reference);
        if (guard && guard->type == TYPE_MAP) {
            Variable* existed = hash_table_lookup(guard->data.map, "button_id");
            if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
                ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->reference);
                return PRO_TK_NO_ERROR;
            }
        }
    }

    // Unique ids (row-aware)
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s_%d", node->reference, *current_row);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s_%d", node->reference, *current_row);

    // Persist control IDs and initialize flags if absent (pre-tagged IF flags are preserved)
    {
        Variable* sel_var = get_symbol(st, node->reference);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* vbtn = (Variable*)malloc(sizeof(Variable));
            Variable* vdar = (Variable*)malloc(sizeof(Variable));
            if (vbtn && vdar) {
                vbtn->type = TYPE_STRING; vbtn->data.string_value = _strdup(button_id);
                vdar->type = TYPE_STRING; vdar->data.string_value = _strdup(button_draw);
                hash_table_insert(sel_var->data.map, "button_id", vbtn);
                hash_table_insert(sel_var->data.map, "draw_area_id", vdar);
            }

            if (!hash_table_lookup(sel_var->data.map, "ui_enabled"))
                set_bool_in_map(sel_var->data.map, "ui_enabled", 1);
            if (!hash_table_lookup(sel_var->data.map, "ui_required"))
                set_bool_in_map(sel_var->data.map, "ui_required", 1);
        }
    }

    // Read flags after persistence (respect any IF pre-tagging)
    int ui_enabled = 1;
    int ui_required = 1;
    {
        Variable* sel_var = get_symbol(st, node->reference);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* en = hash_table_lookup(sel_var->data.map, "ui_enabled");
            Variable* rq = hash_table_lookup(sel_var->data.map, "ui_required");
            ui_enabled = (en && (en->type == TYPE_BOOL || en->type == TYPE_INTEGER)) ? (en->data.int_value != 0) : 1;
            ui_required = (rq && (rq->type == TYPE_BOOL || rq->type == TYPE_INTEGER)) ? (rq->data.int_value != 0) : 1;
        }
    }

    // Container drawing area in the grid
    ProUIGridopts grid = (ProUIGridopts){ 0 };
    grid.row = *current_row;
    grid.column = column;
    grid.horz_cells = 1;
    grid.vert_cells = 1;
    grid.attach_left = PRO_B_TRUE;
    grid.attach_right = PRO_B_TRUE;
    grid.bottom_offset = 3;


    status = ProUILayoutDrawingareaAdd(dialog_name, parent_layout_name, button_draw, &grid);
    if (status != PRO_TK_NO_ERROR) return status;

    if (ui_required) {
        ProUILayoutTextSet(dialog_name, button_draw, L"REQUIRED");
    }

    status = ProUIDrawingareaDrawingheightSet(dialog_name, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog_name, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;

    ProUIDrawingareaPositionSet(dialog_name, button_draw, 0, 0);

    // Pushbutton inside the child drawing area
    status = ProUIDrawingareaPushbuttonAdd(dialog_name, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    // Place the button; final fit later
    ProUIPushbuttonPositionSet(dialog_name, button_id, 0, 1);

    // Keep REQUIRED_SELECTS in sync with ui_required
    if (ui_required) require_select(st, node->reference);
    else             unrequire_select(st, node->reference);

    // Button text
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->reference);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    {
        Variable* select_var = get_symbol(st, node->reference);
        if (select_var && select_var->type == TYPE_MAP) {
            Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
            if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
                snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
            }
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->reference, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }

    status = ProUIPushbuttonTextSet(dialog_name, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    // Tooltip (optional)
    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog_name, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // Wire collector callback
    {
        UserSelectData* sel_data = (UserSelectData*)calloc(1, sizeof(UserSelectData));
        if (!sel_data) return PRO_TK_GENERAL_ERROR;
        sel_data->st = st;
        sel_data->node = node;
        snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
        snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

        status = ProUIPushbuttonActivateActionSet(dialog_name, button_id, UserSelectCallback, (ProAppData)sel_data);
        if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }
    }

    // Auto-fit the button inside its drawing area
    {
        status = fit_pushbutton_to_drawingarea(dialog_name, button_draw, button_id);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
        }

        ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
        if (fit_data) {
            snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
            snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
            status = ProUIDrawingareaPostmanagenotifyActionSet(dialog_name, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
                free(fit_data);
            }
        }
    }

    // Now apply initial enabled/disabled state to actual widgets
    if (!ui_enabled) {
        ProUIPushbuttonDisable(dialog_name, button_id);
        ProUIDrawingareaDisable(dialog_name, button_draw);
    }
    else {
        ProUIPushbuttonEnable(dialog_name, button_id);
        ProUIDrawingareaEnable(dialog_name, button_draw);
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->reference);
        status = ProUIDrawingareaUpdateActionSet(dialog_name, button_draw, UserSelectUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }

    /* Force initial repaint to apply color via callback */
    (void)ProUIDrawingareaClear(dialog_name, button_draw);  /* Triggers update callback for redraw */

    (*current_row)++;
    return PRO_TK_NO_ERROR;
}

ProError OnPictureUserSelectOptional(char* dialog, char* draw_area_name, UserSelectOptionalNode* node, SymbolTable* st)
{
    if (!dialog || !draw_area_name || !node || !node->reference || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    // idempotence guard (unchanged) 
    Variable* sel_var_guard = get_symbol(st, node->reference);
    if (sel_var_guard && sel_var_guard->type == TYPE_MAP) {
        Variable* existed = hash_table_lookup(sel_var_guard->data.map, "button_id");
        if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
            ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->reference);
            return PRO_TK_NO_ERROR;
        }
    }
    // Unique ids (reference-based)
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s", node->reference);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s", node->reference);

    // Persist control IDs into the select variable's map for later toggling 
    Variable* sel_var = get_symbol(st, node->reference);
    if (sel_var && sel_var->type == TYPE_MAP)
    {
        Variable* vbtn = (Variable*)malloc(sizeof(Variable));
        Variable* vdar = (Variable*)malloc(sizeof(Variable));
        if (vbtn && vdar)
        {
            vbtn->type = TYPE_STRING;
            vbtn->data.string_value = _strdup(button_id);
            vdar->type = TYPE_STRING;
            vdar->data.string_value = _strdup(button_draw);
            hash_table_insert(sel_var->data.map, "button_id", vbtn);
            hash_table_insert(sel_var->data.map, "draw_area_id", vdar);

            // initialize runtime UI state flags 
            set_bool_in_map(sel_var->data.map, "ui_enabled", 1);  // starts enabled 
            // ui_required mirrors enabled; REQUIRED_SELECTS is the source of truth 
            set_bool_in_map(sel_var->data.map, "ui_required", 1);

            LogOnlyPrintfChar("Drawingarea ID: %s", vdar->data.string_value);
        }
    }

    // Evaluate positions from expressions (fallback to 0 if missing or evaluation fails)
    int x_pos = 0;
    if (node->posX) {
        long temp_x;
        status = evaluate_to_int(node->posX, st, &temp_x);
        if (status == PRO_TK_NO_ERROR) {
            x_pos = (int)temp_x;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posX for '%s'; using x=0", node->reference);
        }
    }
    else {
        ProPrintfChar("Warning: posX missing in ON_PICTURE for '%s'; using x=0", node->reference);
    }

    int y_pos = 0;
    if (node->posY) {
        long temp_y;
        status = evaluate_to_int(node->posY, st, &temp_y);
        if (status == PRO_TK_NO_ERROR) {
            y_pos = (int)temp_y;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posY for '%s'; using y=0", node->reference);
        }
    }
    else {
        ProPrintfChar("Warning: posY missing in ON_PICTURE for '%s'; using y=0", node->reference);
    }

    // Container drawing area
    status = ProUIDrawingareaDrawingareaAdd(dialog, draw_area_name, button_draw);
    if (status != PRO_TK_NO_ERROR) return status;


    status = ProUIDrawingareaDrawingheightSet(dialog, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;

    ProUIDrawingareaPositionSet(dialog, button_draw, x_pos, y_pos);

    // Pushbutton inside the child drawing area
    status = ProUIDrawingareaPushbuttonAdd(dialog, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    // Place the button; final fit later
    ProUIPushbuttonPositionSet(dialog, button_id, 0, 1);

    unrequire_select(st, node->reference);

    // Button text
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->reference);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    {
        Variable* select_var = get_symbol(st, node->reference);
        if (select_var && select_var->type == TYPE_MAP) {
            Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
            if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
                snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
            }
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->reference, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }


    status = ProUIPushbuttonTextSet(dialog, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    // Tooltip (optional)
    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // Wire collector callback
    {
        UserSelectOptionalData* sel_data = (UserSelectOptionalData*)calloc(1, sizeof(UserSelectOptionalData));
        if (!sel_data) return PRO_TK_GENERAL_ERROR;
        sel_data->st = st;
        sel_data->node = node;
        snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
        snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

        status = ProUIPushbuttonActivateActionSet(dialog, button_id, UserSelectCallback, (ProAppData)sel_data);
        if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }
    }

    // Auto-fit the button inside its drawing area
    {
        status = fit_pushbutton_to_drawingarea(dialog, button_draw, button_id);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
        }

        ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
        if (fit_data) {
            snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
            snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
            status = ProUIDrawingareaPostmanagenotifyActionSet(dialog, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
                free(fit_data);
            }
        }
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->reference);
        status = ProUIDrawingareaUpdateActionSet(dialog, button_draw, UserSelectUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }

    /* Force initial repaint to apply color via callback */
    (void)ProUIDrawingareaClear(dialog, button_draw);  /* Triggers update callback for redraw */

    return PRO_TK_NO_ERROR;

}

ProError addUserSelectOptional(char* dialog_name, char* parent_layout_name, UserSelectOptionalNode* node, int* current_row, int column, SymbolTable* st)
{
    if (!dialog_name || !parent_layout_name || !node || !node->reference || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    // idempotence guard (unchanged) 
    Variable* sel_var_guard = get_symbol(st, node->reference);
    if (sel_var_guard && sel_var_guard->type == TYPE_MAP) {
        Variable* existed = hash_table_lookup(sel_var_guard->data.map, "button_id");
        if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
            ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->reference);
            return PRO_TK_NO_ERROR;
        }
    }

    // unique ids (row-aware, matches your current scheme) 
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s_%d", node->reference, *current_row);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s_%d", node->reference, *current_row);

    // Persist control IDs into the select variable's map for later toggling 
    Variable* sel_var = get_symbol(st, node->reference);
    if (sel_var && sel_var->type == TYPE_MAP)
    {
        Variable* vbtn = (Variable*)malloc(sizeof(Variable));
        Variable* vdar = (Variable*)malloc(sizeof(Variable));
        if (vbtn && vdar)
        {
            vbtn->type = TYPE_STRING;
            vbtn->data.string_value = _strdup(button_id);
            vdar->type = TYPE_STRING;
            vdar->data.string_value = _strdup(button_draw);
            hash_table_insert(sel_var->data.map, "button_id", vbtn);
            hash_table_insert(sel_var->data.map, "draw_area_id", vdar);

            // initialize runtime UI state flags 
            set_bool_in_map(sel_var->data.map, "ui_enabled", 1);  // starts enabled 
            // ui_required mirrors enabled; REQUIRED_SELECTS is the source of truth 
            set_bool_in_map(sel_var->data.map, "ui_required", 1);

            LogOnlyPrintfChar("Drawingarea ID: %s", vdar->data.string_value);
        }
    }

    // add drawing area 
    ProUIGridopts grid = (ProUIGridopts){ 0 };
    grid.row = *current_row;
    grid.column = column;
    grid.horz_cells = 1;
    grid.vert_cells = 1;
    grid.attach_left = PRO_B_TRUE;
    grid.attach_right = PRO_B_TRUE;
    grid.bottom_offset = 3;

    status = ProUILayoutDrawingareaAdd(dialog_name, parent_layout_name, button_draw, &grid);
    if (status != PRO_TK_NO_ERROR) return status;

    // optional title/border 
    ProUILayoutTextSet(dialog_name, button_draw, L"Optional");

    // initial size 
    status = ProUIDrawingareaDrawingheightSet(dialog_name, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog_name, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;


    // pushbutton inside the drawing area 
    status = ProUIDrawingareaPushbuttonAdd(dialog_name, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;


    // initial position (final size via fit helper) 
    ProUIPushbuttonPositionSet(dialog_name, button_id, 0, 1);

    unrequire_select(st, node->reference);

    // label text 
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->reference);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    Variable* select_var = get_symbol(st, node->reference);
    if (select_var && select_var->type == TYPE_MAP) {
        Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
        if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
            snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->reference, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }

    status = ProUIPushbuttonTextSet(dialog_name, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog_name, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // wire collector callback 
    UserSelectOptionalData* sel_data = (UserSelectOptionalData*)calloc(1, sizeof(UserSelectOptionalData));
    if (!sel_data) return PRO_TK_GENERAL_ERROR;
    sel_data->st = st;
    sel_data->node = node;
    snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
    snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

    status = ProUIPushbuttonActivateActionSet(dialog_name, button_id, UserSelectCallback, (ProAppData)sel_data);
    if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }

    // keep button fit to drawing area during resizes 
    status = fit_pushbutton_to_drawingarea(dialog_name, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
    }

    ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
    if (fit_data) {
        snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
        snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
        status = ProUIDrawingareaPostmanagenotifyActionSet(dialog_name, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
            free(fit_data);
        }
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->reference);
        status = ProUIDrawingareaUpdateActionSet(dialog_name, button_draw, UserSelectOptionalUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }


    // next row 
    (*current_row)++;

    return PRO_TK_NO_ERROR;
}


/*=================================================*\
* 
* USER_SELECT_MULTIPLE Display functions
* 
* 
\*=================================================*/
ProError OnPictureUserSelectMultiple(char* dialog, char* draw_area_name, UserSelectMultipleNode* node, SymbolTable* st)
{
    if (!dialog || !draw_area_name || !node || !node->array || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    // Idempotence guard: if we already created this ref's widgets, do nothing
    {
        Variable* guard = get_symbol(st, node->array);
        if (guard && guard->type == TYPE_MAP) {
            Variable* existed = hash_table_lookup(guard->data.map, "button_id");
            if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
                ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->array);
                return PRO_TK_NO_ERROR;
            }
        }
    }

    // Unique ids (reference-based)
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s", node->array);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s", node->array);

    // Persist control IDs and initialize flags if absent (pre-tagged IF flags are preserved)
    {
        Variable* sel_var = get_symbol(st, node->array);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* vbtn = (Variable*)malloc(sizeof(Variable));
            Variable* vdar = (Variable*)malloc(sizeof(Variable));
            if (vbtn && vdar) {
                vbtn->type = TYPE_STRING; vbtn->data.string_value = _strdup(button_id);
                vdar->type = TYPE_STRING; vdar->data.string_value = _strdup(button_draw);
                hash_table_insert(sel_var->data.map, "button_id", vbtn);
                hash_table_insert(sel_var->data.map, "draw_area_id", vdar);
            }

            if (!hash_table_lookup(sel_var->data.map, "ui_enabled"))
                set_bool_in_map(sel_var->data.map, "ui_enabled", 1);
            if (!hash_table_lookup(sel_var->data.map, "ui_required"))
                set_bool_in_map(sel_var->data.map, "ui_required", 1);
        }
    }

    // Read flags after persistence (respect any IF pre-tagging)
    int ui_enabled = 1;
    int ui_required = 1;
    {
        Variable* sel_var = get_symbol(st, node->array);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* en = hash_table_lookup(sel_var->data.map, "ui_enabled");
            Variable* rq = hash_table_lookup(sel_var->data.map, "ui_required");
            ui_enabled = (en && (en->type == TYPE_BOOL || en->type == TYPE_INTEGER)) ? (en->data.int_value != 0) : 1;
            ui_required = (rq && (rq->type == TYPE_BOOL || rq->type == TYPE_INTEGER)) ? (rq->data.int_value != 0) : 1;
        }
    }

    // Evaluate positions from expressions (fallback to 0 if missing or evaluation fails)
    int x_pos = 0;
    if (node->posX) {
        long temp_x;
        status = evaluate_to_int(node->posX, st, &temp_x);
        if (status == PRO_TK_NO_ERROR) {
            x_pos = (int)temp_x;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posX for '%s'; using x=0", node->array);
        }
    }
    else {
        ProPrintfChar("Warning: posX missing in ON_PICTURE for '%s'; using x=0", node->array);
    }

    int y_pos = 0;
    if (node->posY) {
        long temp_y;
        status = evaluate_to_int(node->posY, st, &temp_y);
        if (status == PRO_TK_NO_ERROR) {
            y_pos = (int)temp_y;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posY for '%s'; using y=0", node->array);
        }
    }
    else {
        ProPrintfChar("Warning: posY missing in ON_PICTURE for '%s'; using y=0", node->array);
    }

    // Container drawing area
    status = ProUIDrawingareaDrawingareaAdd(dialog, draw_area_name, button_draw);
    if (status != PRO_TK_NO_ERROR) return status;

    if (ui_required) {
        ProUILayoutTextSet(dialog, button_draw, L"REQUIRED");
    }

    status = ProUIDrawingareaDrawingheightSet(dialog, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;

    ProUIDrawingareaPositionSet(dialog, button_draw, x_pos, y_pos);

    // Pushbutton inside the child drawing area
    status = ProUIDrawingareaPushbuttonAdd(dialog, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    // Place the button; final fit later
    ProUIPushbuttonPositionSet(dialog, button_id, 0, 1);

    // Keep REQUIRED_SELECTS in sync with ui_required
    if (ui_required) require_select(st, node->array);
    else             unrequire_select(st, node->array);

    // Button text
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->array);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    {
        Variable* select_var = get_symbol(st, node->array);
        if (select_var && select_var->type == TYPE_MAP) {
            Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
            if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
                snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
            }
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->array, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }


    status = ProUIPushbuttonTextSet(dialog, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    // Tooltip (optional)
    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // Wire collector callback
    {
        UserSelectMultipleData* sel_data = (UserSelectMultipleData*)calloc(1, sizeof(UserSelectMultipleData));
        if (!sel_data) return PRO_TK_GENERAL_ERROR;
        sel_data->st = st;
        sel_data->node = node;
        snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
        snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

        status = ProUIPushbuttonActivateActionSet(dialog, button_id, UserSelectCallback, (ProAppData)sel_data);
        if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }
    }

    // Auto-fit the button inside its drawing area
    {
        status = fit_pushbutton_to_drawingarea(dialog, button_draw, button_id);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
        }

        ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
        if (fit_data) {
            snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
            snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
            status = ProUIDrawingareaPostmanagenotifyActionSet(dialog, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
                free(fit_data);
            }
        }
    }

    // Now apply initial enabled/disabled state to actual widgets
    if (!ui_enabled) {
        ProUIPushbuttonDisable(dialog, button_id);
        ProUIDrawingareaDisable(dialog, button_draw);
    }
    else {
        ProUIPushbuttonEnable(dialog, button_id);
        ProUIDrawingareaEnable(dialog, button_draw);
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->array);
        status = ProUIDrawingareaUpdateActionSet(dialog, button_draw, UserSelectUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }

    /* Force initial repaint to apply color via callback */
    (void)ProUIDrawingareaClear(dialog, button_draw);  /* Triggers update callback for redraw */

    return PRO_TK_NO_ERROR;

}

ProError addUserSelectMultiple(char* dialog_name, char* parent_layout_name, UserSelectMultipleNode* node, int* current_row, int column, SymbolTable* st)
{
    if (!dialog_name || !parent_layout_name || !node || !node->array || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    // Idempotence guard: if we already created this ref's widgets, do nothing
    {
        Variable* guard = get_symbol(st, node->array);
        if (guard && guard->type == TYPE_MAP) {
            Variable* existed = hash_table_lookup(guard->data.map, "button_id");
            if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
                ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->array);
                return PRO_TK_NO_ERROR;
            }
        }
    }


    // Unique ids (row-aware)
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s_%d", node->array, *current_row);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s_%d", node->array, *current_row);

    // Persist control IDs and initialize flags if absent (pre-tagged IF flags are preserved)
    {
        Variable* sel_var = get_symbol(st, node->array);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* vbtn = (Variable*)malloc(sizeof(Variable));
            Variable* vdar = (Variable*)malloc(sizeof(Variable));
            if (vbtn && vdar) {
                vbtn->type = TYPE_STRING; vbtn->data.string_value = _strdup(button_id);
                vdar->type = TYPE_STRING; vdar->data.string_value = _strdup(button_draw);
                hash_table_insert(sel_var->data.map, "button_id", vbtn);
                hash_table_insert(sel_var->data.map, "draw_area_id", vdar);
            }

            if (!hash_table_lookup(sel_var->data.map, "ui_enabled"))
                set_bool_in_map(sel_var->data.map, "ui_enabled", 1);
            if (!hash_table_lookup(sel_var->data.map, "ui_required"))
                set_bool_in_map(sel_var->data.map, "ui_required", 1);
        }
    }

    // Read flags after persistence (respect any IF pre-tagging)
    int ui_enabled = 1;
    int ui_required = 1;
    {
        Variable* sel_var = get_symbol(st, node->array);
        if (sel_var && sel_var->type == TYPE_MAP && sel_var->data.map) {
            Variable* en = hash_table_lookup(sel_var->data.map, "ui_enabled");
            Variable* rq = hash_table_lookup(sel_var->data.map, "ui_required");
            ui_enabled = (en && (en->type == TYPE_BOOL || en->type == TYPE_INTEGER)) ? (en->data.int_value != 0) : 1;
            ui_required = (rq && (rq->type == TYPE_BOOL || rq->type == TYPE_INTEGER)) ? (rq->data.int_value != 0) : 1;
        }
    }

    // Container drawing area in the grid
    ProUIGridopts grid = (ProUIGridopts){ 0 };
    grid.row = *current_row;
    grid.column = column;
    grid.horz_cells = 1;
    grid.vert_cells = 1;
    grid.attach_left = PRO_B_TRUE;
    grid.attach_right = PRO_B_TRUE;
    grid.bottom_offset = 3;

    status = ProUILayoutDrawingareaAdd(dialog_name, parent_layout_name, button_draw, &grid);
    if (status != PRO_TK_NO_ERROR) return status;

    if (ui_required) {
        ProUILayoutTextSet(dialog_name, button_draw, L"REQUIRED");
    }

    status = ProUIDrawingareaDrawingheightSet(dialog_name, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog_name, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;

    ProUIDrawingareaPositionSet(dialog_name, button_draw, 0, 0);

    // Pushbutton inside the child drawing area
    status = ProUIDrawingareaPushbuttonAdd(dialog_name, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    // Place the button; final fit later
    ProUIPushbuttonPositionSet(dialog_name, button_id, 0, 1);

    // Keep REQUIRED_SELECTS in sync with ui_required
    if (ui_required) require_select(st, node->array);
    else             unrequire_select(st, node->array);

    // Button text
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->array);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    {
        Variable* select_var = get_symbol(st, node->array);
        if (select_var && select_var->type == TYPE_MAP) {
            Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
            if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
                snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
            }
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->array, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }

    status = ProUIPushbuttonTextSet(dialog_name, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    // Tooltip (optional)
    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog_name, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // Wire collector callback
    {
        UserSelectMultipleData* sel_data = (UserSelectMultipleData*)calloc(1, sizeof(UserSelectMultipleData));
        if (!sel_data) return PRO_TK_GENERAL_ERROR;
        sel_data->st = st;
        sel_data->node = node;
        snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
        snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

        status = ProUIPushbuttonActivateActionSet(dialog_name, button_id, UserSelectMultipleCallback, (ProAppData)sel_data);
        if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }
    }

    // Auto-fit the button inside its drawing area
    {
        status = fit_pushbutton_to_drawingarea(dialog_name, button_draw, button_id);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
        }

        ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
        if (fit_data) {
            snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
            snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
            status = ProUIDrawingareaPostmanagenotifyActionSet(dialog_name, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
                free(fit_data);
            }
        }
    }

    // Now apply initial enabled/disabled state to actual widgets
    if (!ui_enabled) {
        ProUIPushbuttonDisable(dialog_name, button_id);
        ProUIDrawingareaDisable(dialog_name, button_draw);
    }
    else {
        ProUIPushbuttonEnable(dialog_name, button_id);
        ProUIDrawingareaEnable(dialog_name, button_draw);
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->array);
        status = ProUIDrawingareaUpdateActionSet(dialog_name, button_draw, UserSelectUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }

    /* Force initial repaint to apply color via callback */
    (void)ProUIDrawingareaClear(dialog_name, button_draw);  /* Triggers update callback for redraw */

    (*current_row)++;
    return PRO_TK_NO_ERROR;
}

ProError OnPictureUserSelectMultipleOptional(char* dialog, char* draw_area_name, UserSelectMultipleOptionalNode* node, SymbolTable* st)
{
    if (!dialog || !draw_area_name || !node || !node->array || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    // idempotence guard (unchanged) 
    Variable* sel_var_guard = get_symbol(st, node->array);
    if (sel_var_guard && sel_var_guard->type == TYPE_MAP) {
        Variable* existed = hash_table_lookup(sel_var_guard->data.map, "button_id");
        if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
            ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->array);
            return PRO_TK_NO_ERROR;
        }
    }
    // Unique ids (reference-based)
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s", node->array);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s", node->array);

    // Persist control IDs into the select variable's map for later toggling 
    Variable* sel_var = get_symbol(st, node->array);
    if (sel_var && sel_var->type == TYPE_MAP)
    {
        Variable* vbtn = (Variable*)malloc(sizeof(Variable));
        Variable* vdar = (Variable*)malloc(sizeof(Variable));
        if (vbtn && vdar)
        {
            vbtn->type = TYPE_STRING;
            vbtn->data.string_value = _strdup(button_id);
            vdar->type = TYPE_STRING;
            vdar->data.string_value = _strdup(button_draw);
            hash_table_insert(sel_var->data.map, "button_id", vbtn);
            hash_table_insert(sel_var->data.map, "draw_area_id", vdar);

            // initialize runtime UI state flags 
            set_bool_in_map(sel_var->data.map, "ui_enabled", 1);  // starts enabled 
            // ui_required mirrors enabled; REQUIRED_SELECTS is the source of truth 
            set_bool_in_map(sel_var->data.map, "ui_required", 1);

            LogOnlyPrintfChar("Drawingarea ID: %s", vdar->data.string_value);
        }
    }

    // Evaluate positions from expressions (fallback to 0 if missing or evaluation fails)
    int x_pos = 0;
    if (node->posX) {
        long temp_x;
        status = evaluate_to_int(node->posX, st, &temp_x);
        if (status == PRO_TK_NO_ERROR) {
            x_pos = (int)temp_x;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posX for '%s'; using x=0", node->array);
        }
    }
    else {
        ProPrintfChar("Warning: posX missing in ON_PICTURE for '%s'; using x=0", node->array);
    }

    int y_pos = 0;
    if (node->posY) {
        long temp_y;
        status = evaluate_to_int(node->posY, st, &temp_y);
        if (status == PRO_TK_NO_ERROR) {
            y_pos = (int)temp_y;
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate posY for '%s'; using y=0", node->array);
        }
    }
    else {
        ProPrintfChar("Warning: posY missing in ON_PICTURE for '%s'; using y=0", node->array);
    }

    // Container drawing area
    status = ProUIDrawingareaDrawingareaAdd(dialog, draw_area_name, button_draw);
    if (status != PRO_TK_NO_ERROR) return status;


    status = ProUIDrawingareaDrawingheightSet(dialog, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;

    ProUIDrawingareaPositionSet(dialog, button_draw, x_pos, y_pos);

    // Pushbutton inside the child drawing area
    status = ProUIDrawingareaPushbuttonAdd(dialog, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    // Place the button; final fit later
    ProUIPushbuttonPositionSet(dialog, button_id, 0, 1);

    unrequire_select(st, node->array);

    // Button text
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->array);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    {
        Variable* select_var = get_symbol(st, node->array);
        if (select_var && select_var->type == TYPE_MAP) {
            Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
            if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
                snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
            }
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->array, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }


    status = ProUIPushbuttonTextSet(dialog, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    // Tooltip (optional)
    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // Wire collector callback
    {
        UserSelectMultipleOptionalData* sel_data = (UserSelectMultipleOptionalData*)calloc(1, sizeof(UserSelectMultipleOptionalData));
        if (!sel_data) return PRO_TK_GENERAL_ERROR;
        sel_data->st = st;
        sel_data->node = node;
        snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
        snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

        status = ProUIPushbuttonActivateActionSet(dialog, button_id, UserSelectCallback, (ProAppData)sel_data);
        if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }
    }

    // Auto-fit the button inside its drawing area
    {
        status = fit_pushbutton_to_drawingarea(dialog, button_draw, button_id);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
        }

        ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
        if (fit_data) {
            snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
            snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
            status = ProUIDrawingareaPostmanagenotifyActionSet(dialog, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
                free(fit_data);
            }
        }
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->array);
        status = ProUIDrawingareaUpdateActionSet(dialog, button_draw, UserSelectOptionalUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }

    /* Force initial repaint to apply color via callback */
    (void)ProUIDrawingareaClear(dialog, button_draw);  /* Triggers update callback for redraw */

    return PRO_TK_NO_ERROR;

}

ProError addUserSelectMultipleOptional (char* dialog_name, char* parent_layout_name, UserSelectMultipleOptionalNode* node, int* current_row, int column, SymbolTable* st)
{
    if (!dialog_name || !parent_layout_name || !node || !node->array || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    // idempotence guard (unchanged) 
    Variable* sel_var_guard = get_symbol(st, node->array);
    if (sel_var_guard && sel_var_guard->type == TYPE_MAP) {
        Variable* existed = hash_table_lookup(sel_var_guard->data.map, "button_id");
        if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
            ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->array);
            return PRO_TK_NO_ERROR;
        }
    }

    // unique ids (row-aware, matches your current scheme) 
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s_%d", node->array, *current_row);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s_%d", node->array, *current_row);

    // Persist control IDs into the select variable's map for later toggling 
    Variable* sel_var = get_symbol(st, node->array);
    if (sel_var && sel_var->type == TYPE_MAP)
    {
        Variable* vbtn = (Variable*)malloc(sizeof(Variable));
        Variable* vdar = (Variable*)malloc(sizeof(Variable));
        if (vbtn && vdar)
        {
            vbtn->type = TYPE_STRING;
            vbtn->data.string_value = _strdup(button_id);
            vdar->type = TYPE_STRING;
            vdar->data.string_value = _strdup(button_draw);
            hash_table_insert(sel_var->data.map, "button_id", vbtn);
            hash_table_insert(sel_var->data.map, "draw_area_id", vdar);

            // initialize runtime UI state flags 
            set_bool_in_map(sel_var->data.map, "ui_enabled", 1);  // starts enabled 
            // ui_required mirrors enabled; REQUIRED_SELECTS is the source of truth 
            set_bool_in_map(sel_var->data.map, "ui_required", 1);

            LogOnlyPrintfChar("Drawingarea ID: %s", vdar->data.string_value);
        }
    }

    // add drawing area 
    ProUIGridopts grid = (ProUIGridopts){ 0 };
    grid.row = *current_row;
    grid.column = column;
    grid.horz_cells = 1;
    grid.vert_cells = 1;
    grid.attach_left = PRO_B_TRUE;
    grid.attach_right = PRO_B_TRUE;
    grid.bottom_offset = 3;

    status = ProUILayoutDrawingareaAdd(dialog_name, parent_layout_name, button_draw, &grid);
    if (status != PRO_TK_NO_ERROR) return status;

    // optional title/border 
    ProUILayoutTextSet(dialog_name, button_draw, L"Optional");

    // initial size 
    status = ProUIDrawingareaDrawingheightSet(dialog_name, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog_name, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;



    // pushbutton inside the drawing area 
    status = ProUIDrawingareaPushbuttonAdd(dialog_name, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    // initial position (final size via fit helper) 
    ProUIPushbuttonPositionSet(dialog_name, button_id, 0, 1);

    unrequire_select(st, node->array);


    // label text 
    char ref_text[160];
    snprintf(ref_text, sizeof(ref_text), "%s", node->array);

    char button_text[256];
    snprintf(button_text, sizeof(button_text), "%s", ref_text);

    Variable* select_var = get_symbol(st, node->array);
    if (select_var && select_var->type == TYPE_MAP) {
        Variable* tag_var = hash_table_lookup(select_var->data.map, "tag");
        if (tag_var && tag_var->type == TYPE_STRING && tag_var->data.string_value && tag_var->data.string_value[0] != '\0') {
            snprintf(button_text, sizeof(button_text), "(%s) %s", tag_var->data.string_value, ref_text);
        }
    }

    wchar_t* w_button_text = char_to_wchar(button_text);
    if (!w_button_text) return PRO_TK_GENERAL_ERROR;

    /* NEW: override w_parameter with sel_list friendly name if available */
    {
        wchar_t* w_friendly = NULL;
        if (selmap_lookup_w(node->array, &w_friendly)) {
            free(w_button_text);
            w_button_text = w_friendly; /* now shows e.g. "ALL SKELS" instead of "ALL_SKELS" */
        }
    }


    status = ProUIPushbuttonTextSet(dialog_name, button_id, w_button_text);
    free(w_button_text);
    if (status != PRO_TK_NO_ERROR) return status;

    if (node->tooltip_message) {
        char* tip = NULL;
        if (evaluate_to_string(node->tooltip_message, st, &tip) == PRO_TK_NO_ERROR && tip) {
            wchar_t* wtip = char_to_wchar(tip);
            if (wtip) {
                ProUIPushbuttonHelptextSet(dialog_name, button_id, wtip);
                free(wtip);
            }
            free(tip);
        }
    }

    // wire collector callback 
    UserSelectMultipleOptionalData* sel_data = (UserSelectMultipleOptionalData*)calloc(1, sizeof(UserSelectMultipleOptionalData));
    if (!sel_data) return PRO_TK_GENERAL_ERROR;
    sel_data->st = st;
    sel_data->node = node;
    snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
    snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

    status = ProUIPushbuttonActivateActionSet(dialog_name, button_id, UserSelectMultipleCallback, (ProAppData)sel_data);
    if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }

    // keep button fit to drawing area during resizes 
    status = fit_pushbutton_to_drawingarea(dialog_name, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Could not fit pushbutton '%s' inside '%s'\n", button_id, button_draw);
    }

    ButtonFitData* fit_data = (ButtonFitData*)calloc(1, sizeof(ButtonFitData));
    if (fit_data) {
        snprintf(fit_data->draw_area, sizeof(fit_data->draw_area), "%s", button_draw);
        snprintf(fit_data->button_id, sizeof(fit_data->button_id), "%s", button_id);
        status = ProUIDrawingareaPostmanagenotifyActionSet(dialog_name, button_draw, UserSelectResizeCallback, (ProAppData)fit_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set resize callback for '%s'\n", button_draw);
            free(fit_data);
        }
    }

    // Allocate and set update data
    UpdateData* update_data = (UpdateData*)calloc(1, sizeof(UpdateData));
    if (update_data) {
        update_data->st = st;
        snprintf(update_data->reference, sizeof(update_data->reference), "%s", node->array);
        status = ProUIDrawingareaUpdateActionSet(dialog_name, button_draw, UserSelectOptionalUpdateCallback, (ProAppData)update_data);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Warning: Could not set update callback for '%s'\n", button_draw);
            free(update_data);
        }
    }

    // next row 
    (*current_row)++;

    return PRO_TK_NO_ERROR;
}

/*=================================================*\
* 
* TABLES INFORMATION FOR EPA
* 
* 
\*=================================================*/



/*=================================================*\
* 
* Resizing of the GLOBAL_PICTURE to make sure it is 
* even on the X position within the dialog
* 
\*=================================================*/
ProError MyPostManageCallback(char* dialog, char* component, ProAppData app_data) {
    (void)component; // Unused, as in original
    ProError status;
    int da_width = 0, da_height = 0;
    int imageW = 0, imageH = 0;
    // Get drawing area dimensions
    status = ProUIDrawingareaDrawingwidthGet(dialog, "drawA1", &da_width);
    if (status != PRO_TK_NO_ERROR)
    {
        ProPrintfChar("GLOBAL_PICTURE Command does not exist: Resorting to default size");
        return status;
    }
    status = ProUIDrawingareaDrawingheightGet(dialog, "drawA1", &da_height);
    if (status != PRO_TK_NO_ERROR)
    {
        ProPrintfChar("Could not get drawingarea height");
        return status;
    }
    // Cast app_data back to SymbolTable*
    SymbolTable* st = (SymbolTable*)app_data;
    if (!st)
    {
        ProPrintfChar("Error: Invalid symbol table in addpicture");
        return PRO_TK_BAD_INPUTS;
    }
    // Retrieve filepath from symbol table(centralized access)
    Variable* pic_var = get_symbol(st, "GLOBAL_PICTURE");
    if (pic_var && pic_var->type == TYPE_STRING && pic_var->data.string_value &&
        pic_var->data.string_value[0] != '\0')
    {
        // Get image dimensions from the path stored in value
        if (!get_gif_dimensions(pic_var->data.string_value, &imageW, &imageH))
        {
            ProPrintfChar("Failed to get image dimensions for GLOBAL_PICTURE");
            imageW = imageH = 0; // Fallback to zero if dimensions cant be retrieved
        }
    }
    else
    {
        ProPrintfChar("GLOBAL_PICTURE not found or has no value");
        imageW = imageH = 0; // Default if entry is missing or empty.
    }
    int offsetX = da_width > 0 ? (da_width - imageW) / 2 : 0;
    if (offsetX < 0) offsetX = 0;
    // Set the position of draw_area
    status = ProUIDrawingareaPositionSet(dialog, "draw_area", offsetX, 0);
    if (status != PRO_TK_NO_ERROR)
    {
        ProPrintfChar("Could not set draw_area position");
        return status;
    }
    // Resize the root table using stored table_id (after dialog is displayed)
    Variable* root_table_var = get_symbol(st, "ROOT_TABLE_ID");
    if (root_table_var && root_table_var->type == TYPE_STRING && root_table_var->data.string_value) {
        char* table_id = root_table_var->data.string_value;
        int da_table_height; int da_table_width;
        status = ProUIDrawingareaSizeGet(dialog, "drawarea_tableid", &da_table_width, &da_table_height);
        if (status == PRO_TK_NO_ERROR && da_table_width > 0 && da_table_height > 0) {
            LogOnlyPrintfChar("Drawingarea size %d %d", da_table_width, da_table_height);
            status = ProUITableSizeSet(dialog, table_id, da_table_width, da_table_height);
            if (status != PRO_TK_NO_ERROR) {
                ProPrintfChar("Error: Could not set table size for '%s'\n", table_id);
            }
        }
        else {
            ProPrintfChar("Error: Could not get valid drawing area size for table resizing\n");
        }
    }
    return PRO_TK_NO_ERROR;
}

/*=================================================*\
* 
* OK Button on the bottom right of creo
* 
* 
\*=================================================*/
ProError PushButtonAction(char* dialog, char* component, ProAppData app_data) {
    (void)component;
    SymbolTable* st = (SymbolTable*)app_data;

    bool all_radios_ok = true;
    bool all_selects_ok = true;
    bool all_checkboxes_ok = true;  // New: Flag for checkbox validation
    bool all_inputs_ok = true;  // New: Flag for input validation

    // Re-validate radios (existing, refactored for clarity)
    Variable* req_radios = get_symbol(st, "REQUIRED_RADIOS");
    if (req_radios && req_radios->type == TYPE_ARRAY) {
        ArrayData* array = &req_radios->data.array;
        for (size_t i = 0; i < array->size; i++) {
            Variable* item = array->elements[i];
            if (item->type != TYPE_STRING || !item->data.string_value) continue;
            Variable* radio_var = get_symbol(st, item->data.string_value);
            if (!radio_var ||
                (radio_var->type == TYPE_STRING && (!radio_var->data.string_value || strlen(radio_var->data.string_value) == 0)) ||
                (radio_var->type == TYPE_INTEGER && radio_var->data.int_value < 0)) {
                all_radios_ok = false;
                break;
            }
        }
    }

    // Re-validate selects (new, mirrored from validate_ok_button)
    Variable* req_selects = get_symbol(st, "REQUIRED_SELECTS");
    if (req_selects && req_selects->type == TYPE_ARRAY) {
        ArrayData* array = &req_selects->data.array;
        for (size_t i = 0; i < array->size; i++) {
            Variable* item = array->elements[i];
            if (item->type != TYPE_STRING || !item->data.string_value) continue;
            Variable* select_var = get_symbol(st, item->data.string_value);
            if (!select_var) {
                all_selects_ok = false;
                break;
            }
            if (select_var->type == TYPE_MAP) {
                Variable* ref_val = hash_table_lookup(select_var->data.map, "reference_value");
                if (!ref_val || ref_val->type != TYPE_REFERENCE || !ref_val->data.reference.reference_value) {
                    all_selects_ok = false;
                    break;
                }
            }
            else if (select_var->type == TYPE_ARRAY) {
                if (select_var->data.array.size == 0) {
                    all_selects_ok = false;
                    break;
                }
                for (size_t j = 0; j < select_var->data.array.size; j++) {
                    Variable* elem = select_var->data.array.elements[j];
                    if (elem->type != TYPE_REFERENCE) {
                        all_selects_ok = false;
                        break;
                    }
                    if (!elem->data.reference.reference_value) {
                        all_selects_ok = false;
                        break;
                    }
                }
                if (!all_selects_ok) break;
            }
            else {
                all_selects_ok = false;
                break;
            }
        }
    }

    // Re-validate checkboxes (new, mirrored from validate_ok_button)
    Variable* req_checkboxes = get_symbol(st, "REQUIRED_CHECKBOXES");
    if (req_checkboxes && req_checkboxes->type == TYPE_ARRAY) {
        ArrayData* array = &req_checkboxes->data.array;
        for (size_t i = 0; i < array->size; i++) {
            Variable* item = array->elements[i];
            if (item->type != TYPE_STRING || !item->data.string_value) continue;
            Variable* check_var = get_symbol(st, item->data.string_value);
            if (!check_var ||
                (check_var->type != TYPE_INTEGER && check_var->type != TYPE_BOOL) ||
                check_var->data.int_value == 0) {  // Unchecked if == 0
                all_checkboxes_ok = false;
                break;
            }
        }
    }

    // Re-validate inputs (new, mirrored from validate_ok_button)
    Variable* req_inputs = get_symbol(st, "REQUIRED_INPUTS");
    if (req_inputs && req_inputs->type == TYPE_ARRAY) {
        ArrayData* array = &req_inputs->data.array;
        for (size_t i = 0; i < array->size; i++) {
            Variable* item = array->elements[i];
            if (item->type != TYPE_STRING || !item->data.string_value) continue;
            Variable* input_var = get_symbol(st, item->data.string_value);
            if (!input_var) {
                all_inputs_ok = false;
                break;
            }
            bool is_valid = true;
            switch (input_var->type) {
            case TYPE_STRING:
                if (!input_var->data.string_value || strlen(input_var->data.string_value) == 0) {
                    is_valid = false;
                }
                break;
            case TYPE_INTEGER:
            case TYPE_BOOL:
                if (input_var->data.int_value == 0) {
                    is_valid = false;
                }
                break;
            case TYPE_DOUBLE:
                if (input_var->data.double_value == 0.0) {
                    is_valid = false;
                }
                break;
            default:
                is_valid = false;
                break;
            }
            if (!is_valid) {
                all_inputs_ok = false;
                break;
            }
        }
    }

    if (!all_radios_ok || !all_selects_ok || !all_checkboxes_ok || !all_inputs_ok) {
        // Unified message (updated to include inputs)
        wchar_t* w_msg = char_to_wchar("Please complete all required radio selections, user selects, checkboxes, and input fields.");
        ProGenericMsg(w_msg);
        free(w_msg);
        return PRO_TK_NO_ERROR;  // Prevent close
    }

    // Proceed with closure
    ProUIDialogExit(dialog, PRO_TK_NO_ERROR);
    ProGenericMsg(L"Dialog cleanup completed successfully.");
    return PRO_TK_NO_ERROR;
}

/*=================================================*\
* 
* The closing call back function this exectues the
* closing of the dialog based on the X selection
* in the top right corner.
* 
\*=================================================*/
ProError CloseCallback(char* dialog_name, char* component, ProAppData app_data)
{
    (void)component;
    (void)app_data;


    ProGenericMsg(L"Dialog close callback triggered. Cleaning up...");


    ProUIDialogExit(dialog_name, PRO_TK_NO_ERROR);

    ProGenericMsg(L"Dialog cleanup completed successfully.");
    return PRO_TK_NO_ERROR;
}

