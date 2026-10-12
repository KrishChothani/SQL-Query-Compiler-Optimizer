/*
 * plan.h — Logical plan node definitions and pretty-printer.
 *
 * Role in compiler pipeline: Phase 4 – Logical Plan (Intermediate Representation).
 * The logical plan is a relational algebra tree.  Each node represents one
 * relational operation.  The planner builds it from the AST; the optimizer
 * rewrites it; the executor evaluates it via the Volcano model.
 *
 * Node types:
 *   PLAN_SCAN     — sequential scan of one table
 *   PLAN_FILTER   — selection (σ predicate)
 *   PLAN_PROJECT  — projection (π column list)
 *   PLAN_JOIN     — inner or left join
 *   PLAN_SORT     — ORDER BY
 *   PLAN_LIMIT    — LIMIT n
 *   PLAN_DISTINCT — DISTINCT elimination
 */
#ifndef PLAN_H
#define PLAN_H

#include <stdio.h>
#include "ast.h"
#include "catalog.h"
#include "semantic.h"

/* ── Physical algorithm hint set by optimizer ──────────────────────── */
typedef enum {
    JOIN_ALGO_NL   = 0,   /* nested-loop (default) */
    JOIN_ALGO_HASH = 1,   /* hash join */
} JoinAlgo;

/* ── Plan node types ───────────────────────────────────────────────── */
typedef enum {
    PLAN_SCAN,
    PLAN_FILTER,
    PLAN_PROJECT,
    PLAN_JOIN,
    PLAN_SORT,
    PLAN_LIMIT,
    PLAN_DISTINCT,
} PlanNodeType;

typedef struct PlanNode PlanNode;

struct PlanNode {
    PlanNodeType type;

    /* Children */
    PlanNode *left;
    PlanNode *right;   /* used by JOIN */

    union {
        /* PLAN_SCAN */
        struct {
            TableDesc *table;
            char      *alias;   /* alias or table name */
            int        tbl_idx; /* index in SemCtx.tables */
        } scan;

        /* PLAN_FILTER */
        struct {
            AstNode *predicate;
        } filter;

        /* PLAN_PROJECT */
        struct {
            AstNode **exprs;    /* array of expressions */
            char    **names;    /* output column names */
            int       count;
        } project;

        /* PLAN_JOIN */
        struct {
            JoinType  join_type;
            JoinAlgo  algo;
            AstNode  *condition;
        } join;

        /* PLAN_SORT */
        struct {
            AstNode **keys;    /* ORDER BY expressions */
            int      *asc;     /* 1=ASC, 0=DESC per key */
            int       count;
        } sort;

        /* PLAN_LIMIT */
        struct {
            long long n;
        } limit;

        /* PLAN_DISTINCT — no extra fields; dedup on all output columns */
    };
};

/* ── Constructors ──────────────────────────────────────────────────── */
PlanNode *plan_scan    (TableDesc *tbl, const char *alias, int tbl_idx);
PlanNode *plan_filter  (PlanNode *child, AstNode *pred);
PlanNode *plan_project (PlanNode *child, AstNode **exprs, char **names, int count);
PlanNode *plan_join    (JoinType jt, PlanNode *left, PlanNode *right, AstNode *cond);
PlanNode *plan_sort    (PlanNode *child, AstNode **keys, int *asc, int count);
PlanNode *plan_limit   (PlanNode *child, long long n);
PlanNode *plan_distinct(PlanNode *child);

/* ── Lifecycle ─────────────────────────────────────────────────────── */
void plan_free(PlanNode *node);

/* ── Pretty printer (box-drawing characters) ───────────────────────── */
void  plan_print (PlanNode *node, int indent, int is_last_child);
void  plan_fprint(FILE *out, PlanNode *node, int indent, int is_last_child);
char *plan_to_str(PlanNode *node);

#endif /* PLAN_H */
