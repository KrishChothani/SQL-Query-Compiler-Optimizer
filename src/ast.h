/*
 * ast.h — Abstract Syntax Tree node definitions.
 *
 * Role in compiler pipeline: The AST is the central Intermediate
 * Representation produced by the parser (Phase 2) and consumed by
 * the semantic analyser (Phase 3) and planner (Phase 4).
 *
 * Each node is a tagged union (NodeKind discriminant + union of payloads).
 * Constructor functions (ast_*) allocate on the heap so ownership is clear.
 * ast_free() performs a full recursive release.
 * ast_print() renders the tree as an indented text diagram.
 */
#ifndef AST_H
#define AST_H

#include <stdio.h>
#include "value.h"

/* ── Node kinds ────────────────────────────────────────────────────── */
typedef enum {
    /* Statement */
    AST_SELECT,

    /* Expression */
    AST_COLUMN,        /* table.col or just col */
    AST_LITERAL,       /* integer / float / string / bool / NULL */
    AST_BINOP,         /* binary operator */
    AST_UNOP,          /* unary operator */
    AST_IS_NULL,       /* expr IS [NOT] NULL */
    AST_LIKE,          /* expr LIKE pattern */
    AST_BETWEEN,       /* expr BETWEEN lo AND hi */
    AST_IN,            /* expr IN (v1, v2, ...) */
    AST_ALIAS,         /* expr AS name  (used in SELECT list) */

    /* List helpers (used as containers) */
    AST_LIST,

    /* Join description */
    AST_JOIN,

    /* Order-by item */
    AST_ORDER_ITEM,
} NodeKind;

/* ── Binary / unary operators ──────────────────────────────────────── */
typedef enum {
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_NOT,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_NEG   /* unary minus */
} OpKind;

/* ── Join type ─────────────────────────────────────────────────────── */
typedef enum { JOIN_INNER, JOIN_LEFT } JoinType;

/* Forward declaration */
typedef struct AstNode AstNode;

/* ── Node struct ───────────────────────────────────────────────────── */
struct AstNode {
    NodeKind kind;
    int      line;   /* source line where this construct starts */

    union {
        /* AST_SELECT */
        struct {
            int      distinct;     /* 1 if DISTINCT */
            AstNode *cols;         /* AST_LIST of col exprs (or NULL for *) */
            char    *from_table;   /* main table name */
            char    *from_alias;   /* alias or NULL */
            AstNode *joins;        /* AST_LIST of AST_JOIN nodes */
            AstNode *where;        /* condition or NULL */
            AstNode *order_by;     /* AST_LIST of AST_ORDER_ITEM */
            int      has_limit;
            long long limit_val;
        } select;

        /* AST_JOIN */
        struct {
            JoinType join_type;
            char    *table;
            char    *alias;
            AstNode *on;           /* join condition */
        } join;

        /* AST_COLUMN */
        struct {
            char *table;     /* qualifier, may be NULL */
            char *name;
            /* Filled in by semantic analyser: */
            int   tbl_idx;   /* index into resolved table list */
            int   col_idx;   /* column index within that table */
        } col;

        /* AST_LITERAL */
        Value literal;

        /* AST_BINOP */
        struct {
            OpKind   op;
            AstNode *left;
            AstNode *right;
        } binop;

        /* AST_UNOP */
        struct {
            OpKind   op;
            AstNode *operand;
        } unop;

        /* AST_IS_NULL */
        struct {
            AstNode *expr;
            int      is_not;   /* 1 = IS NOT NULL */
        } is_null;

        /* AST_LIKE */
        struct {
            AstNode *expr;
            AstNode *pattern;  /* always a string literal */
            int      negated;
        } like;

        /* AST_BETWEEN */
        struct {
            AstNode *expr;
            AstNode *lo;
            AstNode *hi;
            int      negated;
        } between;

        /* AST_IN */
        struct {
            AstNode *expr;
            AstNode *list;   /* AST_LIST of values */
            int      negated;
        } in_expr;

        /* AST_ALIAS */
        struct {
            AstNode *expr;
            char    *name;
        } alias;

        /* AST_LIST */
        struct {
            AstNode **items;
            int       count;
            int       cap;
        } list;

        /* AST_ORDER_ITEM */
        struct {
            AstNode *expr;
            int      asc;    /* 1=ASC (default), 0=DESC */
        } order_item;
    };
};

/* ── Constructors ──────────────────────────────────────────────────── */
AstNode *ast_select  (int line, int distinct,
                      AstNode *cols, char *from_table, char *from_alias,
                      AstNode *joins, AstNode *where,
                      AstNode *order_by, int has_limit, long long lim);
AstNode *ast_join    (int line, JoinType jt, char *table, char *alias, AstNode *on);
AstNode *ast_column  (int line, char *table, char *name);
AstNode *ast_literal (int line, Value v);
AstNode *ast_binop   (int line, OpKind op, AstNode *l, AstNode *r);
AstNode *ast_unop    (int line, OpKind op, AstNode *operand);
AstNode *ast_is_null (int line, AstNode *expr, int is_not);
AstNode *ast_like    (int line, AstNode *expr, AstNode *pat, int negated);
AstNode *ast_between (int line, AstNode *expr, AstNode *lo, AstNode *hi, int neg);
AstNode *ast_in      (int line, AstNode *expr, AstNode *list, int negated);
AstNode *ast_alias   (int line, AstNode *expr, char *name);
AstNode *ast_list    (int line);
void     ast_list_push(AstNode *list, AstNode *item);
AstNode *ast_order_item(int line, AstNode *expr, int asc);

/* ── Lifecycle ─────────────────────────────────────────────────────── */
void ast_free(AstNode *node);

/* ── Pretty-printer ────────────────────────────────────────────────── */
void  ast_print(AstNode *node, int indent);
void  ast_fprint(FILE *out, AstNode *node, int indent);
char *ast_to_str(AstNode *node);

/* ── Operator name ─────────────────────────────────────────────────── */
const char *op_name(OpKind op);

#endif /* AST_H */
