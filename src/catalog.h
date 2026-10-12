/*
 * catalog.h — Table schema registry and CSV loader.
 *
 * Role in compiler pipeline: The catalog is the "symbol table" for tables.
 * It stores schema information (column names and inferred types) and loads
 * actual row data from CSV files in data/.  The semantic analyser looks up
 * tables and columns here; the executor reads rows from it.
 */
#ifndef CATALOG_H
#define CATALOG_H

#include "value.h"

/* ── Column descriptor ─────────────────────────────────────────────── */
typedef struct {
    char    *name;
    ValType  type;   /* inferred from the CSV data */
} ColDesc;

/* ── Table descriptor ──────────────────────────────────────────────── */
typedef struct {
    char    *name;       /* logical name (matches CSV filename without .csv) */
    ColDesc *cols;
    int      ncols;

    /* Row data loaded from CSV */
    Value  **rows;       /* rows[i][j] = cell at row i, column j */
    int      nrows;
    int      rows_cap;
} TableDesc;

/* ── Catalog ───────────────────────────────────────────────────────── */
typedef struct {
    TableDesc **tables;
    int         ntables;
    int         cap;
    char       *data_dir;   /* path to data/ directory */
} Catalog;

/* Create an empty catalog pointing at data_dir */
Catalog    *catalog_new(const char *data_dir);
void        catalog_free(Catalog *cat);

/* Load a CSV file; returns the TableDesc or NULL on error */
TableDesc  *catalog_load_table(Catalog *cat, const char *table_name);

/* Look up a table by name (case-insensitive); returns NULL if not found */
TableDesc  *catalog_find_table(Catalog *cat, const char *name);

/* Look up a column inside a table; returns index or -1 */
int         catalog_find_col(TableDesc *tbl, const char *col_name);

/* Row count estimate for query planning */
long long   catalog_row_estimate(TableDesc *tbl);

#endif /* CATALOG_H */
