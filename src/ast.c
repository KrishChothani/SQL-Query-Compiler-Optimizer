/*
 * ast.c — AST node constructors, destructor and pretty-printer.
 */
#include "ast.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ──────────────────────────────────────────────── */
static AstNode *alloc_node(NodeKind kind, int line) {
    AstNode *n = xmalloc(sizeof(AstNode));
    memset(n, 0, sizeof(AstNode));
    n->kind = kind;
    n->line = line;
    return n;
}

/* ── Constructors ──────────────────────────────────────────────────── */
AstNode *ast_select(int line, int distinct,
                    AstNode *cols, char *from_table, char *from_alias,
                    AstNode *joins, AstNode *where,
                    AstNode *order_by, int has_limit, long long lim) {
    AstNode *n = alloc_node(AST_SELECT, line);
    n->select.distinct    = distinct;
    n->select.cols        = cols;
    n->select.from_table  = from_table ? xstrdup(from_table) : NULL;
    n->select.from_alias  = from_alias ? xstrdup(from_alias) : NULL;
    n->select.joins       = joins;
    n->select.where       = where;
    n->select.order_by    = order_by;
    n->select.has_limit   = has_limit;
    n->select.limit_val   = lim;
    return n;
}
AstNode *ast_join(int line, JoinType jt, char *table, char *alias, AstNode *on) {
    AstNode *n = alloc_node(AST_JOIN, line);
    n->join.join_type = jt;
    n->join.table     = table ? xstrdup(table) : NULL;
    n->join.alias     = alias ? xstrdup(alias) : NULL;
    n->join.on        = on;
    return n;
}
AstNode *ast_column(int line, char *table, char *name) {
    AstNode *n = alloc_node(AST_COLUMN, line);
    n->col.table   = table ? xstrdup(table) : NULL;
    n->col.name    = xstrdup(name);
    n->col.tbl_idx = -1;
    n->col.col_idx = -1;
    return n;
}
AstNode *ast_literal(int line, Value v) {
    AstNode *n = alloc_node(AST_LITERAL, line);
    n->literal = val_copy(v);
    return n;
}
AstNode *ast_binop(int line, OpKind op, AstNode *l, AstNode *r) {
    AstNode *n = alloc_node(AST_BINOP, line);
    n->binop.op    = op;
    n->binop.left  = l;
    n->binop.right = r;
    return n;
}
AstNode *ast_unop(int line, OpKind op, AstNode *operand) {
    AstNode *n = alloc_node(AST_UNOP, line);
    n->unop.op      = op;
    n->unop.operand = operand;
    return n;
}
AstNode *ast_is_null(int line, AstNode *expr, int is_not) {
    AstNode *n = alloc_node(AST_IS_NULL, line);
    n->is_null.expr   = expr;
    n->is_null.is_not = is_not;
    return n;
}
AstNode *ast_like(int line, AstNode *expr, AstNode *pat, int negated) {
    AstNode *n = alloc_node(AST_LIKE, line);
    n->like.expr    = expr;
    n->like.pattern = pat;
    n->like.negated = negated;
    return n;
}
AstNode *ast_between(int line, AstNode *expr, AstNode *lo, AstNode *hi, int neg) {
    AstNode *n = alloc_node(AST_BETWEEN, line);
    n->between.expr    = expr;
    n->between.lo      = lo;
    n->between.hi      = hi;
    n->between.negated = neg;
    return n;
}
AstNode *ast_in(int line, AstNode *expr, AstNode *list, int negated) {
    AstNode *n = alloc_node(AST_IN, line);
    n->in_expr.expr    = expr;
    n->in_expr.list    = list;
    n->in_expr.negated = negated;
    return n;
}
AstNode *ast_alias(int line, AstNode *expr, char *name) {
    AstNode *n = alloc_node(AST_ALIAS, line);
    n->alias.expr = expr;
    n->alias.name = xstrdup(name);
    return n;
}
AstNode *ast_list(int line) {
    AstNode *n = alloc_node(AST_LIST, line);
    n->list.items = NULL; n->list.count = 0; n->list.cap = 0;
    return n;
}
void ast_list_push(AstNode *list, AstNode *item) {
    if (list->list.count == list->list.cap) {
        list->list.cap = list->list.cap ? list->list.cap * 2 : 4;
        list->list.items = xrealloc(list->list.items,
                                    (size_t)list->list.cap * sizeof(AstNode*));
    }
    list->list.items[list->list.count++] = item;
}
AstNode *ast_order_item(int line, AstNode *expr, int asc) {
    AstNode *n = alloc_node(AST_ORDER_ITEM, line);
    n->order_item.expr = expr;
    n->order_item.asc  = asc;
    return n;
}

/* ── Free ──────────────────────────────────────────────────────────── */
void ast_free(AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_SELECT:
            ast_free(n->select.cols);
            free(n->select.from_table);
            free(n->select.from_alias);
            ast_free(n->select.joins);
            ast_free(n->select.where);
            ast_free(n->select.order_by);
            break;
        case AST_JOIN:
            free(n->join.table);
            free(n->join.alias);
            ast_free(n->join.on);
            break;
        case AST_COLUMN:
            free(n->col.table);
            free(n->col.name);
            break;
        case AST_LITERAL:
            val_free(n->literal);
            break;
        case AST_BINOP:
            ast_free(n->binop.left);
            ast_free(n->binop.right);
            break;
        case AST_UNOP:
            ast_free(n->unop.operand);
            break;
        case AST_IS_NULL:
            ast_free(n->is_null.expr);
            break;
        case AST_LIKE:
            ast_free(n->like.expr);
            ast_free(n->like.pattern);
            break;
        case AST_BETWEEN:
            ast_free(n->between.expr);
            ast_free(n->between.lo);
            ast_free(n->between.hi);
            break;
        case AST_IN:
            ast_free(n->in_expr.expr);
            ast_free(n->in_expr.list);
            break;
        case AST_ALIAS:
            ast_free(n->alias.expr);
            free(n->alias.name);
            break;
        case AST_LIST:
            for (int i = 0; i < n->list.count; i++) ast_free(n->list.items[i]);
            free(n->list.items);
            break;
        case AST_ORDER_ITEM:
            ast_free(n->order_item.expr);
            break;
    }
    free(n);
}

/* ── Pretty-printer helpers ────────────────────────────────────────── */
const char *op_name(OpKind op) {
    switch (op) {
        case OP_EQ:  return "=";  case OP_NEQ: return "!=";
        case OP_LT:  return "<";  case OP_GT:  return ">";
        case OP_LE:  return "<="; case OP_GE:  return ">=";
        case OP_AND: return "AND";case OP_OR:  return "OR";
        case OP_NOT: return "NOT";
        case OP_ADD: return "+";  case OP_SUB: return "-";
        case OP_MUL: return "*";  case OP_DIV: return "/";
        case OP_NEG: return "UNARY-";
    }
    return "?";
}

static void indent_fprint(FILE *out, int indent) {
    for (int i = 0; i < indent; i++) fprintf(out, "  ");
}

void ast_fprint(FILE *out, AstNode *n, int indent) {
    if (!n) return;
    indent_fprint(out, indent);
    switch (n->kind) {
        case AST_SELECT: {
            fprintf(out, "SELECT%s\n", n->select.distinct ? " DISTINCT" : "");
            indent_fprint(out, indent+1); fprintf(out, "Columns:\n");
            if (n->select.cols)
                ast_fprint(out, n->select.cols, indent+2);
            else {
                indent_fprint(out, indent+2); fprintf(out, "*\n");
            }
            indent_fprint(out, indent+1); fprintf(out, "FROM: %s", n->select.from_table ? n->select.from_table : "(none)");
            if (n->select.from_alias) fprintf(out, " AS %s", n->select.from_alias);
            fprintf(out, "\n");
            if (n->select.joins) {
                indent_fprint(out, indent+1); fprintf(out, "Joins:\n");
                ast_fprint(out, n->select.joins, indent+2);
            }
            if (n->select.where) {
                indent_fprint(out, indent+1); fprintf(out, "WHERE:\n");
                ast_fprint(out, n->select.where, indent+2);
            }
            if (n->select.order_by) {
                indent_fprint(out, indent+1); fprintf(out, "ORDER BY:\n");
                ast_fprint(out, n->select.order_by, indent+2);
            }
            if (n->select.has_limit) {
                indent_fprint(out, indent+1); fprintf(out, "LIMIT: %lld\n", n->select.limit_val);
            }
            break;
        }
        case AST_JOIN:
            fprintf(out, "%s JOIN %s",
                    n->join.join_type == JOIN_INNER ? "INNER" : "LEFT",
                    n->join.table ? n->join.table : "?");
            if (n->join.alias) fprintf(out, " AS %s", n->join.alias);
            fprintf(out, "\n");
            if (n->join.on) {
                indent_fprint(out, indent+1); fprintf(out, "ON:\n");
                ast_fprint(out, n->join.on, indent+2);
            }
            break;
        case AST_COLUMN:
            if (n->col.table) fprintf(out, "%s.", n->col.table);
            fprintf(out, "%s  [line %d]\n", n->col.name, n->line);
            break;
        case AST_LITERAL: {
            char *s = val_to_string(n->literal);
            fprintf(out, "Literal(%s)  [line %d]\n", s, n->line);
            free(s);
            break;
        }
        case AST_BINOP:
            fprintf(out, "BinOp(%s)  [line %d]\n", op_name(n->binop.op), n->line);
            ast_fprint(out, n->binop.left,  indent+1);
            ast_fprint(out, n->binop.right, indent+1);
            break;
        case AST_UNOP:
            fprintf(out, "UnOp(%s)  [line %d]\n", op_name(n->unop.op), n->line);
            ast_fprint(out, n->unop.operand, indent+1);
            break;
        case AST_IS_NULL:
            fprintf(out, "IS%s NULL  [line %d]\n", n->is_null.is_not?" NOT":"", n->line);
            ast_fprint(out, n->is_null.expr, indent+1);
            break;
        case AST_LIKE:
            fprintf(out, "%sLIKE  [line %d]\n", n->like.negated?"NOT ":"", n->line);
            ast_fprint(out, n->like.expr,    indent+1);
            ast_fprint(out, n->like.pattern, indent+1);
            break;
        case AST_BETWEEN:
            fprintf(out, "%sBETWEEN  [line %d]\n", n->between.negated?"NOT ":"", n->line);
            ast_fprint(out, n->between.expr, indent+1);
            ast_fprint(out, n->between.lo,   indent+1);
            ast_fprint(out, n->between.hi,   indent+1);
            break;
        case AST_IN:
            fprintf(out, "%sIN  [line %d]\n", n->in_expr.negated?"NOT ":"", n->line);
            ast_fprint(out, n->in_expr.expr, indent+1);
            ast_fprint(out, n->in_expr.list, indent+1);
            break;
        case AST_ALIAS:
            fprintf(out, "AS %s  [line %d]\n", n->alias.name, n->line);
            ast_fprint(out, n->alias.expr, indent+1);
            break;
        case AST_LIST:
            fprintf(out, "List[%d]\n", n->list.count);
            for (int i = 0; i < n->list.count; i++)
                ast_fprint(out, n->list.items[i], indent+1);
            break;
        case AST_ORDER_ITEM:
            fprintf(out, "OrderItem(%s)  [line %d]\n", n->order_item.asc?"ASC":"DESC", n->line);
            ast_fprint(out, n->order_item.expr, indent+1);
            break;
    }
}

void ast_print(AstNode *n, int indent) {
    ast_fprint(stdout, n, indent);
}

char *ast_to_str(AstNode *n) {
    if (!n) return xstrdup("");
    char *buf = NULL; size_t sz = 0;
    FILE *f = open_memstream(&buf, &sz);
    if (!f) return xstrdup("");
    ast_fprint(f, n, 0);
    fclose(f);
    return buf ? buf : xstrdup("");
}
