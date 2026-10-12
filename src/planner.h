/*
 * planner.h — AST → Logical Plan translation.
 *
 * Role in compiler pipeline: Phase 4 – Query Planning.
 * Converts the semantic-annotated AST into a logical plan tree following
 * the standard relational algebra order:
 *   Scan → Join(s) → Filter → Project → Distinct → Sort → Limit
 */
#ifndef PLANNER_H
#define PLANNER_H

#include "ast.h"
#include "plan.h"
#include "semantic.h"

/* Build a logical plan from a semantic-checked AST.
   Returns NULL on error. */
PlanNode *planner_build(AstNode *ast, SemCtx *ctx);

/* Get the output schema (column names) of a plan node.
   Caller frees the returned array and each name. */
char **plan_output_names(PlanNode *node, SemCtx *ctx, int *out_count);

#endif /* PLANNER_H */
