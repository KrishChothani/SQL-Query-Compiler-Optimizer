/*
 * expr_eval.c — Runtime expression evaluation.
 *
 * Evaluates AST expression nodes against a materialised row array.
 * Handles NULL semantics: comparisons with NULL yield false/unknown.
 */
#include "expr_eval.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Column offset computation ─────────────────────────────────────── */
int col_offset(int tbl_idx, int col_idx, SemCtx *ctx) {
    int off = 0;
    for (int t = 0; t < tbl_idx; t++)
        off += ctx->tables[t].table->ncols;
    return off + col_idx;
}

int row_width(SemCtx *ctx) {
    int w = 0;
    for (int t = 0; t < ctx->ntables; t++)
        w += ctx->tables[t].table->ncols;
    return w;
}

/* ── LIKE pattern matching ─────────────────────────────────────────── */
/* Supports '%' (any sequence) and '_' (any single character). */
int like_match(const char *str, const char *pattern) {
    if (!str || !pattern) return 0;
    /* Recursive implementation */
    if (*pattern == '\0') return *str == '\0';
    if (*pattern == '%') {
        /* Try matching zero or more characters */
        do {
            if (like_match(str, pattern+1)) return 1;
        } while (*str++);
        return 0;
    }
    if (*pattern == '_') {
        if (*str == '\0') return 0;
        return like_match(str+1, pattern+1);
    }
    return *str == *pattern && like_match(str+1, pattern+1);
}

/* ── Main evaluator ─────────────────────────────────────────────────── */
Value expr_eval(AstNode *expr, Value *row, SemCtx *ctx) {
    if (!expr) return val_null();

    switch (expr->kind) {
        /* ── Literals ─────────────────────────────────────────── */
        case AST_LITERAL:
            return val_copy(expr->literal);

        /* ── Column reference ─────────────────────────────────── */
        case AST_COLUMN: {
            if (expr->col.tbl_idx < 0 || expr->col.col_idx < 0)
                return val_null();
            int off = col_offset(expr->col.tbl_idx, expr->col.col_idx, ctx);
            return val_copy(row[off]);
        }

        /* ── Alias: evaluate inner expression ─────────────────── */
        case AST_ALIAS:
            return expr_eval(expr->alias.expr, row, ctx);

        /* ── Binary operators ─────────────────────────────────── */
        case AST_BINOP: {
            /* Short-circuit AND/OR before evaluating both sides */
            if (expr->binop.op == OP_AND) {
                Value l = expr_eval(expr->binop.left, row, ctx);
                if (!val_is_true(l)) { val_free(l); return val_bool(0); }
                val_free(l);
                Value r = expr_eval(expr->binop.right, row, ctx);
                int res = val_is_true(r);
                val_free(r);
                return val_bool(res);
            }
            if (expr->binop.op == OP_OR) {
                Value l = expr_eval(expr->binop.left, row, ctx);
                if (val_is_true(l)) { val_free(l); return val_bool(1); }
                val_free(l);
                Value r = expr_eval(expr->binop.right, row, ctx);
                int res = val_is_true(r);
                val_free(r);
                return val_bool(res);
            }
            Value l = expr_eval(expr->binop.left,  row, ctx);
            Value r = expr_eval(expr->binop.right, row, ctx);
            Value res;
            switch (expr->binop.op) {
                case OP_ADD: res = val_add(l, r); break;
                case OP_SUB: res = val_sub(l, r); break;
                case OP_MUL: res = val_mul(l, r); break;
                case OP_DIV: res = val_div(l, r); break;
                case OP_EQ: {
                    int cmp = val_cmp(l, r);
                    res = (cmp == -2) ? val_bool(0) : val_bool(cmp == 0);
                    break;
                }
                case OP_NEQ: {
                    int cmp = val_cmp(l, r);
                    res = (cmp == -2) ? val_bool(0) : val_bool(cmp != 0);
                    break;
                }
                case OP_LT: {
                    int cmp = val_cmp(l, r);
                    res = (cmp == -2) ? val_bool(0) : val_bool(cmp < 0);
                    break;
                }
                case OP_GT: {
                    int cmp = val_cmp(l, r);
                    res = (cmp == -2) ? val_bool(0) : val_bool(cmp > 0);
                    break;
                }
                case OP_LE: {
                    int cmp = val_cmp(l, r);
                    res = (cmp == -2) ? val_bool(0) : val_bool(cmp <= 0);
                    break;
                }
                case OP_GE: {
                    int cmp = val_cmp(l, r);
                    res = (cmp == -2) ? val_bool(0) : val_bool(cmp >= 0);
                    break;
                }
                default: res = val_null();
            }
            val_free(l); val_free(r);
            return res;
        }

        /* ── Unary operators ──────────────────────────────────── */
        case AST_UNOP: {
            if (expr->unop.op == OP_NOT) {
                Value v = expr_eval(expr->unop.operand, row, ctx);
                Value res = val_bool(!val_is_true(v));
                val_free(v);
                return res;
            }
            if (expr->unop.op == OP_NEG) {
                Value v = expr_eval(expr->unop.operand, row, ctx);
                Value res = val_neg(v);
                val_free(v);
                return res;
            }
            return val_null();
        }

        /* ── IS NULL / IS NOT NULL ────────────────────────────── */
        case AST_IS_NULL: {
            Value v = expr_eval(expr->is_null.expr, row, ctx);
            int is_null = val_is_null(v);
            val_free(v);
            return val_bool(expr->is_null.is_not ? !is_null : is_null);
        }

        /* ── LIKE ─────────────────────────────────────────────── */
        case AST_LIKE: {
            Value str = expr_eval(expr->like.expr,    row, ctx);
            Value pat = expr_eval(expr->like.pattern, row, ctx);
            int match = 0;
            if (str.type == VAL_STRING && pat.type == VAL_STRING)
                match = like_match(str.sval, pat.sval);
            val_free(str); val_free(pat);
            return val_bool(expr->like.negated ? !match : match);
        }

        /* ── BETWEEN ──────────────────────────────────────────── */
        case AST_BETWEEN: {
            Value v  = expr_eval(expr->between.expr, row, ctx);
            Value lo = expr_eval(expr->between.lo,   row, ctx);
            Value hi = expr_eval(expr->between.hi,   row, ctx);
            int cl = val_cmp(v, lo), ch = val_cmp(v, hi);
            int in = (cl != -2 && ch != -2 && cl >= 0 && ch <= 0);
            val_free(v); val_free(lo); val_free(hi);
            return val_bool(expr->between.negated ? !in : in);
        }

        /* ── IN ───────────────────────────────────────────────── */
        case AST_IN: {
            Value v = expr_eval(expr->in_expr.expr, row, ctx);
            int found = 0;
            if (expr->in_expr.list) {
                AstNode *lst = expr->in_expr.list;
                for (int i = 0; i < lst->list.count && !found; i++) {
                    Value vi = expr_eval(lst->list.items[i], row, ctx);
                    int cmp = val_cmp(v, vi);
                    if (cmp == 0) found = 1;
                    val_free(vi);
                }
            }
            val_free(v);
            return val_bool(expr->in_expr.negated ? !found : found);
        }

        default:
            return val_null();
    }
}
