/*
 * planner.c — Translate a semantic-annotated AST into a logical plan tree.
 *
 * Standard plan construction order:
 *   1. Build a SCAN for each table
 *   2. Nest JOINs left-to-right (left-deep tree)
 *   3. Wrap with FILTER for WHERE clause
 *   4. Wrap with PROJECT for column list
 *   5. Wrap with DISTINCT if needed
 *   6. Wrap with SORT for ORDER BY
 *   7. Wrap with LIMIT
 */
#include "planner.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Column name inference ─────────────────────────────────────────── */

/* Return the display name for a SELECT column expression */
static char *col_display_name(AstNode *expr, SemCtx *ctx) {
    if (!expr) return xstrdup("?");
    switch (expr->kind) {
        case AST_ALIAS:
            return xstrdup(expr->alias.name);
        case AST_COLUMN: {
            /* Use table.col or just col */
            if (expr->col.table) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s.%s", expr->col.table, expr->col.name);
                return xstrdup(buf);
            }
            return xstrdup(expr->col.name);
        }
        case AST_LITERAL: {
            char *s = val_to_string(expr->literal);
            return s;
        }
        case AST_BINOP: {
            char buf[64];
            snprintf(buf, sizeof(buf), "(%s)", op_name(expr->binop.op));
            return xstrdup(buf);
        }
        default:
            return xstrdup("expr");
    }
}

/* ── Plan builder ──────────────────────────────────────────────────── */
PlanNode *planner_build(AstNode *ast, SemCtx *ctx) {
    if (!ast || ast->kind != AST_SELECT) return NULL;

    /* ── Step 1 & 2: Scans + Joins ───────────────────────────────── */
    /* Build scan for FROM table */
    PlanNode *root = NULL;
    if (ctx->ntables == 0) return NULL;

    root = plan_scan(ctx->tables[0].table,
                     ctx->tables[0].alias,
                     0);

    /* Add joins left to right */
    if (ast->select.joins) {
        AstNode *jlist = ast->select.joins;
        for (int i = 0; i < jlist->list.count; i++) {
            AstNode *j  = jlist->list.items[i];
            if (j->kind != AST_JOIN) continue;
            int tidx = i + 1;   /* first join uses table index 1, etc. */
            PlanNode *right = plan_scan(ctx->tables[tidx].table,
                                        ctx->tables[tidx].alias,
                                        tidx);
            root = plan_join(j->join.join_type, root, right, j->join.on);
        }
    }

    /* ── Step 3: Filter (WHERE) ──────────────────────────────────── */
    if (ast->select.where)
        root = plan_filter(root, ast->select.where);

    /* ── Step 4: Sort (ORDER BY) ─────────────────────────────────── */
    if (ast->select.order_by) {
        AstNode *olist = ast->select.order_by;
        int cnt = olist->list.count;
        AstNode **keys = xmalloc((size_t)cnt * sizeof(AstNode*));
        int      *asc  = xmalloc((size_t)cnt * sizeof(int));
        for (int i = 0; i < cnt; i++) {
            AstNode *oi = olist->list.items[i];
            keys[i] = oi->order_item.expr;
            asc[i]  = oi->order_item.asc;
        }
        root = plan_sort(root, keys, asc, cnt);
    }

    /* ── Step 5: Project ─────────────────────────────────────────── */
    /* Build SELECT * expansion or explicit column list */
    if (ast->select.cols) {
        AstNode *clist = ast->select.cols;
        int cnt = clist->list.count;
        AstNode **exprs = xmalloc((size_t)cnt * sizeof(AstNode*));
        char    **names = xmalloc((size_t)cnt * sizeof(char*));
        for (int i = 0; i < cnt; i++) {
            exprs[i] = clist->list.items[i];
            names[i] = col_display_name(exprs[i], ctx);
        }
        root = plan_project(root, exprs, names, cnt);
    } else {
        /* SELECT *: project all columns from all tables */
        int total = 0;
        for (int t = 0; t < ctx->ntables; t++) total += ctx->tables[t].table->ncols;
        AstNode **exprs = xmalloc((size_t)total * sizeof(AstNode*));
        char    **names = xmalloc((size_t)total * sizeof(char*));
        int idx = 0;
        for (int t = 0; t < ctx->ntables; t++) {
            TableDesc *td = ctx->tables[t].table;
            for (int c = 0; c < td->ncols; c++) {
                /* Create a synthetic AST_COLUMN node */
                AstNode *col = ast_column(ast->line, NULL, td->cols[c].name);
                col->col.tbl_idx = t;
                col->col.col_idx = c;
                /* Give it a qualified name for clarity */
                free(col->col.table);
                col->col.table = xstrdup(ctx->tables[t].alias);
                exprs[idx] = col;
                char buf[256];
                snprintf(buf, sizeof(buf), "%s.%s",
                         ctx->tables[t].alias, td->cols[c].name);
                names[idx] = xstrdup(buf);
                idx++;
            }
        }
        root = plan_project(root, exprs, names, total);
    }

    /* ── Step 6: Distinct ────────────────────────────────────────── */
    if (ast->select.distinct)
        root = plan_distinct(root);

    /* ── Step 7: Limit ───────────────────────────────────────────── */
    if (ast->select.has_limit)
        root = plan_limit(root, ast->select.limit_val);

    return root;
}

/* ── Output schema helper ──────────────────────────────────────────── */
char **plan_output_names(PlanNode *node, SemCtx *ctx, int *out_count) {
    if (!node) { *out_count = 0; return NULL; }
    switch (node->type) {
        case PLAN_PROJECT: {
            *out_count = node->project.count;
            char **names = xmalloc((size_t)node->project.count * sizeof(char*));
            for (int i = 0; i < node->project.count; i++)
                names[i] = xstrdup(node->project.names[i]);
            return names;
        }
        case PLAN_FILTER:
        case PLAN_DISTINCT:
        case PLAN_SORT:
        case PLAN_LIMIT:
            return plan_output_names(node->left, ctx, out_count);
        case PLAN_SCAN: {
            TableDesc *td = node->scan.table;
            *out_count = td->ncols;
            char **names = xmalloc((size_t)td->ncols * sizeof(char*));
            for (int c = 0; c < td->ncols; c++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s.%s",
                         node->scan.alias, td->cols[c].name);
                names[c] = xstrdup(buf);
            }
            return names;
        }
        case PLAN_JOIN: {
            int lc, rc;
            char **ln = plan_output_names(node->left,  ctx, &lc);
            char **rn = plan_output_names(node->right, ctx, &rc);
            *out_count = lc + rc;
            char **names = xmalloc((size_t)(lc+rc) * sizeof(char*));
            for (int i = 0; i < lc; i++) names[i]    = ln[i];
            for (int i = 0; i < rc; i++) names[lc+i] = rn[i];
            free(ln); free(rn);
            return names;
        }
    }
    *out_count = 0;
    return NULL;
}
