/*
 * semantic.c — Symbol table construction, name resolution, type checking.
 *
 * The semantic analyser walks the AST in two passes:
 *   Pass 1: resolve all FROM and JOIN table references → build ctx->tables
 *   Pass 2: resolve every AST_COLUMN node and type-check every expression
 */
#include "semantic.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════
   Helpers
   ══════════════════════════════════════════════════════════════════ */

/* Find closest column name for "did you mean?" */
static const char *best_suggestion(const char *name, SemCtx *ctx) {
    static char best[256];
    int best_dist = 9999;
    best[0] = '\0';
    for (int t = 0; t < ctx->ntables; t++) {
        TableDesc *td = ctx->tables[t].table;
        for (int c = 0; c < td->ncols; c++) {
            int d = edit_distance(name, td->cols[c].name);
            if (d < best_dist) {
                best_dist = d;
                strncpy(best, td->cols[c].name, sizeof(best)-1);
            }
        }
    }
    return best_dist <= 3 ? best : NULL;
}

/* ══════════════════════════════════════════════════════════════════
   Pass 1: Resolve tables
   ══════════════════════════════════════════════════════════════════ */

static int add_table(SemCtx *ctx, const char *tname, const char *alias, int line) {
    /* Check for duplicate alias */
    const char *eff_alias = alias ? alias : tname;
    for (int i = 0; i < ctx->ntables; i++) {
        if (str_iequal(ctx->tables[i].alias, eff_alias)) {
            sem_error(line, "duplicate table alias '%s'", eff_alias);
            ctx->error_count++;
            return -1;
        }
    }
    TableDesc *td = catalog_load_table(ctx->catalog, tname);
    if (!td) {
        sem_error(line, "unknown table '%s'", tname);
        ctx->error_count++;
        return -1;
    }
    int idx = ctx->ntables++;
    ctx->tables = xrealloc(ctx->tables, (size_t)ctx->ntables * sizeof(ResolvedTable));
    ctx->tables[idx].alias = xstrdup(eff_alias);
    ctx->tables[idx].table = td;
    ctx->tables[idx].idx   = idx;
    return idx;
}

static void resolve_tables(AstNode *ast, SemCtx *ctx) {
    if (!ast || ast->kind != AST_SELECT) return;
    /* Main FROM table */
    if (ast->select.from_table)
        add_table(ctx, ast->select.from_table, ast->select.from_alias, ast->line);
    /* JOINs */
    if (ast->select.joins) {
        AstNode *jlist = ast->select.joins;
        for (int i = 0; i < jlist->list.count; i++) {
            AstNode *j = jlist->list.items[i];
            if (j->kind == AST_JOIN)
                add_table(ctx, j->join.table, j->join.alias, j->line);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
   Pass 2: Resolve columns and type-check
   ══════════════════════════════════════════════════════════════════ */

static ValType check_expr(AstNode *expr, SemCtx *ctx);

static void resolve_column(AstNode *col, SemCtx *ctx) {
    const char *tname = col->col.table;
    const char *cname = col->col.name;

    if (tname) {
        /* Qualified: find the table alias */
        for (int t = 0; t < ctx->ntables; t++) {
            if (str_iequal(ctx->tables[t].alias, tname)) {
                int ci = catalog_find_col(ctx->tables[t].table, cname);
                if (ci < 0) {
                    const char *sug = best_suggestion(cname, ctx);
                    if (sug)
                        sem_error(col->line,
                                  "unknown column '%s.%s'. Did you mean '%s'?",
                                  tname, cname, sug);
                    else
                        sem_error(col->line,
                                  "unknown column '%s.%s'", tname, cname);
                    ctx->error_count++;
                    return;
                }
                col->col.tbl_idx = t;
                col->col.col_idx = ci;
                return;
            }
        }
        sem_error(col->line, "unknown table or alias '%s'", tname);
        ctx->error_count++;
    } else {
        /* Unqualified: search all tables */
        int found_t = -1, found_c = -1;
        for (int t = 0; t < ctx->ntables; t++) {
            int ci = catalog_find_col(ctx->tables[t].table, cname);
            if (ci >= 0) {
                if (found_t >= 0) {
                    sem_error(col->line,
                              "ambiguous column '%s' (found in '%s' and '%s')",
                              cname,
                              ctx->tables[found_t].alias,
                              ctx->tables[t].alias);
                    ctx->error_count++;
                    return;
                }
                found_t = t; found_c = ci;
            }
        }
        if (found_t < 0) {
            const char *sug = best_suggestion(cname, ctx);
            if (sug)
                sem_error(col->line,
                          "unknown column '%s'. Did you mean '%s'?", cname, sug);
            else
                sem_error(col->line, "unknown column '%s'", cname);
            ctx->error_count++;
            return;
        }
        col->col.tbl_idx = found_t;
        col->col.col_idx = found_c;
    }
}

/* Returns the inferred ValType of an expression.  Also resolves columns. */
static ValType check_expr(AstNode *expr, SemCtx *ctx) {
    if (!expr) return VAL_NULL;
    switch (expr->kind) {
        case AST_LITERAL:
            return expr->literal.type;

        case AST_COLUMN:
            resolve_column(expr, ctx);
            if (expr->col.tbl_idx < 0) return VAL_NULL;
            return ctx->tables[expr->col.tbl_idx]
                       .table->cols[expr->col.col_idx].type;

        case AST_ALIAS:
            return check_expr(expr->alias.expr, ctx);

        case AST_BINOP: {
            ValType lt = check_expr(expr->binop.left,  ctx);
            ValType rt = check_expr(expr->binop.right, ctx);
            OpKind  op = expr->binop.op;

            if (op == OP_AND || op == OP_OR) {
                /* Both sides must be boolean-compatible */
                if (lt != VAL_BOOL && lt != VAL_NULL)
                    sem_error(expr->line, "AND/OR requires boolean operands");
                if (rt != VAL_BOOL && rt != VAL_NULL)
                    sem_error(expr->line, "AND/OR requires boolean operands");
                return VAL_BOOL;
            }
            if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV) {
                if ((lt != VAL_INT && lt != VAL_FLOAT && lt != VAL_NULL) ||
                    (rt != VAL_INT && rt != VAL_FLOAT && rt != VAL_NULL)) {
                    sem_error(expr->line,
                              "arithmetic operator requires numeric operands");
                    ctx->error_count++;
                }
                if (lt == VAL_FLOAT || rt == VAL_FLOAT) return VAL_FLOAT;
                return VAL_INT;
            }
            /* Comparisons */
            if (lt != VAL_NULL && rt != VAL_NULL && lt != rt) {
                /* Allow numeric cross-comparison */
                int lnum = (lt == VAL_INT || lt == VAL_FLOAT);
                int rnum = (rt == VAL_INT || rt == VAL_FLOAT);
                if (!lnum || !rnum) {
                    sem_error(expr->line,
                              "type mismatch in comparison: cannot compare %s and %s",
                              lt == VAL_STRING ? "STRING" :
                              lt == VAL_INT    ? "INT"    :
                              lt == VAL_FLOAT  ? "FLOAT"  :
                              lt == VAL_BOOL   ? "BOOL"   : "NULL",
                              rt == VAL_STRING ? "STRING" :
                              rt == VAL_INT    ? "INT"    :
                              rt == VAL_FLOAT  ? "FLOAT"  :
                              rt == VAL_BOOL   ? "BOOL"   : "NULL");
                    ctx->error_count++;
                }
            }
            return VAL_BOOL;
        }

        case AST_UNOP:
            if (expr->unop.op == OP_NOT) {
                ValType t = check_expr(expr->unop.operand, ctx);
                if (t != VAL_BOOL && t != VAL_NULL)
                    sem_error(expr->line, "NOT requires a boolean operand");
                return VAL_BOOL;
            }
            if (expr->unop.op == OP_NEG) {
                ValType t = check_expr(expr->unop.operand, ctx);
                if (t != VAL_INT && t != VAL_FLOAT && t != VAL_NULL) {
                    sem_error(expr->line, "unary minus requires a numeric operand");
                    ctx->error_count++;
                }
                return t;
            }
            return VAL_NULL;

        case AST_IS_NULL:
            check_expr(expr->is_null.expr, ctx);
            return VAL_BOOL;

        case AST_LIKE:
            {
                ValType t = check_expr(expr->like.expr, ctx);
                if (t != VAL_STRING && t != VAL_NULL)
                    sem_error(expr->line,
                              "LIKE requires a string left operand");
                check_expr(expr->like.pattern, ctx);
            }
            return VAL_BOOL;

        case AST_BETWEEN:
            check_expr(expr->between.expr, ctx);
            check_expr(expr->between.lo,   ctx);
            check_expr(expr->between.hi,   ctx);
            return VAL_BOOL;

        case AST_IN:
            check_expr(expr->in_expr.expr, ctx);
            if (expr->in_expr.list) {
                AstNode *lst = expr->in_expr.list;
                for (int i = 0; i < lst->list.count; i++)
                    check_expr(lst->list.items[i], ctx);
            }
            return VAL_BOOL;

        case AST_LIST:
            for (int i = 0; i < expr->list.count; i++)
                check_expr(expr->list.items[i], ctx);
            return VAL_NULL;

        default:
            return VAL_NULL;
    }
}

/* ══════════════════════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════════════════════ */
int semantic_check(AstNode *ast, Catalog *catalog, SemCtx *ctx) {
    ctx->catalog     = catalog;
    ctx->tables      = NULL;
    ctx->ntables     = 0;
    ctx->error_count = 0;

    if (!ast || ast->kind != AST_SELECT) {
        sem_error(0, "expected a SELECT statement");
        return -1;
    }

    /* Pass 1: resolve tables */
    resolve_tables(ast, ctx);
    if (ctx->error_count) return -1;

    /* Pass 2: type-check SELECT columns */
    if (ast->select.cols) {
        AstNode *cols = ast->select.cols;
        for (int i = 0; i < cols->list.count; i++)
            check_expr(cols->list.items[i], ctx);
    }

    /* Type-check WHERE */
    if (ast->select.where) {
        ValType wt = check_expr(ast->select.where, ctx);
        if (wt != VAL_BOOL && wt != VAL_NULL)
            sem_error(ast->select.where->line,
                      "WHERE clause must be a boolean expression");
    }

    /* Type-check JOIN ON conditions */
    if (ast->select.joins) {
        AstNode *jlist = ast->select.joins;
        for (int i = 0; i < jlist->list.count; i++) {
            AstNode *j = jlist->list.items[i];
            if (j->kind == AST_JOIN && j->join.on) {
                ValType jt = check_expr(j->join.on, ctx);
                if (jt != VAL_BOOL && jt != VAL_NULL)
                    sem_error(j->line,
                              "JOIN ON clause must be a boolean expression");
            }
        }
    }

    /* Type-check ORDER BY */
    if (ast->select.order_by) {
        AstNode *olist = ast->select.order_by;
        for (int i = 0; i < olist->list.count; i++) {
            AstNode *oi = olist->list.items[i];
            if (oi->kind == AST_ORDER_ITEM)
                check_expr(oi->order_item.expr, ctx);
        }
    }

    /* Validate LIMIT */
    if (ast->select.has_limit && ast->select.limit_val < 0) {
        sem_error(ast->line, "LIMIT must be a non-negative integer");
        ctx->error_count++;
    }

    return ctx->error_count == 0 ? 0 : -1;
}

void semctx_free(SemCtx *ctx) {
    for (int i = 0; i < ctx->ntables; i++)
        free(ctx->tables[i].alias);
    free(ctx->tables);
    ctx->tables  = NULL;
    ctx->ntables = 0;
}

ValType expr_type(AstNode *expr, SemCtx *ctx) {
    return check_expr(expr, ctx);
}
