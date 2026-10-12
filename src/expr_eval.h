/*
 * expr_eval.h — Expression evaluation against a row.
 *
 * Role in compiler pipeline: Runtime expression evaluator used by the
 * executor's Filter, Project, Sort, and Join operators.
 *
 * A "row" is a flat array of Values from all joined tables combined.
 * Column references use tbl_idx and col_idx to index into the row.
 *
 * The row layout is: [table0_col0, table0_col1, ..., table1_col0, ...]
 * Column offset = sum of ncols of all tables before tbl_idx + col_idx.
 */
#ifndef EXPR_EVAL_H
#define EXPR_EVAL_H

#include "ast.h"
#include "value.h"
#include "semantic.h"

/* Evaluate expression against a combined row.
   ctx provides the table schema for column offset computation.
   row is a flat array of ncols_total values.
   Returns a Value (caller must val_free it). */
Value expr_eval(AstNode *expr, Value *row, SemCtx *ctx);

/* LIKE pattern matching with % wildcard */
int like_match(const char *str, const char *pattern);

/* Compute the flat row offset for a column reference */
int col_offset(int tbl_idx, int col_idx, SemCtx *ctx);

/* Total number of columns in a combined row */
int row_width(SemCtx *ctx);

#endif /* EXPR_EVAL_H */
