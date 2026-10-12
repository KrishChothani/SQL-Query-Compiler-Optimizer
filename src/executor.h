/*
 * executor.h — Volcano iterator model (open/next/close) operator interface.
 *
 * Role in compiler pipeline: Phase 6 – Physical Execution.
 * Implements the classic "Volcano" (or "iterator") model where each operator
 * exposes three methods:
 *   open()  — initialise the operator and its children
 *   next()  — return the next output row, or NULL if done
 *   close() — release all resources
 *
 * Operators:
 *   SeqScan      — iterate rows of a base table
 *   Filter       — pass rows satisfying a predicate
 *   Project      — evaluate a list of expressions per row
 *   NestedLoop   — cross-product with join condition
 *   HashJoin     — build phase on right, probe on left
 *   LeftJoin     — left outer join (NL or hash)
 *   Sort         — materialise + sort + emit
 *   Limit        — stop after N rows
 *   Distinct     — deduplicate (sort-based)
 */
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "plan.h"
#include "semantic.h"
#include "value.h"
#include "expr_eval.h"

/* ── Output row ─────────────────────────────────────────────────────── */
/* A Row is a heap-allocated array of Values.  The executor always returns
   the combined "wide" row (all joined tables) internally.  The Project
   operator produces the final "narrow" row for output. */
typedef struct {
    Value *vals;
    int    width;
} Row;

Row *row_alloc(int width);
Row *row_copy(Row *r);
void row_free(Row *r);

/* ── Operator statistics ─────────────────────────────────────────────── */
typedef struct {
    long long rows_in;    /* rows consumed from children */
    long long rows_out;   /* rows emitted to parent */
} OpStats;

/* ── Iterator interface ──────────────────────────────────────────────── */
typedef struct Operator Operator;

struct Operator {
    /* Function pointers implementing the Volcano interface */
    void  (*op_open) (Operator *self);
    Row  *(*op_next) (Operator *self);
    void  (*op_close)(Operator *self);

    /* Children */
    Operator *left;
    Operator *right;

    /* Statistics */
    OpStats stats;

    /* Operator-specific state (opaque pointer) */
    void *state;

    /* Context (shared across all operators in the tree) */
    SemCtx  *ctx;
    PlanNode *plan;   /* the plan node this operator implements */
};

/* ── Execution result & materialisation ─────────────────────────────── */
typedef struct {
    Row   **rows;
    int     nrows;
    double  elapsed_ms;
} ExecResult;

ExecResult exec_collect(Operator *root);
void       exec_result_free(ExecResult *res);

/* ── Build an executor tree from a plan tree ─────────────────────────── */
Operator *exec_build(PlanNode *plan, SemCtx *ctx);

/* ── Run the full query; print result table ──────────────────────────── */
/* Returns number of rows output.
   If show_stats is 1, prints per-operator rows_processed after the table. */
long long exec_run(Operator *root, int ncols, char **col_names,
                   int show_stats);

/* ── Print per-operator statistics ──────────────────────────────────── */
void exec_print_stats(Operator *op, int indent);
void exec_stats_to_json(Operator *op, FILE *f, int *is_first);
long long exec_total_rows_processed(Operator *op);

/* ── Free operator tree ──────────────────────────────────────────────── */
void exec_free(Operator *op);

#endif /* EXECUTOR_H */
