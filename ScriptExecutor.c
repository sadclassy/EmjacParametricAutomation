#include "ScriptExecutor.h"
#include "utility.h"
#include "syntaxanalysis.h"
#include "symboltable.h"
#include "guicomponent.h"
#include "semantic_analysis.h"
#include "GuiLogic.h"


// --- Reactive context (file-scope) ---
static DialogState* g_active_state = NULL;
static SymbolTable* g_active_st = NULL;
static int dynamic_table_count = 0;


static int if_gate_id_of(IfNode* n)
{
	return (int)(((uintptr_t)n) & 0x7fffffff);
}


ProError execute_if(IfNode* node, SymbolTable* st, BlockList* block_list, DialogState* state);

ProError execute_assignment(AssignmentNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_declare_variable(DeclareVariableNode* node, SymbolTable* st);
ProError TableSelectCallback(char* dialog, char* table, ProAppData appdata);

ProError InitializeLayout(char* dialog_name, char* parent_layout, char* layout_name, ProUIGridopts* grid_opts, wchar_t* title, int* initialized)
{
	ProError status;
	
	if (*initialized)
	{
		return PRO_TK_NO_ERROR;
	}

	grid_opts->attach_bottom = PRO_B_TRUE;
	grid_opts->attach_left = PRO_B_TRUE;
	grid_opts->attach_right = PRO_B_TRUE;
	grid_opts->attach_top = PRO_B_TRUE;
	grid_opts->horz_resize = PRO_B_TRUE;
	grid_opts->vert_resize = PRO_B_TRUE;

	status = ProUILayoutLayoutAdd(dialog_name, parent_layout, layout_name, grid_opts);
	if (status != PRO_TK_NO_ERROR)
	{
		ProGenericMsg(L"Could not add layout");
		return status;
	}
	status = ProUILayoutDecorate(dialog_name, layout_name);
	if (status != PRO_TK_NO_ERROR)
	{
		ProGenericMsg(L"Could not create border for layout");
		return status;
	}

	status = ProUILayoutTextSet(dialog_name, layout_name, title);
	if (status != PRO_TK_NO_ERROR)
	{
		ProGenericMsg(L"Could not set title for layout");
		return status;
	}
	*initialized = 1;
	return PRO_TK_NO_ERROR;
}

ProError InitializeTableLayout(char* dialog_name, char* parent_layout, char* layout_name, ProUIGridopts* grid_opts, wchar_t* title, int* initialized)
{
	ProError status;

	if (*initialized)
	{
		return PRO_TK_NO_ERROR;
	}

	grid_opts->attach_bottom = PRO_B_TRUE;
	grid_opts->attach_left = PRO_B_TRUE;
	grid_opts->attach_right = PRO_B_FALSE;
	grid_opts->attach_top = PRO_B_TRUE;
	grid_opts->horz_resize = PRO_B_TRUE;
	grid_opts->vert_resize = PRO_B_TRUE;

	status = ProUILayoutLayoutAdd(dialog_name, parent_layout, layout_name, grid_opts);
	if (status != PRO_TK_NO_ERROR)
	{
		ProGenericMsg(L"Could not add layout");
		return status;
	}
	status = ProUILayoutDecorate(dialog_name, layout_name);
	if (status != PRO_TK_NO_ERROR)
	{
		ProGenericMsg(L"Could not create border for layout");
		return status;
	}

	status = ProUILayoutTextSet(dialog_name, layout_name, title);
	if (status != PRO_TK_NO_ERROR)
	{
		ProGenericMsg(L"Could not set title for layout");
		return status;
	}
	*initialized = 1;
	return PRO_TK_NO_ERROR;
}

ProError execute_show_param(ShowParamNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;
	if (node->on_picture)
	{
		return addShowParam(state->dialog_name, state->show_param_layout.name, node, &state->show_param_layout.row, 0, st);
	}
	else
	{

		// Add parameter to standard layout
		status = addShowParam(state->dialog_name, state->show_param_layout.name, node, &state->show_param_layout.row, 0, st);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Error: Could not add parameter '%s' to layout\n", node->parameter);
			return status;
		}

	}


	return PRO_TK_NO_ERROR;
}

ProError execute_checkbox_param(CheckboxParamNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;

	if (node->on_picture)
	{
		return addCheckboxParam(state->dialog_name, state->checkbox_layout.name, node, &state->checkbox_layout.row, 0, st);

	}
	else
	{


		status = addCheckboxParam(state->dialog_name, state->checkbox_layout.name, node, &state->checkbox_layout.row, 0, st);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Error: Could not add Checkbox '%s' to layout\n", node->parameter);
			return status;
		}
	}



	return PRO_TK_NO_ERROR;
}

ProError execute_user_input_param(UserInputParamNode* node, DialogState* state, SymbolTable* st)
{

	ProError status;
	ProUIGridopts grid_opts_uip = { 0 };
	grid_opts_uip.row = 1;
	grid_opts_uip.column = 2;
	grid_opts_uip.attach_bottom = PRO_B_TRUE;
	grid_opts_uip.attach_left = PRO_B_TRUE;
	grid_opts_uip.attach_right = PRO_B_TRUE;
	grid_opts_uip.attach_top = PRO_B_TRUE;
	grid_opts_uip.left_offset = 20;

	status = InitializeLayout(state->dialog_name, state->main_layout_name, state->user_input_layout.name, &grid_opts_uip, L"Enter Values", &state->user_input_layout.initialized);
	if (status != PRO_TK_NO_ERROR)
	{
		ProPrintfChar("Could not set InitializeLayout for USER_INPUT_PARAM");
		return status;
	}

	// Add parameter to standard layout
	status = addUserInputParam(state->dialog_name, state->user_input_layout.name, node, &state->user_input_layout.row, 1, st);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not add parameter '%ls' to layout\n", node->parameter);
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_radiobutton_param(RadioButtonParamNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;

	status = addRadioButtonParam(state->dialog_name, state->radiobutton_layout.name, node, &state->radiobutton_layout.row, 3, st);
	if (status != PRO_TK_NO_ERROR)
	{
		ProPrintfChar("Error: Could not add parameter '%s' to layout\n", node->parameter);
		return status;
	}

	return PRO_TK_NO_ERROR;
	
}

ProError execute_user_select_param(UserSelectNode* node, DialogState* state, SymbolTable* st)
{
	Variable* existing = get_symbol(st, node->reference);
	if (existing && existing->type == TYPE_MAP && hash_table_lookup(existing->data.map, "draw_area_id") != NULL)
	{
		return PRO_TK_NO_ERROR;
	}

	ProError status;


	// Create (once) an inner grid inside the user_select_layout
	char s_us_grid_name[64] = "user_select_grid";
	if (!state->user_select_layout.s_us_grid_initialized)
	{
		ProUIGridopts sub_grid = { 0 };
		sub_grid.row = 0;
		sub_grid.column = 0;
		sub_grid.horz_resize = PRO_B_TRUE;
		sub_grid.attach_right = PRO_B_TRUE;
		sub_grid.attach_left = PRO_B_TRUE;
		sub_grid.horz_cells = 2;
		sub_grid.vert_cells = 1;

		status = ProUILayoutLayoutAdd(state->dialog_name, state->user_select_layout.name, s_us_grid_name, &sub_grid);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Error: Could not set layout inside the main user select layotu");
			return status;
		}

		ProUILayoutDecorate(state->dialog_name, s_us_grid_name);
		ProUILayoutTextSet(state->dialog_name, s_us_grid_name, L"Required Selection");
		
		state->user_select_layout.row = 1;
		state->user_select_layout.s_us_grid_initialized = 1;
	}
	


	// Add the selection parameter to the target sub-layout
	status = addUserSelect(state->dialog_name, s_us_grid_name, node, &state->user_select_layout.row, 0, st);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Error adding user select parameter");
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_user_select_optional_param(UserSelectOptionalNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;

	// Create (once) an inner grid inside the user_select_layout
	char s_us_grid_name_optional[64] = "user_select_grid_optional";
	if (!state->user_select_layout.s_us_gridO_initialize)
	{
		ProUIGridopts sub_grid = { 0 };
		sub_grid.row = 2;
		sub_grid.column = 0;
		sub_grid.horz_resize = PRO_B_TRUE;
		sub_grid.attach_right = PRO_B_TRUE;
		sub_grid.attach_left = PRO_B_TRUE;
		sub_grid.horz_cells = 2;
		sub_grid.vert_cells = 1;

		status = ProUILayoutLayoutAdd(state->dialog_name, state->user_select_layout.name, s_us_grid_name_optional, &sub_grid);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Error: Could not set layout inside the main user select layotu");
			return status;
		}

		ProUILayoutDecorate(state->dialog_name, s_us_grid_name_optional);
		ProUILayoutTextSet(state->dialog_name, s_us_grid_name_optional, L"Optional Selection");

		state->user_select_layout.row = 1;
		state->user_select_layout.s_us_gridO_initialize = 1;
	}



	// Add the selection parameter to the target sub-layout
	status = addUserSelectOptional(state->dialog_name, s_us_grid_name_optional, node, &state->user_select_layout.row, 0, st);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Error adding user select parameter");
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_user_select_multiple_param(UserSelectMultipleNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;


	// Create (once) an inner grid inside the user_select_layout
	char s_us_grid_name1[64] = "user_select_grid1";
	if (!state->user_select_layout.s_us_grid1_initialized)
	{
		ProUIGridopts sub_grid = { 0 };
		sub_grid.row = 1;
		sub_grid.column = 0;
		sub_grid.horz_resize = PRO_B_TRUE;
		sub_grid.attach_right = PRO_B_TRUE;
		sub_grid.attach_left = PRO_B_TRUE;
		sub_grid.horz_cells = 2;
		sub_grid.vert_cells = 1;

		status = ProUILayoutLayoutAdd(state->dialog_name, state->user_select_layout.name, s_us_grid_name1, &sub_grid);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Error: Could not set layout inside the main user select layotu");
			return status;
		}

		ProUILayoutDecorate(state->dialog_name, s_us_grid_name1);
		ProUILayoutTextSet(state->dialog_name, s_us_grid_name1, L"Multiple Required Selection");

		state->user_select_layout.row = 1;
		state->user_select_layout.s_us_grid1_initialized = 1;
	}



	// Add the selection parameter to the target sub-layout
	status = addUserSelectMultiple(state->dialog_name, s_us_grid_name1, node, &state->user_select_layout.row, 0, st);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Error adding user select parameter");
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_user_select_multiple_optional_param(UserSelectMultipleOptionalNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;

	// Create (once) an inner grid inside the user_select_layout
	char s_us_grid_name_optional[64] = "s_us_grid_name_optional";
	if (!state->user_select_layout.s_us_gridM_initialized)
	{
		ProUIGridopts sub_grid = { 0 };
		sub_grid.row = 3;
		sub_grid.column = 0;
		sub_grid.horz_resize = PRO_B_TRUE;
		sub_grid.attach_right = PRO_B_TRUE;
		sub_grid.attach_left = PRO_B_TRUE;
		sub_grid.horz_cells = 2;
		sub_grid.vert_cells = 1;

		status = ProUILayoutLayoutAdd(state->dialog_name, state->user_select_layout.name, s_us_grid_name_optional, &sub_grid);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Error: Could not set layout inside the main user select layotu");
			return status;
		}

		ProUILayoutDecorate(state->dialog_name, s_us_grid_name_optional);
		ProUILayoutTextSet(state->dialog_name, s_us_grid_name_optional, L"Multiple Optional Selection");

		state->user_select_layout.row = 1;
		state->user_select_layout.s_us_gridM_initialized = 1;
	}



	// Add the selection parameter to the target sub-layout
	status = addUserSelectMultipleOptional(state->dialog_name, s_us_grid_name_optional, node, &state->user_select_layout.row, 0, st);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Error adding user select parameter");
		return status;
	}

	return PRO_TK_NO_ERROR;
}

// Set IF-gate flags BEFORE creation so addUserSelect honors them 
static void pretag_if_gated(SymbolTable* st, const char* name, int gate_id)
{
	if (!st || !name) return;
	Variable* sv = get_symbol(st, (char*)name);
	if (!sv || sv->type != TYPE_MAP || !sv->data.map) return;

	set_bool_in_map(sv->data.map, "if_gated", 1);
	add_int_to_map(sv->data.map, "if_gate_id", gate_id);

	set_bool_in_map(sv->data.map, "ui_enabled", 0);  // start disabled 
	set_bool_in_map(sv->data.map, "ui_required", 0);  // start unrequired 
	unrequire_select(st, (char*)name);
}

// In prepare_if_user_selects: pretag, then create 
ProError prepare_if_user_selects(IfNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_NO_ERROR;
	const int gate_id = (int)(((uintptr_t)node) & 0x7fffffff);

	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		for (size_t i = 0; i < br->command_count; ++i) {
			CommandNode* c = br->commands[i];
			if (!c) continue;

			if (c->type == COMMAND_USER_SELECT) {
				UserSelectNode* un = (UserSelectNode*)c->data;
				pretag_if_gated(st, un->reference, gate_id);
				execute_user_select_param(un, state, st);
			}
			else if (c->type == COMMAND_USER_SELECT_OPTIONAL) {
				UserSelectOptionalNode* un = (UserSelectOptionalNode*)c->data;
				pretag_if_gated(st, un->reference, gate_id);
				execute_user_select_optional_param(un, state, st);
			}
			else if (c->type == COMMAND_USER_SELECT_MULTIPLE) {
				UserSelectMultipleNode* mn = (UserSelectMultipleNode*)c->data;
				pretag_if_gated(st, mn->array, gate_id);
				execute_user_select_multiple_param(mn, state, st);
			}
			else if (c->type == COMMAND_USER_SELECT_MULTIPLE_OPTIONAL) {
				UserSelectMultipleOptionalNode* mn = (UserSelectMultipleOptionalNode*)c->data;
				pretag_if_gated(st, mn->array, gate_id);
				execute_user_select_multiple_optional_param(mn, state, st);
			}
			else if (c->type == COMMAND_IF) {
				prepare_if_user_selects((IfNode*)c->data, state, st);
			}
		}
	}

	for (size_t i = 0; i < node->else_command_count; ++i) {
		CommandNode* c = node->else_commands[i];
		if (!c) continue;

		if (c->type == COMMAND_USER_SELECT) {
			UserSelectNode* un = (UserSelectNode*)c->data;
			pretag_if_gated(st, un->reference, gate_id);
			execute_user_select_param(un, state, st);
		}
		else if (c->type == COMMAND_USER_SELECT_OPTIONAL) {
			UserSelectOptionalNode* un = (UserSelectOptionalNode*)c->data;
			pretag_if_gated(st, un->reference, gate_id);
			execute_user_select_optional_param(un, state, st);
		}
		else if (c->type == COMMAND_USER_SELECT_MULTIPLE) {
			UserSelectMultipleNode* mn = (UserSelectMultipleNode*)c->data;
			pretag_if_gated(st, mn->array, gate_id);
			execute_user_select_multiple_param(mn, state, st);
		}
		else if (c->type == COMMAND_USER_SELECT_MULTIPLE_OPTIONAL) {
			UserSelectMultipleOptionalNode* mn = (UserSelectMultipleOptionalNode*)c->data;
			pretag_if_gated(st, mn->array, gate_id);
			execute_user_select_multiple_optional_param(mn, state, st);
		}
		else if (c->type == COMMAND_IF) {
			prepare_if_user_selects((IfNode*)c->data, state, st);
		}
	}

	validate_ok_button(state->dialog_name, st);
	return PRO_TK_NO_ERROR;
}

ProError execute_global_picture(GlobalPictureNode* node, DialogState* state, SymbolTable* st) {
	(void)node;
	g_active_state = state;
	g_active_st = st;

	ProError status;
	int imageH = 0, imageW = 0;

	Variable* pic_var = get_symbol(st, "GLOBAL_PICTURE");
	if (!pic_var || pic_var->type != TYPE_STRING) {
		ProPrintfChar("Error: GLOBAL_PICTURE not found or invalid type in symbol table.\n");
		return PRO_TK_GENERAL_ERROR;
	}

	char* filepath = pic_var->data.string_value;
	if (!filepath) {
		ProPrintfChar("Error: Image path in symbol table is null.\n");
		return PRO_TK_GENERAL_ERROR;
	}

	ProPrintfChar("Image Path from filepath: %s\n", filepath);

	if (!get_gif_dimensions(filepath, &imageW, &imageH)) {
		ProPrintfChar("Error: Could not load image '%s' to get dimensions\n", filepath);
		return PRO_TK_GENERAL_ERROR;
	}

	ProPrintfChar("Retrieved image height: %d, width: %d\n", imageH, imageW);

	char* drawA1 = "drawA1";
	ProUIGridopts grid_opts = { 0 };
	grid_opts.row = 0;  // Fixed top row
	grid_opts.column = 0;  // Fixed left column, no insertion
	grid_opts.horz_cells = state->num_param_sections;  // Span all parameter columns
	grid_opts.vert_cells = 1;
	grid_opts.attach_bottom = PRO_B_TRUE;
	grid_opts.attach_top = PRO_B_TRUE;
	grid_opts.attach_left = PRO_B_TRUE;
	grid_opts.attach_right = PRO_B_TRUE;
	grid_opts.horz_resize = PRO_B_TRUE;
	grid_opts.vert_resize = PRO_B_TRUE;
	grid_opts.left_offset = 0;
	grid_opts.top_offset = 0;

	status = ProUILayoutDrawingareaAdd(state->dialog_name, state->main_layout_name, drawA1, &grid_opts);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not add drawing area to dialog");
		return status;
	}

	char* draw_area = "draw_area";
	status = ProUIDrawingareaDrawingareaAdd(state->dialog_name, drawA1, draw_area);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set drawing area to dialog");
		return status;
	}

	status = ProUIDrawingareaBackgroundcolorSet(state->dialog_name, drawA1, PRO_UI_COLOR_LT_GREY);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set background color");
		return status;
	}

	status = ProUIDrawingareaDrawingheightSet(state->dialog_name, drawA1, imageH);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set height of drawA1");
		return status;
	}
	status = ProUIDrawingareaDrawingwidthSet(state->dialog_name, drawA1, imageW);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set width of drawA1");
		return status;
	}

	status = ProUIDrawingareaDrawingheightSet(state->dialog_name, draw_area, imageH);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set drawing height");
		return status;
	}
	status = ProUIDrawingareaDrawingwidthSet(state->dialog_name, draw_area, imageW);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set width for draw_area");
		return status;
	}

	status = ProUIDrawingareaDrawingmodeSet(state->dialog_name, draw_area, PROUIDRWMODE_COPY);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set Drawingmode");
		return status;
	}

	status = ProUIDrawingareaPostmanagenotifyActionSet(state->dialog_name, draw_area, addpicture, (ProAppData)st);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not set paint callback\n");
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_sub_picture(SubPictureNode* node, SymbolTable* st)
{
	if (!node || !node->picture_expr || !node->posX_expr || !node->posY_expr) {
		ProPrintfChar("Runtime Error: Missing expressions in SUB_PICTURE\n");
		return PRO_TK_GENERAL_ERROR;
	}

	// Evaluate now (snapshot) 
	char* filename = NULL;
	if (evaluate_to_string(node->picture_expr, st, &filename) != 0 || !filename) {
		ProPrintfChar("Runtime Error: SUB_PICTURE filename could not be evaluated\n");
		free(filename);
		return PRO_TK_GENERAL_ERROR;
	}

	double x_val = 0.0, y_val = 0.0;
	Variable* vx = NULL;
	Variable* vy = NULL;

	if (evaluate_expression(node->posX_expr, st, &vx) != 0 || !vx) {
		ProPrintfChar("Runtime Error: SUB_PICTURE posX could not be evaluated\n");
		free(filename);
		return PRO_TK_GENERAL_ERROR;
	}
	if (vx->type == TYPE_DOUBLE) x_val = vx->data.double_value;
	else if (vx->type == TYPE_INTEGER || vx->type == TYPE_BOOL) x_val = (double)vx->data.int_value;
	else { free_variable(vx); free(filename); ProPrintfChar("Type error: posX not numeric\n"); return PRO_TK_GENERAL_ERROR; }
	free_variable(vx);

	if (evaluate_expression(node->posY_expr, st, &vy) != 0 || !vy) {
		ProPrintfChar("Runtime Error: SUB_PICTURE posY could not be evaluated\n");
		free(filename);
		return PRO_TK_GENERAL_ERROR;
	}
	if (vy->type == TYPE_DOUBLE) y_val = vy->data.double_value;
	else if (vy->type == TYPE_INTEGER || vy->type == TYPE_BOOL) y_val = (double)vy->data.int_value;
	else { free_variable(vy); free(filename); ProPrintfChar("Type error: posY not numeric\n"); return PRO_TK_GENERAL_ERROR; }
	free_variable(vy);

	// Ensure array exists 
	Variable* array_var = get_symbol(st, "SUB_PICTURES");
	if (!array_var) {
		array_var = (Variable*)malloc(sizeof(Variable));
		if (!array_var) { free(filename); return PRO_TK_GENERAL_ERROR; }
		array_var->type = TYPE_ARRAY;
		array_var->data.array.size = 0;
		array_var->data.array.elements = NULL;
		set_symbol(st, "SUB_PICTURES", array_var);
	}
	else if (array_var->type != TYPE_ARRAY) {
		free(filename);
		return PRO_TK_GENERAL_ERROR;
	}

	HashTable* sub_map = create_hash_table(16);
	if (!sub_map) { free(filename); return PRO_TK_GENERAL_ERROR; }

	// Store as TYPE_EXPR but with literal nodes (constants) 

	// filename_expr -> literal string 
	Variable* filename_expr_var = (Variable*)malloc(sizeof(Variable));
	if (!filename_expr_var) { free_hash_table(sub_map); free(filename); return PRO_TK_GENERAL_ERROR; }
	filename_expr_var->type = TYPE_EXPR;
	filename_expr_var->data.expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
	if (!filename_expr_var->data.expr) { free_variable(filename_expr_var); free_hash_table(sub_map); free(filename); return PRO_TK_GENERAL_ERROR; }
	filename_expr_var->data.expr->type = EXPR_LITERAL_STRING;
	filename_expr_var->data.expr->data.string_val = filename; // take ownership 

	if (!add_var_to_map(sub_map, "filename_expr", filename_expr_var)) {
		free_variable(filename_expr_var);
		free_hash_table(sub_map);
		return PRO_TK_GENERAL_ERROR;
	}

	// posX_expr -> literal double 
	Variable* posX_expr_var = (Variable*)malloc(sizeof(Variable));
	if (!posX_expr_var) { free_hash_table(sub_map); return PRO_TK_GENERAL_ERROR; }
	posX_expr_var->type = TYPE_EXPR;
	posX_expr_var->data.expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
	if (!posX_expr_var->data.expr) { free_variable(posX_expr_var); free_hash_table(sub_map); return PRO_TK_GENERAL_ERROR; }
	posX_expr_var->data.expr->type = EXPR_LITERAL_DOUBLE;
	posX_expr_var->data.expr->data.double_val = x_val;

	if (!add_var_to_map(sub_map, "posX_expr", posX_expr_var)) {
		free_variable(posX_expr_var);
		free_hash_table(sub_map);
		return PRO_TK_GENERAL_ERROR;
	}

	// posY_expr -> literal double (freezes DIMY at this moment) 
	Variable* posY_expr_var = (Variable*)malloc(sizeof(Variable));
	if (!posY_expr_var) { free_hash_table(sub_map); return PRO_TK_GENERAL_ERROR; }
	posY_expr_var->type = TYPE_EXPR;
	posY_expr_var->data.expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
	if (!posY_expr_var->data.expr) { free_variable(posY_expr_var); free_hash_table(sub_map); return PRO_TK_GENERAL_ERROR; }
	posY_expr_var->data.expr->type = EXPR_LITERAL_DOUBLE;
	posY_expr_var->data.expr->data.double_val = y_val;

	if (!add_var_to_map(sub_map, "posY_expr", posY_expr_var)) {
		free_variable(posY_expr_var);
		free_hash_table(sub_map);
		return PRO_TK_GENERAL_ERROR;
	}

	Variable* map_var = (Variable*)malloc(sizeof(Variable));
	if (!map_var) { free_hash_table(sub_map); return PRO_TK_GENERAL_ERROR; }
	map_var->type = TYPE_MAP;
	map_var->data.map = sub_map;

	size_t new_size = array_var->data.array.size + 1;
	Variable** new_elements = (Variable**)realloc(array_var->data.array.elements, new_size * sizeof(Variable*));
	if (!new_elements) { free_variable(map_var); return PRO_TK_GENERAL_ERROR; }
	array_var->data.array.elements = new_elements;
	array_var->data.array.elements[array_var->data.array.size] = map_var;
	array_var->data.array.size = new_size;

	return PRO_TK_NO_ERROR;
}



ProError ClearTableContents(char* dialog, char* table_id, int hide)
{
	ProError status;

	char** existing_rows = NULL;
	int existing_row_count = 0;

	status = ProUITableRownamesGet(dialog, table_id, &existing_row_count, &existing_rows);
	if (status == PRO_TK_NO_ERROR && existing_row_count > 0)
	{
		status = ProUITableRowsDelete(dialog, table_id, existing_row_count, existing_rows);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Failed to delete rows in table %s (error: $d)", table_id, status);
			return status;
		}
		ProStringarrayFree(existing_rows, existing_row_count);
	}
	// Clear existing columns
	char** columns = NULL;
	int column_count = 0;
	status = ProUITableColumnnamesGet(dialog, table_id, &column_count, &columns);
	if (status == PRO_TK_NO_ERROR && column_count > 0) {
		status = ProUITableColumnsDelete(dialog, table_id, column_count, columns);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Failed to delete columns in table %s (error: %d)", table_id, status);
		}
		ProStringarrayFree(columns, column_count);
	}

	if (hide)
	{
		status = ProUITableHide(dialog, table_id);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Failed to hide table %s", table_id);
			return status;
		}
		else
		{
			ProPrintfChar("Successfully hid table %s", table_id);
			return status;
		}
	}
	else
	{
		status = ProUITableShow(dialog, table_id);
		if (status != PRO_TK_NO_ERROR)
		{
			ProPrintfChar("Failed to show cleared table %s", table_id);
			return status;
		}
		else
		{
			ProPrintfChar("Successfully showed cleared table %s", table_id);
			return status;
		}
	}
}

ProError build_table_from_sym(char* dialog, char* table_id, SymbolTable* st)
{
	ProError status;
	Variable* table_var = get_symbol(st, table_id);
	if (!table_var || table_var->type != TYPE_ARRAY || table_var->data.array.size == 0) {
		ProPrintfChar("Error: Table '%s' not found or empty in symbol table\n", table_id);
		return PRO_TK_BAD_INPUTS;
	}

	size_t num_rows = table_var->data.array.size;

	// Check if the table already exists by attempting to retrieve row names
	char** existing_rows = NULL;
	int existing_row_count = 0;
	status = ProUITableRownamesGet(dialog, table_id, &existing_row_count, &existing_rows);
	bool table_exists = (status == PRO_TK_NO_ERROR);

	if (table_exists) {
		// Table exists: Clear contents without hiding, then repopulate
		status = ClearTableContents(dialog, table_id, 0);  // hide=0 to show after clearing
		if (status != PRO_TK_NO_ERROR) {
			return status;
		}
		// Ensure shown after clear
		status = ProUITableShow(dialog, table_id);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Failed to show existing table %s (error: %d)", table_id, status);
			return status;
		}
	}
	else {
		// Table does not exist: Build new sub-layout and add table
		// Generate unique layout name for this table's sub-layout
		char sub_layout[128];
		snprintf(sub_layout, sizeof(sub_layout), "table_layout_%s", table_id);

		ProUIGridopts grid;
		memset(&grid, 0, sizeof(grid));
		grid.row = 0;  // Assume row 0 for horizontal chain
		grid.column = ++dynamic_table_count;  // Dynamic column
		grid.horz_cells = 1;  // Explicitly set row span to 1
		grid.vert_cells = 1;  // Explicitly set column span to 1
		grid.attach_bottom = PRO_B_TRUE;
		grid.attach_left = PRO_B_TRUE;
		grid.attach_right = PRO_B_FALSE;
		grid.attach_top = PRO_B_TRUE;
		grid.horz_resize = PRO_B_TRUE;
		grid.vert_resize = PRO_B_TRUE;
		grid.left_offset = 5;
		status = ProUILayoutLayoutAdd(dialog, "individual_table", sub_layout, &grid);
		if (status != PRO_TK_NO_ERROR) {
			LogOnlyPrintfChar("Could not build layout within individual_table layout");
			return status;
		}

		ProUIGridopts table_grid;
		memset(&table_grid, 0, sizeof(table_grid));
		table_grid.horz_cells = 1;  // Explicitly set row span to 1
		table_grid.vert_cells = 1;  // Explicitly set column span to 1
		table_grid.attach_bottom = PRO_B_TRUE;
		table_grid.attach_left = PRO_B_TRUE;
		table_grid.attach_right = PRO_B_TRUE;
		table_grid.attach_top = PRO_B_TRUE;
		table_grid.horz_resize = PRO_B_TRUE;
		table_grid.vert_resize = PRO_B_TRUE;
		table_grid.top_offset = 5;
		table_grid.row = 0;
		table_grid.column = 0;
		status = ProUILayoutTableAdd(dialog, sub_layout, table_id, &table_grid);
		if (status != PRO_TK_NO_ERROR) {
			LogOnlyPrintfChar("Could not add table to individual_table layout");
			return status;
		}

		// Selection policy: single row
		status = ProUITableSelectionpolicySet(dialog, table_id, PROUISELPOLICY_SINGLE);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Error: Could not set row selection policy for '%s'\n", table_id);
			return status;
		}

		// Enable row auto-highlighting
		status = ProUITableAutohighlightEnable(dialog, table_id);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Warning: Could not enable row auto-highlighting for '%s'\n", table_id);
		}
	}

	// Common repopulation logic (for both existing and new tables)
	char col0_buf[32] = "COL_0";
	char* col_ptrs[1] = { col0_buf };
	status = ProUITableColumnsInsert(dialog, table_id, NULL, 1, col_ptrs);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not insert column for '%s'\n", table_id);
		return status;
	}

	// Insert rows based on symbol table data
	char** row_ptrs = (char**)malloc(num_rows * sizeof(char*));
	if (!row_ptrs) return PRO_TK_GENERAL_ERROR;
	for (size_t i = 0; i < num_rows; i++) {
		row_ptrs[i] = (char*)malloc(32);
		if (!row_ptrs[i]) {
			for (size_t j = 0; j < i; j++) free(row_ptrs[j]);
			free(row_ptrs);
			return PRO_TK_GENERAL_ERROR;
		}
		snprintf(row_ptrs[i], 32, "ROW_%zu", i);
	}

	status = ProUITableRowsInsert(dialog, table_id, NULL, (int)num_rows, row_ptrs);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not insert rows for '%s'\n", table_id);
		for (size_t i = 0; i < num_rows; i++) free(row_ptrs[i]);
		free(row_ptrs);
		return status;
	}

	// Set cell labels from SEL_STRING in each row map
	for (size_t i = 0; i < num_rows; i++) {
		Variable* row_var = table_var->data.array.elements[i];
		if (row_var && row_var->type == TYPE_MAP) {
			Variable* label_var = hash_table_lookup(row_var->data.map, "SEL_STRING");
			char* label_utf8 = (label_var && label_var->type == TYPE_STRING && label_var->data.string_value) ? label_var->data.string_value : "";
			wchar_t* label_w = char_to_wchar(label_utf8);
			status = ProUITableCellLabelSet(dialog, table_id, row_ptrs[i], col0_buf, label_w ? label_w : L"");
			if (label_w) free(label_w);
			if (status != PRO_TK_NO_ERROR) {
				ProPrintfChar("Error: Failed to set cell label for row %zu in '%s'\n", i, table_id);
				for (size_t j = 0; j < num_rows; j++) free(row_ptrs[j]);
				free(row_ptrs);
				return status;
			}
		}
	}

	// Cleanup row names
	for (size_t i = 0; i < num_rows; i++) free(row_ptrs[i]);
	free(row_ptrs);

	// Set recursive selection callback (safe to set multiple times)
	status = ProUITableSelectActionSet(dialog, table_id, TableSelectCallback, (ProAppData)st);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Failed to set select callback for '%s'\n", table_id);
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError TableSelectCallback(char* dialog, char* table, ProAppData appdata)
{
	SymbolTable* st = (SymbolTable*)appdata;
	if (!st || !dialog || !table) {
		ProPrintfChar("Error: Invalid inputs in TableSelectCallback\n");
		return PRO_TK_GENERAL_ERROR;
	}
	// Step 1: Get selected row names (assume single selection)
	char** selected_rows;
	int num_selected = 0;
	ProError status = ProUITableSelectednamesGet(dialog, table, &num_selected, &selected_rows);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Failed to get selected rows in table '%s'\n", table);
		if (selected_rows) ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	if (num_selected == 0) {
		// No selection is a valid state (e.g., init or deselection); log debug and return quietly
		LogOnlyPrintfChar("Debug: No rows selected in table '%s'\n", table);
		if (selected_rows) ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	if (!selected_rows || !selected_rows[0]) {
		ProPrintfChar("Error: Invalid selected rows data in table '%s'\n", table);
		if (selected_rows) ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	// Convert selected row name to char* (direct copy, as names are char*)
	char* selected_row_name = _strdup(selected_rows[0]);
	if (!selected_row_name) {
		ProPrintfChar("Error: Failed to copy selected row name\n");
		ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	LogOnlyPrintfChar("Debug: Selected row in table '%s': %s\n", table, selected_row_name);
	// Step 2: Lookup the table variable in symbol table (key = table component name)
	Variable* table_var = get_symbol(st, table);
	if (!table_var || table_var->type != TYPE_ARRAY) {
		ProPrintfChar("Error: Table '%s' not found in symbol table or not an ARRAY\n", table);
		free(selected_row_name);
		ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	// Step 3: Get all row names to find the index of the selected row
	char** all_row_names = NULL;
	int num_rows = 0;
	status = ProUITableRownamesGet(dialog, table, &num_rows, &all_row_names);
	if (status != PRO_TK_NO_ERROR || num_rows == 0 || !all_row_names) {
		ProPrintfChar("Error: Failed to get row names for table '%s'\n", table);
		free(selected_row_name);
		if (all_row_names) ProArrayFree((ProArray*)&all_row_names);
		ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	size_t selected_row_index = (size_t)-1;
	for (int i = 0; i < num_rows; i++) {
		if (strcmp(all_row_names[i], selected_row_name) == 0) {
			selected_row_index = (size_t)i;
			break;
		}
	}
	if (selected_row_index == (size_t)-1) {
		ProPrintfChar("Error: Selected row '%s' not found in table '%s'\n", selected_row_name, table);
		free(selected_row_name);
		ProArrayFree((ProArray*)&all_row_names);
		ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	LogOnlyPrintfChar("Debug: Selected row index: %zu\n", selected_row_index);
	// Clean up row names
	ProArrayFree((ProArray*)&all_row_names);
	free(selected_row_name);
	ProArrayFree((ProArray*)&selected_rows);
	// Step 4: Get the row variable from symbol table (table_var->data.array.elements[row_index])
	if (selected_row_index >= table_var->data.array.size) {
		ProPrintfChar("Error: Row index %zu out of bounds in table '%s'\n", selected_row_index, table);
		return PRO_TK_GENERAL_ERROR;
	}
	Variable* row_var = table_var->data.array.elements[selected_row_index];
	if (!row_var || row_var->type != TYPE_MAP) {
		ProPrintfChar("Error: Row at index %zu in table '%s' is not a MAP\n", selected_row_index, table);
		return PRO_TK_GENERAL_ERROR;
	}
	// Step 5: Scan row map for TYPE_SUBTABLE and extract its identifier
	char* subtable_id = NULL;
	char* subtable_key = NULL;
	HashTable* row_map = row_var->data.map;
	for (size_t i = 0; i < row_map->key_count; i++) {
		const char* key = row_map->key_order[i];
		Variable* cell = hash_table_lookup(row_map, key);
		if (cell && cell->type == TYPE_SUBTABLE && cell->data.string_value && strlen(cell->data.string_value) > 0) {
			subtable_id = _strdup(cell->data.string_value);
			subtable_key = _strdup(key);
			LogOnlyPrintfChar("Debug: Found SUBTABLE in row %zu, key '%s': %s\n", selected_row_index, key, subtable_id);
			break; // Assume first SUBTABLE found is the one (common in your data structure)
		}
	}
	// Collect union of all propagatable keys across all rows in this table
	char** dynamic_keys = NULL;
	size_t dk_count = 0;
	size_t dk_capacity = 0;
	for (size_t r = 0; r < table_var->data.array.size; r++) {
		Variable* rv = table_var->data.array.elements[r];
		if (rv && rv->type == TYPE_MAP) {
			HashTable* rm = rv->data.map;
			for (size_t k = 0; k < rm->key_count; k++) {
				const char* key = rm->key_order[k];
				if (strcmp(key, "SEL_STRING") == 0) continue;
				Variable* cell = hash_table_lookup(rm, key);
				if (!cell || cell->type == TYPE_UNKNOWN || cell->type == TYPE_SUBTABLE) continue;
				// Check if key is already in dynamic_keys
				int found = 0;
				for (size_t d = 0; d < dk_count; d++) {
					if (strcmp(dynamic_keys[d], key) == 0) {
						found = 1;
						break;
					}
				}
				if (!found) {
					// Resize if necessary
					if (dk_count >= dk_capacity) {
						size_t new_cap = dk_capacity ? dk_capacity * 2 : 8;
						char** new_dk = realloc(dynamic_keys, new_cap * sizeof(char*));
						if (!new_dk) {
							// On failure, skip cleanup (non-fatal; proceed with partial or no removal)
							goto skip_cleanup;
						}
						dynamic_keys = new_dk;
						dk_capacity = new_cap;
					}
					dynamic_keys[dk_count] = _strdup(key);
					if (!dynamic_keys[dk_count]) {
						// On failure, skip adding
						continue;
					}
					dk_count++;
				}
			}
		}
	}
	// Remove all collected dynamic keys from symbol table
	for (size_t d = 0; d < dk_count; d++) {
		remove_symbol(st, dynamic_keys[d]);
	}
skip_cleanup:
	// New: Propagate row data as global symbols
	for (size_t k = 0; k < row_map->key_count; k++) {
		const char* key = row_map->key_order[k];
		if (strcmp(key, "SEL_STRING") == 0 || (subtable_key && strcmp(key, subtable_key) == 0)) {
			continue;
		}
		Variable* cell = hash_table_lookup(row_map, key);
		if (!cell || cell->type == TYPE_UNKNOWN || cell->type == TYPE_SUBTABLE) {
			continue;
		}
		Variable* global_var = malloc(sizeof(Variable));
		if (!global_var) continue;
		global_var->type = cell->type;
		switch (cell->type) {
		case TYPE_INTEGER:
		case TYPE_BOOL:
			global_var->data.int_value = cell->data.int_value;
			break;
		case TYPE_DOUBLE:
			global_var->data.double_value = cell->data.double_value;
			break;
		case TYPE_STRING:
			global_var->data.string_value = _strdup(cell->data.string_value ? cell->data.string_value : "");
			if (!global_var->data.string_value) {
				free(global_var);
				continue;
			}
			break;
		default:
			free(global_var);
			continue;
		}
		set_symbol(st, key, global_var);
		LogOnlyPrintfChar("Set global '%s' from selected row in '%s'\n", key, table);
	}
	if (!subtable_id) {
		LogOnlyPrintfChar("No SUBTABLE found in selected row %zu of table '%s'\n", selected_row_index, table);
		// Optionally: Clear downstream tables here if needed (e.g., destroy layouts beyond current column)
	}
	// Step 6: Validate and build dynamically if it's a table
	Variable* next_table_var = get_symbol(st, subtable_id);
	if (next_table_var && next_table_var->type == TYPE_ARRAY) {
		LogOnlyPrintfChar("SUBTABLE '%s' matches a table in symbol table; building dynamically.\n", subtable_id);
		// Use global state to access DialogState
		if (!dialog) {
			ProPrintfChar("Error: No active DialogState for dynamic table build\n");
			free(subtable_id);
			if (subtable_key) free(subtable_key);
			return PRO_TK_GENERAL_ERROR;
		}
		// Build the next table (column auto-incremented via NEXT_COLUMN)
		status = build_table_from_sym(dialog, subtable_id, st);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Error: Failed to build dynamic table '%s'\n", subtable_id);
			free(subtable_id);
			if (subtable_key) free(subtable_key);
			return status;
		}
		// Trigger refresh (e.g., for images or validation)
		EPA_ReactiveRefresh();
	}
	else {
		LogOnlyPrintfChar("SUBTABLE '%s' does not match a table; no dynamic build.\n", subtable_id);
	}
	free(subtable_id);
	if (subtable_key) free(subtable_key);
	EPA_ReactiveRefresh();
	for (size_t d = 0; d < dk_count; d++) {
		free(dynamic_keys[d]);
	}
	free(dynamic_keys);
	return PRO_TK_NO_ERROR;
}

ProError execute_begin_table(TableNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_BAD_INPUTS;
	// Build only the first root table PER DIALOG
	if (state->root_table_built) {
		return PRO_TK_NO_ERROR; // don't build more roots here
	}
	// If a specific root was requested, skip non-matching tables
	if (state->root_identifier[0] != '\0' &&
		_stricmp(state->root_identifier, node->identifier ? node->identifier : "") != 0) {
		return PRO_TK_NO_ERROR; // not the root we want
	}
	ProError status = PRO_TK_NO_ERROR;
	// Step 1: Evaluate table title (fallbacks to identifier/constant)
	wchar_t* table_title = L"TABLE";
	char* title_utf8 = NULL;
	bool is_default_title = true;
	if (node->name) {
		if (evaluate_to_string(node->name, st, &title_utf8) == 0 && title_utf8 && title_utf8[0] != '\0') {
			wchar_t* w_title = char_to_wchar(title_utf8);
			if (w_title) {
				table_title = w_title;
				is_default_title = false;
			}
		}
	}
	if (is_default_title && node->identifier && node->identifier[0] != '\0') {
		wchar_t* w_id = char_to_wchar(node->identifier);
		if (w_id) table_title = w_id;
	}
	// Step 2: Initialize the table layout
	ProUIGridopts main_table_grid;
	memset(&main_table_grid, 0, sizeof(main_table_grid));
	main_table_grid.row = 0;
	main_table_grid.column = 0;  // Use auto-insertion for consistency
	main_table_grid.horz_cells = 1;
	main_table_grid.vert_cells = 1;
	main_table_grid.attach_bottom = PRO_B_TRUE;
	main_table_grid.attach_top = PRO_B_TRUE;
	main_table_grid.attach_left = PRO_B_TRUE;
	main_table_grid.attach_right = PRO_B_FALSE;
	main_table_grid.horz_resize = PRO_B_TRUE;
	main_table_grid.vert_resize = PRO_B_TRUE;
	status = InitializeTableLayout(state->dialog_name, state->table_layout_name, state->indvidual_table.name,
		&main_table_grid, L"TABLE", &state->indvidual_table.initialized);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not initialize table layout\n");
		if (title_utf8) free(title_utf8);
		return status;
	}
	// Step 3: Prepare table identifier (component name)
	char* table_id = node->identifier;
	if (!table_id || table_id[0] == '\0') {
		ProPrintfChar("Error: Table identifier is missing\n");
		if (title_utf8) free(title_utf8);
		return PRO_TK_BAD_INPUTS;
	}
	// Step 4: Add the table component inside the layout
	ProUIGridopts table_grid;
	memset(&table_grid, 0, sizeof(table_grid));
	table_grid.row = 0;
	table_grid.column = PRO_UI_INSERT_NEW_COLUMN;
	table_grid.horz_cells = 1;
	table_grid.vert_cells = 1;
	table_grid.attach_bottom = PRO_B_TRUE;
	table_grid.attach_left = PRO_B_TRUE;
	table_grid.attach_right = PRO_B_FALSE;
	table_grid.attach_top = PRO_B_TRUE;
	table_grid.horz_resize = PRO_B_TRUE;
	table_grid.vert_resize = PRO_B_TRUE;
	table_grid.top_offset = 5;
	LogOnlyPrintfChar("Building first table '%s' into layout '%s'\n", table_id, state->indvidual_table.name);
	status = ProUILayoutTableAdd(state->dialog_name, state->indvidual_table.name, table_id, &table_grid);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not add table to layout\n");
		if (title_utf8) free(title_utf8);
		return status;
	}
	// Done: lock to only first table
	state->root_table_built = 1;
	// Step 5: Selection policy (single for rows, none for columns to enforce row selection)
	status = ProUITableSelectionpolicySet(state->dialog_name, table_id, PROUISELPOLICY_SINGLE);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not set the row selection policy\n");
		if (title_utf8) free(title_utf8);
		return status;
	}
	// Optional: Enable row auto-highlighting for visual feedback on cell click
	status = ProUITableAutohighlightEnable(state->dialog_name, table_id);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Warning: Could not enable row auto-highlighting\n");
	}

	// Insert exactly one column: COL_0
	char col0_buf[32];
	sprintf_s(col0_buf, sizeof(col0_buf), "COL_0");
	char* col0_ptrs[1] = { col0_buf };
	status = ProUITableColumnsInsert(state->dialog_name, table_id, NULL, 1, col0_ptrs);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: ProUITableColumnsInsert failed for COL_0\n");
		if (title_utf8) free(title_utf8);
		return status;
	}
	// Determine number of rows and source (prioritize rows over sel_strings)
	int num_rows = 0;
	int use_rows = (node->rows && node->row_count > 0);
	if (use_rows) {
		num_rows = node->row_count;
	}
	else if (node->sel_strings && node->sel_string_count > 0) {
		num_rows = node->sel_string_count;
	}
	if (num_rows == 0) {
		// Fallback to minimal single empty row if no data
		num_rows = 1;
	}
	// Prepare array of row names
	char** row_ptrs = (char**)malloc(num_rows * sizeof(char*));
	if (!row_ptrs) {
		if (title_utf8) free(title_utf8);
		return PRO_TK_GENERAL_ERROR;
	}
	for (int i = 0; i < num_rows; i++) {
		char* row_buf = (char*)malloc(32 * sizeof(char));
		if (!row_buf) {
			for (int j = 0; j < i; j++) free(row_ptrs[j]);
			free(row_ptrs);
			if (title_utf8) free(title_utf8);
			return PRO_TK_GENERAL_ERROR;
		}
		sprintf_s(row_buf, 32, "ROW%d", i);
		row_ptrs[i] = row_buf;
	}
	// Insert all rows at once at the top (preserves order: row_ptrs[0] at top)
	status = ProUITableRowsInsert(state->dialog_name, table_id, NULL, num_rows, row_ptrs);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: ProUITableRowsInsert failed for rows\n");
		for (int i = 0; i < num_rows; i++) free(row_ptrs[i]);
		free(row_ptrs);
		if (title_utf8) free(title_utf8);
		return status;
	}
	// Set cell labels and handle metadata for each row
	for (int i = 0; i < num_rows; i++) {
		// Compute the cell label: Priority: rows[i][0] -> sel_strings[i] -> table name -> identifier -> empty
		char* cell_utf8 = NULL;
		if (use_rows && node->rows[i] && node->rows[i][0]) {
			if (evaluate_to_string(node->rows[i][0], st, &cell_utf8) != 0 || !cell_utf8) {
				cell_utf8 = _strdup("");
				LogOnlyPrintfChar("Debug: Failed to evaluate rows[%d][0] for cell label; using empty string\n", i);
			}
		}
		else if (!use_rows && node->sel_strings && node->sel_strings[i]) {
			if (evaluate_to_string(node->sel_strings[i], st, &cell_utf8) != 0 || !cell_utf8) {
				cell_utf8 = _strdup("");
				LogOnlyPrintfChar("Debug: Failed to evaluate sel_strings[%d] for cell label; using empty string\n", i);
			}
			else {
				LogOnlyPrintfChar("Debug: Evaluated sel_strings[%d] cell label: %s\n", i, cell_utf8);
			}
		}
		else if (title_utf8 && title_utf8[0] != '\0') {
			cell_utf8 = _strdup(title_utf8);
			LogOnlyPrintfChar("Debug: Using table title as fallback cell label for row %d: %s\n", i, cell_utf8);
		}
		else if (table_id && table_id[0] != '\0') {
			cell_utf8 = _strdup(table_id);
			LogOnlyPrintfChar("Debug: Using table identifier as fallback cell label for row %d: %s\n", i, cell_utf8);
		}
		else {
			cell_utf8 = _strdup("");
			LogOnlyPrintfChar("Debug: No sources available for cell label for row %d; using empty string\n", i);
		}
		wchar_t* cell_w = cell_utf8 ? char_to_wchar(cell_utf8) : L"";
		status = ProUITableCellLabelSet(state->dialog_name, table_id, row_ptrs[i], col0_buf, cell_w ? cell_w : L"");
		if (cell_w) free(cell_w);
		if (status != PRO_TK_NO_ERROR) {
			if (cell_utf8) free(cell_utf8);
			for (int j = 0; j < num_rows; j++) free(row_ptrs[j]);
			free(row_ptrs);
			if (title_utf8) free(title_utf8);
			ProPrintfChar("Error: Failed to set cell label for %s/%s\n", row_ptrs[i], col0_buf);
			return status;
		}
		// Build the row key from the evaluated cell label (for potential mapping/callback use)
		char* map_row_key = cell_utf8 ? _strdup(cell_utf8) : _strdup("");
		if (!map_row_key) {
			if (cell_utf8) free(cell_utf8);
			for (int j = 0; j < num_rows; j++) free(row_ptrs[j]);
			free(row_ptrs);
			if (title_utf8) free(title_utf8);
			return PRO_TK_GENERAL_ERROR;
		}
		free(map_row_key); // Assuming not used further; extend if needed for app data
		// Optional: Prepare sub-table metadata (constructed but not set in original; log for debug)
		wchar_t* meta_w = NULL;
		do {
			char* sub_utf8 = NULL;
			if (use_rows && node->rows[i] && node->rows[i][1]) {
				if (evaluate_to_string(node->rows[i][1], st, &sub_utf8) != PRO_TK_NO_ERROR) {
					sub_utf8 = NULL;
					LogOnlyPrintfChar("Debug: Failed to evaluate rows[%d][1] for subtable meta; skipping\n", i);
				}
			}
			else if (!use_rows && node->sel_strings && i + 1 < node->sel_string_count && node->sel_strings[i + 1]) {
				if (evaluate_to_string(node->sel_strings[i + 1], st, &sub_utf8) != PRO_TK_NO_ERROR) {
					sub_utf8 = NULL;
					LogOnlyPrintfChar("Debug: Failed to evaluate sel_strings[%d] for subtable meta; skipping\n", i + 1);
				}
			}
			if (sub_utf8 && sub_utf8[0] != '\0') {
				char meta_buf_stack[256];
				int need = snprintf(meta_buf_stack, sizeof(meta_buf_stack), "SUBTABLE=%s", sub_utf8);
				if (need >= 0 && need < (int)sizeof(meta_buf_stack)) {
					meta_w = char_to_wchar(meta_buf_stack);
				}
				else {
					// Fallback allocate if longer than stack buf
					size_t slen = strlen(sub_utf8);
					size_t cap = slen + 16; // "SUBTABLE=" + null
					char* meta_dyn = (char*)malloc(cap);
					if (meta_dyn) {
						snprintf(meta_dyn, cap, "SUBTABLE=%s", sub_utf8);
						meta_w = char_to_wchar(meta_dyn);
						free(meta_dyn);
					}
					else {
						LogOnlyPrintfChar("Debug: Memory allocation failed for subtable metadata string for row %d\n", i);
					}
				}
			}
			if (sub_utf8) free(sub_utf8);
		} while (0);
		if (meta_w) free(meta_w); // Not used; extend if needed (e.g., ProUITableRowNoteSet)
		if (cell_utf8) free(cell_utf8);
	}
	// Clean up row names array
	for (int i = 0; i < num_rows; i++) free(row_ptrs[i]);
	free(row_ptrs);
	status = ProUITableSelectActionSet(state->dialog_name, table_id, TableSelectCallback, (ProAppData)st);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Failed to set select callback for table '%s'\n", table_id);
		if (title_utf8) free(title_utf8);
		return status;
	}
	if (title_utf8) free(title_utf8);
	return PRO_TK_NO_ERROR;
}

ProError execute_gui_command(CommandNode* node, DialogState* state, SymbolTable* st)
{
	BlockList* block_list = NULL;


	
	switch (node->type)
	{
	case COMMAND_GLOBAL_PICTURE:
		execute_global_picture((GlobalPictureNode*)node->data, state, st);
		break;
	case COMMAND_SUB_PICTURE:
		execute_sub_picture((SubPictureNode*)node->data, st);
		break;
	case COMMAND_SHOW_PARAM:
		execute_show_param((ShowParamNode*)node->data, state, st);
		break;
	case COMMAND_CHECKBOX_PARAM:
		execute_checkbox_param((CheckboxParamNode*)node->data, state, st);
		break;
	case COMMAND_USER_INPUT_PARAM:
		execute_user_input_param((UserInputParamNode*)node->data, state, st);
		break;
	case COMMAND_RADIOBUTTON_PARAM:
		execute_radiobutton_param((RadioButtonParamNode*)node->data, state, st);
		break;
	case COMMAND_USER_SELECT:
		execute_user_select_param((UserSelectNode*)node->data, state, st);
		break;
	case COMMAND_USER_SELECT_OPTIONAL:
		execute_user_select_optional_param((UserSelectOptionalNode*)node->data, state, st);
		break;
	case COMMAND_USER_SELECT_MULTIPLE:
		execute_user_select_multiple_param((UserSelectMultipleNode*)node->data, state, st);
		break;
	case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL:
		execute_user_select_multiple_optional_param((UserSelectMultipleOptionalNode*)node->data, state, st);
		break;
	case COMMAND_BEGIN_TABLE:
		execute_begin_table((TableNode*)node->data, state, st);
		break;
	case COMMAND_IF:
		execute_if((IfNode*)node->data, st, block_list, state);
		break;
	case COMMAND_ASSIGNMENT:
		execute_assignment((AssignmentNode*)node->data, st, block_list);
		break;
	}

	return PRO_TK_NO_ERROR;
}

static void scan_commands_for_needs(const Block* block, DialogState* state) {
	if (!block || !state) return;

	for (size_t i = 0; i < block->command_count; i++) {
		CommandNode* cmd = block->commands[i];
		if (!cmd) continue;

		switch (cmd->type) {
		case COMMAND_SHOW_PARAM: {
			ShowParamNode* node = (ShowParamNode*)cmd->data;
			if (node && !node->on_picture) {
				state->needs_show_param = true;
			}
			break;
		}
		case COMMAND_CHECKBOX_PARAM: {
			CheckboxParamNode* node = (CheckboxParamNode*)cmd->data;
			if (node && !node->on_picture) {
				state->needs_checkbox = true;
			}
			break;
		}
		case COMMAND_USER_INPUT_PARAM:
			state->needs_user_input = true;
			break;
		case COMMAND_RADIOBUTTON_PARAM:
			state->needs_radiobutton = true;
			break;
		case COMMAND_USER_SELECT:
		case COMMAND_USER_SELECT_OPTIONAL:
		case COMMAND_USER_SELECT_MULTIPLE:
		case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL:
			state->needs_user_select = true;
			break;
		case COMMAND_GLOBAL_PICTURE:
			state->needs_picture = true;
			break;
		case COMMAND_IF: {
			IfNode* if_node = (IfNode*)cmd->data;
			if (if_node) {
				for (size_t b = 0; b < if_node->branch_count; ++b) {
					// Assuming IfBranch is compatible with Block for scanning
					Block branch_block = { if_node->branches[b]->command_count, if_node->branches[b]->commands };
					scan_commands_for_needs(&branch_block, state);
				}
				Block else_block = { if_node->else_command_count, if_node->else_commands };
				scan_commands_for_needs(&else_block, state);
			}
			break;
		}
					   // Ignore other types
		}
	}
}

ProError execute_gui_block(Block* gui_block, SymbolTable* st, BlockList* block_list) {
	ProError status;
	DialogState state = { 0 };
	int dialog_status;

	// Assign default values to prevent NULL pointers
	state.dialog_name = "gui_dialog";
	state.main_layout_name = "main_layout";
	state.table_layout_name = "table_layout";
	state.confirmation_layout_name = "confirmation_layout_name";
	strcpy_s(state.show_param_layout.name, sizeof(state.show_param_layout.name), "show_param_layout");
	strcpy_s(state.user_input_layout.name, sizeof(state.user_input_layout.name), "user_input_layout");
	strcpy_s(state.radiobutton_layout.name, sizeof(state.radiobutton_layout.name), "radiobutton_layout");
	strcpy_s(state.checkbox_layout.name, sizeof(state.checkbox_layout.name), "checkbox_layout");
	strcpy_s(state.user_select_layout.name, sizeof(state.user_select_layout.name), "user_select_layout");
	strcpy_s(state.indvidual_table.name, sizeof(state.indvidual_table.name), "individual_table");

	state.gui_block = gui_block;
	state.tab_block = find_block(block_list, BLOCK_TAB);
	state.st = st;

	status = ProUIDialogCreate(state.dialog_name, NULL);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not create dialog");
		return status;
	}

	// Retrieve CONFIG_ELEM configuration from symbol table (unchanged)
	Variable* config_var = get_symbol(st, "CONFIG_ELEM");
	if (config_var && config_var->type == TYPE_MAP) {
		HashTable* config_map = config_var->data.map;
		Variable* width_var = hash_table_lookup(config_map, "width");
		if (width_var && width_var->type == TYPE_DOUBLE && width_var->data.double_value > 0) {
			double width = width_var->data.double_value;
			int width_pixels = (int)width;
			ProUIDialogWidthSet(state.dialog_name, width_pixels);
		}
		Variable* height_var = hash_table_lookup(config_map, "height");
		if (height_var && height_var->type == TYPE_DOUBLE && height_var->data.double_value > 0) {
			double height = height_var->data.double_value;
			int height_pixels = (int)height;
			ProUIDialogHeightSet(state.dialog_name, height_pixels);
		}
	}

	// First pass: Recursively scan for needed sections
	scan_commands_for_needs(gui_block, &state);

	// Calculate number of parameter sections
	int num_sections = 0;
	if (state.needs_show_param) num_sections++;
	if (state.needs_checkbox) num_sections++;
	if (state.needs_user_input) num_sections++;
	if (state.needs_radiobutton) num_sections++;
	if (state.needs_user_select) num_sections++;
	state.num_param_sections = num_sections > 0 ? num_sections : 1;  // Fallback for no parameters

	// Set main layout vertical cells based on needs
	ProUIGridopts grid_opts_main = { 0 };
	grid_opts_main.attach_bottom = PRO_B_FALSE;
	grid_opts_main.attach_top = PRO_B_TRUE;
	grid_opts_main.attach_left = PRO_B_TRUE;
	grid_opts_main.attach_right = PRO_B_TRUE;
	grid_opts_main.horz_cells = state.num_param_sections;
	grid_opts_main.vert_cells = (state.needs_picture ? 1 : 0) + (num_sections > 0 ? 1 : 0);
	grid_opts_main.vert_resize = PRO_B_TRUE;
	grid_opts_main.horz_resize = PRO_B_TRUE;
	grid_opts_main.left_offset = 0;  // Explicitly ensure no left gap

	int dialog_row = 0;
	grid_opts_main.row = dialog_row;
	grid_opts_main.column = 0;
	status = ProUIDialogLayoutAdd(state.dialog_name, state.main_layout_name, &grid_opts_main);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not add main layout to dialog\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}
	status = ProUILayoutDecorate(state.dialog_name, state.main_layout_name);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not add border around main layout\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}
	dialog_row += grid_opts_main.vert_cells;

	// Table layout (consecutive row)
	ProUIGridopts grid_opts_table = { 0 };
	grid_opts_table.attach_bottom = PRO_B_TRUE;
	grid_opts_table.attach_top = PRO_B_TRUE;
	grid_opts_table.attach_left = PRO_B_TRUE;
	grid_opts_table.attach_right = PRO_B_TRUE;
	grid_opts_table.row = dialog_row;
	grid_opts_table.column = 0;
	grid_opts_table.horz_cells = state.num_param_sections;  // Align with main width
	grid_opts_table.vert_cells = 1;
	grid_opts_table.horz_resize = PRO_B_TRUE;
	grid_opts_table.vert_resize = PRO_B_TRUE;
	status = ProUIDialogLayoutAdd(state.dialog_name, state.table_layout_name, &grid_opts_table);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not add table layout to dialog\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}
	status = ProUILayoutDecorate(state.dialog_name, state.table_layout_name);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not add border around table layout\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}
	dialog_row += grid_opts_table.vert_cells;

	// Confirmation layout (consecutive row)
	ProUIGridopts pbgrid = { 0 };
	pbgrid.attach_bottom = PRO_B_TRUE;
	pbgrid.attach_top = PRO_B_TRUE;
	pbgrid.attach_left = PRO_B_TRUE;
	pbgrid.attach_right = PRO_B_TRUE;
	pbgrid.horz_resize = PRO_B_TRUE;
	pbgrid.vert_resize = PRO_B_FALSE;
	pbgrid.row = dialog_row;
	pbgrid.column = 0;
	pbgrid.horz_cells = state.num_param_sections;  // Align with main width
	pbgrid.vert_cells = 1;
	status = ProUIDialogLayoutAdd(state.dialog_name, state.confirmation_layout_name, &pbgrid);
	if (status != PRO_TK_NO_ERROR) {
		ProTKWprintf(L"Could not add pushbutton layout\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}
	ProUILayoutDecorate(state.dialog_name, state.confirmation_layout_name);

	// Add pushbutton (unchanged)
	char* pushbuttonName = "ok_button";
	ProUIGridopts subpbgrid = { 0 };
	subpbgrid.attach_bottom = PRO_B_TRUE;
	subpbgrid.attach_top = PRO_B_TRUE;
	subpbgrid.attach_left = PRO_B_FALSE;
	subpbgrid.attach_right = PRO_B_TRUE;
	subpbgrid.horz_resize = PRO_B_TRUE;
	subpbgrid.vert_resize = PRO_B_FALSE;
	subpbgrid.row = 0;
	subpbgrid.column = 1;
	subpbgrid.horz_cells = 1;
	subpbgrid.vert_cells = 1;
	status = ProUILayoutPushbuttonAdd(state.dialog_name, state.confirmation_layout_name, pushbuttonName, &subpbgrid);
	if (status != PRO_TK_NO_ERROR) {
		ProTKWprintf(L"Could not add pushbutton to layout\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}
	ProUIPushbuttonTextSet(state.dialog_name, pushbuttonName, L"OK");
	status = ProUIPushbuttonActivateActionSet(state.dialog_name, pushbuttonName, PushButtonAction, NULL);
	if (status != PRO_TK_NO_ERROR) {
		ProTKWprintf(L"Could not set pushbutton activate action\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}

	// Initialize parameter layouts in type order with dynamic columns
	int current_col = 0;
	ProUIGridopts grid_opts = { 0 };
	grid_opts.row = state.needs_picture ? 1 : 0;  // Parameters below picture if present
	grid_opts.attach_bottom = PRO_B_TRUE;
	grid_opts.attach_top = PRO_B_TRUE;
	grid_opts.attach_left = PRO_B_TRUE;
	grid_opts.attach_right = PRO_B_TRUE;
	grid_opts.horz_resize = PRO_B_TRUE;
	grid_opts.vert_resize = PRO_B_TRUE;
	if (state.needs_show_param) {
		grid_opts.column = current_col;
		grid_opts.left_offset = (current_col > 0 ? 20 : 0);
		current_col++;
		status = InitializeLayout(state.dialog_name, state.main_layout_name, state.show_param_layout.name, &grid_opts, L"Info", &state.show_param_layout.initialized);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Could not add the show param initial layout");
			return status;
		}
	}
	if (state.needs_checkbox) {
		grid_opts.column = current_col;
		grid_opts.left_offset = (current_col > 0 ? 20 : 0);
		current_col++;
		status = InitializeLayout(state.dialog_name, state.main_layout_name, state.checkbox_layout.name, &grid_opts, L"True/False", &state.checkbox_layout.initialized);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Could not add the checkbox param initial layout");
			return status;
		}
	}
	if (state.needs_user_input) {
		grid_opts.column = current_col;
		grid_opts.left_offset = (current_col > 0 ? 20 : 0);
		current_col++;
		status = InitializeLayout(state.dialog_name, state.main_layout_name, state.user_input_layout.name, &grid_opts, L"Enter Values", &state.user_input_layout.initialized);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Could not add the user input param initial layout");
			return status;
		}
	}
	if (state.needs_radiobutton) {
		grid_opts.column = current_col;
		grid_opts.left_offset = (current_col > 0 ? 20 : 0);
		current_col++;
		status = InitializeLayout(state.dialog_name, state.main_layout_name, state.radiobutton_layout.name, &grid_opts, L"Choose Options", &state.radiobutton_layout.initialized);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Could not add the radiobutton param initial layout");
			return status;
		}
	}
	if (state.needs_user_select) {
		grid_opts.column = current_col;
		grid_opts.left_offset = (current_col > 0 ? 20 : 0);
		current_col++;
		status = InitializeLayout(state.dialog_name, state.main_layout_name, state.user_select_layout.name, &grid_opts, L"Selection", &state.user_select_layout.initialized);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Could not add the user select initial layout");
			return status;
		}
	}

	// Handle tab block and GUI commands (unchanged)
	if (state.tab_block) {
		for (size_t i = 0; i < state.tab_block->command_count; ++i) {
			CommandNode* cmd = state.tab_block->commands[i];
			execute_gui_command(cmd, &state, st);
		}
	}
	else {
		status = ProUILayoutHide(state.dialog_name, state.table_layout_name);
		if (status != PRO_TK_NO_ERROR) {
			ProGenericMsg(L"Could not hide table layout\n");
			return status;
		}
	}

	for (size_t i = 0; i < gui_block->command_count; i++) {
		CommandNode* cmd = gui_block->commands[i];
		execute_gui_command(cmd, &state, st);
	}

	// Validate OK button and set actions (unchanged)
	status = validate_ok_button(state.dialog_name, st);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Error: Initial validation of OK button failed");
		return status;
	}

	ProUIDialogPostmanagenotifyActionSet(state.dialog_name, MyPostManageCallback, st);
	ProUIDialogCloseActionSet(state.dialog_name, CloseCallback, NULL);

	g_active_state = &state;
	g_active_st = st;

	status = ProUIDialogActivate(state.dialog_name, &dialog_status);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not activate dialog");
		return status;
	}

	ProUIDialogDestroy(state.dialog_name);
	return PRO_TK_NO_ERROR;
}

void execute_config_elem(CommandData* config, SymbolTable* st, BlockList* block_list)
{

	// Process Config_elem options (e.g., NO_TABLES, SCREEN_LOCATION)
	//Example: Retireve and apply configuration from SymbolTable

	

	Variable* options = get_symbol(st, "CONFIG_ELEM");
	if (options)
	{
		ProPrintfChar("Executing CONFIG_ELEM with options: 5s\n", options);
	}

	// Execute GUI block if NO_GUI is not set
	if (!config->config_elem.no_gui)
	{
		Block* gui_block = find_block(block_list, BLOCK_GUI);
		if (gui_block)
		{
			execute_gui_block(gui_block, st, block_list);
		}
		else
		{
			ProPrintfChar("NO_GUI block found\n");
		}
	}
	else
	{
		ProPrintfChar("NO_GUI option is set, skipping GUI block execution\n");
	}
	


}

// Executor for DECLARE_VARIABLE (handles declarations and redeclarations with duplication warning)
ProError execute_declare_variable(DeclareVariableNode* node, SymbolTable* st) {
	if (!node || !node->name) return PRO_TK_BAD_INPUTS;
	VariableType mapped_type = map_variable_type(node->var_type, (node->var_type == VAR_PARAMETER ? node->data.parameter.subtype : PARAM_INT));
	if (mapped_type == -1) {
		ProPrintfChar("Runtime Error: Invalid variable type for '%s'\n", node->name);
		return PRO_TK_GENERAL_ERROR;
	}
	// Check for existing variable
	Variable* existing = get_symbol(st, node->name);
	if (existing) {
		// Check if INVALIDATE_PARAM exists for this variable (suppress warning if present)
		bool has_invalidate = false;
		Variable* inv_list = get_symbol(st, "INVALIDATED_PARAMS");
		if (inv_list && inv_list->type == TYPE_ARRAY) {
			for (size_t i = 0; i < inv_list->data.array.size; i++) {
				Variable* param = inv_list->data.array.elements[i];
				if (param && param->type == TYPE_STRING && strcmp(param->data.string_value, node->name) == 0) {
					has_invalidate = true;
					break;
				}
			}
		}
		// Issue warning only if no INVALIDATE_PARAM and count > 1
		if (!has_invalidate && existing->declaration_count > 1) {
			ProPrintfChar("WARNING: Duplicated variable '%s' needs to be invalidated first before re-declaring:\nUSING ORIGINAL VALUE UNTIL INVALIDATED AND RE_DECLARED", node->name);
			return PRO_TK_NO_ERROR;  // Skip setting value to preserve original; proceed without error
		}
		// Increment count for redeclaration (even if invalidated elsewhere)
		existing->declaration_count++;
	}
	else {
		// No existing variable: Create new (e.g., after invalidation or first time)
		Variable* var = malloc(sizeof(Variable));
		if (!var) {
			ProPrintfChar("Memory allocation failed for variable '%s'\n", node->name);
			return PRO_TK_GENERAL_ERROR;
		}
		var->type = mapped_type;
		var->declaration_count = 1;  // Runtime initialization
		// Handle default value based on var_type (zero/NULL initialization here; set below)
		switch (node->var_type) {
		case VAR_REFERENCE:
			var->data.reference.reference_value = NULL;
			break;
		case VAR_FILE_DESCRIPTOR:
			var->data.file_descriptor = NULL;
			break;
		case VAR_ARRAY:
			var->data.array.elements = NULL;
			var->data.array.size = 0;
			break;
		case VAR_MAP:
		case VAR_STRUCTURE:
			var->data.map = create_hash_table(16);
			if (!var->data.map) {
				free(var);
				return PRO_TK_GENERAL_ERROR;
			}
			break;
		default:
			// Basic types: Set to zero/empty via set_default_value after creation
			break;
		}
		set_symbol(st, node->name, var);
		existing = var;  // Now use the new variable for value setting
	}
	// Set or update value (for new declarations or redeclarations without warning)
	switch (node->var_type) {
	case VAR_PARAMETER: {
		if (node->data.parameter.default_expr) {
			Variable* default_val = NULL;
			int status = evaluate_expression(node->data.parameter.default_expr, st, &default_val);
			if (status != 0 || !default_val) {
				ProPrintfChar("Runtime Error: Failed to evaluate default for '%s'\n", node->name);
				return PRO_TK_GENERAL_ERROR;
			}
			// Check type compatibility with coercion
			bool compatible = false;
			if (existing->type == default_val->type) {
				compatible = true;
			}
			else if (existing->type == TYPE_DOUBLE && (default_val->type == TYPE_INTEGER || default_val->type == TYPE_BOOL)) {
				compatible = true;
			}
			else if (existing->type == TYPE_INTEGER && default_val->type == TYPE_DOUBLE) {
				compatible = true;  // Allow with potential precision loss
			}
			else if (existing->type == TYPE_INTEGER && default_val->type == TYPE_BOOL) {
				compatible = true;
			}
			else if (existing->type == TYPE_BOOL && (default_val->type == TYPE_INTEGER || default_val->type == TYPE_DOUBLE)) {
				compatible = true;  // Non-zero to true
			}
			if (!compatible) {
				free_variable(default_val);
				ProPrintfChar("Runtime Error: Type mismatch in default for '%s' (var type %d, default type %d)\n",
					node->name, existing->type, default_val->type);
				return PRO_TK_GENERAL_ERROR;
			}
			// Copy/coerce value
			switch (existing->type) {
			case TYPE_INTEGER:
			case TYPE_BOOL:
				if (default_val->type == TYPE_INTEGER || default_val->type == TYPE_BOOL) {
					existing->data.int_value = default_val->data.int_value;
				}
				else if (default_val->type == TYPE_DOUBLE) {
					existing->data.int_value = (int)default_val->data.double_value;
				}
				break;
			case TYPE_DOUBLE:
				if (default_val->type == TYPE_DOUBLE) {
					existing->data.double_value = default_val->data.double_value;
				}
				else if (default_val->type == TYPE_INTEGER || default_val->type == TYPE_BOOL) {
					existing->data.double_value = (double)default_val->data.int_value;
				}
				break;
			case TYPE_STRING:
				free(existing->data.string_value);  // Free old if any
				existing->data.string_value = _strdup(default_val->data.string_value);
				if (!existing->data.string_value) {
					free_variable(default_val);
					return PRO_TK_GENERAL_ERROR;
				}
				break;
			default:
				free_variable(default_val);
				ProPrintfChar("Runtime Error: Unsupported type %d for default in '%s'\n", existing->type, node->name);
				return PRO_TK_GENERAL_ERROR;
			}
			free_variable(default_val);
		}
		else {
			set_default_value(existing);  // Type-based default if no expression
		}
		break;
	}
					  // Extend for other var_type cases as needed (e.g., VAR_REFERENCE defaults to NULL, already set)
	default:
		// For non-parameter types, defaults are already set during creation
		break;
	}
	// Log the declaration with value (type-specific printing)
	LogOnlyPrintfChar("Note: Variable '%s' (type %d) declared with value: ", node->name, mapped_type);
	switch (existing->type) {
	case TYPE_INTEGER:
	case TYPE_BOOL:
		LogOnlyPrintfChar("%d\n", existing->data.int_value);
		break;
	case TYPE_DOUBLE:
		LogOnlyPrintfChar("%.2f\n", existing->data.double_value);
		break;
	case TYPE_STRING:
		LogOnlyPrintfChar("%s\n", existing->data.string_value ? existing->data.string_value : "NULL");
		break;
	case TYPE_REFERENCE:
		LogOnlyPrintfChar("(reference: %p)\n", existing->data.reference.reference_value);
		break;
	case TYPE_FILE_DESCRIPTOR:
		LogOnlyPrintfChar("(file descriptor: %p)\n", existing->data.file_descriptor);
		break;
	case TYPE_ARRAY:
		LogOnlyPrintfChar("(array of size %zu)\n", existing->data.array.size);
		break;
	case TYPE_MAP:
	case TYPE_STRUCTURE:
		LogOnlyPrintfChar("(map/struct with %zu entries)\n", existing->data.map ? existing->data.map->count : 0);
		break;
	default:
		LogOnlyPrintfChar("(unsupported type for logging)\n");
		break;
	}
	return PRO_TK_NO_ERROR;
}

// Executor for INVALIDATE_PARAM
ProError execute_invalidate_param(InvalidateParamNode* node, SymbolTable* st) {
	if (!node || !st) {
		printf("Error: Invalid INVALIDATE_PARAM node or symbol table\n");
		return -1;
	}

	Variable* var = get_symbol(st, node->parameter);
	if (!var) {
		printf("Warning: Parameter '%s' does not exist; nothing to invalidate\n", node->parameter);
		return 0;  // No error, just no-op
	}

	// Restrict to basic parameter types (as in original semantics)
	if (var->type != TYPE_INTEGER && var->type != TYPE_DOUBLE && var->type != TYPE_STRING && var->type != TYPE_BOOL) {
		printf("Error: INVALIDATE_PARAM can only invalidate parameter types (int, double, string, bool) for '%s'\n", node->parameter);
		return -1;
	}

	remove_symbol(st, node->parameter);
	return 0;
}

ProError execute_command(CommandNode* node, SymbolTable* st, BlockList* block_list)
{
	switch (node->type)
	{
	case COMMAND_CONFIG_ELEM:
		execute_config_elem(node->data, st, block_list);
		break;
	case COMMAND_DECLARE_VARIABLE:
		execute_declare_variable((DeclareVariableNode*)node->data, st);
		break;
	case COMMAND_INVALIDATE_PARAM:
		execute_invalidate_param((InvalidateParamNode*)node->data, st);
		break;
	case COMMAND_IF:
		execute_if((IfNode*)node->data, st, block_list, NULL);
		break;
	case COMMAND_ASSIGNMENT:
		execute_assignment((AssignmentNode*)node->data, st, block_list);
		break;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_assignment(AssignmentNode* node, SymbolTable* st, BlockList* block_list)
{
	(void)block_list;


	if (!node || !node->lhs || node->lhs->type != EXPR_VARIABLE_REF) {
		ProPrintfChar("Error: Invalid assignment LHS (must be variable ref)\n");
		return PRO_TK_GENERAL_ERROR;
	}

	const char* lhs_name = node->lhs->data.string_val;
	if (!lhs_name || lhs_name[0] == '\0') {
		ProPrintfChar("Error: Empty variable name in assignment\n");
		return PRO_TK_GENERAL_ERROR;
	}

	Variable* dst = get_symbol(st, lhs_name);
	if (!dst) {
		ProPrintfChar("Error: Assignment to undeclared variable '%s'\n", lhs_name);
		return PRO_TK_GENERAL_ERROR;
	}

	// STRING path: use evaluate_to_string so tokens like AdjShelf2.gif (no quotes) are treated as literal strings and + does concatenation, same as SUB_PICTURE. 
	if (dst->type == TYPE_STRING) {
		char* sval = NULL;
		if (evaluate_to_string(node->rhs, st, &sval) != 0 || !sval) {
			ProPrintfChar("Error: RHS does not evaluate to a string for '%s'\n", lhs_name);
			return PRO_TK_GENERAL_ERROR;
		}
		// replace previous string 
		if (dst->data.string_value) {
			free(dst->data.string_value);
		}
		dst->data.string_value = sval;
		return PRO_TK_NO_ERROR;
	}

	// Non-string path: use general evaluator and coerce as needed. 
	Variable* rval = NULL;
	if (evaluate_expression(node->rhs, st, &rval) != 0 || !rval) {
		ProPrintfChar("Error: Failed to evaluate RHS for assignment to '%s'\n", lhs_name);
		return PRO_TK_GENERAL_ERROR;
	}

	ProError status = PRO_TK_NO_ERROR;
	switch (dst->type) {
	case TYPE_INTEGER:
		if (rval->type == TYPE_INTEGER || rval->type == TYPE_BOOL) {
			dst->data.int_value = rval->data.int_value;
		}
		else if (rval->type == TYPE_DOUBLE) {
			dst->data.int_value = (int)rval->data.double_value;
		}
		else {
			ProPrintfChar("Error: Type mismatch assigning to integer '%s'\n", lhs_name);
			status = PRO_TK_GENERAL_ERROR;
		}
		break;

	case TYPE_DOUBLE:
		if (rval->type == TYPE_DOUBLE) {
			dst->data.double_value = rval->data.double_value;
		}
		else if (rval->type == TYPE_INTEGER || rval->type == TYPE_BOOL) {
			dst->data.double_value = (double)rval->data.int_value;
		}
		else {
			ProPrintfChar("Error: Type mismatch assigning to double '%s'\n", lhs_name);
			status = PRO_TK_GENERAL_ERROR;
		}
		break;

	case TYPE_BOOL:
		if (rval->type == TYPE_BOOL || rval->type == TYPE_INTEGER) {
			dst->data.int_value = (rval->type == TYPE_BOOL ? rval->data.int_value
				: (rval->data.int_value != 0));
		}
		else if (rval->type == TYPE_DOUBLE) {
			dst->data.int_value = (rval->data.double_value != 0.0);
		}
		else {
			ProPrintfChar("Error: Type mismatch assigning to bool '%s'\n", lhs_name);
			status = PRO_TK_GENERAL_ERROR;
		}
		break;

	default:
		ProPrintfChar("Error: Unsupported assignment target type for '%s'\n", lhs_name);
		status = PRO_TK_GENERAL_ERROR;
		break;
	}

	free_variable(rval);
	return status;
}

ProError apply_ui_gate_to_block(Block* blk, ExecContext* ctx, ProBoolean enabled)
{
	if (!blk || !ctx || !ctx->st) return PRO_TK_NO_ERROR;

	char* dialog = NULL;
	if (ctx->ui && ctx->ui->dialog_name) dialog = ctx->ui->dialog_name;

	ProBoolean required = enabled;

	for (size_t i = 0; i < blk->command_count; ++i) {
		CommandNode* cmd = blk->commands[i];
		if (!cmd) continue;

		switch (cmd->type) {
		case COMMAND_USER_SELECT:
		{
			UserSelectNode* un = (UserSelectNode*)cmd->data;
			if (un && un->reference) (void)set_user_select_enabled(dialog, ctx->st, un->reference, enabled, required);
			break;
		}
		case COMMAND_USER_SELECT_OPTIONAL:
		{
			UserSelectOptionalNode* un = (UserSelectOptionalNode*)cmd->data;
			if (un && un->reference) (void)set_user_select_optional_enabled(dialog, ctx->st, un->reference, enabled, required);
			break;
		}
		case COMMAND_USER_SELECT_MULTIPLE:
		{
			UserSelectMultipleNode* mn = (UserSelectMultipleNode*)cmd->data;
			if (mn && mn->array) (void)set_user_select_enabled(dialog, ctx->st, mn->array, enabled, required);
			break;
		}
		case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL:
		{
			UserSelectMultipleOptionalNode* mn = (UserSelectMultipleOptionalNode*)cmd->data;
			if (mn && mn->array) (void)set_user_select_optional_enabled(dialog, ctx->st, mn->array, enabled, required);
			break;
		}
		default:
			break; // Other components are created only when their branch executes 
		}
	}
	return PRO_TK_NO_ERROR;
}

// Re-evaluate IF conditions and only gate UI (no command execution). 
ProError recompute_if_gates_only(Block* blk, ExecContext* ctx)
{
	if (!blk || !ctx || !ctx->st) return PRO_TK_NO_ERROR;

	for (size_t i = 0; i < blk->command_count; ++i) {
		CommandNode* cmd = blk->commands[i];
		if (!cmd) continue;

		if (cmd->type == COMMAND_IF) {
			IfNode* node = (IfNode*)cmd->data;

			// 0) Turn everything OFF 
			for (size_t b = 0; b < node->branch_count; ++b) {
				Block off = { 0 };
				off.command_count = node->branches[b]->command_count;
				off.commands = node->branches[b]->commands;
				(void)apply_ui_gate_to_block(&off, ctx, PRO_B_FALSE);
				
			}
			if (node->else_command_count > 0) {
				Block off_else = { 0 };
				off_else.command_count = node->else_command_count;
				off_else.commands = node->else_commands;
				(void)apply_ui_gate_to_block(&off_else, ctx, PRO_B_FALSE);
			}

			// 1) Find winning branch 
			size_t winning = (size_t)-1;
			for (size_t b = 0; b < node->branch_count; ++b) {
				Variable* cond_val = NULL;
				int est = evaluate_expression(node->branches[b]->condition, ctx->st, &cond_val);
				if (est != 0 || !cond_val) { continue; }

				bool truth = false;
				if (cond_val->type == TYPE_BOOL || cond_val->type == TYPE_INTEGER)
					truth = (cond_val->data.int_value != 0);
				else if (cond_val->type == TYPE_DOUBLE)
					truth = (cond_val->data.double_value != 0.0);

				free_variable(cond_val);
				if (truth) { winning = b; break; }
			}

			// 2) Gate ON the winner (or ELSE) and recurse into that block 
			if (winning != (size_t)-1) {
				Block on = { 0 };
				on.command_count = node->branches[winning]->command_count;
				on.commands = node->branches[winning]->commands;
				(void)apply_ui_gate_to_block(&on, ctx, PRO_B_TRUE);
				(void)recompute_if_gates_only(&on, ctx); // nested IFs 
			}
			else if (node->else_command_count > 0) {
				Block on_else = { 0 };
				on_else.command_count = node->else_command_count;
				on_else.commands = node->else_commands;
				(void)apply_ui_gate_to_block(&on_else, ctx, PRO_B_TRUE);
				(void)recompute_if_gates_only(&on_else, ctx);
			}

			// Continue scanning peers after this IF 
		}
	}
	return PRO_TK_NO_ERROR;
}

ProError exec_command_in_context(CommandNode* node, ExecContext* ctx)
{
	if (!node || !ctx || !ctx->st) return PRO_TK_BAD_INPUTS;

	switch (node->type)
	{
	case COMMAND_IF:
		return execute_if_ctx((IfNode*)node->data, ctx);

		// --- GUI-aware commands 
	case COMMAND_SHOW_PARAM:
		return (ctx->ui ? execute_show_param((ShowParamNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_CHECKBOX_PARAM:
		return (ctx->ui ? execute_checkbox_param((CheckboxParamNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_USER_INPUT_PARAM:
		return (ctx->ui ? execute_user_input_param((UserInputParamNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_RADIOBUTTON_PARAM:
		return (ctx->ui ? execute_radiobutton_param((RadioButtonParamNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_USER_SELECT:
		return (ctx->ui ? execute_user_select_param((UserSelectNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_USER_SELECT_OPTIONAL:
		return (ctx->ui ? execute_user_select_optional_param((UserSelectOptionalNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_USER_SELECT_MULTIPLE:
		return(ctx->ui ? execute_user_select_multiple_param((UserSelectMultipleNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL:
		return (ctx->ui ? execute_user_select_multiple_optional_param((UserSelectMultipleOptionalNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_GLOBAL_PICTURE:
		return (ctx->ui ? execute_global_picture((GlobalPictureNode*)node->data, ctx->ui, ctx->st)
			: execute_command(node, ctx->st, ctx->block_list));

	case COMMAND_SUB_PICTURE:
		return execute_sub_picture((SubPictureNode*)node->data, ctx->st);

		// --- non-GUI core commands 
	case COMMAND_DECLARE_VARIABLE:
		return execute_declare_variable((DeclareVariableNode*)node->data, ctx->st);

	case COMMAND_ASSIGNMENT:
		return execute_assignment((AssignmentNode*)node->data, ctx->st, ctx->block_list);

	default:
		// Fallback to your existing dispatcher so new commands keep working 
		return execute_command(node, ctx->st, ctx->block_list);
	}
}

ProError execute_if_ctx(IfNode* node, ExecContext* ctx)
{
	if (!node || !ctx || !ctx->st) return PRO_TK_BAD_INPUTS;

	// 0) GUI pre-pass: ensure USER_SELECT controls exist so gating can work. We keep the scope tight: only for this IF subtree. 
	if (ctx->ui) {
		(void)prepare_if_user_selects(node, ctx->ui, ctx->st); // pre-create once 
		// Proactively gate everything OFF; the winning branch will be enabled once found. 
		for (size_t b = 0; b < node->branch_count; ++b) {
			Block temp = { 0 };
			temp.command_count = node->branches[b]->command_count;
			temp.commands = node->branches[b]->commands;
			(void)apply_ui_gate_to_block(&temp, ctx, PRO_B_FALSE);
		}
		if (node->else_command_count > 0) {
			Block temp = { 0 };
			temp.command_count = node->else_command_count;
			temp.commands = node->else_commands;
			(void)apply_ui_gate_to_block(&temp, ctx, PRO_B_FALSE);
		}
	}

	// 1) Find the first true branch (single-pass, no duplicate evaluation). 
	size_t winning = (size_t)-1;
	for (size_t b = 0; b < node->branch_count; ++b) {
		Variable* cond_val = NULL;
		int est = evaluate_expression(node->branches[b]->condition, ctx->st, &cond_val);
		if (est != 0 || !cond_val) {
			ProGenericMsg(L"Error: IF condition evaluation failed");
			return PRO_TK_GENERAL_ERROR;
		}

		// Accept bool or numeric truthiness (unchanged semantics). 
		bool truth = false;
		if (cond_val->type == TYPE_BOOL || cond_val->type == TYPE_INTEGER) {
			truth = (cond_val->data.int_value != 0);
		}
		else if (cond_val->type == TYPE_DOUBLE) {
			truth = (cond_val->data.double_value != 0.0);
		}
		else {
			free_variable(cond_val);
			ProGenericMsg(L"Error: IF condition must be bool or numeric");
			return PRO_TK_GENERAL_ERROR;
		}
		free_variable(cond_val);

		if (truth) { winning = b; break; }
	}

	// 2) Gate ON the winning branch (or ELSE), then execute only that block. 
	if (winning != (size_t)-1) {
		IfBranch* br = node->branches[winning];

		if (ctx->ui) {
			Block temp = { 0 };
			temp.command_count = br->command_count;
			temp.commands = br->commands;
			(void)apply_ui_gate_to_block(&temp, ctx, PRO_B_TRUE);
		}

		for (size_t i = 0; i < br->command_count; ++i) {
			ProError s = exec_command_in_context(br->commands[i], ctx);
			if (s != PRO_TK_NO_ERROR) return s;
		}
		return PRO_TK_NO_ERROR;
	}

	// ELSE branch 
	if (node->else_command_count > 0) {
		if (ctx->ui) {
			Block temp = { 0 };
			temp.command_count = node->else_command_count;
			temp.commands = node->else_commands;
			(void)apply_ui_gate_to_block(&temp, ctx, PRO_B_TRUE);
		}

		for (size_t i = 0; i < node->else_command_count; ++i) {
			ProError s = exec_command_in_context(node->else_commands[i], ctx);
			if (s != PRO_TK_NO_ERROR) return s;
		}
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_if(IfNode* node, SymbolTable* st, BlockList* block_list, DialogState* state)
{
	ExecContext ctx = { 0 };
	ctx.st = st;
	ctx.block_list = block_list;
	ctx.ui = state;
	return execute_if_ctx(node, &ctx);
}

// Function to execute all commands in the ASM block
void execute_asm_block(Block* asm_block, SymbolTable* st, BlockList* block_list)
{
	for (size_t i = 0; i < asm_block->command_count; i++)
	{
		CommandNode* cmd = asm_block->commands[i];
		execute_command(cmd, st, block_list);
	}

}


/*=================================================*\
*
* CORE GUI LOGIC FUNCTIONALITY
* (REFRESH)
*
*
\*=================================================*/
void EPA_ReactiveRefresh(void)
{
	if (!g_active_state || !g_active_state->dialog_name || !g_active_st || !g_active_state->gui_block || !g_active_state->tab_block)
		return;


	// 1) Rebuild SUB_PICTURES for the current truth of all IFs
	remove_symbol(g_active_st, "SUB_PICTURES");   // clear prior set
	rebuild_sub_pictures_only(g_active_state->gui_block, g_active_st);

	// 2) Redraw images (GLOBAL + new SUB_PICTURES)
	addpicture(g_active_state->dialog_name, "draw_area", (ProAppData)g_active_st);

	ExecContext ctx = { 0 };
	ctx.st = g_active_st;
	ctx.ui = g_active_state;
	recompute_if_gates_only(g_active_state->gui_block, &ctx);



	// 5) Re-validate OK button (you already centralize required checks here)
	validate_ok_button(g_active_state->dialog_name, g_active_st);  


}
