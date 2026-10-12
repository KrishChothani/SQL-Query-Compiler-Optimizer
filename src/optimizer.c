/*
 * optimizer.c — Query optimizer: fixed-point rewrite rule engine.
 *
 * Each rule is a function that takes the plan tree root and returns a
 * (possibly modified) root, setting *changed=1 if it made any modification.
 * The driver calls all rules repeatedly until a full pass makes no changes.
 */
#include "optimizer.h"
#include "util.h"
#include "planner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════ */
/* OptLog                                                             */
/* ══════════════════════════════════════════════════════════════════ */
void optlog_init(OptLog *log) { log->rules=NULL; log->count=0; log->cap=0; }
void optlog_push(OptLog *log, const char *rule) {
    if (log->count == log->cap) {
        log->cap = log->cap ? log->cap*2 : 8;
        log->rules = xrealloc(log->rules, (size_t)log->cap * sizeof(char*));
    }
    log->rules[log->count++] = xstrdup(rule);
}
void optlog_print(OptLog *log) {
    if (log->count == 0) { printf("  (no rules fired)\n"); return; }
    for (int i = 0; i < log->count; i++)
        printf("  Rule applied: %s\n", log->rules[i]);
}
void optlog_free(OptLog *log) {
    for (int i = 0; i < log->count; i++) free(log->rules[i]);
    free(log->rules);
    log->rules=NULL; log->count=0; log->cap=0;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Constant Folding helpers                                           */
/* ══════════════════════════════════════════════════════════════════ */

/* Returns 1 if the expression is a compile-time constant (no columns) */
static int is_const(AstNode *e) {
    if (!e) return 1;
    switch (e->kind) {
        case AST_LITERAL: return 1;
        case AST_COLUMN:  return 0;
        case AST_BINOP:
            return is_const(e->binop.left) && is_const(e->binop.right);
        case AST_UNOP:
            return is_const(e->unop.operand);
        case AST_IS_NULL:
            return is_const(e->is_null.expr);
        case AST_BETWEEN:
            return is_const(e->between.expr) &&
                   is_const(e->between.lo) && is_const(e->between.hi);
        case AST_IN:
            if (!is_const(e->in_expr.expr)) return 0;
            if (e->in_expr.list)
                for (int i = 0; i < e->in_expr.list->list.count; i++)
                    if (!is_const(e->in_expr.list->list.items[i])) return 0;
            return 1;
        default: return 0;
    }
}

/* Forward declaration */
static Value eval_const(AstNode *e);

static Value eval_const(AstNode *e) {
    if (!e) return val_null();
    switch (e->kind) {
        case AST_LITERAL:
            return val_copy(e->literal);
        case AST_BINOP: {
            Value l = eval_const(e->binop.left);
            Value r = eval_const(e->binop.right);
            Value res;
            switch (e->binop.op) {
                case OP_ADD: res = val_add(l, r); break;
                case OP_SUB: res = val_sub(l, r); break;
                case OP_MUL: res = val_mul(l, r); break;
                case OP_DIV: res = val_div(l, r); break;
                case OP_EQ:  res = val_bool(val_cmp(l,r)==0 && l.type!=VAL_NULL); break;
                case OP_NEQ: res = val_bool(val_cmp(l,r)!=0 || l.type==VAL_NULL); break;
                case OP_LT:  {int c=val_cmp(l,r); res=(c==-2)?val_null():val_bool(c<0);} break;
                case OP_GT:  {int c=val_cmp(l,r); res=(c==-2)?val_null():val_bool(c>0);} break;
                case OP_LE:  {int c=val_cmp(l,r); res=(c==-2)?val_null():val_bool(c<=0);} break;
                case OP_GE:  {int c=val_cmp(l,r); res=(c==-2)?val_null():val_bool(c>=0);} break;
                case OP_AND: res = val_bool(val_is_true(l) && val_is_true(r)); break;
                case OP_OR:  res = val_bool(val_is_true(l) || val_is_true(r)); break;
                default: res = val_null();
            }
            val_free(l); val_free(r);
            return res;
        }
        case AST_UNOP: {
            Value v = eval_const(e->unop.operand);
            Value res;
            switch (e->unop.op) {
                case OP_NEG: res = val_neg(v); break;
                case OP_NOT: res = val_bool(!val_is_true(v)); break;
                default:     res = val_null();
            }
            val_free(v);
            return res;
        }
        case AST_IS_NULL: {
            Value v = eval_const(e->is_null.expr);
            Value res = val_bool(e->is_null.is_not ? !val_is_null(v) : val_is_null(v));
            val_free(v);
            return res;
        }
        default:
            return val_null();
    }
}

/* Replace expression e with a literal if it's constant.
   Returns new node (caller must ast_free old if replaced), or e itself. */
static AstNode *fold_expr(AstNode *e, int *changed) {
    if (!e) return e;
    /* First recursively fold children */
    switch (e->kind) {
        case AST_BINOP:
            e->binop.left  = fold_expr(e->binop.left,  changed);
            e->binop.right = fold_expr(e->binop.right, changed);
            break;
        case AST_UNOP:
            e->unop.operand = fold_expr(e->unop.operand, changed);
            break;
        case AST_IS_NULL:
            e->is_null.expr = fold_expr(e->is_null.expr, changed);
            break;
        case AST_BETWEEN:
            e->between.expr = fold_expr(e->between.expr, changed);
            e->between.lo   = fold_expr(e->between.lo,   changed);
            e->between.hi   = fold_expr(e->between.hi,   changed);
            break;
        default: break;
    }
    if (is_const(e) && e->kind != AST_LITERAL) {
        Value v = eval_const(e);
        AstNode *lit = ast_literal(e->line, v);
        val_free(v);
        *changed = 1;
        return lit;
    }
    return e;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Rule 1: Constant Folding                                           */
/* ══════════════════════════════════════════════════════════════════ */
static PlanNode *rule_const_fold(PlanNode *node, int *changed, OptLog *log) {
    if (!node) return node;
    node->left  = rule_const_fold(node->left,  changed, log);
    node->right = rule_const_fold(node->right, changed, log);

    if (node->type == PLAN_FILTER) {
        int local = 0;
        node->filter.predicate = fold_expr(node->filter.predicate, &local);
        if (local) { *changed = 1; optlog_push(log, "Constant Folding"); }
    }
    if (node->type == PLAN_JOIN && node->join.condition) {
        int local = 0;
        node->join.condition = fold_expr(node->join.condition, &local);
        if (local) { *changed = 1; optlog_push(log, "Constant Folding"); }
    }
    return node;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Rule 2: Boolean Simplification                                     */
/* ══════════════════════════════════════════════════════════════════ */
static int is_literal_bool(AstNode *e, int bval) {
    return e && e->kind == AST_LITERAL &&
           e->literal.type == VAL_BOOL && e->literal.bval == bval;
}
static int is_literal_true(AstNode *e)  { return is_literal_bool(e, 1); }
static int is_literal_false(AstNode *e) { return is_literal_bool(e, 0); }

static AstNode *simplify_expr(AstNode *e, int *changed) {
    if (!e) return e;
    /* Recurse first */
    switch (e->kind) {
        case AST_BINOP:
            e->binop.left  = simplify_expr(e->binop.left,  changed);
            e->binop.right = simplify_expr(e->binop.right, changed);
            break;
        case AST_UNOP:
            e->unop.operand = simplify_expr(e->unop.operand, changed);
            break;
        default: break;
    }

    if (e->kind == AST_BINOP) {
        AstNode *l = e->binop.left, *r = e->binop.right;
        if (e->binop.op == OP_AND) {
            if (is_literal_true(l)) {
                /* TRUE AND x → x */
                *changed = 1; return r;
            }
            if (is_literal_true(r)) {
                /* x AND TRUE → x */
                *changed = 1; return l;
            }
            if (is_literal_false(l) || is_literal_false(r)) {
                /* FALSE AND x → FALSE */
                *changed = 1; return ast_literal(e->line, val_bool(0));
            }
        }
        if (e->binop.op == OP_OR) {
            if (is_literal_false(l)) {
                /* FALSE OR x → x */
                *changed = 1; return r;
            }
            if (is_literal_false(r)) {
                /* x OR FALSE → x */
                *changed = 1; return l;
            }
            if (is_literal_true(l) || is_literal_true(r)) {
                /* x OR TRUE → TRUE */
                *changed = 1; return ast_literal(e->line, val_bool(1));
            }
        }
    }
    if (e->kind == AST_UNOP && e->unop.op == OP_NOT) {
        /* NOT NOT x → x */
        if (e->unop.operand && e->unop.operand->kind == AST_UNOP &&
            e->unop.operand->unop.op == OP_NOT) {
            *changed = 1; return e->unop.operand->unop.operand;
        }
    }
    return e;
}

static PlanNode *rule_bool_simplify(PlanNode *node, int *changed, OptLog *log) {
    if (!node) return node;
    node->left  = rule_bool_simplify(node->left,  changed, log);
    node->right = rule_bool_simplify(node->right, changed, log);

    if (node->type == PLAN_FILTER) {
        int local = 0;
        node->filter.predicate = simplify_expr(node->filter.predicate, &local);
        if (local) { *changed = 1; optlog_push(log, "Boolean Simplification"); }
    }
    return node;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Rule 3: Filter Removal (predicate is literal TRUE)                 */
/* ══════════════════════════════════════════════════════════════════ */
static PlanNode *rule_filter_removal(PlanNode *node, int *changed, OptLog *log) {
    if (!node) return node;
    node->left  = rule_filter_removal(node->left,  changed, log);
    node->right = rule_filter_removal(node->right, changed, log);

    if (node->type == PLAN_FILTER && is_literal_true(node->filter.predicate)) {
        PlanNode *child = node->left;
        node->left = NULL;
        /* Don't free the predicate AST — it's shared with the original AST */
        free(node);
        *changed = 1;
        optlog_push(log, "Filter Removal");
        return child;
    }
    return node;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Rule 4: Predicate Splitting (AND → separate Filters)               */
/* ══════════════════════════════════════════════════════════════════ */
static PlanNode *rule_pred_split(PlanNode *node, int *changed, OptLog *log) {
    if (!node) return node;
    node->left  = rule_pred_split(node->left,  changed, log);
    node->right = rule_pred_split(node->right, changed, log);

    if (node->type == PLAN_FILTER &&
        node->filter.predicate &&
        node->filter.predicate->kind == AST_BINOP &&
        node->filter.predicate->binop.op == OP_AND) {

        AstNode *pred  = node->filter.predicate;
        AstNode *left  = pred->binop.left;
        AstNode *right = pred->binop.right;

        /* Build two separate filters */
        PlanNode *inner = node->left; node->left = NULL;
        free(node);
        PlanNode *f1 = plan_filter(inner, left);
        PlanNode *f2 = plan_filter(f1,    right);
        *changed = 1;
        optlog_push(log, "Predicate Splitting");
        return f2;
    }
    return node;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Rule 5: Predicate Pushdown                                         */
/* ══════════════════════════════════════════════════════════════════ */

/* Collect all table indices referenced by an expression */
static void collect_tables(AstNode *e, int *mask, int max) {
    if (!e) return;
    switch (e->kind) {
        case AST_COLUMN:
            if (e->col.tbl_idx >= 0 && e->col.tbl_idx < max)
                mask[e->col.tbl_idx] = 1;
            break;
        case AST_BINOP:
            collect_tables(e->binop.left,  mask, max);
            collect_tables(e->binop.right, mask, max);
            break;
        case AST_UNOP:
            collect_tables(e->unop.operand, mask, max);
            break;
        case AST_IS_NULL:
            collect_tables(e->is_null.expr, mask, max);
            break;
        case AST_BETWEEN:
            collect_tables(e->between.expr, mask, max);
            collect_tables(e->between.lo,   mask, max);
            collect_tables(e->between.hi,   mask, max);
            break;
        case AST_IN:
            collect_tables(e->in_expr.expr, mask, max);
            if (e->in_expr.list)
                for (int i=0;i<e->in_expr.list->list.count;i++)
                    collect_tables(e->in_expr.list->list.items[i], mask, max);
            break;
        default: break;
    }
}

/* Returns set of table indices reachable from a plan subtree */
static void scan_tables(PlanNode *node, int *mask, int max) {
    if (!node) return;
    if (node->type == PLAN_SCAN) {
        if (node->scan.tbl_idx < max)
            mask[node->scan.tbl_idx] = 1;
        return;
    }
    scan_tables(node->left,  mask, max);
    scan_tables(node->right, mask, max);
}

/* Returns 1 if all tables in pred_mask are in sub_mask */
static int tables_subset(int *pred_mask, int *sub_mask, int max) {
    for (int i = 0; i < max; i++)
        if (pred_mask[i] && !sub_mask[i]) return 0;
    return 1;
}

#define MAX_TABLES 32

/* Try to push a filter predicate down through a join */
static PlanNode *push_filter_through_join(AstNode *pred,
                                          PlanNode *join_node,
                                          int *pushed,
                                          int max_tables,
                                          OptLog *log) {
    /* Determine which tables the predicate references */
    int pred_mask[MAX_TABLES] = {0};
    collect_tables(pred, pred_mask, max_tables);

    /* Get which tables are on each side */
    int left_mask[MAX_TABLES]  = {0};
    int right_mask[MAX_TABLES] = {0};
    scan_tables(join_node->left,  left_mask,  max_tables);
    scan_tables(join_node->right, right_mask, max_tables);

    if (tables_subset(pred_mask, left_mask, max_tables)) {
        /* Can push to left side */
        /* Do NOT push to right side of a LEFT JOIN */
        join_node->left = plan_filter(join_node->left, pred);
        *pushed = 1;
        optlog_push(log, "Predicate Pushdown");
        return join_node;
    }
    if (join_node->join.join_type == JOIN_INNER &&
        tables_subset(pred_mask, right_mask, max_tables)) {
        /* Push to right side (only for INNER JOIN) */
        join_node->right = plan_filter(join_node->right, pred);
        *pushed = 1;
        optlog_push(log, "Predicate Pushdown");
        return join_node;
    }
    /* Cannot push: re-wrap with filter above join */
    return plan_filter(join_node, pred);
}

static PlanNode *rule_pred_pushdown(PlanNode *node, int *changed,
                                    OptLog *log, int max_tables) {
    if (!node) return node;
    node->left  = rule_pred_pushdown(node->left,  changed, log, max_tables);
    node->right = rule_pred_pushdown(node->right, changed, log, max_tables);

    if (node->type == PLAN_FILTER && node->left &&
        node->left->type == PLAN_JOIN) {
        AstNode  *pred = node->filter.predicate;
        PlanNode *join = node->left;
        node->filter.predicate = NULL;
        node->left             = NULL;
        free(node);
        int pushed = 0;
        PlanNode *result = push_filter_through_join(pred, join, &pushed,
                                                    max_tables, log);
        if (pushed) *changed = 1;
        return result;
    }
    return node;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Rule 7: Join Algorithm Selection                                   */
/* ══════════════════════════════════════════════════════════════════ */

/* Returns 1 if the condition is an equality between columns from both sides */
static int is_equijoin(AstNode *cond, PlanNode *left, PlanNode *right,
                       int max_tables) {
    if (!cond || cond->kind != AST_BINOP || cond->binop.op != OP_EQ)
        return 0;
    AstNode *l = cond->binop.left, *r = cond->binop.right;
    if (l->kind != AST_COLUMN || r->kind != AST_COLUMN) return 0;

    int lm[MAX_TABLES]={0}, rm[MAX_TABLES]={0};
    int lml[MAX_TABLES]={0}, rmr[MAX_TABLES]={0};
    collect_tables(l, lm, max_tables);
    collect_tables(r, rm, max_tables);
    scan_tables(left,  lml, max_tables);
    scan_tables(right, rmr, max_tables);

    int l_in_left  = tables_subset(lm,lml,max_tables);
    int r_in_right = tables_subset(rm,rmr,max_tables);
    int l_in_right = tables_subset(lm,rmr,max_tables);
    int r_in_left  = tables_subset(rm,lml,max_tables);

    return (l_in_left && r_in_right) || (l_in_right && r_in_left);
}

static PlanNode *rule_join_algo(PlanNode *node, int *changed,
                                OptLog *log, int max_tables) {
    if (!node) return node;
    node->left  = rule_join_algo(node->left,  changed, log, max_tables);
    node->right = rule_join_algo(node->right, changed, log, max_tables);

    if (node->type == PLAN_JOIN &&
        node->join.algo == JOIN_ALGO_NL &&
        is_equijoin(node->join.condition,
                    node->left, node->right, max_tables)) {
        node->join.algo = JOIN_ALGO_HASH;
        *changed = 1;
        optlog_push(log, "Join Algorithm Selection (Hash Join)");
    }
    return node;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Rule 8 (Bonus): Join Reordering by row count estimate             */
/* ══════════════════════════════════════════════════════════════════ */
static long long subtree_row_estimate(PlanNode *node) {
    if (!node) return 1;
    if (node->type == PLAN_SCAN) return node->scan.table->nrows;
    long long l = subtree_row_estimate(node->left);
    long long r = subtree_row_estimate(node->right);
    if (node->type == PLAN_JOIN) return l * r / 10; /* rough estimate */
    return l > r ? l : r;
}

static PlanNode *rule_join_reorder(PlanNode *node, int *changed,
                                   OptLog *log) {
    if (!node) return node;
    node->left  = rule_join_reorder(node->left,  changed, log);
    node->right = rule_join_reorder(node->right, changed, log);

    /* Only reorder INNER joins where both children are simple scans */
    if (node->type == PLAN_JOIN &&
        node->join.join_type == JOIN_INNER &&
        node->left  && node->left->type  == PLAN_SCAN &&
        node->right && node->right->type == PLAN_SCAN) {
        long long le = subtree_row_estimate(node->left);
        long long re = subtree_row_estimate(node->right);
        if (re < le) {
            /* Swap: put smaller table on left (build side for hash join) */
            PlanNode *tmp = node->left;
            node->left    = node->right;
            node->right   = tmp;
            *changed = 1;
            optlog_push(log, "Join Reordering");
        }
    }
    return node;
}

/* ══════════════════════════════════════════════════════════════════ */
/* Optimization driver                                                */
/* ══════════════════════════════════════════════════════════════════ */
PlanNode *optimizer_run(PlanNode *root, SemCtx *ctx, OptLog *log) {
    int max_tables = ctx ? ctx->ntables : MAX_TABLES;
    int changed;
    do {
        changed = 0;
        root = rule_const_fold    (root, &changed, log);
        root = rule_bool_simplify (root, &changed, log);
        root = rule_filter_removal(root, &changed, log);
        root = rule_pred_split    (root, &changed, log);
        root = rule_pred_pushdown (root, &changed, log, max_tables);
        root = rule_join_algo     (root, &changed, log, max_tables);
        root = rule_join_reorder  (root, &changed, log);
    } while (changed);
    return root;
}
