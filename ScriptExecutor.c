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



ProError execute_if(IfNode* node, SymbolTable* st, BlockList* block_list, DialogState* state);
ProError execute_assignment(AssignmentNode* node, SymbolTable* st, BlockList* block_list);
ProError execute_declare_variable(DeclareVariableNode* node, SymbolTable* st);
ProError TableSelectCallback(char* dialog, char* table, ProAppData appdata);
static void colplan_scan_command(CommandNode* c, ColumnPlan* p);
void EPA_MarkDirty(SymbolTable* st, const char* param_name);



/* unchanged helpers, but kept here for completeness */
int st_get_int(SymbolTable* st, const char* key, int* out)
{
	if (!st || !key || !out) return 0;
	Variable* v = get_symbol(st, (char*)key);
	if (!v || v->type != TYPE_INTEGER) return 0;
	*out = v->data.int_value;
	return 1;
}

void st_put_int(SymbolTable* st, const char* key, int value)
{
	if (!st || !key) return;
	Variable* v = get_symbol(st, (char*)key);
	if (v && v->type == TYPE_INTEGER) { v->data.int_value = value; return; }
	Variable* nv = (Variable*)malloc(sizeof(Variable));
	if (!nv) return;
	nv->type = TYPE_INTEGER;
	nv->data.int_value = value;
	set_symbol(st, (char*)key, nv);
}

/* REVISED: prefer canonical parser/semantic ID, sync legacy slot for compatibility */
int if_gate_id_of(IfNode* n, SymbolTable* st)
{
	if (!n) return 0;

	/* 1) Canonical path: semantic analysis assigns stable ids (node->id > 0) */
	if (n->id > 0) {                               /* canonical id exists */
		if (st) {
			char k[64];
			snprintf(k, sizeof(k), "IF_ID.%p", (void*)n);

			/* Back-fill legacy slot if missing or wrong */
			int have = 0, old = 0;
			have = st_get_int(st, k, &old);
			if (!have || old != n->id) {
				st_put_int(st, k, n->id);
			}

			/* Ensure IF_ID_SEQ never hands out something <= canonical id */
			int seq = 0;
			if (st_get_int(st, "IF_ID_SEQ", &seq) && seq <= n->id) {
				st_put_int(st, "IF_ID_SEQ", n->id + 1);
			}
		}
		return n->id;
	}

	/* 2) Fallback (legacy): no canonical id present. Keep old behavior. */
	if (!st) {
		/* Last-resort: pointer hash (keeps existing behavior if st unavailable) */
		return (int)(((uintptr_t)n) & 0x7fffffff);
	}

	char key[64];
	snprintf(key, sizeof(key), "IF_ID.%p", (void*)n);

	int have = 0, id = 0;
	have = st_get_int(st, key, &id);
	if (have) return id;

	/* Not assigned yet: take next value from IF_ID_SEQ (legacy path) */
	int next = 0;
	if (!st_get_int(st, "IF_ID_SEQ", &next) || next <= 0) next = 1;

	st_put_int(st, key, next);
	st_put_int(st, "IF_ID_SEQ", next + 1);
	return next;
}


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

/* ======================================================================= */
/* Dense column planning for main_layout_name (no gaps when slots missing) */
/* ======================================================================= */

static void colplan_reset(ColumnPlan* p)
{
	memset(p, 0, sizeof(*p));
	for (int i = 0; i < SLOT_COUNT; ++i) p->slot_to_dense[i] = -1;
}

static void colplan_mark(ColumnPlan* p, int slot)
{
	if (slot >= 0 && slot < SLOT_COUNT) p->present_mask |= (1u << slot);
}

static void colplan_compute(ColumnPlan* p)
{
	int next = 0;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		if (p->present_mask & (1u << slot)) {
			p->slot_to_dense[slot] = next++;
		}
	}
	p->dense_count = next;
	p->computed = 1;
}

/* Return 1 if this command is off-picture (i.e., needs a layout column) */
static int is_off_picture_slot(CommandNode* c, int* out_slot)
{
	if (!c || !out_slot) return 0;
	switch (c->type) {
	case COMMAND_SHOW_PARAM: {
		ShowParamNode* n = (ShowParamNode*)c->data;
		if (n && !n->on_picture) { *out_slot = SLOT_SHOW_PARAM; return 1; }
		break;
	}
	case COMMAND_CHECKBOX_PARAM: {
		CheckboxParamNode* n = (CheckboxParamNode*)c->data;
		if (n && !n->on_picture) { *out_slot = SLOT_CHECKBOX_PARAM; return 1; }
		break;
	}
	case COMMAND_USER_INPUT_PARAM: {
		UserInputParamNode* n = (UserInputParamNode*)c->data;
		if (n && !n->on_picture) { *out_slot = SLOT_USER_INPUT_PARAM; return 1; }
		break;
	}
	case COMMAND_RADIOBUTTON_PARAM: {
		RadioButtonParamNode* n = (RadioButtonParamNode*)c->data;
		if (n && !n->on_picture) { *out_slot = SLOT_RADIOBUTTON_PARAM; return 1; }
		break;
	}
	case COMMAND_USER_SELECT:
	case COMMAND_USER_SELECT_OPTIONAL:
	case COMMAND_USER_SELECT_MULTIPLE:
	case COMMAND_USER_SELECT_MULTIPLE_OPTIONAL: {
		/* All user-select variants use the same column family */
		/* Each has on_picture; we only include when off-picture */
		int onpic = 0;
		if (c->type == COMMAND_USER_SELECT) {
			UserSelectNode* n = (UserSelectNode*)c->data; onpic = (n ? n->on_picture : 0);
		}
		else if (c->type == COMMAND_USER_SELECT_OPTIONAL) {
			UserSelectOptionalNode* n = (UserSelectOptionalNode*)c->data; onpic = (n ? n->on_picture : 0);
		}
		else if (c->type == COMMAND_USER_SELECT_MULTIPLE) {
			UserSelectMultipleNode* n = (UserSelectMultipleNode*)c->data; onpic = (n ? n->on_picture : 0);
		}
		else {
			UserSelectMultipleOptionalNode* n = (UserSelectMultipleOptionalNode*)c->data; onpic = (n ? n->on_picture : 0);
		}
		if (!onpic) { *out_slot = SLOT_USER_SELECT; return 1; }
		break;
	}
	default: break;
	}
	return 0;
}

static void colplan_scan_block(Block* b, ColumnPlan* p)
{
	if (!b || !b->commands) return;
	for (size_t i = 0; i < b->command_count; ++i) {
		colplan_scan_command(b->commands[i], p);
	}
}

static void colplan_scan_if(IfNode* node, ColumnPlan* p)
{
	if (!node) return;
	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		if (br && br->commands && br->command_count > 0) {
			Block tmp = { 0 };
			tmp.command_count = br->command_count;
			tmp.commands = br->commands;
			colplan_scan_block(&tmp, p);
		}
	}
	if (node->else_commands && node->else_command_count > 0) {
		Block eb = { 0 };
		eb.command_count = node->else_command_count;
		eb.commands = node->else_commands;
		colplan_scan_block(&eb, p);
	}
}

static void colplan_scan_command(CommandNode* c, ColumnPlan* p)
{
	if (!c || !p) return;
	int slot = -1;
	if (is_off_picture_slot(c, &slot)) {
		colplan_mark(p, slot);
		return;
	}
	if (c->type == COMMAND_IF) {
		colplan_scan_if((IfNode*)c->data, p);
	}
}

/* Ensure column plan exists on the DialogState (compute once) */
static void ensure_column_plan(DialogState* state)
{
	if (!state) return;
	if (state->column_plan.computed) return;

	colplan_reset(&state->column_plan);

	/* Prefer scanning the whole GUI block once (handles IF nesting) */
	if (state->gui_block) {
		colplan_scan_block(state->gui_block, &state->column_plan);
	}

	colplan_compute(&state->column_plan);
	/* Optional: debug print */
	ProPrintfChar("Column plan: mask=0x%08X, dense=%d, map=[%d,%d,%d,%d,%d]\n",
		state->column_plan.present_mask,
		state->column_plan.dense_count,
		state->column_plan.slot_to_dense[0],
		state->column_plan.slot_to_dense[1],
		state->column_plan.slot_to_dense[2],
		state->column_plan.slot_to_dense[3],
		state->column_plan.slot_to_dense[4]);
}

/* Public helper: ask for the dense column for a slot (fallback = canonical) */
static int ui_column_for(DialogState* state, int slot)
{
	ensure_column_plan(state);
	if (!state) return slot;
	int col = (slot >= 0 && slot < SLOT_COUNT) ? state->column_plan.slot_to_dense[slot] : -1;
	return (col >= 0 ? col : slot);
}




ProError execute_show_param(ShowParamNode* node, DialogState* state, SymbolTable* st)
{

	if (node->on_picture)
	{
		return OnPictureShowParam(state->dialog_name, "draw_area", node, st);
	}
	else
	{
		// Initialize layout grid options
		ProUIGridopts grid_opts_showParam = { 0 };
		grid_opts_showParam.row = 1;
		grid_opts_showParam.column = ui_column_for(state, SLOT_SHOW_PARAM);
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
			ProPrintfChar("Error: Could not add parameter '%s' to layout\n", node->parameter);
			return status;
		}

	}


	return PRO_TK_NO_ERROR;
}

ProError prepare_if_show_param(IfNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_NO_ERROR;
	const int gate_id = if_gate_id_of(node, st);

	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		for (size_t i = 0; i < br->command_count; ++i) {
			CommandNode* c = br->commands[i];
			if (!c) continue;

			if (c->type == COMMAND_SHOW_PARAM) {
				ShowParamNode* sp = (ShowParamNode*)c->data;
				pretag_if_gated(st, sp->parameter, gate_id);
				execute_show_param(sp, state, st);
			}
			else if (c->type == COMMAND_IF) {
				prepare_if_show_param((IfNode*)c->data, state, st);
			}
		}
	}

	for (size_t i = 0; i < node->else_command_count; ++i) {
		CommandNode* c = node->else_commands[i];
		if (!c) continue;

		if (c->type == COMMAND_SHOW_PARAM) {
			ShowParamNode* sp = (ShowParamNode*)c->data;
			pretag_if_gated(st, sp->parameter, gate_id);
			execute_show_param(sp, state, st);
		}
		else if (c->type == COMMAND_IF) {
			prepare_if_show_param((IfNode*)c->data, state, st);
		}
	}

	return PRO_TK_NO_ERROR;
}


ProError execute_checkbox_param(CheckboxParamNode* node, DialogState* state, SymbolTable* st)
{
	if (node->on_picture)
	{
		return OnPictureCheckboxParam(state->dialog_name, "draw_area", node, st);

	}
	else
	{
		ProUIGridopts grid_opt_cbp = { 0 };
		grid_opt_cbp.row = 1;
		grid_opt_cbp.column = ui_column_for(state, SLOT_CHECKBOX_PARAM);
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
	}



	return PRO_TK_NO_ERROR;
}

ProError prepare_if_checkbox_param(IfNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_NO_ERROR;
	const int gate_id = if_gate_id_of(node, st);

	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		for (size_t i = 0; i < br->command_count; ++i) {
			CommandNode* c = br->commands[i];
			if (!c) continue;

			if (c->type == COMMAND_CHECKBOX_PARAM) {
				CheckboxParamNode* cb = (CheckboxParamNode*)c->data;
				pretag_if_gated(st, cb->parameter, gate_id);
				execute_checkbox_param(cb, state, st);
			}
			else if (c->type == COMMAND_IF) {
				prepare_if_checkbox_param((IfNode*)c->data, state, st);
			}
		}
	}

	for (size_t i = 0; i < node->else_command_count; ++i) {
		CommandNode* c = node->else_commands[i];
		if (!c) continue;

		if (c->type == COMMAND_CHECKBOX_PARAM) {
			CheckboxParamNode* cb = (CheckboxParamNode*)c->data;
			pretag_if_gated(st, cb->parameter, gate_id);
			execute_checkbox_param(cb, state, st);
		}
		else if (c->type == COMMAND_IF) {
			prepare_if_checkbox_param((IfNode*)c->data, state, st);
		}
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_user_input_param(UserInputParamNode* node, DialogState* state, SymbolTable* st)
{

	if (node->on_picture)
	{
		return OnPictureUserInputParam(state->dialog_name, "draw_area", node, st);
	}
	else 
	{
		ProUIGridopts grid_opts_uip = { 0 };
		grid_opts_uip.row = 1;
		grid_opts_uip.column = ui_column_for(state, SLOT_USER_INPUT_PARAM);
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

	}
	
	return PRO_TK_NO_ERROR;
}

ProError prepare_if_user_input_param(IfNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_NO_ERROR;
	const int gate_id = if_gate_id_of(node, st);

	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		for (size_t i = 0; i < br->command_count; ++i) {
			CommandNode* c = br->commands[i];
			if (!c) continue;

			if (c->type == COMMAND_USER_INPUT_PARAM) {
				UserInputParamNode* uip = (UserInputParamNode*)c->data;
				pretag_if_gated(st, uip->parameter, gate_id);
				execute_user_input_param(uip, state, st);
			}
			else if (c->type == COMMAND_IF) {
				prepare_if_user_input_param((IfNode*)c->data, state, st);
			}
		}
	}

	for (size_t i = 0; i < node->else_command_count; ++i) {
		CommandNode* c = node->else_commands[i];
		if (!c) continue;

		if (c->type == COMMAND_USER_INPUT_PARAM) {
			UserInputParamNode* uip = (UserInputParamNode*)c->data;
			pretag_if_gated(st, uip->parameter, gate_id);
			execute_user_input_param(uip, state, st);
		}
		else if (c->type == COMMAND_IF) {
			prepare_if_user_input_param((IfNode*)c->data, state, st);
		}
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_radiobutton_param(RadioButtonParamNode* node, DialogState* state, SymbolTable* st)
{

	if (node->on_picture)
	{
		return OnPictureRadioButtonParam(state->dialog_name, "draw_area", node, st);
	}
	else
	{
		ProUIGridopts grid_opts_radio = { 0 };
		grid_opts_radio.column = ui_column_for(state, SLOT_RADIOBUTTON_PARAM);
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
	}
	

	return PRO_TK_NO_ERROR;
	
}

ProError prepare_if_radiobutton_param(IfNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_NO_ERROR;
	const int gate_id = if_gate_id_of(node, st);

	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		for (size_t i = 0; i < br->command_count; ++i) {
			CommandNode* c = br->commands[i];
			if (!c) continue;

			if (c->type == COMMAND_RADIOBUTTON_PARAM) {
				RadioButtonParamNode* rbp = (RadioButtonParamNode*)c->data;
				pretag_if_gated(st, rbp->parameter, gate_id);
				execute_radiobutton_param(rbp, state, st);
			}
			else if (c->type == COMMAND_IF) {
				prepare_if_radiobutton_param((IfNode*)c->data, state, st);
			}
		}
	}

	for (size_t i = 0; i < node->else_command_count; ++i) {
		CommandNode* c = node->else_commands[i];
		if (!c) continue;

		if (c->type == COMMAND_RADIOBUTTON_PARAM) {
			RadioButtonParamNode* rbp = (RadioButtonParamNode*)c->data;
			pretag_if_gated(st, rbp->parameter, gate_id);
			execute_radiobutton_param(rbp, state, st);
		}
		else if (c->type == COMMAND_IF) {
			prepare_if_radiobutton_param((IfNode*)c->data, state, st);
		}
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

	if (node->on_picture)
	{
		return OnPictureUserSelect(state->dialog_name, "draw_area", node, st);
	}
	else
	{

		// Ensure the outer "user_select_layotu" 
		ProUIGridopts grid_opts_US = { 0 };
		grid_opts_US.row = 1;
		grid_opts_US.column = ui_column_for(state, SLOT_USER_SELECT);
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
	}

	return PRO_TK_NO_ERROR;
}

ProError execute_user_select_optional_param(UserSelectOptionalNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;

	if (node->on_picture)
	{
		return OnPictureUserSelectOptional(state->dialog_name, "draw_area", node, st);
	}
	else
	{

		// Ensure the outer "user_select_layotu" 
		ProUIGridopts grid_opts_US = { 0 };
		grid_opts_US.row = 1;
		grid_opts_US.column = ui_column_for(state, SLOT_USER_SELECT);
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

	}

	return PRO_TK_NO_ERROR;
}

ProError execute_user_select_multiple_param(UserSelectMultipleNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;

	if (node->on_picture)
	{
		return OnPictureUserSelectMultiple(state->dialog_name, "draw_area", node, st);
	}
	else
	{
		// Ensure the outer "user_select_layotu" 
		ProUIGridopts grid_opts_US = { 0 };
		grid_opts_US.row = 1;
		grid_opts_US.column = ui_column_for(state, SLOT_USER_SELECT);
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
	}
	
	return PRO_TK_NO_ERROR;
}

ProError execute_user_select_multiple_optional_param(UserSelectMultipleOptionalNode* node, DialogState* state, SymbolTable* st)
{
	ProError status;

	if (node->on_picture)
	{
		return OnPictureUserSelectMultipleOptional(state->dialog_name, "draw_area", node, st);
	}
	else
	{

		// Ensure the outer "user_select_layotu" 
		ProUIGridopts grid_opts_US = { 0 };
		grid_opts_US.row = 1;
		grid_opts_US.column = ui_column_for(state, SLOT_USER_SELECT);
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
	}
	return PRO_TK_NO_ERROR;
}


// In prepare_if_user_selects: pretag, then create 
ProError prepare_if_user_selects(IfNode* node, DialogState* state, SymbolTable* st)
{
	if (!node || !state || !st) return PRO_TK_NO_ERROR;
	const int gate_id = if_gate_id_of(node, st);
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

	/* Ensure SUB_PICTURES array exists */
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

	/* Build the per-subpicture map */
	HashTable* sub_map = create_hash_table(16);
	if (!sub_map) { free(filename); return PRO_TK_GENERAL_ERROR; }

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

	/* posY_expr -> literal double */
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

	/* --- IF TAGGING: prefer live __CURRENT_IF_ID, else fall back to prepare tag --- */
	{
		int cur_if = 0;
		if (st_get_int(st, "__CURRENT_IF_ID", &cur_if) && cur_if > 0) {
			set_bool_in_map(sub_map, "if_gated", 1);
			add_int_to_map(sub_map, "if_gate_id", cur_if);
		}
		else {
			int gid = 0;
			char key[64];
			snprintf(key, sizeof(key), "SUBPIC_IF_TAGS.%p", (void*)node);
			if (st_get_int(st, key, &gid) && gid > 0) {
				set_bool_in_map(sub_map, "if_gated", 1);
				add_int_to_map(sub_map, "if_gate_id", gid);
			}
		}
	}

	/* Push into SUB_PICTURES array */
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

static void tag_subpicture_for_if(SymbolTable* st, SubPictureNode* sp, int gate_id)
{
	if (!st || !sp || gate_id <= 0) return;
	char key[64];
	snprintf(key, sizeof(key), "SUBPIC_IF_TAGS.%p", (void*)sp);
	st_put_int(st, key, gate_id);
}

/* --- Prepare pass for SUB_PICTURE inside an IF tree --- */
ProError prepare_if_sub_picture(IfNode* node, SymbolTable* st)
{
	if (!node || !st) return PRO_TK_NO_ERROR;

	const int gate_id = if_gate_id_of(node, st);

	/* All branches */
	for (size_t b = 0; b < node->branch_count; ++b) {
		IfBranch* br = node->branches[b];
		if (!br) continue;
		for (size_t i = 0; i < br->command_count; ++i) {
			CommandNode* c = br->commands[i];
			if (!c) continue;

			if (c->type == COMMAND_SUB_PICTURE) {
				SubPictureNode* sn = (SubPictureNode*)c->data;
				tag_subpicture_for_if(st, sn, gate_id);
			}
			else if (c->type == COMMAND_IF) {
				/* Recurse nested IFs */
				(void)prepare_if_sub_picture((IfNode*)c->data, st);
			}
		}
	}

	/* Else branch */
	for (size_t i = 0; i < node->else_command_count; ++i) {
		CommandNode* c = node->else_commands[i];
		if (!c) continue;

		if (c->type == COMMAND_SUB_PICTURE) {
			SubPictureNode* sn = (SubPictureNode*)c->data;
			tag_subpicture_for_if(st, sn, gate_id);
		}
		else if (c->type == COMMAND_IF) {
			(void)prepare_if_sub_picture((IfNode*)c->data, st);
		}
	}

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
			ProPrintfChar("Failed to delete rows in table %s (error: %d)", table_id, status);
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

/* Returns the wrapper map if table is a wrapper; else NULL */
static Variable* get_table_wrapper(SymbolTable* st, const char* table_id) {
	Variable* v = get_symbol(st, (char*)table_id);
	return (v && v->type == TYPE_MAP) ? v : NULL;
}

/* From wrapper, translate "index after visible" -> column key string.
   0 means the first column AFTER the visible column, so real index = 1 + idx */
static const char* get_filter_column_key(Variable* wrapper, int idx_after_visible) {
	if (!wrapper || wrapper->type != TYPE_MAP) return NULL;
	Variable* cols = hash_table_lookup(wrapper->data.map, "columns");
	if (!cols || cols->type != TYPE_ARRAY) return NULL;
	long real_index = 1 + idx_after_visible; /* skip visible label column */
	if (real_index < 0 || (size_t)real_index >= cols->data.array.size) return NULL;
	Variable* s = cols->data.array.elements[real_index];
	return (s && s->type == TYPE_STRING && s->data.string_value) ? s->data.string_value : NULL;
}

/* Compare two cells by type (ints, doubles, strings). Extend as needed. */
static int cell_equals(Variable* a, Variable* b) {
	if (!a || !b || a->type != b->type) return 0;
	switch (a->type) {
	case TYPE_INTEGER: return a->data.int_value == b->data.int_value;
	case TYPE_DOUBLE:  return a->data.double_value == b->data.double_value;
	case TYPE_STRING:  return (a->data.string_value && b->data.string_value) &&
		strcmp(a->data.string_value, b->data.string_value) == 0;
	default: return 0;
	}
}

/* Prefer newly exported alias <key>_SELECTED, else fall back to key (and optional FILTER_key) */
static Variable* lookup_selected_or_key(SymbolTable* st, const char* key) {
	char buf[256];
	snprintf(buf, sizeof(buf), "%s_SELECTED", key);
	Variable* v = get_symbol(st, buf);
	if (v) return v;
	v = get_symbol(st, (char*)key);
	if (v) return v;
	snprintf(buf, sizeof(buf), "FILTER_%s", key);  /* optional back-compat hook */
	return get_symbol(st, buf);
}


/* Decide if a row passes the active context filter for this table. */
static int row_passes_filter(SymbolTable* st, Variable* wrapper, HashTable* row_map)
{
	if (!row_map) return 0;

	/* NEW: if no explicit filter flags were provided, don't filter at all. */
	if (!wrapper || wrapper->type != TYPE_MAP) return 1;
	{
		Variable* fc = hash_table_lookup(wrapper->data.map, "filter_column");
		Variable* foc = hash_table_lookup(wrapper->data.map, "filter_only_column");
		int has_fc = (fc && fc->type == TYPE_INTEGER && fc->data.int_value >= 0);
		int has_foc = (foc && foc->type == TYPE_INTEGER && foc->data.int_value >= 0);
		if (!has_fc && !has_foc) {
			return 1; /* No filtering requested => show every row */
		}
	}

	/* Optional: obey NO_FILTER if you persist it; otherwise just proceed. */
	/* Example:
	Variable* nf = hash_table_lookup(wrapper->data.map, "NO_FILTER");
	if (nf && nf->type == TYPE_BOOL && nf->data.int_value) return 1;
	*/

	/* Prefer strict FILTER_ONLY_COLUMN if present */
	int idx = -1;
	Variable* foc = wrapper ? hash_table_lookup(wrapper->data.map, "filter_only_column") : NULL;
	if (foc && foc->type == TYPE_INTEGER && foc->data.int_value >= 0) {
		idx = foc->data.int_value;
		const char* key = get_filter_column_key(wrapper, idx);
		if (!key) return 1; /* invalid config -> show all */
		Variable* expect = lookup_selected_or_key(st, key);
		if (!expect) return 1; /* nothing selected yet -> show all */
		Variable* cell = hash_table_lookup(row_map, key);
		return cell_equals(cell, expect);
	}

	/* Softer FILTER_COLUMN: if present, filter by that one column;
	   otherwise fall back to your multi-key matching behavior. */
	Variable* fc = wrapper ? hash_table_lookup(wrapper->data.map, "filter_column") : NULL;
	if (fc && fc->type == TYPE_INTEGER && fc->data.int_value >= 0) {
		idx = fc->data.int_value;
		const char* key = get_filter_column_key(wrapper, idx);
		if (!key) return 1;
		Variable* expect = lookup_selected_or_key(st, key);
		if (!expect) return 1;
		Variable* cell = hash_table_lookup(row_map, key);
		return cell_equals(cell, expect);
	}

	/* Fallback: original behavior — require all matching context keys to match.
	   NOTE: Because of the early-exit above, this block now runs ONLY when a
	   filter flag exists but neither strict nor soft branch returned yet. */
	for (size_t k = 0; k < row_map->key_count; ++k) {
		const char* key = row_map->key_order[k];
		if (!key || strcmp(key, "SEL_STRING") == 0 || strcmp(key, "SUBTABLE") == 0) continue;
		Variable* expect = lookup_selected_or_key(st, key);
		if (!expect) continue; /* no constraint on this key */
		Variable* cell = hash_table_lookup(row_map, key);
		if (!cell_equals(cell, expect)) return 0;
	}
	return 1;
}

static Variable* get_table_rows(SymbolTable* st, const char* table_id)
{
	if (!st || !table_id || !*table_id) return NULL;
	Variable* v = get_symbol(st, (char*)table_id);
	if (!v) return NULL;
	if (v->type == TYPE_ARRAY) return v;
	if (v->type == TYPE_MAP && v->data.map) {
		Variable* rows = hash_table_lookup(v->data.map, "rows");
		if (rows && rows->type == TYPE_ARRAY) return rows;
	}
	return NULL;
}

// Helper: Remove all dynamic (propagatable) keys for a specific table from the symbol table
void remove_dynamic_keys_for_table(const char* table_id, SymbolTable* st)
{
	Variable* rows = get_table_rows(st, table_id);
	if (!rows) {
		LogOnlyPrintfChar("Debug: No table rows for '%s' found for dynamic key removal\n", table_id);
		return;
	}

	char** dynamic_keys = NULL;
	size_t dk_count = 0, dk_capacity = 0;

	for (size_t r = 0; r < rows->data.array.size; r++) {
		Variable* rv = rows->data.array.elements[r];
		if (rv && rv->type == TYPE_MAP) {
			HashTable* rm = rv->data.map;
			for (size_t k = 0; k < rm->key_count; k++) {
				const char* key = rm->key_order[k];
				if (strcmp(key, "SEL_STRING") == 0) continue;
				/* --- NEW: never remove/revert symbols that name tables --- */
				if (_stricmp(key, table_id) == 0) continue;            /* do not touch our own table id */
				if (get_table_rows(st, key) != NULL) continue;         /* key matches any table id */
				Variable* cell = hash_table_lookup(rm, key);
				if (!cell || cell->type == TYPE_UNKNOWN || cell->type == TYPE_SUBTABLE) continue;

				int found = 0;
				for (size_t d = 0; d < dk_count; d++) {
					if (strcmp(dynamic_keys[d], key) == 0) { found = 1; break; }
				}
				if (!found) {
					if (dk_count >= dk_capacity) {
						size_t new_cap = dk_capacity ? dk_capacity * 2 : 8;
						char** new_dk = (char**)realloc(dynamic_keys, new_cap * sizeof(char*));
						if (!new_dk) {
							LogOnlyPrintfChar("Error: Realloc failed for dynamic keys in '%s'\n", table_id);
							goto cleanup;
						}
						dynamic_keys = new_dk;
						dk_capacity = new_cap;
					}
					dynamic_keys[dk_count] = _strdup(key);
					if (dynamic_keys[dk_count]) dk_count++;
				}
			}
		}
	}

cleanup:
	for (size_t d = 0; d < dk_count; d++) {
		const char* key = dynamic_keys[d];
		if (st_has_baseline(st, key)) {
			st_revert_to_baseline(st, key);
			LogOnlyPrintfChar("Reverted dynamic key '%s' to baseline in table '%s'\n", key, table_id);
		}
		else {
			remove_symbol(st, key);
			LogOnlyPrintfChar("Removed dynamic key '%s' from table '%s'\n", key, table_id);
		}
		free(dynamic_keys[d]);
	}
	free(dynamic_keys);
}

// Recursive: Clear a table, its symbols, and its downstream chain
ProError clear_chain(char* table_id, SymbolTable* st, char* dialog) {
	if (!table_id || !st || !dialog) return PRO_TK_BAD_INPUTS;

	LogOnlyPrintfChar("Clearing chain starting from table '%s'\n", table_id);

	// Deselect any rows in the table
	char** empty_rows = NULL;
	ProError status = ProUITableSelectednamesSet(dialog, table_id, 0, empty_rows);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Warning: Failed to deselect rows in '%s' (error: %d)\n", table_id, status);
	}

	// Clear contents and hide the table
	status = ClearTableContents(dialog, table_id, 1);  // hide=1
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Failed to clear/hide table '%s'\n", table_id);
		return status;
	}
	// Hide the parent drawing area (using consistent naming)
	char da_name[128];
	snprintf(da_name, sizeof(da_name), "table_layout_%s", table_id);
	status = ProUIDrawingareaHide(dialog, da_name);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Warning: Failed to hide drawing area '%s' for table '%s' (error: %d)\n", da_name, table_id, status);
	}
	else {
		LogOnlyPrintfChar("Debug: Successfully hid drawing area '%s'\n", da_name);
	}


	// Remove dynamic keys propagated from this table
	remove_dynamic_keys_for_table(table_id, st);

	// Get the sub_key for this table's downstream SUBTABLE (if any)
	char sub_key[256];
	snprintf(sub_key, sizeof(sub_key), "_subtable_of_%s", table_id);
	LogOnlyPrintfChar("Debug: Looking for next subtable using key '%s'\n", sub_key);

	char* next_sub_id = NULL;
	Variable* sub_var = get_symbol(st, sub_key);
	if (sub_var && sub_var->type == TYPE_STRING && sub_var->data.string_value && strlen(sub_var->data.string_value) > 0) {
		next_sub_id = _strdup(sub_var->data.string_value);
		LogOnlyPrintfChar("Debug: Found next subtable '%s' for '%s'\n", next_sub_id, table_id);
	}
	else {
		LogOnlyPrintfChar("Debug: No next subtable found for '%s'\n", table_id);
	}

	// Remove the tracking symbol
	remove_symbol(st, sub_key);

	// Recurse to next in chain if exists
	if (next_sub_id) {
		status = clear_chain(next_sub_id, st, dialog);
		free(next_sub_id);
		if (status != PRO_TK_NO_ERROR) return status;
	}

	return PRO_TK_NO_ERROR;
}

ProError build_table_from_sym(char* dialog, char* table_id, SymbolTable* st)
{
	ProError status;

	Variable* rows = get_table_rows(st, table_id);
	if (!rows || rows->data.array.size == 0) {
		ProPrintfChar("Error: Table '%s' not found or empty in symbol table\n", table_id);
		return PRO_TK_BAD_INPUTS;
	}
	/* Wrapper holds metadata like columns[], filter_column, filter_only_column (may be NULL) */
	Variable* wrapper = get_table_wrapper(st, table_id);

	/* Generate unique layout name for this table */
	char da_name[128];
	snprintf(da_name, sizeof(da_name), "table_layout_%s", table_id);

	/* Detect if UI table already exists */
	char** existing_rows = NULL;
	int existing_row_count = 0;
	status = ProUITableRownamesGet(dialog, table_id, &existing_row_count, &existing_rows);
	bool table_exists = (status == PRO_TK_NO_ERROR);

	if (table_exists) {
		status = ClearTableContents(dialog, table_id, 0); /* keep shown */
		if (status != PRO_TK_NO_ERROR) return status;

		status = ProUITableShow(dialog, table_id);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Failed to show existing table %s (error: %d)", table_id, status);
			return status;
		}

		status = ProUIDrawingareaShow(dialog, da_name);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Warning: Failed to show drawing area '%s' for table '%s' (error: %d)\n",
				da_name, table_id, status);
		}
		else {
			LogOnlyPrintfChar("Debug: Successfully showed drawing area '%s'\n", da_name);
		}
	}
	else {
		ProUIGridopts grid;
		memset(&grid, 0, sizeof(grid));
		grid.row = 0;
		grid.column = ++dynamic_table_count;
		grid.horz_cells = 1;
		grid.vert_cells = 1;
		grid.attach_bottom = PRO_B_TRUE;
		grid.attach_left = PRO_B_TRUE;
		grid.attach_right = PRO_B_FALSE;
		grid.attach_top = PRO_B_TRUE;
		grid.horz_resize = PRO_B_TRUE;
		grid.vert_resize = PRO_B_TRUE;
		grid.left_offset = 5;

		status = ProUILayoutDrawingareaAdd(dialog, "individual_table", da_name, &grid);
		status = ProUIDrawingareaTableAdd(dialog, da_name, table_id);
		if (status != PRO_TK_NO_ERROR) {
			LogOnlyPrintfChar("Could not add table to individual_table layout");
			return status;
		}

		ProUITablePositionSet(dialog, table_id, 0, 0);
		ProUITableUseScrollbarswhenNeeded(dialog, table_id);

		int da_height; int da_width;
		ProUIDrawingareaSizeGet(dialog, da_name, &da_width, &da_height);
		ProUITableSizeSet(dialog, table_id, da_width, da_height);

		status = ProUITableSelectionpolicySet(dialog, table_id, PROUISELPOLICY_SINGLE);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Error: Could not set row selection policy for '%s'\n", table_id);
			return status;
		}

		status = ProUITableAutohighlightEnable(dialog, table_id);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Warning: Could not enable row auto-highlighting for '%s'\n", table_id);
		}
	}

	/* Insert one visible column */
	char col0_buf[32] = "COL_0";
	char* col_ptrs[1] = { col0_buf };
	status = ProUITableColumnsInsert(dialog, table_id, NULL, 1, col_ptrs);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not insert column for '%s'\n", table_id);
		return status;
	}

	/* Compute visible row indices, honoring FILTER_COLUMN / FILTER_ONLY_COLUMN if present */
	size_t total = rows->data.array.size;
	size_t* vis_idx = (size_t*)malloc(total * sizeof(size_t));
	if (!vis_idx) return PRO_TK_GENERAL_ERROR;

	size_t vis_count = 0;
	for (size_t r = 0; r < total; ++r) {
		Variable* rv = rows->data.array.elements[r];
		if (!rv || rv->type != TYPE_MAP) continue;
		HashTable* row_map = rv->data.map;

		if (wrapper) {
			if (!row_passes_filter(st, wrapper, row_map)) continue; /* filtered out */
		}
		/* Keep this row */
		vis_idx[vis_count++] = r;
	}

	/* Create ROW_<original_index> names for only the visible rows */
	char** row_ptrs = NULL;
	if (vis_count > 0) {
		row_ptrs = (char**)malloc(vis_count * sizeof(char*));
		if (!row_ptrs) {
			free(vis_idx);
			return PRO_TK_GENERAL_ERROR;
		}
		for (size_t i = 0; i < vis_count; i++) {
			row_ptrs[i] = (char*)malloc(32);
			if (!row_ptrs[i]) {
				for (size_t j = 0; j < i; j++) free(row_ptrs[j]);
				free(row_ptrs);
				free(vis_idx);
				return PRO_TK_GENERAL_ERROR;
			}
			/* IMPORTANT: keep original index in the UI name so callback can map back */
			snprintf(row_ptrs[i], 32, "ROW_%zu", vis_idx[i]);
		}

		status = ProUITableRowsInsert(dialog, table_id, NULL, (int)vis_count, row_ptrs);
		if (status != PRO_TK_NO_ERROR) {
			ProPrintfChar("Error: Could not insert rows for '%s'\n", table_id);
			for (size_t i = 0; i < vis_count; i++) free(row_ptrs[i]);
			free(row_ptrs);
			free(vis_idx);
			return status;
		}
	}

	/* Labels from SEL_STRING in each visible row */
	for (size_t i = 0; i < vis_count; i++) {
		size_t r = vis_idx[i];
		Variable* row_var = rows->data.array.elements[r];
		if (row_var && row_var->type == TYPE_MAP) {
			Variable* label_var = hash_table_lookup(row_var->data.map, "SEL_STRING");
			char* label_utf8 = (label_var && label_var->type == TYPE_STRING && label_var->data.string_value)
				? label_var->data.string_value : "";
			wchar_t* label_w = char_to_wchar(label_utf8);
			status = ProUITableCellLabelSet(dialog, table_id, row_ptrs[i], col0_buf,
				label_w ? label_w : L"");
			if (label_w) free(label_w);
			if (status != PRO_TK_NO_ERROR) {
				ProPrintfChar("Error: Failed to set cell label for row %zu in '%s'\n", r, table_id);
				for (size_t j = 0; j < vis_count; j++) free(row_ptrs[j]);
				free(row_ptrs);
				free(vis_idx);
				return status;
			}
		}
	}

	/* Free UI row name buffers */
	if (vis_count > 0) {
		for (size_t i = 0; i < vis_count; i++) free(row_ptrs[i]);
		free(row_ptrs);
	}
	free(vis_idx);

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

	/* Fetch selection (single-select assumed) */
	char** selected_rows = NULL;
	int num_selected = 0;
	ProError status = ProUITableSelectednamesGet(dialog, table, &num_selected, &selected_rows);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Failed to get selected rows in table '%s'\n", table);
		if (selected_rows) ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}

	/* Track current subtable for this table */
	char sub_key[256];
	snprintf(sub_key, sizeof(sub_key), "_subtable_of_%s", table);

	/* Deselection: clear chain and dynamic keys */
	if (num_selected == 0) {
		LogOnlyPrintfChar("Debug: Deselection in table '%s'; clearing chain\n", table);
		char* old_sub = NULL;
		Variable* old_v = get_symbol(st, sub_key);
		if (old_v && old_v->type == TYPE_STRING && old_v->data.string_value) {
			old_sub = _strdup(old_v->data.string_value);
		}
		remove_symbol(st, sub_key);
		remove_dynamic_keys_for_table(table, st);
		if (old_sub) { clear_chain(old_sub, st, dialog); free(old_sub); }
		if (selected_rows) ProArrayFree((ProArray*)&selected_rows);
		EPA_ReactiveRefresh();
		return PRO_TK_NO_ERROR;
	}

	if (!selected_rows || !selected_rows[0]) {
		ProPrintfChar("Error: Invalid selected rows data in table '%s'\n", table);
		if (selected_rows) ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}

	/* Copy selected row name */
	char* selected_row_name = _strdup(selected_rows[0]);
	if (!selected_row_name) {
		ProPrintfChar("Error: Failed to copy selected row name\n");
		ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	LogOnlyPrintfChar("Debug: Selected row in table '%s': %s\n", table, selected_row_name);

	/* Unified rows view (ARRAY or MAP{rows}) */
	Variable* rows = get_table_rows(st, table);
	if (!rows) {
		ProPrintfChar("Error: Table '%s' not found in symbol table or has no rows\n", table);
		free(selected_row_name);
		ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}

	/* =======================
	   Resolve original data index
	   ======================= */
	size_t selected_row_index = (size_t)-1;

	/* Primary: parse original index from "ROW_<n>" */
	if (selected_row_name && strncmp(selected_row_name, "ROW_", 4) == 0) {
		char* end = NULL;
		unsigned long n = strtoul(selected_row_name + 4, &end, 10);
		if (end && *end == '\0') {
			selected_row_index = (size_t)n;
		}
	}

	/* Fallback: legacy tables (no filtering / sequential names) — use UI position */
	if (selected_row_index == (size_t)-1) {
		char** all_row_names = NULL;
		int num_rows = 0;
		ProError st2 = ProUITableRownamesGet(dialog, table, &num_rows, &all_row_names);
		if (st2 == PRO_TK_NO_ERROR && all_row_names) {
			for (int i = 0; i < num_rows; i++) {
				if (strcmp(all_row_names[i], selected_row_name) == 0) {
					selected_row_index = (size_t)i;
					break;
				}
			}
			ProArrayFree((ProArray*)&all_row_names);
		}
	}

	if (selected_row_index == (size_t)-1 || selected_row_index >= rows->data.array.size) {
		ProPrintfChar("Error: Selected row '%s' resolved to invalid index\n", selected_row_name);
		free(selected_row_name);
		ProArrayFree((ProArray*)&selected_rows);
		return PRO_TK_GENERAL_ERROR;
	}
	LogOnlyPrintfChar("Debug: Selected row index (data): %zu\n", selected_row_index);

	/* Cleanup selection buffers from UI */
	free(selected_row_name);
	ProArrayFree((ProArray*)&selected_rows);

	/* Use the ORIGINAL index to fetch the data row */
	Variable* row_var = rows->data.array.elements[selected_row_index];
	if (!row_var || row_var->type != TYPE_MAP) {
		ProPrintfChar("Error: Row at index %zu in table '%s' is not a MAP\n", selected_row_index, table);
		return PRO_TK_GENERAL_ERROR;
	}

	remove_dynamic_keys_for_table(table, st);

	/* =======================
	   NEW: Remember previously-built downstream head
	   ======================= */
	char* old_sub = NULL;
	{
		Variable* old_v = get_symbol(st, sub_key);
		if (old_v && old_v->type == TYPE_STRING && old_v->data.string_value) {
			old_sub = _strdup(old_v->data.string_value);
		}
	}

	/* Scan row map: export non-SUBTABLE cells to globals; capture SUBTABLE id */
	char* subtable_id = NULL;
	char* subtable_key = NULL;
	HashTable* row_map = row_var->data.map;
	for (size_t i = 0; i < row_map->key_count; i++) {
		const char* key = row_map->key_order[i];
		Variable* cell = hash_table_lookup(row_map, key);
		if (!cell) continue;

		if (cell->type == TYPE_SUBTABLE) {
			if (cell->data.string_value && cell->data.string_value[0]) {
				subtable_id = _strdup(cell->data.string_value);
				subtable_key = _strdup(key);
			}
			continue;
		}

		if (strcmp(key, "SEL_STRING") == 0) continue; /* label only */
		const char* out_key = key;
		char alias_buf[192];
		if (_stricmp(out_key, table) == 0 || get_table_rows(st, out_key) != NULL) {
			/* e.g., STYLE -> STYLE_SELECTED; also avoids collisions with any table id */
			snprintf(alias_buf, sizeof(alias_buf), "%s_SELECTED", out_key);
			out_key = alias_buf;
		}

		Variable* global_var = (Variable*)malloc(sizeof(Variable));
		if (!global_var) continue;
		memset(global_var, 0, sizeof(*global_var));
		global_var->type = cell->type;

		switch (cell->type) {
		case TYPE_INTEGER: global_var->data.int_value = cell->data.int_value; break;
		case TYPE_BOOL:    global_var->data.int_value = cell->data.int_value; break;
		case TYPE_DOUBLE:  global_var->data.double_value = cell->data.double_value; break;
		case TYPE_STRING:
			global_var->data.string_value = _strdup(cell->data.string_value ? cell->data.string_value : "");
			if (!global_var->data.string_value) { free(global_var); continue; }
			break;
		default:
			free(global_var);
			continue;
		}
		set_symbol(st, (char*)out_key, global_var);
		EPA_MarkDirty(st, (const char*)out_key);
		LogOnlyPrintfChar("Set global '%s' from selected row in '%s'\n", key, table);
	}

	/* =======================
	   NEW: If new selection has no/different SUBTABLE, clear the old chain
	   ======================= */
	if (old_sub && (!subtable_id || strcmp(subtable_id, old_sub) != 0)) {
		LogOnlyPrintfChar("Reselection changed/removed SUBTABLE: clearing old chain '%s'\n", old_sub);
		(void)clear_chain(old_sub, st, dialog);   /* recursive clear of following tables */
	}
	if (old_sub) { free(old_sub); old_sub = NULL; }

	/* Update tracking symbol with new SUBTABLE (if present) */
	remove_symbol(st, sub_key);
	if (subtable_id && subtable_id[0]) {
		Variable* new_sub_v = (Variable*)malloc(sizeof(Variable));
		if (new_sub_v) {
			new_sub_v->type = TYPE_STRING;
			new_sub_v->data.string_value = _strdup(subtable_id);
			set_symbol(st, sub_key, new_sub_v);
			LogOnlyPrintfChar("Updated tracking '%s' to '%s'\n", sub_key, subtable_id);
		}
	}
	else {
		LogOnlyPrintfChar("Selection has no SUBTABLE; downstream cleared and tracking removed\n");
	}

	/* Build next table only if that id resolves to a rows array (ARRAY or MAP{rows}) */
	if (subtable_id) {
		Variable* next_rows = get_table_rows(st, subtable_id);
		if (next_rows) {
			LogOnlyPrintfChar("SUBTABLE '%s' matches a table in symbol table; building dynamically.\n", subtable_id);
			if (!dialog) {
				ProPrintfChar("Error: No active DialogState for dynamic table build\n");
				free(subtable_id);
				if (subtable_key) free(subtable_key);
				return PRO_TK_GENERAL_ERROR;
			}
			status = build_table_from_sym(dialog, subtable_id, st);
			if (status != PRO_TK_NO_ERROR) {
				ProPrintfChar("Error: Failed to build dynamic table '%s'\n", subtable_id);
				free(subtable_id);
				if (subtable_key) free(subtable_key);
				return status;
			}
			EPA_ReactiveRefresh();
		}
		else {
			LogOnlyPrintfChar("SUBTABLE '%s' does not match a table; no dynamic build.\n", subtable_id);
		}
		free(subtable_id);
		if (subtable_key) free(subtable_key);
	}

	EPA_ReactiveRefresh();
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
	char* drawarea_tableid = "drawarea_tableid";
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
	status = ProUILayoutDrawingareaAdd(state->dialog_name, state->indvidual_table.name, drawarea_tableid, &table_grid);
	if (status != PRO_TK_NO_ERROR) {
		ProPrintfChar("Error: Could not add table to layout\n");
		if (title_utf8) free(title_utf8);
		return status;
	}
	status = ProUIDrawingareaTableAdd(state->dialog_name, drawarea_tableid, table_id);
	if (status != PRO_TK_NO_ERROR)
	{
		ProPrintfChar("Could not build table within Drawingarea");
		return status;
	}
	status = ProUITablePositionSet(state->dialog_name, table_id, 0, 0);
	if (status != PRO_TK_NO_ERROR)
	{
		ProPrintfChar("Could not set position of table");
		return status;
	}
	// Store root table_id in symbol table for callback access
	Variable* root_table_var = (Variable*)malloc(sizeof(Variable));
	if (root_table_var) {
		root_table_var->type = TYPE_STRING;
		root_table_var->data.string_value = _strdup(table_id);
		set_symbol(st, "ROOT_TABLE_ID", root_table_var);
	}
	else {
		ProPrintfChar("Warning: Failed to allocate variable for ROOT_TABLE_ID\n");
	}
	ProUITableUseScrollbarswhenNeeded(state->dialog_name, table_id);
	// Done: lock to only first table
	state->root_table_built = 1;
	
	if (!state->root_table_id) {
		state->root_table_id = _strdup(table_id);
	}


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
		return status;
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
	strcpy_s(state.indvidual_table.name, sizeof(state.indvidual_table.name), "individual_table");

	state.gui_block = gui_block;
	state.tab_block = find_block(block_list, BLOCK_TAB);  // new
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
	grid_opts_main.attach_bottom = PRO_B_TRUE;  // Disable bottom attach to allow stacking
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
	grid_opts_table.attach_bottom = PRO_B_TRUE;  // Disable bottom attach for stacking
	grid_opts_table.attach_top = PRO_B_TRUE;
	grid_opts_table.attach_left = PRO_B_TRUE;
	grid_opts_table.attach_right = PRO_B_TRUE;
	grid_opts_table.row = 3;        // Row below main layout
	grid_opts_table.column = 0;
	grid_opts_table.horz_cells = 5; // Define a visible grid
	grid_opts_table.vert_cells = 1; // Consistent with main
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
	state.tab_block = tab_block;

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
		st_baseline_remember(st, node->name, var);   // keep original value
		existing = var;
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

/* ---------- helpers: symbol-table map access (local-only) ---------- */
static HashTable* sym_lookup_map(SymbolTable* st, const char* key)
{
	if (!st || !key) return NULL;
	Variable* v = get_symbol(st, (char*)key);
	if (!v || v->type != TYPE_MAP || !v->data.map) return NULL;
	return v->data.map;
}

static int map_get_int(HashTable* map, const char* key, int defv)
{
	if (!map || !key) return defv;
	Variable* v = hash_table_lookup(map, key);
	if (!v) return defv;
	switch (v->type) {
	case TYPE_INTEGER:
	case TYPE_BOOL:   return v->data.int_value;
	case TYPE_DOUBLE: return (int)v->data.double_value;
	default:          return defv;
	}
}

static const char* map_get_str(HashTable* map, const char* key)
{
	if (!map || !key) return NULL;
	Variable* v = hash_table_lookup(map, key);
	if (!v || v->type != TYPE_STRING) return NULL;
	return v->data.string_value;
}

/* Find this assignment's (assign_id, if_id) by consulting ASSIGNMENTS.
   Prefer an entry whose lhs_name == lhs_name and, if current_if_id>0,
   whose if_id == current_if_id. Falls back to the first matching lhs. */
static int scan_assign_registry_for(SymbolTable* st, const char* lhs_name, int current_if_id, int* out_assign_id, int* out_if_id)
{
	if (out_assign_id) *out_assign_id = 0;
	if (out_if_id)     *out_if_id = 0;
	if (!st || !lhs_name) return 0;

	HashTable* reg = sym_lookup_map(st, "ASSIGNMENTS");
	if (!reg) return 0;

	int found_any = 0;
	int found_id = 0;
	int found_if = 0;

	for (size_t k = 0; k < reg->key_count; ++k) {
		const char* kname = reg->key_order[k];
		if (!kname) continue;
		if (strncmp(kname, "ASSIGN_", 7) != 0) continue;

		Variable* v = hash_table_lookup(reg, kname);
		if (!v || v->type != TYPE_MAP || !v->data.map) continue;

		HashTable* m = v->data.map;
		const char* lhs = map_get_str(m, "lhs_name");
		if (!lhs || strcmp(lhs, lhs_name) != 0) continue;

		int id = map_get_int(m, "assign_id", 0);
		int ifid = map_get_int(m, "if_id", 0);

		if (current_if_id > 0) {
			if (ifid == current_if_id) {
				if (out_assign_id) *out_assign_id = id;
				if (out_if_id)     *out_if_id = ifid;
				return 1; /* perfect match */
			}
			/* else keep scanning; we may still record a fallback */
		}

		if (!found_any) {
			found_any = 1;
			found_id = id;
			found_if = ifid;
		}
	}

	if (found_any) {
		if (out_assign_id) *out_assign_id = found_id;
		if (out_if_id)     *out_if_id = found_if;
		return 1;
	}
	return 0;
}

/* Gate decision:
   - __TARGET_ASSIGN_ID (optional): if set (>0), only run the matching assignment id.
   - __CURRENT_IF_ID     (optional): if set (>0), prefer/limit to that IF's assignments.
   If metadata is missing, we do not block execution. */
static int should_gate_assignment(SymbolTable* st, int node_assign_id, int node_if_id)
{
	int target_assign = 0;
	{
		Variable* v = get_symbol(st, "__TARGET_ASSIGN_ID");
		if (v) {
			if (v->type == TYPE_INTEGER || v->type == TYPE_BOOL)
				target_assign = v->data.int_value;
			else if (v->type == TYPE_DOUBLE)
				target_assign = (int)v->data.double_value;
		}
	}

	int current_if = 0;
	{
		Variable* v = get_symbol(st, "__CURRENT_IF_ID");
		if (v) {
			if (v->type == TYPE_INTEGER || v->type == TYPE_BOOL)
				current_if = v->data.int_value;
			else if (v->type == TYPE_DOUBLE)
				current_if = (int)v->data.double_value;
		}
	}

	if (target_assign > 0 && node_assign_id > 0 && node_assign_id != target_assign) {
		LogOnlyPrintfChar("Skip assignment id=%d (target=%d)\n", node_assign_id, target_assign);
		return 0;
	}

	/* If a current IF is declared, prefer to run only assignments tied to it.
	   If the assignment has no known if_id, do not block. */
	if (current_if > 0 && node_if_id > 0 && node_if_id != current_if) {
		LogOnlyPrintfChar("Skip assignment id=%d (if_id=%d) due to __CURRENT_IF_ID=%d\n",
			node_assign_id, node_if_id, current_if);
		return 0;
	}

	return 1;
}

/* -------------------------- REVISED execute_assignment -------------------------- */
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

	Variable* dst = get_symbol(st, (char*)lhs_name);
	if (!dst) {
		ProPrintfChar("Error: Assignment to undeclared variable '%s'\n", lhs_name);
		return PRO_TK_GENERAL_ERROR;
	}

	/* Resolve metadata from symbol table so we can gate */
	int current_if_id = 0;
	{
		Variable* v = get_symbol(st, "__CURRENT_IF_ID");
		if (v) {
			if (v->type == TYPE_INTEGER || v->type == TYPE_BOOL)
				current_if_id = v->data.int_value;
			else if (v->type == TYPE_DOUBLE)
				current_if_id = (int)v->data.double_value;
		}
	}

	int meta_assign_id = 0;
	int meta_if_id = 0;
	(void)scan_assign_registry_for(st, lhs_name, current_if_id, &meta_assign_id, &meta_if_id); /* best effort */

	if (!should_gate_assignment(st, meta_assign_id, meta_if_id)) {
		/* Politely decline to run; not an error */
		return PRO_TK_NO_ERROR;
	}

	/* Evaluate RHS and assign with type-aware coercion.
	   For strings, keep your SUB_PICTURE-friendly concatenation semantics. */
	if (dst->type == TYPE_STRING) {
		char* sval = NULL;
		if (evaluate_to_string(node->rhs, st, &sval) != 0 || !sval) {
			ProPrintfChar("Error: Failed to evaluate string RHS for '%s'\n", lhs_name);
			return PRO_TK_GENERAL_ERROR;
		}
		if (dst->data.string_value) free(dst->data.string_value);
		dst->data.string_value = sval; /* take ownership */
		LogOnlyPrintfChar("Assignment[%d] (if=%d): %s := \"%s\"\n",
			meta_assign_id, meta_if_id, lhs_name,
			dst->data.string_value ? dst->data.string_value : "");
		return PRO_TK_NO_ERROR;
	}

	/* General expression path */
	Variable* rhs = NULL;
	int est = evaluate_expression(node->rhs, st, &rhs);
	if (est != 0 || !rhs) {
		ProPrintfChar("Error: Failed to evaluate RHS for '%s'\n", lhs_name);
		return PRO_TK_GENERAL_ERROR;
	}

	/* Type-compatible write with simple coercions */
	switch (dst->type) {
	case TYPE_INTEGER:
	case TYPE_BOOL:
		if (rhs->type == TYPE_INTEGER || rhs->type == TYPE_BOOL) {
			dst->data.int_value = rhs->data.int_value;
		}
		else if (rhs->type == TYPE_DOUBLE) {
			dst->data.int_value = (int)rhs->data.double_value;
		}
		else {
			free_variable(rhs);
			ProPrintfChar("Error: Type mismatch assigning to '%s'\n", lhs_name);
			return PRO_TK_GENERAL_ERROR;
		}
		break;

	case TYPE_DOUBLE:
		if (rhs->type == TYPE_DOUBLE) {
			dst->data.double_value = rhs->data.double_value;
		}
		else if (rhs->type == TYPE_INTEGER || rhs->type == TYPE_BOOL) {
			dst->data.double_value = (double)rhs->data.int_value;
		}
		else {
			free_variable(rhs);
			ProPrintfChar("Error: Type mismatch assigning to '%s'\n", lhs_name);
			return PRO_TK_GENERAL_ERROR;
		}
		break;

	default:
		free_variable(rhs);
		ProPrintfChar("Error: Unsupported LHS type for '%s'\n", lhs_name);
		return PRO_TK_GENERAL_ERROR;
	}

	LogOnlyPrintfChar("Assignment[%d] (if=%d): %s := (type %d)\n",
		meta_assign_id, meta_if_id, lhs_name, dst->type);
	free_variable(rhs);
	return PRO_TK_NO_ERROR;
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
		case COMMAND_CHECKBOX_PARAM: {
			CheckboxParamNode* cb = (CheckboxParamNode*)cmd->data;
			if (cb && cb->parameter) {
				(void)set_checkbox_param_enabled(dialog, ctx->st, cb->parameter, enabled);
			}
			break;
		}
		case COMMAND_USER_INPUT_PARAM: {
			UserInputParamNode* uip = (UserInputParamNode*)cmd->data;
			if (uip && uip->parameter) {
				(void)set_inputpanel_param_enabled(dialog, ctx->st, uip->parameter, enabled);
			}
			break;
		}
		case COMMAND_RADIOBUTTON_PARAM: {
			RadioButtonParamNode* rbp = (RadioButtonParamNode*)cmd->data;
			if (rbp && rbp->parameter) {
				(void)set_radiobutton_param_enabled(dialog, ctx->st, rbp->parameter, enabled);
			}
			break;
		}
		case COMMAND_SHOW_PARAM: {
			ShowParamNode* sp = (ShowParamNode*)cmd->data;
			if (sp && sp->parameter && sp->on_picture) {  // Restrict to ON_PICTURE only
				(void)set_show_param_enabled(dialog, ctx->st, sp->parameter, enabled);
			}
			else
			{
				if (sp && sp->parameter)
				{
					(void)set_show_param_enabled(dialog, ctx->st, sp->parameter, enabled);
				}
			}
			break;
		}
		default:
			break; // Other components are created only when their branch executes 
		}
	}
	return PRO_TK_NO_ERROR;
}

/*=================================================*
 * recompute_if_gates_only (target-aware)
 * - If __TARGET_IF_ID == 0: behave exactly as before (all IFs).
 * - If __TARGET_IF_ID != 0: only recompute/gate that IF id.
 *=================================================*/
static ProError recompute_if_gates_only(Block* blk, ExecContext* ctx)
{
	if (!blk || !ctx || !ctx->st) return PRO_TK_NO_ERROR;

	int target_if_id = 0;
	{
		Variable* v = get_symbol(ctx->st, "__TARGET_IF_ID");
		if (v && v->type == TYPE_INTEGER) target_if_id = v->data.int_value;
	}

	for (size_t i = 0; i < blk->command_count; ++i) {
		CommandNode* cmd = blk->commands[i];
		if (!cmd) continue;

		if (cmd->type == COMMAND_IF) {
			IfNode* node = (IfNode*)cmd->data;
			int gate_id = if_gate_id_of(node, g_active_st);

			/* skip unrelated IFs when a target is requested */
			if (target_if_id != 0 && gate_id != target_if_id) {
				/* do not recurse into children, whole tree is skipped */
				continue;
			}

			/* evaluate winner */
			size_t winning = (size_t)-1;
			for (size_t b = 0; b < node->branch_count; ++b) {
				Variable* cond_val = NULL;
				int est = evaluate_expression(node->branches[b]->condition, ctx->st, &cond_val);
				if (est != 0 || !cond_val) { return PRO_TK_GENERAL_ERROR; }

				/* numeric/bool truthiness as in your engine */
				{
					int truth = 0;
					if (cond_val->type == TYPE_BOOL || cond_val->type == TYPE_INTEGER)
						truth = (cond_val->data.int_value != 0);
					else if (cond_val->type == TYPE_DOUBLE)
						truth = (cond_val->data.double_value != 0.0);
					free_variable(cond_val);
					if (truth) { winning = b; break; }
				}
			}

			/* read previous winner */
			char key_prev[64]; snprintf(key_prev, sizeof(key_prev), "IF_STATE.%d.winner", gate_id);
			Variable* prev_v = get_symbol(ctx->st, key_prev);
			int prev = -1;
			if (prev_v && prev_v->type == TYPE_INTEGER) prev = prev_v->data.int_value;

			/* store current winner */
			{
				Variable* nv = (Variable*)malloc(sizeof(Variable));
				if (!nv) return PRO_TK_GENERAL_ERROR;
				nv->type = TYPE_INTEGER;
				nv->data.int_value = (int)winning;
				set_symbol(ctx->st, key_prev, nv);
			}

			/* mark dirty if changed */
			if ((int)winning != prev) {
				char key_dirty[64]; snprintf(key_dirty, sizeof(key_dirty), "IF_STATE.%d.dirty", gate_id);
				Variable* dv = (Variable*)malloc(sizeof(Variable));
				if (!dv) return PRO_TK_GENERAL_ERROR;
				dv->type = TYPE_INTEGER;
				dv->data.int_value = 1;
				set_symbol(ctx->st, key_dirty, dv);
			}

			/* NEW: gate OFF all branches (and ELSE), then gate ON only the winner */
			if (ctx->ui) {
				/* First, force every branch OFF */
				for (size_t b = 0; b < node->branch_count; ++b) {
					Block off = { 0 };
					off.command_count = node->branches[b]->command_count;
					off.commands = node->branches[b]->commands;
					(void)apply_ui_gate_to_block(&off, ctx, PRO_B_FALSE);
				}
				if (node->else_command_count > 0) {
					Block offe = (Block){ 0 };
					offe.command_count = node->else_command_count;
					offe.commands = node->else_commands;
					(void)apply_ui_gate_to_block(&offe, ctx, PRO_B_FALSE);
				}

				/* Now, enable only the winner (or ELSE if no winner) */
				if (winning != (size_t)-1) {
					IfBranch* br = node->branches[winning];
					Block on = (Block){ 0 };
					on.command_count = br->command_count;
					on.commands = br->commands;
					(void)apply_ui_gate_to_block(&on, ctx, PRO_B_TRUE);
				}
				else if (node->else_command_count > 0) {
					Block on = (Block){ 0 };
					on.command_count = node->else_command_count;
					on.commands = node->else_commands;
					(void)apply_ui_gate_to_block(&on, ctx, PRO_B_TRUE);
				}
			}

			/* do not recurse when targeted; recursion would reach unrelated IFs below */
			if (target_if_id == 0) {
				/* all IFs path: keep walking nested blocks */
				if (winning != (size_t)-1) {
					IfBranch* br = node->branches[winning];
					Block nb = { 0 }; nb.command_count = br->command_count; nb.commands = br->commands;
					(void)recompute_if_gates_only(&nb, ctx);
				}
				else if (node->else_command_count > 0) {
					Block eb = { 0 }; eb.command_count = node->else_command_count; eb.commands = node->else_commands;
					(void)recompute_if_gates_only(&eb, ctx);
				}
			}
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
		(void)prepare_if_checkbox_param(node, ctx->ui, ctx->st);
		(void)prepare_if_user_input_param(node, ctx->ui, ctx->st);
		(void)prepare_if_radiobutton_param(node, ctx->ui, ctx->st);
		(void)prepare_if_show_param(node, ctx->ui, ctx->st);
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

		/* NEW: push __CURRENT_IF_ID for this IF while we execute its branch */
		int gate_id = if_gate_id_of(node, ctx->st);
		int old_cur = 0, had_old = st_get_int(ctx->st, "__CURRENT_IF_ID", &old_cur);
		st_put_int(ctx->st, "__CURRENT_IF_ID", gate_id);

		if (ctx->ui) {
			Block temp = { 0 };
			temp.command_count = br->command_count;
			temp.commands = br->commands;
			(void)apply_ui_gate_to_block(&temp, ctx, PRO_B_TRUE);
		}

		for (size_t i = 0; i < br->command_count; ++i) {
			ProError s = exec_command_in_context(br->commands[i], ctx);
			if (s != PRO_TK_NO_ERROR) {
				/* restore before returning */
				if (had_old) st_put_int(ctx->st, "__CURRENT_IF_ID", old_cur);
				else remove_symbol(ctx->st, "__CURRENT_IF_ID");
				return s;
			}
		}

		/* restore previous __CURRENT_IF_ID */
		if (had_old) st_put_int(ctx->st, "__CURRENT_IF_ID", old_cur);
		else remove_symbol(ctx->st, "__CURRENT_IF_ID");

		return PRO_TK_NO_ERROR;
	}

	/* ELSE branch */
	if (node->else_command_count > 0) {
		/* NEW: push __CURRENT_IF_ID for this IF while we execute ELSE */
		int gate_id = if_gate_id_of(node, ctx->st);
		int old_cur = 0, had_old = st_get_int(ctx->st, "__CURRENT_IF_ID", &old_cur);
		st_put_int(ctx->st, "__CURRENT_IF_ID", gate_id);

		if (ctx->ui) {
			Block temp = { 0 };
			temp.command_count = node->else_command_count;
			temp.commands = node->else_commands;
			(void)apply_ui_gate_to_block(&temp, ctx, PRO_B_TRUE);
		}

		for (size_t i = 0; i < node->else_command_count; ++i) {
			ProError s = exec_command_in_context(node->else_commands[i], ctx);
			if (s != PRO_TK_NO_ERROR) {
				if (had_old) st_put_int(ctx->st, "__CURRENT_IF_ID", old_cur);
				else remove_symbol(ctx->st, "__CURRENT_IF_ID");
				return s;
			}
		}

		if (had_old) st_put_int(ctx->st, "__CURRENT_IF_ID", old_cur);
		else remove_symbol(ctx->st, "__CURRENT_IF_ID");
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
	/* NEW: top-level executes outside any IF; make that explicit */
	st_put_int(st, "__CURRENT_IF_ID", 0);

	for (size_t i = 0; i < asm_block->command_count; i++)
	{
		CommandNode* cmd = asm_block->commands[i];
		execute_command(cmd, st, block_list);
	}
}



/* -------------------------------------------------------------------------
 * Dirty UI param journal and targeted reactive refresh
 * ------------------------------------------------------------------------- */

static Variable* ensure_string_array(SymbolTable* st, const char* key)
{
	Variable* v = get_symbol(st, (char*)key);
	if (!v) {
		v = (Variable*)calloc(1, sizeof(Variable));
		if (!v) return NULL;
		v->type = TYPE_ARRAY;
		v->data.array.size = 0;
		v->data.array.elements = NULL;
		set_symbol(st, (char*)key, v);
	}
	else if (v->type != TYPE_ARRAY) {
		return NULL;
	}
	return v;
}

static int string_array_contains(Variable* arr, const char* s)
{
	if (!arr || arr->type != TYPE_ARRAY || !s) return 0;
	for (size_t i = 0; i < arr->data.array.size; ++i) {
		Variable* it = arr->data.array.elements[i];
		if (it && it->type == TYPE_STRING && it->data.string_value &&
			strcmp(it->data.string_value, s) == 0) return 1;
	}
	return 0;
}

void EPA_MarkDirty(SymbolTable* st, const char* param_name)
{
	if (!st || !param_name || !param_name[0]) return;
	Variable* arr = ensure_string_array(st, "DIRTY_UI_PARAMS");
	if (!arr) return;

	if (!string_array_contains(arr, param_name)) {
		Variable* sv = (Variable*)calloc(1, sizeof(Variable));
		if (!sv) return;
		sv->type = TYPE_STRING;
		sv->data.string_value = _strdup(param_name);
		if (!sv->data.string_value) { free(sv); return; }

		size_t n = arr->data.array.size + 1;
		Variable** grown = (Variable**)realloc(arr->data.array.elements, n * sizeof(*grown));
		if (!grown) { free(sv->data.string_value); free(sv); return; }
		arr->data.array.elements = grown;
		arr->data.array.elements[n - 1] = sv;
		arr->data.array.size = n;
	}
}

/* fetch if_gate_id previously attached by pretag_if_gated(...) */
static int gate_for_param(SymbolTable* st, const char* param_name)
{
	Variable* v = get_symbol(st, (char*)param_name);
	if (!v || v->type != TYPE_MAP || !v->data.map) return 0;

	Variable* tag = hash_table_lookup(v->data.map, "if_gate_id");
	if (tag && tag->type == TYPE_INTEGER && tag->data.int_value > 0) {
		return tag->data.int_value;
	}
	return 0; /* unknown -> full fallback */
}

/* de-dupe small int set */
static int contains_id(const int* ids, size_t n, int id)
{
	for (size_t i = 0; i < n; ++i) if (ids[i] == id) return 1;
	return 0;
}

/* REPLACEMENT for EPA_ReactiveRefresh: targeted, multi-gate aware */
void EPA_ReactiveRefresh(void)
{
	if (!g_active_state || !g_active_state->dialog_name || !g_active_st || !g_active_state->gui_block)
		return;

	/* Build target set from DIRTY_UI_PARAMS; fall back to {0} if empty. */
	int targets[64]; size_t tcount = 0;

	Variable* arr = get_symbol(g_active_st, "DIRTY_UI_PARAMS");
	if (arr && arr->type == TYPE_ARRAY && arr->data.array.size > 0) {
		for (size_t i = 0; i < arr->data.array.size; ++i) {
			Variable* it = arr->data.array.elements[i];
			if (!it || it->type != TYPE_STRING || !it->data.string_value) continue;
			int gid = gate_for_param(g_active_st, it->data.string_value);
			if (!contains_id(targets, tcount, gid) && tcount < (sizeof(targets) / sizeof(targets[0]))) {
				targets[tcount++] = gid; /* gid==0 means “unknown” -> full pass */
			}
		}
		/* Clear the journal so next refresh considers only new dirties */
		remove_symbol(g_active_st, "DIRTY_UI_PARAMS");
	}

	if (tcount == 0) {
		targets[tcount++] = 0; /* full pass */
	}

	/* Run one targeted pass per gate id */
	for (size_t k = 0; k < tcount; ++k) {
		const int target_if_id = targets[k];

		/* Publish the target so your walkers can honor it */
		st_put_int(g_active_st, "__TARGET_IF_ID", target_if_id);

		/* Pictures:
		   - full pass: wipe all then re-emit
		   - targeted: prune only pictures from this gate, then re-emit winners */
		if (target_if_id == 0) {
			remove_symbol(g_active_st, "SUB_PICTURES"); /* full wipe only on full rebuild */
		}
		rebuild_sub_pictures_only(g_active_state->gui_block, g_active_st); /* targeted-aware walk */

		/* Redraw backing area once per pass (cheap) */
		addpicture(g_active_state->dialog_name, "draw_area", (ProAppData)g_active_st);

		/* Recompute active IF winners (reactive flag honored inside your exec ctx) */
		ExecContext ctx = { 0 };
		ctx.st = g_active_st;
		ctx.ui = g_active_state;
		ctx.reactive = 1;
		recompute_if_gates_only(g_active_state->gui_block, &ctx); /* existing helper */  /* :contentReference[oaicite:7]{index=7} */

		/* Re-apply assignments (winner-aware, targeted by __TARGET_IF_ID) */
		update_assignments_only(g_active_state->gui_block, g_active_st);  /* :contentReference[oaicite:8]{index=8} */

		/* Refresh bound readouts (winner-aware, targeted) */
		refresh_all_show_params(g_active_state->gui_block, g_active_state->dialog_name, g_active_st); /* :contentReference[oaicite:9]{index=9} */
	}

	/* Validate once at the end */
	validate_ok_button(g_active_state->dialog_name, g_active_st); /* same as before */  /* :contentReference[oaicite:10]{index=10} */

	/* Optional: clear targeting after pass */
	remove_symbol(g_active_st, "__TARGET_IF_ID");
}

