#ifndef SEMANTIC_ANALYSIS_H
#define SEMANTIC_ANALYSIS_H


#include "utility.h"
#include "syntaxanalysis.h"


int perform_semantic_analysis(BlockList* block_list, SymbolTable* st);
int evaluate_expression(ExpressionNode* expr, SymbolTable* st, Variable** result);
int evaluate_expression(ExpressionNode* expr, SymbolTable* st, Variable** result);
int evaluate_to_string(ExpressionNode* expr, SymbolTable* st, char** result);
int evaluate_to_int(ExpressionNode* expr, SymbolTable* st, long* result);
int evaluate_to_double(ExpressionNode* expr, SymbolTable* st, double* result);
VariableType map_variable_type(VariableType vtype, ParameterSubType pstype);
void set_default_value(Variable* var);







#endif // !SEMANTIC_ANALYSIS_H
