/*
 * parser.y — Bison LALR(1) grammar for the SQL subset.
 *
 * Role in compiler pipeline: Phase 2 – Syntax Analysis.
 * Converts the token stream from the lexer into an Abstract Syntax Tree.
 *
 * Precedence (lowest  →  highest):
 *   OR  <  AND  <  NOT  <  comparisons  <  +/-  <  *  /  <  unary minus
 *
 * Target: 0 shift/reduce, 0 reduce/reduce conflicts.
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "value.h"
#include "util.h"

extern int  yylex(void);
extern int  yyline;
extern int  yytoken_col;

/* Parse result — written by the start rule */
AstNode *g_parse_result = NULL;
int      g_parse_error  = 0;

/* Forward declaration */
void yyerror(const char *msg);

/* Temporary storage for ORDER BY and LIMIT passed up the grammar */
static AstNode *s_order_by  = NULL;
static int      s_has_limit = 0;
static long long s_limit_val = 0;
%}

/* ── Semantic value types ──────────────────────────────────────────── */
%union {
    long long  ival;
    double     fval;
    char      *sval;
    AstNode   *node;
    JoinType   jtype;
    int        inum;
}

/* ── Tokens ────────────────────────────────────────────────────────── */
%token <sval>  IDENT STRING_LIT
%token <ival>  INT_LIT
%token <fval>  FLOAT_LIT

%token SELECT DISTINCT FROM WHERE AS
%token JOIN INNER LEFT ON
%token ORDER BY ASC DESC LIMIT
%token AND OR NOT LIKE BETWEEN IN IS
%token NULLTOK TRUE_LIT FALSE_LIT
%token EXPLAIN ANALYZE
%token EQ NEQ LT GT LE GE
%token PLUS MINUS STAR SLASH
%token LPAREN RPAREN COMMA DOT SEMI

/* ── Operator precedence (lowest → highest) ───────────────────────── */
%left  OR
%left  AND
%right NOT
%nonassoc EQ NEQ LT GT LE GE
%nonassoc IS IN LIKE BETWEEN
%left  PLUS MINUS
%left  STAR SLASH
%precedence UMINUS

/* ── Non-terminal types ────────────────────────────────────────────── */
%start program
%type <node>  program statement select_stmt
%type <node>  col_list col_expr
%type <node>  join_list join_clause
%type <node>  opt_where
%type <node>  expr in_value_list b_expr
%type <node>  opt_order_by order_list order_item
%type <inum>  opt_distinct opt_asc_desc opt_limit
%type <jtype> join_type
%type <sval>  table_ref opt_alias

/* Enable verbose error messages */
%define parse.error verbose

%%

/* ════════════════════════════════════════════════════════════════════ */
/* Top-level program                                                    */
/* ════════════════════════════════════════════════════════════════════ */
program
    : statement           { g_parse_result = $1; $$ = $1; }
    | statement SEMI      { g_parse_result = $1; $$ = $1; }
    | EXPLAIN statement SEMI
        {
            /* Wrap with a special marker reusing literal for EXPLAIN flag */
            /* For simplicity we store the query and mark it at runtime via a global */
            g_parse_result = $2; $$ = $2;
            extern int g_explain_mode, g_explain_analyze;
            g_explain_mode = 1; g_explain_analyze = 0;
        }
    | EXPLAIN ANALYZE statement SEMI
        {
            g_parse_result = $3; $$ = $3;
            extern int g_explain_mode, g_explain_analyze;
            g_explain_mode = 1; g_explain_analyze = 1;
        }
    ;

statement
    : select_stmt  { $$ = $1; }
    ;

/* ════════════════════════════════════════════════════════════════════ */
/* SELECT statement                                                     */
/* ════════════════════════════════════════════════════════════════════ */
select_stmt
    : SELECT opt_distinct col_list
      FROM table_ref opt_alias
      join_list
      opt_where
      opt_order_by
      opt_limit
        {
            AstNode *node = ast_select(
                yyline, $2,
                $3,          /* column list */
                $5, $6,      /* from table + alias */
                $7,          /* joins */
                $8,          /* WHERE */
                $9,          /* ORDER BY */
                $10 >= 0,    /* has LIMIT */
                (long long)$10
            );
            free($5);
            if ($6) free($6);
            $$ = node;
        }
    ;

opt_distinct
    : DISTINCT   { $$ = 1; }
    | %empty     { $$ = 0; }
    ;

/* ── Column list ───────────────────────────────────────────────────── */
col_list
    : STAR
        {
            $$ = NULL;  /* NULL means SELECT * */
        }
    | col_expr
        {
            AstNode *lst = ast_list(yyline);
            ast_list_push(lst, $1);
            $$ = lst;
        }
    | col_list COMMA col_expr
        {
            ast_list_push($1, $3);
            $$ = $1;
        }
    ;

col_expr
    : expr AS IDENT
        {
            $$ = ast_alias(yyline, $1, $3);
            free($3);
        }
    | expr
        { $$ = $1; }
    ;

/* ── Table reference with optional alias ──────────────────────────── */
table_ref
    : IDENT  { $$ = $1; }
    ;

opt_alias
    : AS IDENT   { $$ = $2; }
    | IDENT      { $$ = $1; }
    | %empty     { $$ = NULL; }
    ;

/* ── JOIN clauses ──────────────────────────────────────────────────── */
join_list
    : join_list join_clause
        {
            if ($1 == NULL) {
                AstNode *lst = ast_list(yyline);
                ast_list_push(lst, $2);
                $$ = lst;
            } else {
                ast_list_push($1, $2);
                $$ = $1;
            }
        }
    | %empty  { $$ = NULL; }
    ;

join_clause
    : join_type JOIN table_ref opt_alias ON expr
        {
            $$ = ast_join(yyline, $1, $3, $4, $6);
            free($3);
            if ($4) free($4);
        }
    ;

join_type
    : INNER      { $$ = JOIN_INNER; }
    | LEFT       { $$ = JOIN_LEFT; }
    | %empty     { $$ = JOIN_INNER; }
    ;

/* ── WHERE clause ──────────────────────────────────────────────────── */
opt_where
    : WHERE expr  { $$ = $2; }
    | %empty      { $$ = NULL; }
    ;

/* ── ORDER BY clause ───────────────────────────────────────────────── */
opt_order_by
    : ORDER BY order_list  { $$ = $3; }
    | %empty               { $$ = NULL; }
    ;

order_list
    : order_item
        {
            AstNode *lst = ast_list(yyline);
            ast_list_push(lst, $1);
            $$ = lst;
        }
    | order_list COMMA order_item
        {
            ast_list_push($1, $3);
            $$ = $1;
        }
    ;

order_item
    : expr opt_asc_desc
        { $$ = ast_order_item(yyline, $1, $2); }
    ;

opt_asc_desc
    : ASC    { $$ = 1; }
    | DESC   { $$ = 0; }
    | %empty { $$ = 1; }
    ;

/* ── LIMIT clause ──────────────────────────────────────────────────── */
opt_limit
    : LIMIT INT_LIT  { $$ = (int)$2; }
    | %empty         { $$ = -1; }
    ;

/* ════════════════════════════════════════════════════════════════════ */
/* Expressions                                                          */
/* ════════════════════════════════════════════════════════════════════ */
expr
    /* ── Logical ────────────────────────────────────────────────── */
    : expr OR expr
        { $$ = ast_binop(yyline, OP_OR, $1, $3); }
    | expr AND expr
        { $$ = ast_binop(yyline, OP_AND, $1, $3); }
    | NOT expr
        { $$ = ast_unop(yyline, OP_NOT, $2); }

    /* ── Comparisons ────────────────────────────────────────────── */
    | expr EQ  expr  { $$ = ast_binop(yyline, OP_EQ,  $1, $3); }
    | expr NEQ expr  { $$ = ast_binop(yyline, OP_NEQ, $1, $3); }
    | expr LT  expr  { $$ = ast_binop(yyline, OP_LT,  $1, $3); }
    | expr GT  expr  { $$ = ast_binop(yyline, OP_GT,  $1, $3); }
    | expr LE  expr  { $$ = ast_binop(yyline, OP_LE,  $1, $3); }
    | expr GE  expr  { $$ = ast_binop(yyline, OP_GE,  $1, $3); }

    /* ── IS NULL / IS NOT NULL ───────────────────────────────────── */
    | expr IS NULLTOK
        { $$ = ast_is_null(yyline, $1, 0); }
    | expr IS NOT NULLTOK
        { $$ = ast_is_null(yyline, $1, 1); }

    /* ── LIKE ────────────────────────────────────────────────────── */
    | expr LIKE expr
        { $$ = ast_like(yyline, $1, $3, 0); }
    | expr NOT LIKE expr
        { $$ = ast_like(yyline, $1, $4, 1); }

    /* ── BETWEEN ─────────────────────────────────────────────────── */
    | expr BETWEEN b_expr AND b_expr
        { $$ = ast_between(yyline, $1, $3, $5, 0); }
    | expr NOT BETWEEN b_expr AND b_expr
        { $$ = ast_between(yyline, $1, $4, $6, 1); }

    /* ── IN ──────────────────────────────────────────────────────── */
    | expr IN LPAREN in_value_list RPAREN
        { $$ = ast_in(yyline, $1, $4, 0); }
    | expr NOT IN LPAREN in_value_list RPAREN
        { $$ = ast_in(yyline, $1, $5, 1); }

    /* ── Arithmetic ─────────────────────────────────────────────── */
    | expr PLUS  expr  { $$ = ast_binop(yyline, OP_ADD, $1, $3); }
    | expr MINUS expr  { $$ = ast_binop(yyline, OP_SUB, $1, $3); }
    | expr STAR  expr  { $$ = ast_binop(yyline, OP_MUL, $1, $3); }
    | expr SLASH expr  { $$ = ast_binop(yyline, OP_DIV, $1, $3); }

    /* ── Unary minus ─────────────────────────────────────────────── */
    | MINUS expr  %prec UMINUS
        { $$ = ast_unop(yyline, OP_NEG, $2); }

    /* ── Parentheses ─────────────────────────────────────────────── */
    | LPAREN expr RPAREN
        { $$ = $2; }

    /* ── Column references ──────────────────────────────────────── */
    | IDENT DOT IDENT
        { $$ = ast_column(yyline, $1, $3); free($1); free($3); }
    | IDENT
        { $$ = ast_column(yyline, NULL, $1); free($1); }

    /* ── Literals ────────────────────────────────────────────────── */
    | INT_LIT
        { $$ = ast_literal(yyline, val_int($1)); }
    | FLOAT_LIT
        { $$ = ast_literal(yyline, val_float($1)); }
    | STRING_LIT
        { Value v = val_string($1); free($1); $$ = ast_literal(yyline, v); val_free(v); }
    | TRUE_LIT
        { $$ = ast_literal(yyline, val_bool(1)); }
    | FALSE_LIT
        { $$ = ast_literal(yyline, val_bool(0)); }
    | NULLTOK
        { $$ = ast_literal(yyline, val_null()); }
    ;

/* ── IN value list ─────────────────────────────────────────────────── */
in_value_list
    : expr
        {
            AstNode *lst = ast_list(yyline);
            ast_list_push(lst, $1);
            $$ = lst;
        }
    | in_value_list COMMA expr
        {
            ast_list_push($1, $3);
            $$ = $1;
        }
    ;

/* ── Sub-expressions for BETWEEN (arithmetic/atoms, no top-level AND/OR) ─ */
b_expr
    : b_expr PLUS  b_expr  { $$ = ast_binop(yyline, OP_ADD, $1, $3); }
    | b_expr MINUS b_expr  { $$ = ast_binop(yyline, OP_SUB, $1, $3); }
    | b_expr STAR  b_expr  { $$ = ast_binop(yyline, OP_MUL, $1, $3); }
    | b_expr SLASH b_expr  { $$ = ast_binop(yyline, OP_DIV, $1, $3); }
    | MINUS b_expr %prec UMINUS
        { $$ = ast_unop(yyline, OP_NEG, $2); }
    | LPAREN expr RPAREN
        { $$ = $2; }
    | IDENT DOT IDENT
        { $$ = ast_column(yyline, $1, $3); free($1); free($3); }
    | IDENT
        { $$ = ast_column(yyline, NULL, $1); free($1); }
    | INT_LIT
        { $$ = ast_literal(yyline, val_int($1)); }
    | FLOAT_LIT
        { $$ = ast_literal(yyline, val_float($1)); }
    | STRING_LIT
        { Value v = val_string($1); free($1); $$ = ast_literal(yyline, v); val_free(v); }
    | TRUE_LIT
        { $$ = ast_literal(yyline, val_bool(1)); }
    | FALSE_LIT
        { $$ = ast_literal(yyline, val_bool(0)); }
    | NULLTOK
        { $$ = ast_literal(yyline, val_null()); }
    ;

%%

/* ── Error handler ─────────────────────────────────────────────────── */
void yyerror(const char *msg) {
    extern char *yytext;
    parse_error(yyline, yytoken_col,
                "%s (near '%s')", msg, yytext);
    g_parse_error = 1;
}

/* Globals referenced by parser actions */
int g_explain_mode    = 0;
int g_explain_analyze = 0;
