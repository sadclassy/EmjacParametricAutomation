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


ProError execute_if(IfNode* node, SymbolTable* st, BlockList* block_list, DialogState* state);

ProError execute_assignment(AssignmentNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_declare_variable(DeclareVariableNode* node, SymbolTable* st);

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

ProError execute_show_param(ShowParamNode* node, DialogState* state, SymbolTable* st)
{
	// Initialize layout grid options
	ProUIGridopts grid_opts_showParam = { 0 };
	grid_opts_showParam.row = 1;
	grid_opts_showParam.column = 0;
	grid_opts_showParam.attach_bottom = PRO_B_TRUE;
	grid_opts_showParam.attach_top = PRO_B_TRUE;
	grid_opts_showParam.attach_right = PRO_B_TRUE;
	grid_opts_showParam.attach_left = PRO_B_TRUE;
	grid_opts_showParam.horz_resize = PRO_B_TRUE;
	grid_opts_showParam.vert_resize = PRO_B_TRUE;

	// Always initialize the layout if not already done
	ProError status = InitializeLayout(state->dialog_name, state->main_layout_name, state->show_param_layout.name, &grid_opts_showParam, L"Info", &state->show_param_layout.initialized);
	if (status != PRO_TK_NO_ERROR) {
		return status;
	}

	// Add parameter to standard layout
	status = addShowParam(state->dialog_name, state->show_param_layout.name, node, &state->show_param_layout.row, 0, st);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintf(L"Error: Could not add parameter '%s' to layout\n", node->parameter);
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_checkbox_param(CheckboxParamNode* node, DialogState* state, SymbolTable* st)
{
	ProUIGridopts grid_opt_cbp = { 0 };
	grid_opt_cbp.row = 1;
	grid_opt_cbp.column = 1;
	grid_opt_cbp.attach_bottom = PRO_B_TRUE;
	grid_opt_cbp.attach_left = PRO_B_TRUE;
	grid_opt_cbp.attach_right = PRO_B_TRUE;
	grid_opt_cbp.attach_top = PRO_B_TRUE;
	grid_opt_cbp.horz_resize = PRO_B_TRUE;
	grid_opt_cbp.vert_resize = PRO_B_TRUE;
	grid_opt_cbp.left_offset = 20;


	// Always initialize the layout if not already done
	ProError status = InitializeLayout(state->dialog_name, state->main_layout_name, state->checkbox_layout.name, &grid_opt_cbp, L"True/False", &state->checkbox_layout.initialized);
	if (status != PRO_TK_NO_ERROR) {
		return status;
	}


	status = addCheckboxParam(state->dialog_name, state->checkbox_layout.name, node, &state->checkbox_layout.row, 0, st);
	if (status != PRO_TK_NO_ERROR)
	{
		ProPrintfChar("Error: Could not add Checkbox '%s' to layout\n", node->parameter);
		return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_user_input_param(UserInputParamNode* node, DialogState* state, SymbolTable* st)
{
	ProUIGridopts grid_opts_uip = { 0 };
	grid_opts_uip.row = 1;
	grid_opts_uip.column = 2;
	grid_opts_uip.attach_bottom = PRO_B_TRUE;
	grid_opts_uip.attach_left = PRO_B_TRUE;
	grid_opts_uip.attach_right = PRO_B_TRUE;
	grid_opts_uip.attach_top = PRO_B_TRUE;
	grid_opts_uip.left_offset = 20;

	ProError status = InitializeLayout(state->dialog_name, state->main_layout_name, state->user_input_layout.name, &grid_opts_uip, L"Enter Values", &state->user_input_layout.initialized);
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
	ProUIGridopts grid_opts_radio = { 0 };
	grid_opts_radio.column = 3;
	grid_opts_radio.row = 1;
	grid_opts_radio.attach_bottom = PRO_B_TRUE;
	grid_opts_radio.attach_top = PRO_B_TRUE;
	grid_opts_radio.attach_left = PRO_B_TRUE;
	grid_opts_radio.attach_right = PRO_B_TRUE;
	grid_opts_radio.horz_resize = PRO_B_TRUE;
	grid_opts_radio.vert_resize = PRO_B_TRUE;
	grid_opts_radio.left_offset = 20;

	ProError status = InitializeLayout(state->dialog_name, state->main_layout_name, state->radiobutton_layout.name, &grid_opts_radio, L"Choose Options", &state->radiobutton_layout.initialized);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Error: Could not Initialize layout for RADIOBUTTON_PARAM");
		return status;
	}

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
	ProError status;

	// Ensure the outer "user_select_layotu" 
	ProUIGridopts grid_opts_US = { 0 };
	grid_opts_US.row = 1;
	grid_opts_US.column = 4;
	grid_opts_US.attach_bottom = PRO_B_TRUE;
	grid_opts_US.attach_top = PRO_B_TRUE;
	grid_opts_US.attach_right = PRO_B_TRUE;
	grid_opts_US.attach_left = PRO_B_TRUE;
	grid_opts_US.horz_resize = PRO_B_TRUE;
	grid_opts_US.vert_resize = PRO_B_TRUE;
	grid_opts_US.left_offset = 20;

	status = InitializeLayout(state->dialog_name, state->main_layout_name, state->user_select_layout.name, &grid_opts_US, L"Selection", &state->user_select_layout.initialized);
	if (status != PRO_TK_NO_ERROR)
	{
		ProPrintfChar("Error: Initialize user select layout");
		return status;
	}

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

// Pre-create USER_SELECTs found anywhere inside this IF (all branches, nested IFs)
ProError prepare_if_user_selects(IfNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_NO_ERROR;

	// Walk true/else-if branches
	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		for (size_t i = 0; i < br->command_count; ++i) {
			CommandNode* c = br->commands[i];
			if (!c) continue;
			if (c->type == COMMAND_USER_SELECT) {
				(void)execute_user_select_param((UserSelectNode*)c->data, state, st);
			}
			else if (c->type == COMMAND_IF) {
				(void)prepare_if_user_selects((IfNode*)c->data, state, st);
			}
		}
	}

	// Walk ELSE branch
	for (size_t i = 0; i < node->else_command_count; ++i) {
		CommandNode* c = node->else_commands[i];
		if (!c) continue;
		if (c->type == COMMAND_USER_SELECT) {
			(void)execute_user_select_param((UserSelectNode*)c->data, state, st);
		}
		else if (c->type == COMMAND_IF) {
			(void)prepare_if_user_selects((IfNode*)c->data, state, st);
		}
	}
	return PRO_TK_NO_ERROR;
}

ProError execute_global_picture(GlobalPictureNode* node, DialogState* state, SymbolTable* st)

{
	(void)node;
	g_active_state = state;
	g_active_st = st;

	ProError status;
	int imageH = 0, imageW = 0;

	Variable* pic_var = get_symbol(st, "GLOBAL_PICTURE");
	if (!pic_var || pic_var->type != TYPE_STRING)
	{
		ProPrintfChar("Error: GLOBAL_PICTURE not found or invalid type in symbol table.\n");
		return PRO_TK_GENERAL_ERROR;
	}

	char* filepath = pic_var->data.string_value;
	if (!filepath)
	{
		ProPrintfChar("Error: Image path in symbol table is null.\n");
		return PRO_TK_GENERAL_ERROR;
	}

	ProPrintfChar("Image Path from filepath: %s\n", filepath);

	if (!get_gif_dimensions(filepath, &imageW, &imageH))
	{
		ProPrintfChar("Error: Could not load image '%s' to get dimensions\n", filepath);
		return PRO_TK_GENERAL_ERROR;
	}

	ProPrintfChar("Retrieved image height: %d, width: %d\n", imageH, imageW);

	char* drawA1 = "drawA1";
	ProUIGridopts grid_opts = { 0 };
	grid_opts.horz_cells = 5;
	grid_opts.vert_cells = 1;
	grid_opts.attach_bottom = PRO_B_TRUE;
	grid_opts.attach_top = PRO_B_TRUE;
	grid_opts.attach_left = PRO_B_TRUE;
	grid_opts.attach_right = PRO_B_TRUE;
	grid_opts.horz_resize = PRO_B_TRUE;
	grid_opts.vert_resize = PRO_B_TRUE;
	grid_opts.row = PRO_UI_INSERT_NEW_ROW;
	grid_opts.column = PRO_UI_INSERT_NEW_COLUMN;
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

	status = ProUIDrawingareaBackgroundcolorSet(state->dialog_name, drawA1, PRO_UI_COLOR_LT_GREY);  // Set on inner draw_area for accuracy
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

	status = ProUIDrawingareaDrawingmodeSet(state->dialog_name, draw_area, PROUIDRWMODE_COPY);  // Change to COPY for direct pixel rendering
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Could not set Drawingmode");
		return status;
	}

	// Change to paint callback for persistence on repaint events
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

	/* Evaluate now (snapshot) */
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

	/* Ensure array exists */
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

	/* Store as TYPE_EXPR but with literal nodes (constants) */

	/* filename_expr -> literal string */
	Variable* filename_expr_var = (Variable*)malloc(sizeof(Variable));
	if (!filename_expr_var) { free_hash_table(sub_map); free(filename); return PRO_TK_GENERAL_ERROR; }
	filename_expr_var->type = TYPE_EXPR;
	filename_expr_var->data.expr = (ExpressionNode*)malloc(sizeof(ExpressionNode));
	if (!filename_expr_var->data.expr) { free_variable(filename_expr_var); free_hash_table(sub_map); free(filename); return PRO_TK_GENERAL_ERROR; }
	filename_expr_var->data.expr->type = EXPR_LITERAL_STRING;
	filename_expr_var->data.expr->data.string_val = filename; /* take ownership */

	if (!add_var_to_map(sub_map, "filename_expr", filename_expr_var)) {
		free_variable(filename_expr_var);
		free_hash_table(sub_map);
		return PRO_TK_GENERAL_ERROR;
	}

	/* posX_expr -> literal double */
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

	/* posY_expr -> literal double (freezes DIMY at this moment) */
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
	case COMMAND_IF:
		execute_if((IfNode*)node->data, st, block_list, state);
		break;
	case COMMAND_ASSIGNMENT:
		execute_assignment((AssignmentNode*)node->data, st, block_list);
		break;
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_gui_block(Block* gui_block, SymbolTable* st, BlockList* block_list)
{
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

	state.gui_block = gui_block;
	state.st = st;

	status = ProUIDialogCreate(state.dialog_name, NULL);
	if (status != PRO_TK_NO_ERROR)
	{
		ProGenericMsg(L"Could not create dialog");
		return status;
	}

	// Retrieve CONFIG_ELEM configuration from symbol table
	Variable* config_var = get_symbol(st, "CONFIG_ELEM");
	if (config_var && config_var->type == TYPE_MAP)
	{
		HashTable* config_map = config_var->data.map;

		//Get width from the map
		Variable* width_var = hash_table_lookup(config_map, "width");
		if (width_var && width_var->type == TYPE_DOUBLE && width_var->data.double_value > 0)
		{
			double width = width_var->data.double_value;
			int width_pixels = (int)width;
			ProUIDialogWidthSet(state.dialog_name, width_pixels);
		}

		// Get height from the map
		Variable* height_var = hash_table_lookup(config_map, "height");
		if (height_var && height_var->type == TYPE_DOUBLE && height_var->data.double_value > 0)
		{
			double height = height_var->data.double_value;
			int height_pixels = (int)height;
			ProUIDialogHeightSet(state.dialog_name, height_pixels);
		}

	}

	ProUIGridopts grid_opts_main = { 0 };
	grid_opts_main.attach_bottom = PRO_B_FALSE;  // Disable bottom attach to allow stacking
	grid_opts_main.attach_top = PRO_B_TRUE;
	grid_opts_main.attach_left = PRO_B_TRUE;
	grid_opts_main.attach_right = PRO_B_TRUE;
	grid_opts_main.row = 0;        // Top row
	grid_opts_main.column = 0;
	grid_opts_main.horz_cells = 5; // Define a visible grid
	grid_opts_main.vert_cells = 3; // Adjusted to accommodate three stacked layouts
	grid_opts_main.vert_resize = PRO_B_TRUE;
	grid_opts_main.horz_resize = PRO_B_TRUE;

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

	ProUIGridopts grid_opts_table = { 0 };
	grid_opts_table.attach_bottom = PRO_B_FALSE;  // Disable bottom attach for stacking
	grid_opts_table.attach_top = PRO_B_TRUE;
	grid_opts_table.attach_left = PRO_B_TRUE;
	grid_opts_table.attach_right = PRO_B_TRUE;
	grid_opts_table.row = 4;        // Row below main layout
	grid_opts_table.column = 0;
	grid_opts_table.horz_cells = 5; // Define a visible grid
	grid_opts_table.vert_cells = 3; // Consistent with main

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

	ProUIGridopts pbgrid = { 0 };
	pbgrid.attach_bottom = PRO_B_TRUE;  // Enable bottom attach for the lowest layout
	pbgrid.attach_top = PRO_B_TRUE;
	pbgrid.attach_left = PRO_B_TRUE;
	pbgrid.attach_right = PRO_B_TRUE;
	pbgrid.horz_resize = PRO_B_TRUE;
	pbgrid.vert_resize = PRO_B_FALSE;  // Optional: Limit vertical resize if buttons are fixed-height
	pbgrid.row = 7;  // Row below table layout
	pbgrid.column = 0;
	pbgrid.horz_cells = 5;
	pbgrid.vert_cells = 1;  // Reduced for button row

	status = ProUIDialogLayoutAdd(state.dialog_name, state.confirmation_layout_name, &pbgrid);
	if (status != PRO_TK_NO_ERROR) {
		ProTKWprintf(L"Could not add pushbutton layout\n");
		ProUIDialogDestroy(state.dialog_name);
		return status;
	}
	ProUILayoutDecorate(state.dialog_name, state.confirmation_layout_name);


	// Add the Pushbutton to the Layout
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

	// REMOVED: Premature check for REQUIRE_RADIOS and disable here.
	// It will be handled by validate_ok_button after all components are added.

	Block* tab_block = find_block(block_list, BLOCK_TAB);
	if (!tab_block)
	{
		status = ProUILayoutHide(state.dialog_name, state.table_layout_name);
		if (status != PRO_TK_NO_ERROR)
		{
			ProGenericMsg(L"Could not hide table layout\n");
			return status;
		}
	}

	for (size_t i = 0; i < gui_block->command_count; i++)
	{
		CommandNode* cmd = gui_block->commands[i];
		execute_gui_command(cmd, &state, st);
	}

	// Validate OK state after all components are added
	status = validate_ok_button(state.dialog_name, st);
	if (status != PRO_TK_NO_ERROR) {
		ProGenericMsg(L"Error: Initial validation of OK button failed");
		return status;
	}

	ProUIDialogPostmanagenotifyActionSet(state.dialog_name, MyPostManageCallback, st);
	ProUIDialogCloseActionSet(state.dialog_name, CloseCallback, NULL);

	// Make the active dialog/state globally available to callbacks
	g_active_state = &state;   // << NEW
	g_active_st = st;       // << NEW


	status = ProUIDialogActivate(state.dialog_name, &dialog_status);
	if (status != PRO_TK_NO_ERROR)
	{
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

	/* STRING path: use evaluate_to_string so tokens like AdjShelf2.gif (no quotes)
	   are treated as literal strings and + does concatenation, same as SUB_PICTURE. */
	if (dst->type == TYPE_STRING) {
		char* sval = NULL;
		if (evaluate_to_string(node->rhs, st, &sval) != 0 || !sval) {
			ProPrintfChar("Error: RHS does not evaluate to a string for '%s'\n", lhs_name);
			return PRO_TK_GENERAL_ERROR;
		}
		/* replace previous string */
		if (dst->data.string_value) {
			free(dst->data.string_value);
		}
		dst->data.string_value = sval;
		return PRO_TK_NO_ERROR;
	}

	/* Non-string path: use general evaluator and coerce as needed. */
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

ProError execute_if(IfNode* node, SymbolTable* st, BlockList* block_list, DialogState* state) {
	if (!node) return PRO_TK_BAD_INPUTS;

	// 0) GUI: pre-create all USER_SELECTs that live inside this IF (all branches)
	if (state != NULL) {
		(void)prepare_if_user_selects(node, state, st);

		// Gate: disable all branches, then enable the active one now
		// First disable all branch blocks
		for (size_t b = 0; b < node->branch_count; ++b) {
			Block temp = { 0 };
			temp.command_count = node->branches[b]->command_count;
			temp.commands = node->branches[b]->commands;
			toggle_user_selects_in_block(&temp, st, state->dialog_name, PRO_B_FALSE);
		}
		if (node->else_command_count > 0) {
			Block temp = { 0 };
			temp.command_count = node->else_command_count;
			temp.commands = node->else_commands;
			toggle_user_selects_in_block(&temp, st, state->dialog_name, PRO_B_FALSE);
		}
	}

	// 1) Evaluate IF as before and execute the first true branch (or ELSE)
	bool executed = false;
	for (size_t b = 0; b < node->branch_count; b++) {
		IfBranch* branch = node->branches[b];
		Variable* cond_val = NULL;
		int status = evaluate_expression(branch->condition, st, &cond_val);
		if (status != 0 || !cond_val) {
			ProGenericMsg(L"Error: Failed to evaluate IF condition");
			return PRO_TK_GENERAL_ERROR;
		}
		bool cond_true = false;
		if (cond_val->type == TYPE_BOOL)         cond_true = (cond_val->data.int_value != 0);
		else if (cond_val->type == TYPE_INTEGER) cond_true = (cond_val->data.int_value != 0);
		else if (cond_val->type == TYPE_DOUBLE)  cond_true = (cond_val->data.double_value != 0.0);
		else {
			free_variable(cond_val);
			ProGenericMsg(L"Error: IF condition must evaluate to bool or numeric");
			return PRO_TK_GENERAL_ERROR;
		}
		free_variable(cond_val);

		if (cond_true) {
			// Gate: enable this true branch now (if in GUI context)
			if (state != NULL) {
				Block temp = { 0 };
				temp.command_count = branch->command_count;
				temp.commands = branch->commands;
				toggle_user_selects_in_block(&temp, st, state->dialog_name, PRO_B_TRUE);
			}

			for (size_t c = 0; c < branch->command_count; c++) {
				ProError cmd_status = (state != NULL)
					? execute_gui_command(branch->commands[c], state, st)
					: execute_command(branch->commands[c], st, block_list);
				if (cmd_status != PRO_TK_NO_ERROR) return cmd_status;
			}
			executed = true;
			break;  // Stop after first true branch
		}
	}

	if (!executed && node->else_command_count > 0) {
		// Gate: enable ELSE branch when nothing matched (GUI context)
		if (state != NULL) {
			Block temp = { 0 };
			temp.command_count = node->else_command_count;
			temp.commands = node->else_commands;
			toggle_user_selects_in_block(&temp, st, state->dialog_name, PRO_B_TRUE);
		}

		for (size_t c = 0; c < node->else_command_count; c++) {
			ProError cmd_status = (state != NULL)
				? execute_gui_command(node->else_commands[c], state, st)
				: execute_command(node->else_commands[c], st, block_list);
			if (cmd_status != PRO_TK_NO_ERROR) return cmd_status;
		}
	}

	return PRO_TK_NO_ERROR;
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
	if (!g_active_state || !g_active_state->dialog_name || !g_active_st || !g_active_state->gui_block)
		return;

	// 1) Rebuild SUB_PICTURES for the current truth of all IFs
	remove_symbol(g_active_st, "SUB_PICTURES");   // clear prior set
	(void)rebuild_sub_pictures_only(g_active_state->gui_block, g_active_st);

	// 2) Redraw images (GLOBAL + new SUB_PICTURES)
	(void)addpicture(g_active_state->dialog_name, "draw_area", (ProAppData)g_active_st);

	// 4) Enable/disable USER_SELECT buttons based on IF truth right now
	toggle_user_selects_in_block(g_active_state->gui_block, g_active_st, g_active_state->dialog_name, PRO_B_TRUE);

	// 5) Re-validate OK button (you already centralize required checks here)
	(void)validate_ok_button(g_active_state->dialog_name, g_active_st);  // :contentReference[oaicite:8]{index=8}
}
