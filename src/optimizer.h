/*
 * optimizer.h — Query optimizer interface.
 *
 * Role in compiler pipeline: Phase 5 – Query Optimization.
 * Rewrites the logical plan tree using a fixed-point rule application strategy.
 * Rules are applied repeatedly until no more changes occur ("fixed point").
 *
 * Implemented rules:
 *  1. Constant Folding        — evaluate constant sub-expressions at compile time
 *  2. Boolean Simplification  — TRUE AND x → x, FALSE OR x → x, NOT NOT x → x
 *  3. Filter Removal          — drop Filter nodes whose predicate is always TRUE
 *  4. Predicate Splitting     — split ANDs in Filter into separate Filter nodes
 *  5. Predicate Pushdown      — push filters below joins when possible
 *  6. Projection Pushdown     — insert early projections above scans
 *  7. Join Algorithm Selection— choose HASH JOIN for equi-joins
 *  8. Join Reordering (bonus) — reorder joins by estimated row count
 */
#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "plan.h"
#include "semantic.h"

/* Log of which rules fired (for --verbose output) */
typedef struct {
    char **rules;
    int    count;
    int    cap;
} OptLog;

void optlog_init(OptLog *log);
void optlog_push(OptLog *log, const char *rule);
void optlog_print(OptLog *log);
void optlog_free(OptLog *log);

/* Run all optimization passes to fixed point.
   Returns the (possibly new) root of the optimized plan. */
PlanNode *optimizer_run(PlanNode *root, SemCtx *ctx, OptLog *log);

#endif /* OPTIMIZER_H */
