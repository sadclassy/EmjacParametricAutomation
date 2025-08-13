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

        /* Draw using evaluated values */
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
    wchar_t* value_w = variable_value_to_wstring(var);
    if (!name_w || !value_w) {
        free(name_w); free(value_w);
        ProPrintfChar("Error: Failed to format text for '%s'", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    wchar_t label_text[256];
    if (wcslen(value_w) > 0)
        swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls: %ls", name_w, value_w);
    else
        swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls: (undefined)", name_w);
    free(name_w);
    free(value_w);

    status = ProUIDrawingareaLabelAdd(dialog, draw_area_name, label_name);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add label '%s' to drawing area '%s'", label_name, draw_area_name);
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
    if (node->on_picture) {
        return OnPictureShowParam(dialog_name, "draw_area", node, st);
    }
    else {
        label_grid.column = column;
        label_grid.row = *current_row;
        (*current_row)++;  // Increment row for sequential placement
    }
    label_grid.horz_cells = 1;
    label_grid.vert_cells = 1;
    label_grid.top_offset = 20;
    label_grid.attach_bottom = PRO_B_TRUE;
    label_grid.attach_left = PRO_B_TRUE;
    label_grid.attach_right = PRO_B_TRUE;
    label_grid.attach_top = PRO_B_TRUE;

    // Create label ID (unchanged)
    char label_id[100];
    snprintf(label_id, sizeof(label_id), "show_label_%s", node->parameter);

    // Add label to layout (unchanged)
    status = ProUILayoutLabelAdd(dialog_name, parent_layout_name, label_id, &label_grid);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Could not add label for '%s'\n", node->parameter);
        return status;
    }

    // Get type string (unchanged)
    char* param_type_str = get_variable_type_string(node->var_type, node->subtype);

    // Convert to wide strings (unchanged)
    wchar_t* w_parameter = char_to_wchar(node->parameter);
    wchar_t* w_type = char_to_wchar(param_type_str);  // Not used in final display per requirement

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
    swprintf(label_text, sizeof(label_text) / sizeof(wchar_t), L"%ls (%ls)", w_parameter, w_value);

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
    snprintf(cbp_label_id, sizeof(cbp_label_id), "label_%s_%d", cbNode->parameter, *current_row);

    // Initialize grid options
    ProUIGridopts grid_opts_cbp = { 0 };
    grid_opts_cbp.attach_bottom = PRO_B_TRUE;
    grid_opts_cbp.attach_left = PRO_B_TRUE;
    grid_opts_cbp.attach_right = PRO_B_TRUE;
    grid_opts_cbp.attach_top = PRO_B_TRUE;
    grid_opts_cbp.horz_cells = 1;
    grid_opts_cbp.vert_cells = 1;
    

    if (cbNode->on_picture) {
        return OnPictureCheckboxParam(dialog_name, "draw_area", cbNode, st);
    }
    else {
        grid_opts_cbp.column = column + 1;
        grid_opts_cbp.row = *current_row;
        (*current_row)++;  // Increment row for sequential placement
    }

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
        swprintf(cbp_label_text, sizeof(cbp_label_text) / sizeof(wchar_t), L"(%ls) %ls", w_parameter, w_tag);
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

    /* validate */
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
        /* persist last valid */
        free(filter_data->last_valid);
        filter_data->last_valid = _strdup(current_str);

        /* write symbol */
        Variable* var = get_symbol(filter_data->st, filter_data->parameter);
        if (var) {
            switch (filter_data->subtype) {
            case PARAM_DOUBLE: var->data.double_value = strtod(current_str, NULL); break;
            case PARAM_INT:
            case PARAM_BOOL:   var->data.int_value = (int)strtol(current_str, NULL, 10); break;
            case PARAM_STRING: free(var->data.string_value);
                var->data.string_value = _strdup(current_str); break;
            default: break;
            }
        }

        /* debug + SHOW_PARAM label update (if present) */
        Variable* after = get_symbol(filter_data->st, filter_data->parameter);
        if (after) {
            debug_print_symbol_update(filter_data->parameter, after);             /* logs change */
            update_show_param_label_text(dialog, filter_data->parameter, after);  /* refresh label */
        }

        /* repaint + reactive IF rebuild (critical for DE_MASTER-driven pictures) */
        (void)ProUIDrawingareaClear(dialog, "draw_area");
        (void)addpicture(dialog, "draw_area", (ProAppData)filter_data->st);
        validate_ok_button(dialog, filter_data->st);
        refresh_required_input_highlights(dialog, filter_data->st);

    }
    else {
        /* revert to last valid */
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
        if (status == PRO_TK_NO_ERROR) var->data.double_value = val;
        break;
    }
    case PARAM_INT:
    case PARAM_BOOL: {
        int val; status = ProUIInputpanelIntegerGet(dialog, component, &val);
        if (status == PRO_TK_NO_ERROR) var->data.int_value = val;
        break;
    }
    case PARAM_STRING: {
        char* new_str = NULL;
        status = ProUIInputpanelStringGet(dialog, component, &new_str);
        if (status == PRO_TK_NO_ERROR && new_str) {
            free(var->data.string_value);
            var->data.string_value = _strdup(new_str);
            ProStringFree(new_str);
        }
        break;
    }
    default:
        status = PRO_TK_GENERAL_ERROR;
    }

    if (status == PRO_TK_NO_ERROR) {
        // NEW: print the updated value
        debug_print_symbol_update(filter_data->parameter, var);

        // CENTRALIZED: update SHOW_PARAM label if it exists
        update_show_param_label_text(dialog, filter_data->parameter, var);
        
        // Your existing repaint
        (void)ProUIDrawingareaClear(dialog, "draw_area");
        (void)addpicture(dialog, "draw_area", (ProAppData)filter_data->st);
    }

    // Your existing validations
    validate_ok_button(dialog, filter_data->st);
    refresh_required_input_highlights(dialog, filter_data->st);
    EPA_ReactiveRefresh();  // makes IF branches/picture choice/button gating react now

    filter_data->in_activate = PRO_B_FALSE;
    return status;
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

    if (node->on_picture) {
        long pos_x;
        status = evaluate_to_int(node->posX, st, &pos_x);
        if (status != 0) {
            ProPrintfChar("Error: Failed to evaluate posX for '%s'\n", node->parameter);
            return status;
        }
        long pos_y;
        status = evaluate_to_int(node->posY, st, &pos_y);
        if (status != 0) {
            ProPrintfChar("Error: Failed to evaluate posY for '%s'\n", node->parameter);
            return status;
        }
        label_grid.column = (int)(pos_x / 100);  // Example mapping
        label_grid.row = (int)(pos_y / 100);
    }
    else {
        label_grid.column = column;
        label_grid.row = *current_row;
    }

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

    return PRO_TK_NO_ERROR;
}

/*=================================================*\
* 
* RADIOBUTTON_PARAM command display execution
* --this displays the radiobutton within the dialog--
* 
\*=================================================*/
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
    EPA_ReactiveRefresh();

    return PRO_TK_NO_ERROR;
}

ProError addRadioButtonParam(char* dialog_name, char* parent_layout_name, RadioButtonParamNode* node, int* current_row, int column, SymbolTable* st)
{
    ProError status;

    // Check if there are options available for the radio buttons
    if (node->option_count == 0) {
        ProPrintfChar("Error: No options provided for radio button group '%s'.\n", node->parameter);
        return PRO_TK_GENERAL_ERROR;
    }

    // Generate a unique name for the sub-layout using a static counter
    static int sub_layout_count = 0;
    char sub_layout_name[100];
    snprintf(sub_layout_name, sizeof(sub_layout_name), "radio_sub_layout_%d", sub_layout_count++);

    // Set up grid options for the sub-layout in the parent layout
    ProUIGridopts grid_opts_sub = { 0 };
    grid_opts_sub.horz_cells = 1;
    grid_opts_sub.vert_cells = 1;
    grid_opts_sub.attach_bottom = PRO_B_TRUE;
    grid_opts_sub.attach_left = PRO_B_TRUE;
    grid_opts_sub.attach_right = PRO_B_TRUE;
    grid_opts_sub.attach_top = PRO_B_TRUE;

    if (node->on_picture) {
        long pos_x;
        status = evaluate_to_int(node->posX, st, &pos_x);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Error: Failed to evaluate posX for '%s'\n", node->parameter);
            return status;
        }
        long pos_y;
        status = evaluate_to_int(node->posY, st, &pos_y);
        if (status != PRO_TK_NO_ERROR) {
            ProPrintfChar("Error: Failed to evaluate posY for '%s'\n", node->parameter);
            return status;
        }
        grid_opts_sub.column = pos_x / 100;  // Map x position to column
        grid_opts_sub.row = pos_y / 100;      // Map y position to row
    }
    else {
        grid_opts_sub.column = column;
        grid_opts_sub.row = *current_row;
        (*current_row)++;  // Increment row for sequential placement
    }

    // Add the sub-layout to the parent layout
    status = ProUILayoutLayoutAdd(dialog_name, parent_layout_name, sub_layout_name, &grid_opts_sub);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add sub-layout for radio group '%s'.\n", node->parameter);
        return status;
    }

    // Decorate the sub-layout
    status = ProUILayoutDecorate(dialog_name, sub_layout_name);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not decorate sub-layout for radio group '%s'.\n", node->parameter);
        return status;
    }

    // Convert parameter to wide string
    wchar_t* w_parameter = char_to_wchar(node->parameter);
    if (!w_parameter) {
        ProPrintf(L"Conversion to wide string failed for parameter\n");
        return PRO_TK_GENERAL_ERROR;
    }

    // Determine if the required option is present and format the label accordingly
    wchar_t layout_text[200];
    if (node->required) {
        swprintf(layout_text, sizeof(layout_text) / sizeof(wchar_t), L"%ls (REQUIRED)", w_parameter);

        // NEW: Add to REQUIRE_RADIOS array in symbol table
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
        swprintf(layout_text, sizeof(layout_text) / sizeof(wchar_t), L"%ls", w_parameter);
    }

    // Set the sub-layout text using the formatted label
    status = ProUILayoutTextSet(dialog_name, sub_layout_name, layout_text);
    free(w_parameter);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set text for sub-layout '%s'.\n", node->parameter);
        return status;
    }

    // Generate a unique name for the radio group component
    char rb_component_name[100];
    snprintf(rb_component_name, sizeof(rb_component_name), "radio_group_%d", sub_layout_count);

    // Set up grid options for the radio group within the sub-layout
    ProUIGridopts grid_opts_rb = { 0 };
    grid_opts_rb.column = 0;
    grid_opts_rb.row = 0;
    grid_opts_rb.horz_cells = 1;
    grid_opts_rb.vert_cells = 1;

    // Add the radio group to the sub-layout
    status = ProUILayoutRadiogroupAdd(dialog_name, sub_layout_name, rb_component_name, &grid_opts_rb);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not add radio group '%s' to sub-layout.\n", node->parameter);
        return status;
    }

    // Set the radio group orientation to vertical
    status = ProUIRadiogroupOrientationSet(dialog_name, rb_component_name, 1);
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
    status = ProUIRadiogroupNamesSet(dialog_name, rb_component_name, option_count, button_names);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set names for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    // Set the radio group labels
    status = ProUIRadiogroupLabelsSet(dialog_name, rb_component_name, option_count, button_labels);
    if (status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Error: Could not set labels for radio group '%s'.\n", node->parameter);
        goto cleanup;
    }

    // Allocate and initialize callback data
    RadioSelectData* select_data = (RadioSelectData*)malloc(sizeof(RadioSelectData));
    if (!select_data) {
        ProPrintfChar("Error: Memory allocation failed for RadioSelectData of '%s'\n", node->parameter);
        goto cleanup;  // Assuming existing cleanup label handles freeing resources
    }
    select_data->st = st;
    select_data->parameter = _strdup(node->parameter);
    if (!select_data->parameter) {
        free(select_data);
        ProPrintfChar("Error: Memory allocation failed for parameter name '%s'\n", node->parameter);
        goto cleanup;
    }

    // Register the selection callback
    status = ProUIRadiogroupSelectActionSet(dialog_name, rb_component_name, RadioSelectCallback, (ProAppData)select_data);
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
                ProUIRadiogroupHelptextSet(dialog_name, rb_component_name, w_tooltip);
                free(w_tooltip);
            }
            free(tooltip_str);
        }
    }

    // After successful addition, initialize value if INTEGER
    Variable* var = get_symbol(st, node->parameter);
    if (var && var->type == TYPE_INTEGER) {
        var->data.int_value = -1;  // Unselected state
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

/*=================================================*\
* 
* USER_SELECT command to display the push buttons
* -- This displays the pushbutton within the gui but no logic 
* to make the pushbutton do an action--
* 
\*=================================================*/
// Helper function to compare two ProSelections (implement based on selobj.html)
static ProBoolean is_selection_equal(ProSelection sel1, ProSelection sel2) {
    if (!sel1 || !sel2) return PRO_B_FALSE;

    ProAsmcomppath path1, path2;
    ProModelitem item1, item2;

    ProError status1 = ProSelectionAsmcomppathGet(sel1, &path1);
    ProError status2 = ProSelectionAsmcomppathGet(sel2, &path2);
    if (status1 != PRO_TK_NO_ERROR || status2 != PRO_TK_NO_ERROR) return PRO_B_FALSE;

    // Compare paths (simple memcmp if sizes match; otherwise, deeper comparison)
    if (path1.table_num != path2.table_num || memcmp(path1.comp_id_table, path2.comp_id_table, path1.table_num * sizeof(int)) != 0) {
        return PRO_B_FALSE;
    }

    status1 = ProSelectionModelitemGet(sel1, &item1);
    status2 = ProSelectionModelitemGet(sel2, &item2);
    if (status1 != PRO_TK_NO_ERROR || status2 != PRO_TK_NO_ERROR) return PRO_B_FALSE;

    // Compare model items
    return (item1.id == item2.id && item1.type == item2.type && item1.owner == item2.owner) ? PRO_B_TRUE : PRO_B_FALSE;
}

ProError UserSelectCallback(char* dialog, char* component, ProAppData app_data) {
    (void)component;

    static ProBoolean in_callback = PRO_B_FALSE;
    if (in_callback == PRO_B_TRUE) {
        ProPrintfChar("Warning: Reentrant call to UserSelectCallback detected; skipping");
        return PRO_TK_NO_ERROR;
    }
    in_callback = PRO_B_TRUE;

    UserSelectData* data = (UserSelectData*)app_data;
    if (!data || !data->node || !data->st) {
        ProPrintfChar("Error: Invalid data in UserSelectCallback");
        in_callback = PRO_B_FALSE;
        return PRO_TK_BAD_INPUTS;
    }
    ProPrintfChar("Debug: Entering UserSelectCallback for reference '%s'", data->node->reference);

    // Step 1: Build selection type string dynamically
    size_t num_types = data->node->type_count;
    size_t total_len = 0;
    for (size_t t = 0; t < num_types; t++) {
        char* type_str = NULL;
        ProError eval_status = evaluate_to_string(data->node->types[t], data->st, &type_str);
        if (eval_status != PRO_TK_NO_ERROR || !type_str) {
            ProPrintfChar("Error: Failed to evaluate type expression %zu", t);
            in_callback = PRO_B_FALSE;
            return PRO_TK_GENERAL_ERROR;  // Early exit; no allocations yet
        }
        total_len += strlen(type_str) + 1;  // +1 for comma or null
        free(type_str);  // Free temp string from evaluate_to_string
    }

    char* sel_type = malloc(total_len + 1);  // +1 for safety
    if (!sel_type) {
        ProPrintfChar("Error: Memory allocation failed for selection type string");
        in_callback = PRO_B_FALSE;
        return PRO_TK_GENERAL_ERROR;
    }
    sel_type[0] = '\0';  // Initialize

    size_t offset = 0;
    for (size_t t = 0; t < num_types; t++) {
        char* type_str = NULL;
        evaluate_to_string(data->node->types[t], data->st, &type_str);  // Re-evaluate (inefficient but safe)
        if (type_str) {
            size_t len = strlen(type_str);
            for (size_t idx = 0; idx < len; idx++) {
                type_str[idx] = (char)tolower((unsigned char)type_str[idx]);
            }
            if (offset > 0) {
                sel_type[offset++] = ',';  // Manual concat to avoid strcat_s dependency
            }
            memcpy(sel_type + offset, type_str, len);
            offset += len;
            free(type_str);
        }
    }
    sel_type[offset] = '\0';
    ProPrintfChar("Debug: Constructed selection type: %s", sel_type);

    // Step 2: Clear existing selections
    ProSelbufferClear();

    // Step 3: Hide dialog to avoid UI conflicts
    ProError status = ProUIDialogHide(dialog);
    if (status != PRO_TK_NO_ERROR) {
        free(sel_type);
        ProPrintfChar("Error: Failed to hide dialog for selection");
        in_callback = PRO_B_FALSE;
        return status;
    }

    // Step 4: Perform interactive selection
    ProSelection* p_sel = NULL;
    int n_sel = 0;
    int max_sel = -1;  // Unlimited
    status = ProSelect(sel_type, max_sel, NULL, NULL, NULL, NULL, &p_sel, &n_sel);
    free(sel_type);  // Cleanup early



    // Step 5: Show dialog after selection
    ProError show_status = ProUIDialogShow(dialog);
    if (show_status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Failed to show dialog after selection");
    }

    if (status != PRO_TK_NO_ERROR || n_sel < 1) {
        ProPrintfChar("Debug: No selection; requesting repaint for ref='%s' draw='%s'",
            data->node->reference, data->draw_area_id);
        (void)paint_user_select_area(dialog, data->draw_area_id, data->st, data->node->reference);
        in_callback = PRO_B_FALSE;
        return PRO_TK_NO_ERROR;
    }
    ProPrintfChar("Selection made, processing %d items...", n_sel);


    // Step 6: Get or create the array variable
    Variable* arr_var = get_symbol(data->st, data->node->reference);
    ProBoolean is_new = PRO_B_FALSE;
    if (!arr_var || arr_var->type != TYPE_ARRAY) {
        arr_var = malloc(sizeof(Variable));
        if (!arr_var) {
            ProPrintfChar("Error: Memory allocation failed for array variable");
            in_callback = PRO_B_FALSE;
            return PRO_TK_GENERAL_ERROR;
        }
        arr_var->type = TYPE_ARRAY;
        arr_var->data.array.size = 0;
        arr_var->data.array.elements = NULL;
        is_new = PRO_B_TRUE;
    }

    // Step 7: Collect new elements in temp array, skipping duplicates
    Variable** new_elems = malloc((size_t)n_sel * sizeof(Variable*));
    if (!new_elems) {
        if (is_new) free(arr_var);
        ProPrintfChar("Error: Memory allocation failed for temporary elements array");
        in_callback = PRO_B_FALSE;
        return PRO_TK_GENERAL_ERROR;
    }
    int added = 0;
    for (int k = 0; k < n_sel; k++) {
        ProModelitem mdlitem;
        status = ProSelectionModelitemGet(p_sel[k], &mdlitem);
        if (status != PRO_TK_NO_ERROR) continue;

        ProPrintfChar("Selected: %s ID: %d\n", get_item_type_str(mdlitem.type), mdlitem.id);

        ProSelection copied_sel;
        status = ProSelectionCopy(p_sel[k], &copied_sel);
        if (status != PRO_TK_NO_ERROR) continue;

        Variable* elem = malloc(sizeof(Variable));
        if (!elem) {
            ProSelectionFree(&copied_sel);
            continue;
        }
        elem->type = TYPE_REFERENCE;
        elem->data.reference.reference_value = copied_sel;

        ProBoolean is_duplicate = PRO_B_FALSE;
        for (size_t i = 0; i < arr_var->data.array.size; i++) {
            Variable* existing = arr_var->data.array.elements[i];
            if (existing->type == TYPE_REFERENCE &&
                is_selection_equal(existing->data.reference.reference_value, copied_sel)) {
                is_duplicate = PRO_B_TRUE;
                break;
            }
        }

        if (is_duplicate) {
            ProSelectionFree(&copied_sel);
            free(elem);
            continue;
        }

        new_elems[added++] = elem;
        ProSelectionUnhighlight(p_sel[k]);
    }

    // Step 8: Append if additions were made
    if (added > 0) {
        size_t old_size = arr_var->data.array.size;
        size_t new_size = old_size + (size_t)added;
        Variable** new_elements = realloc(arr_var->data.array.elements, new_size * sizeof(Variable*));
        if (!new_elements) {
            // Free new elems only; keep original array intact
            for (int i = 0; i < added; i++) {
                // Cast void* to ProSelection for safe freeing
                ProSelection sel = (ProSelection)new_elems[i]->data.reference.reference_value;
                ProSelectionFree(&sel);
                // Assign back (typically NULL after free) with reverse cast
                new_elems[i]->data.reference.reference_value = (void*)sel;
                free(new_elems[i]);
            }
            free(new_elems);
            if (is_new) free(arr_var);
            ProPrintfChar("Error: Memory reallocation failed for array elements");
            in_callback = PRO_B_FALSE;
            return PRO_TK_GENERAL_ERROR;
        }
        arr_var->data.array.elements = new_elements;
        arr_var->data.array.size = new_size;
        memcpy(arr_var->data.array.elements + old_size, new_elems, (size_t)added * sizeof(Variable*));
    }
    free(new_elems);  // Free temp array (contents transferred or freed)

    // Step 9: Store in symbol table if new and additions made
    if (is_new) {
        if (added > 0) {
            set_symbol(data->st, data->node->reference, arr_var);
        }
        else {
            free(arr_var);
        }
    }

    ProPrintfChar("Completed selection storage with %d new items, total %zu.\n", added, arr_var->data.array.size);
    ProPrintfChar("Debug: Exiting UserSelectCallback successfully");


    // Step 10: Re-validate OK button
    ProError val_status = validate_ok_button(dialog, data->st);
    if (val_status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Failed to re-validate OK button after selection");
    }


    /* NEW: repaint this select's drawing area based on current satisfaction */
    (void)paint_user_select_area(dialog, data->draw_area_id, data->st, data->node->reference);


    in_callback = PRO_B_FALSE;
    return PRO_TK_NO_ERROR;
}

// Walk GUI and toggle USER_SELECT pushbuttons on/off based on IF truth
void toggle_user_selects_in_block(Block* blk, SymbolTable* st, const char* dialog, ProBoolean enabled)
{
    if (!blk) return;

    for (size_t i = 0; i < blk->command_count; ++i)
    {
        CommandNode* cmd = blk->commands[i];

        if (cmd->type == COMMAND_USER_SELECT)
        {
            UserSelectNode* un = (UserSelectNode*)cmd->data;
            // Lookup the variable's map to retrieve the button_id we saved at creation
            Variable* v = get_symbol(st, un->reference);
            if (v && v->type == TYPE_MAP)
            {
                Variable* bid = hash_table_lookup(v->data.map, "button_id");
                if (bid && bid->type == TYPE_STRING && bid->data.string_value)
                {
                    if (enabled)
                        (void)ProUIPushbuttonEnable((char*)dialog, bid->data.string_value);
                    else
                        (void)ProUIPushbuttonDisable((char*)dialog, bid->data.string_value);
                }
            }
        }
        else if (cmd->type == COMMAND_IF)
        {
            // Evaluate every branch: enable true branch, disable others
            IfNode* ifn = (IfNode*)cmd->data;

            // First mark all branches disabled
            for (size_t b = 0; b < ifn->branch_count; ++b)
            {
                Block temp = { 0 };
                temp.command_count = ifn->branches[b]->command_count;
                temp.commands = ifn->branches[b]->commands;
                toggle_user_selects_in_block(&temp, st, dialog, PRO_B_FALSE);
            }
            if (ifn->else_command_count > 0)
            {
                Block temp = { 0 };
                temp.command_count = ifn->else_command_count;
                temp.commands = ifn->else_commands;
                toggle_user_selects_in_block(&temp, st, dialog, PRO_B_FALSE);
            }

            // Now enable the active branch
            for (size_t b = 0; b < ifn->branch_count; ++b)
            {
                Variable* cond_val = NULL;
                IfBranch* br = ifn->branches[b];
                if (evaluate_expression(br->condition, st, &cond_val) == 0 && cond_val)
                {
                    bool cond_true = false;
                    if (cond_val->type == TYPE_BOOL || cond_val->type == TYPE_INTEGER)
                        cond_true = (cond_val->data.int_value != 0);
                    else if (cond_val->type == TYPE_DOUBLE)
                        cond_true = (cond_val->data.double_value != 0.0);
                    free_variable(cond_val);

                    if (cond_true)
                    {
                        Block temp = { 0 };
                        temp.command_count = br->command_count;
                        temp.commands = br->commands;
                        toggle_user_selects_in_block(&temp, st, dialog, PRO_B_TRUE);
                        break;
                    }
                }
            }
        }
    }
}


ProError addUserSelect(char* dialog_name, char* parent_layout_name, UserSelectNode* node, int* current_row, int column, SymbolTable* st)
{
    if (!dialog_name || !parent_layout_name || !node || !node->reference || !st)
        return PRO_TK_BAD_INPUTS;

    ProError status;

    Variable* sel_var_guard = get_symbol(st, node->reference);
    if (sel_var_guard && sel_var_guard->type == TYPE_MAP) {
        Variable* existed = hash_table_lookup(sel_var_guard->data.map, "button_id");
        if (existed && existed->type == TYPE_STRING && existed->data.string_value) {
            ProPrintfChar("Info: USER_SELECT '%s' already exists; skipping re-create", node->reference);
            return PRO_TK_NO_ERROR;
        }
    }

    /* unique ids (row-aware, matches your current scheme) */
    char button_draw[128];
    char button_id[128];
    snprintf(button_draw, sizeof(button_draw), "button_draw_%s_%d", node->reference, *current_row);
    snprintf(button_id, sizeof(button_id), "user_select_button_%s_%d", node->reference, *current_row);

    // Persist control IDs into the select variable's map for later toggling
    Variable* sel_var = get_symbol(st, node->reference);
    if (sel_var && sel_var->type == TYPE_MAP)
    {
        Variable* vbtn = malloc(sizeof(Variable));
        Variable* vdar = malloc(sizeof(Variable));
        if (vbtn && vdar)
        {
            vbtn->type = TYPE_STRING;
            vbtn->data.string_value = _strdup(button_id);
            vdar->type = TYPE_STRING;
            vdar->data.string_value = _strdup(button_draw);

            hash_table_insert(sel_var->data.map, "button_id", vbtn);
            hash_table_insert(sel_var->data.map, "draw_area_id", vdar);
        }
    }

    /* add drawing area */
    ProUIGridopts grid = (ProUIGridopts){ 0 };
    grid.row = *current_row;
    grid.column = column;
    grid.horz_cells = 1;
    grid.vert_cells = 1;
    grid.attach_left = PRO_B_TRUE;
    grid.attach_right = PRO_B_TRUE;

    status = ProUILayoutDrawingareaAdd(dialog_name, parent_layout_name, button_draw, &grid);
    if (status != PRO_TK_NO_ERROR) return status;

    /* optional title/border (kept as-is) */
    ProUILayoutTextSet(dialog_name, button_draw, L"Required");

    /* initial size, kept as-is */
    status = ProUIDrawingareaDrawingheightSet(dialog_name, button_draw, 25);
    if (status != PRO_TK_NO_ERROR) return status;
    status = ProUIDrawingareaDrawingwidthSet(dialog_name, button_draw, 146);
    if (status != PRO_TK_NO_ERROR) return status;

    /* NEW: mark as required and paint RED initially */
    (void)require_select(st, node->reference);
    paint_user_select_area(dialog_name, button_draw, st, node->reference);

    /* pushbutton inside the drawing area */
    status = ProUIDrawingareaPushbuttonAdd(dialog_name, button_draw, button_id);
    if (status != PRO_TK_NO_ERROR) return status;

    /* initial position (final size via fit helper) */
    ProUIPushbuttonPositionSet(dialog_name, button_id, 0, 1);

    /* set button text the same way you already do */
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

    /* wire collector callback */
    UserSelectData* sel_data = (UserSelectData*)calloc(1, sizeof(UserSelectData));
    if (!sel_data) return PRO_TK_GENERAL_ERROR;
    sel_data->st = st;
    sel_data->node = node;
    snprintf(sel_data->draw_area_id, sizeof(sel_data->draw_area_id), "%s", button_draw);
    snprintf(sel_data->button_id, sizeof(sel_data->button_id), "%s", button_id);

    status = ProUIPushbuttonActivateActionSet(dialog_name, button_id, UserSelectCallback, (ProAppData)sel_data);
    if (status != PRO_TK_NO_ERROR) { free(sel_data); return status; }

    /* keep button fit to drawing area during resizes */
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

    /* next row */
    (*current_row)++;

    return PRO_TK_NO_ERROR;
}

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
        ProPrintfChar("Could not get drawingarea width");
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

