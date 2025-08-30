#include "utility.h"
#include "symboltable.h"
#include "guicomponent.h"
#include "syntaxanalysis.h"
#include "ScriptExecutor.h"	
#include "GuiLogic.h"
#include "semantic_analysis.h"



// Define the helper function outside ProSelect (static to limit scope to this file)
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



/*=================================================*\
* 
* CORE GUI Components tracking
* 
* 
\*=================================================*/
/* Maintain an array symbol UI_PARAMS = [ "DE_MASTER", "SUB_MASTER", ... ] */
ProError track_ui_param(SymbolTable* st, const char* param_name) {
    if (!st || !param_name) return PRO_TK_BAD_INPUTS;

    Variable* arrv = get_symbol(st, "UI_PARAMS");
    if (!arrv) {
        arrv = (Variable*)calloc(1, sizeof(Variable));
        if (!arrv) return PRO_TK_GENERAL_ERROR;
        arrv->type = TYPE_ARRAY;
        arrv->data.array.size = 0;
        arrv->data.array.elements = NULL;
        set_symbol(st, "UI_PARAMS", arrv);
    }
    else if (arrv->type != TYPE_ARRAY) {
        ProPrintfChar("Error: UI_PARAMS is not an array\n");
        return PRO_TK_GENERAL_ERROR;
    }

    /* dedupe */
    for (size_t i = 0; i < arrv->data.array.size; ++i) {
        Variable* it = arrv->data.array.elements[i];
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

    size_t n = arrv->data.array.size + 1;
    Variable** grown = (Variable**)realloc(arrv->data.array.elements, n * sizeof(*grown));
    if (!grown) { free(namev->data.string_value); free(namev); return PRO_TK_GENERAL_ERROR; }
    arrv->data.array.elements = grown;
    arrv->data.array.elements[n - 1] = namev;
    arrv->data.array.size = n;

    return PRO_TK_NO_ERROR;
}

ProBoolean is_ui_param(SymbolTable* st, const char* param_name) {
    if (!st || !param_name) return PRO_B_FALSE;
    Variable* arrv = get_symbol(st, "UI_PARAMS");
    if (!arrv || arrv->type != TYPE_ARRAY) return PRO_B_FALSE;

    for (size_t i = 0; i < arrv->data.array.size; ++i) {
        Variable* it = arrv->data.array.elements[i];
        if (it && it->type == TYPE_STRING && it->data.string_value &&
            strcmp(it->data.string_value, param_name) == 0) {
            return PRO_B_TRUE;
        }
    }
    return PRO_B_FALSE;
}








/*=================================================*\
* 
* USER_SELECT_MULTIPLE Logical Function
* 
* 
\*=================================================*/
ProError UserSelectMultipleCallback(char* dialog, char* component, ProAppData app_data)
{
    (void)component;

    static ProBoolean in_callback = PRO_B_FALSE;
    if (in_callback == PRO_B_TRUE) {
        ProPrintfChar("Warning: Reentrant call to UserSelectCallback detected; skipping");
        return PRO_TK_NO_ERROR;
    }
    in_callback = PRO_B_TRUE;

    UserSelectMultipleData* data = (UserSelectMultipleData*)app_data;
    if (!data || !data->node || !data->st) {
        ProPrintfChar("Error: Invalid data in UserSelectCallback");
        in_callback = PRO_B_FALSE;
        return PRO_TK_BAD_INPUTS;
    }
    ProPrintfChar("Debug: Entering UserSelectCallback for reference '%s'", data->node->array);

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
    int max_sel = -1;
    if (data->node->max_sel) {
        Variable* msv = NULL;
        if (evaluate_expression(data->node->max_sel, data->st, &msv) == 0 && msv) {
            /* Coerce to int with conservative rules */
            switch (msv->type) {
            case TYPE_INTEGER:
                max_sel = (int)msv->data.int_value;
                break;
            case TYPE_DOUBLE:
                /* Truncate toward zero (consistent with C cast) */
                max_sel = (int)msv->data.double_value;
                break;
            case TYPE_STRING:
                if (msv->data.string_value && msv->data.string_value[0] != '\0') {
                    char* endp = NULL;
                    long v = strtol(msv->data.string_value, &endp, 10);
                    if (endp && *endp == '\0') {
                        max_sel = (int)v;
                    }
                    else {
                        ProPrintfChar("Warning: max_sel string is not an integer; using unlimited");
                        max_sel = -1;
                    }
                }
                break;
            case TYPE_BOOL:
                /* Treat TRUE as unlimited, FALSE as unlimited (user probably did not intend 0 cap) */
                max_sel = -1;
                break;
            default:
                ProPrintfChar("Warning: Unsupported max_sel type %d; using unlimited", (int)msv->type);
                max_sel = -1;
                break;
            }
            free_variable(msv);
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate max_sel; using unlimited");
        }

        /* Sanitize: non-positive means unlimited per ProSelect semantics */
        if (max_sel < 1) max_sel = -1;
    }
    ProPrintfChar("Debug: Using max_sel=%d (-1 means unlimited)", max_sel);
    status = ProSelect(sel_type, max_sel, NULL, NULL, NULL, NULL, &p_sel, &n_sel);
    free(sel_type);  // Cleanup early



    // Step 5: Show dialog after selection
    ProError show_status = ProUIDialogShow(dialog);
    if (show_status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Failed to show dialog after selection");
    }

    if (status != PRO_TK_NO_ERROR || n_sel < 1) {
        ProPrintfChar("Debug: No selection; requesting repaint for ref='%s' draw='%s'",
            data->node->array, data->draw_area_id);
        in_callback = PRO_B_FALSE;
        return PRO_TK_NO_ERROR;
    }
    ProPrintfChar("Selection made, processing %d items...", n_sel);


    // Step 6: Get or create the array variable
    Variable* arr_var = get_symbol(data->st, data->node->array);
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
            set_symbol(data->st, data->node->array, arr_var);
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

    // Trigger repaint to update color based on new satisfied state
    if (strlen(data->draw_area_id) > 0) {
        UpdateData temp_data;
        temp_data.st = data->st;
        snprintf(temp_data.reference, sizeof(temp_data.reference), "%s", data->node->array);
        (void)UserSelectUpdateCallback(dialog, data->draw_area_id, (ProAppData)&temp_data);
    }


    /* Keep your existing OK revalidation and reactive refresh */
    EPA_ReactiveRefresh();


    in_callback = PRO_B_FALSE;
    return PRO_TK_NO_ERROR;
}

ProError UserSelectMultipleOptionalCallback(char* dialog, char* component, ProAppData app_data)
{
    (void)component;

    static ProBoolean in_callback = PRO_B_FALSE;
    if (in_callback == PRO_B_TRUE) {
        ProPrintfChar("Warning: Reentrant call to UserSelectCallback detected; skipping");
        return PRO_TK_NO_ERROR;
    }
    in_callback = PRO_B_TRUE;

    UserSelectMultipleOptionalData* data = (UserSelectMultipleOptionalData*)app_data;
    if (!data || !data->node || !data->st) {
        ProPrintfChar("Error: Invalid data in UserSelectCallback");
        in_callback = PRO_B_FALSE;
        return PRO_TK_BAD_INPUTS;
    }
    ProPrintfChar("Debug: Entering UserSelectCallback for reference '%s'", data->node->array);

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
    int max_sel = -1;
    if (data->node->max_sel) {
        Variable* msv = NULL;
        if (evaluate_expression(data->node->max_sel, data->st, &msv) == 0 && msv) {
            /* Coerce to int with conservative rules */
            switch (msv->type) {
            case TYPE_INTEGER:
                max_sel = (int)msv->data.int_value;
                break;
            case TYPE_DOUBLE:
                /* Truncate toward zero (consistent with C cast) */
                max_sel = (int)msv->data.double_value;
                break;
            case TYPE_STRING:
                if (msv->data.string_value && msv->data.string_value[0] != '\0') {
                    char* endp = NULL;
                    long v = strtol(msv->data.string_value, &endp, 10);
                    if (endp && *endp == '\0') {
                        max_sel = (int)v;
                    }
                    else {
                        ProPrintfChar("Warning: max_sel string is not an integer; using unlimited");
                        max_sel = -1;
                    }
                }
                break;
            case TYPE_BOOL:
                /* Treat TRUE as unlimited, FALSE as unlimited (user probably did not intend 0 cap) */
                max_sel = -1;
                break;
            default:
                ProPrintfChar("Warning: Unsupported max_sel type %d; using unlimited", (int)msv->type);
                max_sel = -1;
                break;
            }
            free_variable(msv);
        }
        else {
            ProPrintfChar("Warning: Failed to evaluate max_sel; using unlimited");
        }

        /* Sanitize: non-positive means unlimited per ProSelect semantics */
        if (max_sel < 1) max_sel = -1;
    }
    ProPrintfChar("Debug: Using max_sel=%d (-1 means unlimited)", max_sel);
    status = ProSelect(sel_type, max_sel, NULL, NULL, NULL, NULL, &p_sel, &n_sel);
    free(sel_type);  // Cleanup early



    // Step 5: Show dialog after selection
    ProError show_status = ProUIDialogShow(dialog);
    if (show_status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Failed to show dialog after selection");
    }

    if (status != PRO_TK_NO_ERROR || n_sel < 1) {
        ProPrintfChar("Debug: No selection; requesting repaint for ref='%s' draw='%s'",
            data->node->array, data->draw_area_id);
        in_callback = PRO_B_FALSE;
        return PRO_TK_NO_ERROR;
    }
    ProPrintfChar("Selection made, processing %d items...", n_sel);


    // Step 6: Get or create the array variable
    Variable* arr_var = get_symbol(data->st, data->node->array);
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
            set_symbol(data->st, data->node->array, arr_var);
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

    // Trigger repaint to update color based on new satisfied state
    if (strlen(data->draw_area_id) > 0) {
        UpdateData temp_data;
        temp_data.st = data->st;
        snprintf(temp_data.reference, sizeof(temp_data.reference), "%s", data->node->array);
        (void)UserSelectOptionalUpdateCallback(dialog, data->draw_area_id, (ProAppData)&temp_data);
    }

    /* Keep your existing OK revalidation and reactive refresh */
    EPA_ReactiveRefresh();


    in_callback = PRO_B_FALSE;
    return PRO_TK_NO_ERROR;
}


/*=================================================*\
* 
* USER_SELECT_OPTIONAL Logical Function
* 
* 
\*=================================================*/
ProError UserSelectOptionalCallback(char* dialog, char* component, ProAppData app_data) {
    (void)component;

    static ProBoolean in_callback = PRO_B_FALSE;
    if (in_callback == PRO_B_TRUE) {
        ProPrintfChar("Warning: Reentrant call to UserSelectCallback detected; skipping");
        return PRO_TK_NO_ERROR;
    }
    in_callback = PRO_B_TRUE;

    UserSelectOptionalData* data = (UserSelectOptionalData*)app_data;
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
    int max_sel = 1;  // Unlimited
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

    // Trigger repaint to update color based on new satisfied state
    if (strlen(data->draw_area_id) > 0) {
        UpdateData temp_data;
        temp_data.st = data->st;
        snprintf(temp_data.reference, sizeof(temp_data.reference), "%s", data->node->reference);
        (void)UserSelectOptionalUpdateCallback(dialog, data->draw_area_id, (ProAppData)&temp_data);
    }

    /* Keep your existing OK revalidation and reactive refresh */
    EPA_ReactiveRefresh();


    in_callback = PRO_B_FALSE;
    return PRO_TK_NO_ERROR;
}



/*=================================================*\
* 
* USER_SELECT LOGICAL FUNCTIONS
*
* 
\*=================================================*/
// Helper function to compare two ProSelections (implement based on selobj.html)
ProBoolean is_selection_equal(ProSelection sel1, ProSelection sel2) {
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
    LogOnlyPrintfChar("Debug: Entering UserSelectCallback for reference '%s'", data->node->reference);

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
    LogOnlyPrintfChar("Debug: Constructed selection type: %s", sel_type);

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
    int max_sel = 1;  // Unlimited
    status = ProSelect(sel_type, max_sel, NULL, NULL, NULL, NULL, &p_sel, &n_sel);
    free(sel_type);  // Cleanup early


    // Step 5: Show dialog after selection
    ProError show_status = ProUIDialogShow(dialog);
    if (show_status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Failed to show dialog after selection");
    }

    if (status != PRO_TK_NO_ERROR || n_sel < 1) {
        LogOnlyPrintfChar("Debug: No selection; requesting repaint for ref='%s' draw='%s'",
            data->node->reference, data->draw_area_id);
        // Trigger repaint even on no selection to ensure consistency
        if (strlen(data->draw_area_id) > 0) {
            UpdateData temp_data;
            temp_data.st = data->st;
            snprintf(temp_data.reference, sizeof(temp_data.reference), "%s", data->node->reference);
            (void)UserSelectUpdateCallback(dialog, data->draw_area_id, (ProAppData)&temp_data);
        }
        in_callback = PRO_B_FALSE;
        return PRO_TK_NO_ERROR;
    }
    LogOnlyPrintfChar("Selection made, processing %d items...", n_sel);


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

    LogOnlyPrintfChar("Completed selection storage with %d new items, total %zu.\n", added, arr_var->data.array.size);
    LogOnlyPrintfChar("Debug: Exiting UserSelectCallback successfully");


    // Step 10: Re-validate OK button
    ProError val_status = validate_ok_button(dialog, data->st);
    if (val_status != PRO_TK_NO_ERROR) {
        ProPrintfChar("Warning: Failed to re-validate OK button after selection");
    }

    // Trigger repaint to update color based on new satisfied state
    if (strlen(data->draw_area_id) > 0) {
        UpdateData temp_data;
        temp_data.st = data->st;
        snprintf(temp_data.reference, sizeof(temp_data.reference), "%s", data->node->reference);
        (void)UserSelectUpdateCallback(dialog, data->draw_area_id, (ProAppData)&temp_data);
    }

    /* Keep your existing OK revalidation and reactive refresh */
    EPA_ReactiveRefresh();


    in_callback = PRO_B_FALSE;
    return PRO_TK_NO_ERROR;
}

ProError unrequire_select(SymbolTable* st, const char* reference) {
    if (!st || !reference) return PRO_TK_BAD_INPUTS;

    Variable* req = get_symbol(st, "REQUIRED_SELECTS");
    if (!req || req->type != TYPE_ARRAY) return PRO_TK_NO_ERROR;  // Nothing to remove; silent success

    ArrayData* arr = &req->data.array;
    for (size_t i = 0; i < arr->size; ++i) {
        Variable* item = arr->elements[i];
        if (item && item->type == TYPE_STRING && item->data.string_value &&
            strcmp(item->data.string_value, reference) == 0) {
            // Free the item and shift remaining elements
            free_variable(item);
            for (size_t j = i; j < arr->size - 1; ++j) {
                arr->elements[j] = arr->elements[j + 1];
            }
            arr->size--;
            // Optional: Shrink allocation if desired
            if (arr->size > 0) {
                Variable** shrunk = (Variable**)realloc(arr->elements, arr->size * sizeof(Variable*));
                if (shrunk) arr->elements = shrunk;
            }
            else {
                free(arr->elements);
                arr->elements = NULL;
            }
            ProPrintfChar("Debug: Unrequired select '%s'", reference);
            return PRO_TK_NO_ERROR;
        }
    }
    return PRO_TK_NO_ERROR;  // Not found; silent success
}

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

ProBoolean is_select_satisfied(SymbolTable* st, char* reference)
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

int var_to_bool(const Variable* v, int dflt)
{
    if (!v) return dflt;
    if (v->type == TYPE_BOOL || v->type == TYPE_INTEGER) return v->data.int_value ? 1 : 0;
    return dflt;
}

// Helper to map ProUIColor to string for logging
const char* uicolor_to_string(ProUIColor color) {
    switch (color) {
    case PRO_UI_COLOR_WHITE: return "WHITE";
    case PRO_UI_COLOR_RED: return "RED";
        // Add other colors if your policy expands (e.g., PRO_UI_COLOR_LT_GREY)
    default: return "UNKNOWN";
    }
}

ProError UserSelectOptionalUpdateCallback(char* dialog, char* component, ProAppData app_data)
{
    if (!dialog || !component || !app_data) return PRO_TK_BAD_INPUTS;

    UpdateData* data = (UpdateData*)app_data;
    SymbolTable* st = data->st;
    char* reference = data->reference;

    if (!st || !reference[0]) return PRO_TK_BAD_INPUTS;

    /* Defaults: enabled and required unless explicitly gated */
    int enabled = 1;
    int required = 1;
    Variable* v = get_symbol(st, reference);
    if (v && v->type == TYPE_MAP) {
        enabled = var_to_bool(hash_table_lookup(v->data.map, "ui_enabled"), 1);
        required = var_to_bool(hash_table_lookup(v->data.map, "ui_required"), 1);
    }

    /* Coloring policy:
       disabled                          -> WHITE
       enabled && !required              -> WHITE
       enabled && required && satisfied  -> WHITE
       enabled && required && !satisfied -> RED
    */
    ProBoolean satisfied = PRO_B_FALSE;  /* Only evaluated when needed */
    ProUIColorType target = PRO_UI_COLOR_WHITE;

    if (enabled && required) {
        satisfied = is_select_satisfied(st, reference);
        target = (satisfied == PRO_B_TRUE) ? PRO_UI_COLOR_WHITE : PRO_UI_COLOR_GREEN;
    }

    /* Log the full state for debugging */
    LogOnlyPrintfChar("Debug: Paint '%s' (draw_id '%s'): enabled=%d required=%d satisfied=%d target=%s disabled=%d\n",
        reference, component, enabled, required, satisfied,
        uicolor_to_string(target), !enabled);

    /* Set the foreground color */
    ProError status = ProUIDrawingareaFgcolorSet(dialog, component, target);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Set failed for '%s' (target %s): status=%d\n",
            reference, uicolor_to_string(target), status);
        return status;
    }

    /* Clear the drawing area to remove previous drawings */
    ProUIDrawingareaClear(dialog, component);

    /* Retrieve current drawing area dimensions for rectangle redraw */
    int da_w = 0, da_h = 0;
    status = ProUIDrawingareaDrawingwidthGet(dialog, component, &da_w);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Could not get width for '%s' (draw_id '%s'): status=%d\n",
            reference, component, status);
        return status;
    }
    status = ProUIDrawingareaDrawingheightGet(dialog, component, &da_h);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Could not get height for '%s' (draw_id '%s'): status=%d\n",
            reference, component, status);
        return status;
    }

    /* Draw the rectangle outline matching the full size of the drawing area */
    ProUIRectangle rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = 146;  /* Adjust for precise border outline */
    rect.height = 25; /* Adjust for precise border outline */
    status = ProUIDrawingareaRectDraw(dialog, component, &rect);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Could not draw rectangle for '%s' (draw_id '%s'): status=%d\n",
            reference, component, status);
        return status;
    }

    /* Log confirmation after set and redraw */
    LogOnlyPrintfChar("Debug: Set and redraw succeeded for '%s': new color=%s\n",
        reference, uicolor_to_string(target));

    return PRO_TK_NO_ERROR;
}

ProError UserSelectUpdateCallback(char* dialog, char* component, ProAppData app_data)
{
    if (!dialog || !component || !app_data) return PRO_TK_BAD_INPUTS;

    UpdateData* data = (UpdateData*)app_data;
    SymbolTable* st = data->st;
    char* reference = data->reference;

    if (!st || !reference[0]) return PRO_TK_BAD_INPUTS;

    /* Defaults: enabled and required unless explicitly gated */
    int enabled = 1;
    int required = 1;
    Variable* v = get_symbol(st, reference);
    if (v && v->type == TYPE_MAP) {
        enabled = var_to_bool(hash_table_lookup(v->data.map, "ui_enabled"), 1);
        required = var_to_bool(hash_table_lookup(v->data.map, "ui_required"), 1);
    }

    /* Coloring policy:
       disabled                          -> WHITE
       enabled && !required              -> WHITE
       enabled && required && satisfied  -> WHITE
       enabled && required && !satisfied -> RED
    */
    ProBoolean satisfied = PRO_B_FALSE;  /* Only evaluated when needed */
    ProUIColorType target = PRO_UI_COLOR_WHITE;

    if (enabled && required) {
        satisfied = is_select_satisfied(st, reference);
        target = (satisfied == PRO_B_TRUE) ? PRO_UI_COLOR_WHITE : PRO_UI_COLOR_RED;
    }

    /* Log the full state for debugging */
    LogOnlyPrintfChar("Debug: Paint '%s' (draw_id '%s'): enabled=%d required=%d satisfied=%d target=%s disabled=%d\n",
        reference, component, enabled, required, satisfied,
        uicolor_to_string(target), !enabled);

    /* Set the foreground color */
    ProError status = ProUIDrawingareaFgcolorSet(dialog, component, target);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Set failed for '%s' (target %s): status=%d\n",
            reference, uicolor_to_string(target), status);
        return status;
    }

    /* Clear the drawing area to remove previous drawings */
    ProUIDrawingareaClear(dialog, component);

    /* Retrieve current drawing area dimensions for rectangle redraw */
    int da_w = 0, da_h = 0;
    status = ProUIDrawingareaDrawingwidthGet(dialog, component, &da_w);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Could not get width for '%s' (draw_id '%s'): status=%d\n",
            reference, component, status);
        return status;
    }
    status = ProUIDrawingareaDrawingheightGet(dialog, component, &da_h);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Could not get height for '%s' (draw_id '%s'): status=%d\n",
            reference, component, status);
        return status;
    }

    /* Draw the rectangle outline matching the full size of the drawing area */
    ProUIRectangle rect;
    rect.x = 0;
    rect.y = 0;
    rect.width = 146;  /* Adjust for precise border outline */
    rect.height = 25; /* Adjust for precise border outline */
    status = ProUIDrawingareaRectDraw(dialog, component, &rect);
    if (status != PRO_TK_NO_ERROR) {
        LogOnlyPrintfChar("Debug: Could not draw rectangle for '%s' (draw_id '%s'): status=%d\n",
            reference, component, status);
        return status;
    }

    /* Log confirmation after set and redraw */
    LogOnlyPrintfChar("Debug: Set and redraw succeeded for '%s': new color=%s\n",
        reference, uicolor_to_string(target));

    return PRO_TK_NO_ERROR;
}

ProError set_user_select_enabled(char* dialog, SymbolTable* st, const char* reference, ProBoolean enabled, ProBoolean required)
{
    if (!st || !reference) return PRO_TK_BAD_INPUTS; /* allow dialog == NULL for headless gating */

    Variable* sv = get_symbol(st, (char*)reference);
    if (!sv || sv->type != TYPE_MAP || !sv->data.map) {
        /* Not fatal: IF might gate before creation */
        ProPrintfChar("Info: set_user_select_enabled('%s'): symbol not created yet; staged flags only\n", reference);
        return PRO_TK_NO_ERROR;
    }

    HashTable* map = sv->data.map;

    /* Final flags: enabled and required are now independent */
    const int en = (enabled == PRO_B_TRUE) ? 1 : 0;
    const int rq = (required == PRO_B_TRUE) ? 1 : 0;

    /* Persist runtime UI flags */
    set_bool_in_map(map, "ui_enabled", en);
    set_bool_in_map(map, "ui_required", rq);

    if (en == 0 && sv && sv->type == TYPE_MAP && sv->data.map) {
        Variable* refv = hash_table_lookup(sv->data.map, "reference_value");
        if (refv) {
            free_variable(refv);
            hash_table_remove(sv->data.map, "reference_value");
        }
    }
    else if (en == 0 && sv && sv->type == TYPE_ARRAY) {
        ArrayData* arr = &sv->data.array;
        for (size_t i = 0; i < arr->size; ++i) {
            free_variable(arr->elements[i]);
        }
        free(arr->elements);
        arr->elements = NULL;
        arr->size = 0;
    }

    /* Keep REQUIRED_SELECTS in sync with required (independent of enabled) */
    if (rq) require_select(st, reference);
    else unrequire_select(st, reference);

    /* Resolve component ids (if present) */
    char* button_id = NULL;
    char* draw_id = NULL;
    {
        Variable* bid = hash_table_lookup(map, "button_id");
        if (bid && bid->type == TYPE_STRING && bid->data.string_value && bid->data.string_value[0] != '\0')
            button_id = bid->data.string_value;

        Variable* da = hash_table_lookup(map, "draw_area_id");
        if (da && da->type == TYPE_STRING && da->data.string_value && da->data.string_value[0] != '\0')
            draw_id = da->data.string_value;
    }

    if (dialog && button_id) {
        if (en) {
            (void)ProUIPushbuttonEnable(dialog, button_id);
        }
        else {
            (void)ProUIPushbuttonDisable(dialog, button_id);
        }
    }
    if (dialog && draw_id) {
        if (en) {
            (void)ProUIDrawingareaEnable(dialog, draw_id);
        }
        else {
            (void)ProUIDrawingareaDisable(dialog, draw_id);
        }
    }

    /* Manually trigger repaint to apply color changes based on new state */
    if (dialog && draw_id) {
        UpdateData temp_data;
        temp_data.st = st;
        snprintf(temp_data.reference, sizeof(temp_data.reference), "%s", reference);
        (void)UserSelectUpdateCallback(dialog, draw_id, (ProAppData)&temp_data);
    }

    /* Single-line debug: shows what you asked for */
    LogOnlyPrintfChar(
        "Debug: USER_SELECT '%s' -> enabled=%d required=%d | button=%s draw=%s\n",
        reference,
        en,
        rq,
        button_id ? button_id : "<none>",
        draw_id ? draw_id : "<none>"
    );

    return PRO_TK_NO_ERROR;
}

ProError set_user_select_optional_enabled(char* dialog, SymbolTable* st, const char* reference, ProBoolean enabled, ProBoolean required)
{
    if (!st || !reference) return PRO_TK_BAD_INPUTS; /* allow dialog == NULL for headless gating */

    Variable* sv = get_symbol(st, (char*)reference);
    if (!sv || sv->type != TYPE_MAP || !sv->data.map) {
        /* Not fatal: IF might gate before creation */
        ProPrintfChar("Info: set_user_select_enabled('%s'): symbol not created yet; staged flags only\n", reference);
        return PRO_TK_NO_ERROR;
    }

    HashTable* map = sv->data.map;

    /* Final flags: enabled and required are now independent */
    const int en = (enabled == PRO_B_TRUE) ? 1 : 0;
    const int rq = (required == PRO_B_TRUE) ? 1 : 0;

    /* Persist runtime UI flags */
    set_bool_in_map(map, "ui_enabled", en);
    set_bool_in_map(map, "ui_required", rq);

    if (en == 0 && sv && sv->type == TYPE_MAP && sv->data.map) {
        Variable* refv = hash_table_lookup(sv->data.map, "reference_value");
        if (refv) {
            free_variable(refv);
            hash_table_remove(sv->data.map, "reference_value");
        }
    }
    else if (en == 0 && sv && sv->type == TYPE_ARRAY) {
        ArrayData* arr = &sv->data.array;
        for (size_t i = 0; i < arr->size; ++i) {
            free_variable(arr->elements[i]);
        }
        free(arr->elements);
        arr->elements = NULL;
        arr->size = 0;
    }

    /* Keep REQUIRED_SELECTS in sync with required (independent of enabled) */
    if (rq) require_select(st, reference);
    else unrequire_select(st, reference);

    /* Resolve component ids (if present) */
    char* button_id = NULL;
    char* draw_id = NULL;
    {
        Variable* bid = hash_table_lookup(map, "button_id");
        if (bid && bid->type == TYPE_STRING && bid->data.string_value && bid->data.string_value[0] != '\0')
            button_id = bid->data.string_value;

        Variable* da = hash_table_lookup(map, "draw_area_id");
        if (da && da->type == TYPE_STRING && da->data.string_value && da->data.string_value[0] != '\0')
            draw_id = da->data.string_value;
    }

    if (dialog && button_id) {
        if (en) {
            (void)ProUIPushbuttonEnable(dialog, button_id);
        }
        else {
            (void)ProUIPushbuttonDisable(dialog, button_id);
        }
    }
    if (dialog && draw_id) {
        if (en) {
            (void)ProUIDrawingareaEnable(dialog, draw_id);
        }
        else {
            (void)ProUIDrawingareaDisable(dialog, draw_id);
        }
    }

    /* Manually trigger repaint to apply color changes based on new state */
    if (dialog && draw_id) {
        UpdateData temp_data;
        temp_data.st = st;
        snprintf(temp_data.reference, sizeof(temp_data.reference), "%s", reference);
        (void)UserSelectOptionalUpdateCallback(dialog, draw_id, (ProAppData)&temp_data);
    }

    /* Single-line debug: shows what you asked for */
    LogOnlyPrintfChar(
        "Debug: USER_SELECT '%s' -> enabled=%d required=%d | button=%s draw=%s\n",
        reference,
        en,
        rq,
        button_id ? button_id : "<none>",
        draw_id ? draw_id : "<none>"
    );

    return PRO_TK_NO_ERROR;
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
    ButtonFitData* data = (ButtonFitData*)app_data;
    if (!dialog || !data) return PRO_TK_BAD_INPUTS;

    return fit_pushbutton_to_drawingarea(dialog, component, data->button_id);
    
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
/* Rebuild only the SUB_PICTURES array from a GUI block */
 ProError rebuild_sub_pictures_only(Block* gui_block, SymbolTable* st)
 {
     if (!gui_block) return PRO_TK_NO_ERROR;

     for (size_t i = 0; i < gui_block->command_count; ++i) {
         CommandNode* cmd = gui_block->commands[i];
         switch (cmd->type)
         {
         case COMMAND_DECLARE_VARIABLE: {
             /* Keep your existing reset of GUI-local defaults */
             DeclareVariableNode* dv = (DeclareVariableNode*)cmd->data;
             if (dv && dv->name) {
                 remove_symbol(st, dv->name);
                 (void)execute_declare_variable(dv, st);
             }
         } break;

         case COMMAND_ASSIGNMENT: {
             /* NEW: do not overwrite user-driven params during reactive rebuild */
             AssignmentNode* asn = (AssignmentNode*)cmd->data;
             const char* lhs_name = NULL;
             if (asn && asn->lhs && asn->lhs->type == EXPR_VARIABLE_REF) {
                 lhs_name = asn->lhs->data.string_val; /* identifier text */
             }
             if (lhs_name && is_ui_param(st, lhs_name) == PRO_B_TRUE) {
                 /* skip mutating input parameters */
                 break;
             }
             (void)execute_assignment(asn, st, NULL);
         } break;

         case COMMAND_SUB_PICTURE:
             (void)execute_sub_picture((SubPictureNode*)cmd->data, st);
             break;

         case COMMAND_IF: {
             IfNode* ifn = (IfNode*)cmd->data;
             bool matched = false;

             for (size_t b = 0; b < ifn->branch_count; ++b) {
                 Variable* cond_val = NULL;
                 IfBranch* br = ifn->branches[b];
                 if (evaluate_expression(br->condition, st, &cond_val) == 0 && cond_val) {
                     bool cond_true = false;
                     if (cond_val->type == TYPE_BOOL || cond_val->type == TYPE_INTEGER)
                         cond_true = (cond_val->data.int_value != 0);
                     else if (cond_val->type == TYPE_DOUBLE)
                         cond_true = (cond_val->data.double_value != 0.0);
                     free_variable(cond_val);
                     if (cond_true) {
                         Block tmp = { 0 };
                         tmp.command_count = br->command_count;
                         tmp.commands = br->commands;
                         (void)rebuild_sub_pictures_only(&tmp, st);
                         matched = true;
                         break;
                     }
                 }
             }
             if (!matched && ifn->else_command_count > 0) {
                 Block tmp = { 0 };
                 tmp.command_count = ifn->else_command_count;
                 tmp.commands = ifn->else_commands;
                 (void)rebuild_sub_pictures_only(&tmp, st);
             }
         } break;

         default: break;
         }
     }

     return PRO_TK_NO_ERROR;
 }
