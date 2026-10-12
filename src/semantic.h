/*
 * semantic.h — Symbol table, name resolution and type checking.
 *
 * Role in compiler pipeline: Phase 3 – Semantic Analysis.
 * After parsing, the AST is syntactically correct but not necessarily
 * semantically valid.  This phase:
 *  - Resolves table references against the catalog
 *  - Resolves column references (qualified and unqualified)
 *  - Annotates AST_COLUMN nodes with tbl_idx and col_idx
 *  - Performs type checking on expressions
 *  - Reports "did you mean?" suggestions using edit distance
 */
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include "catalog.h"
#include "value.h"

/* ── Resolved table entry ──────────────────────────────────────────── */
typedef struct {
    char      *alias;     /* alias used in query (or table name if none) */
    TableDesc *table;     /* pointer into catalog */
    int        idx;       /* sequential index (0, 1, ...) */
} ResolvedTable;

/* ── Semantic context ──────────────────────────────────────────────── */
typedef struct {
    Catalog        *catalog;
    ResolvedTable  *tables;     /* array of resolved tables */
    int             ntables;
    int             error_count;
} SemCtx;

/* Run semantic analysis.  Returns 0 on success, -1 if any error.
   Populates ctx->tables with the resolved table list.
   Annotates AST_COLUMN nodes in place. */
int semantic_check(AstNode *ast, Catalog *catalog, SemCtx *ctx);

/* Free resources inside a SemCtx (does NOT free the catalog). */
void semctx_free(SemCtx *ctx);

/* Type of an expression (after semantic analysis). */
ValType expr_type(AstNode *expr, SemCtx *ctx);

#endif /* SEMANTIC_H */
