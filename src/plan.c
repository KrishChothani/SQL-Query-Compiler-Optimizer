/*
 * plan.c — Logical plan node constructors, destructor and pretty-printer.
 */
#include "plan.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Constructors ──────────────────────────────────────────────────── */
static PlanNode *alloc_plan(PlanNodeType t) {
    PlanNode *p = xmalloc(sizeof(PlanNode));
    memset(p, 0, sizeof(PlanNode));
    p->type = t;
    return p;
}

PlanNode *plan_scan(TableDesc *tbl, const char *alias, int tbl_idx) {
    PlanNode *p = alloc_plan(PLAN_SCAN);
    p->scan.table   = tbl;
    p->scan.alias   = xstrdup(alias ? alias : tbl->name);
    p->scan.tbl_idx = tbl_idx;
    return p;
}
PlanNode *plan_filter(PlanNode *child, AstNode *pred) {
    PlanNode *p = alloc_plan(PLAN_FILTER);
    p->left           = child;
    p->filter.predicate = pred;
    return p;
}
PlanNode *plan_project(PlanNode *child, AstNode **exprs, char **names, int count) {
    PlanNode *p = alloc_plan(PLAN_PROJECT);
    p->left             = child;
    p->project.exprs    = exprs;
    p->project.names    = names;
    p->project.count    = count;
    return p;
}
PlanNode *plan_join(JoinType jt, PlanNode *left, PlanNode *right, AstNode *cond) {
    PlanNode *p = alloc_plan(PLAN_JOIN);
    p->left           = left;
    p->right          = right;
    p->join.join_type = jt;
    p->join.algo      = JOIN_ALGO_NL;
    p->join.condition = cond;
    return p;
}
PlanNode *plan_sort(PlanNode *child, AstNode **keys, int *asc, int count) {
    PlanNode *p = alloc_plan(PLAN_SORT);
    p->left        = child;
    p->sort.keys   = keys;
    p->sort.asc    = asc;
    p->sort.count  = count;
    return p;
}
PlanNode *plan_limit(PlanNode *child, long long n) {
    PlanNode *p = alloc_plan(PLAN_LIMIT);
    p->left      = child;
    p->limit.n   = n;
    return p;
}
PlanNode *plan_distinct(PlanNode *child) {
    PlanNode *p = alloc_plan(PLAN_DISTINCT);
    p->left = child;
    return p;
}

/* ── Free ──────────────────────────────────────────────────────────── */
void plan_free(PlanNode *node) {
    if (!node) return;
    plan_free(node->left);
    plan_free(node->right);
    switch (node->type) {
        case PLAN_SCAN:
            free(node->scan.alias);
            break;
        case PLAN_PROJECT:
            /* exprs and names are owned by the AST / planner; don't free here
               because the optimizer may share them.  We only free the arrays. */
            free(node->project.exprs);
            for (int i = 0; i < node->project.count; i++)
                free(node->project.names[i]);
            free(node->project.names);
            break;
        case PLAN_SORT:
            free(node->sort.keys);
            free(node->sort.asc);
            break;
        default: break;
    }
    free(node);
}

/* ── Pretty-printer ────────────────────────────────────────────────── */
static void print_fprefix(FILE *out, int indent, int is_last) {
    /* Box-drawing prefix */
    for (int i = 0; i < indent - 1; i++) fprintf(out, "   ");
    if (indent > 0) fprintf(out, is_last ? "└─ " : "├─ ");
}

/* Print a compact expression for display */
static void print_expr_inline_f(FILE *out, AstNode *n) {
    if (!n) { fprintf(out, "NULL"); return; }
    switch (n->kind) {
        case AST_COLUMN:
            if (n->col.table) fprintf(out, "%s.", n->col.table);
            fprintf(out, "%s", n->col.name);
            break;
        case AST_LITERAL: {
            char *s = val_to_string(n->literal);
            fprintf(out, "%s", s); free(s);
            break;
        }
        case AST_BINOP:
            fprintf(out, "(");
            print_expr_inline_f(out, n->binop.left);
            fprintf(out, " %s ", op_name(n->binop.op));
            print_expr_inline_f(out, n->binop.right);
            fprintf(out, ")");
            break;
        case AST_UNOP:
            fprintf(out, "%s", op_name(n->unop.op));
            print_expr_inline_f(out, n->unop.operand);
            break;
        case AST_IS_NULL:
            print_expr_inline_f(out, n->is_null.expr);
            fprintf(out, " IS%s NULL", n->is_null.is_not?" NOT":"");
            break;
        case AST_LIKE:
            print_expr_inline_f(out, n->like.expr);
            fprintf(out, "%s LIKE ", n->like.negated?" NOT":"");
            print_expr_inline_f(out, n->like.pattern);
            break;
        case AST_BETWEEN:
            print_expr_inline_f(out, n->between.expr);
            fprintf(out, "%s BETWEEN ", n->between.negated?" NOT":"");
            print_expr_inline_f(out, n->between.lo);
            fprintf(out, " AND ");
            print_expr_inline_f(out, n->between.hi);
            break;
        case AST_ALIAS:
            print_expr_inline_f(out, n->alias.expr);
            fprintf(out, " AS %s", n->alias.name);
            break;
        default:
            fprintf(out, "...");
    }
}

void plan_fprint(FILE *out, PlanNode *node, int indent, int is_last_child) {
    if (!node) return;
    print_fprefix(out, indent, is_last_child);

    switch (node->type) {
        case PLAN_SCAN:
            fprintf(out, "Scan %s", node->scan.table->name);
            if (strcmp(node->scan.alias, node->scan.table->name) != 0)
                fprintf(out, " AS %s", node->scan.alias);
            fprintf(out, "  [%d rows]\n", node->scan.table->nrows);
            break;

        case PLAN_FILTER:
            fprintf(out, "Filter [");
            print_expr_inline_f(out, node->filter.predicate);
            fprintf(out, "]\n");
            plan_fprint(out, node->left, indent+1, 1);
            break;

        case PLAN_PROJECT: {
            fprintf(out, "Project [");
            for (int i = 0; i < node->project.count; i++) {
                if (i) fprintf(out, ", ");
                fprintf(out, "%s", node->project.names[i]);
            }
            fprintf(out, "]\n");
            plan_fprint(out, node->left, indent+1, 1);
            break;
        }

        case PLAN_JOIN:
            fprintf(out, "Join %s [",
                    node->join.join_type == JOIN_INNER ? "INNER" : "LEFT");
            if (node->join.algo == JOIN_ALGO_HASH) fprintf(out, "HASH ");
            else fprintf(out, "NL ");
            print_expr_inline_f(out, node->join.condition);
            fprintf(out, "]\n");
            plan_fprint(out, node->left,  indent+1, 0);
            plan_fprint(out, node->right, indent+1, 1);
            break;

        case PLAN_SORT:
            fprintf(out, "Sort [");
            for (int i = 0; i < node->sort.count; i++) {
                if (i) fprintf(out, ", ");
                print_expr_inline_f(out, node->sort.keys[i]);
                fprintf(out, " %s", node->sort.asc[i] ? "ASC" : "DESC");
            }
            fprintf(out, "]\n");
            plan_fprint(out, node->left, indent+1, 1);
            break;

        case PLAN_LIMIT:
            fprintf(out, "Limit [%lld]\n", node->limit.n);
            plan_fprint(out, node->left, indent+1, 1);
            break;

        case PLAN_DISTINCT:
            fprintf(out, "Distinct\n");
            plan_fprint(out, node->left, indent+1, 1);
            break;
    }
}

void plan_print(PlanNode *node, int indent, int is_last_child) {
    plan_fprint(stdout, node, indent, is_last_child);
}

char *plan_to_str(PlanNode *node) {
    if (!node) return xstrdup("");
    char *buf = NULL; size_t sz = 0;
    FILE *f = open_memstream(&buf, &sz);
    if (!f) return xstrdup("");
    plan_fprint(f, node, 0, 1);
    fclose(f);
    return buf ? buf : xstrdup("");
}
