/*
 * catalog.c — CSV loader and schema registry.
 *
 * Type inference algorithm:
 *   1. Try to parse the value as a long long (INT).
 *   2. Try to parse as a double (FLOAT).
 *   3. If the entire column is "true"/"false" (case-insensitive), mark BOOL.
 *   4. Otherwise STRING.
 * NULL marker: empty cell or the literal string "NULL" (case-insensitive).
 */
#include "catalog.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── CSV parsing helpers ───────────────────────────────────────────── */

/* Parse one CSV line into fields.  Returns number of fields.
   Fields are returned as heap strings; caller frees them. */
static int parse_csv_line(char *line, char ***out_fields) {
    /* Remove trailing newline */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

    int cap = 8, count = 0;
    char **fields = xmalloc((size_t)cap * sizeof(char*));
    char *p = line;
    while (1) {
        if (count == cap) {
            cap *= 2;
            fields = xrealloc(fields, (size_t)cap * sizeof(char*));
        }
        if (*p == '"') {
            /* Quoted field */
            p++;
            char buf[4096]; int bi = 0;
            while (*p && !(*p == '"' && *(p+1) != '"')) {
                if (*p == '"' && *(p+1) == '"') { buf[bi++] = '"'; p += 2; }
                else buf[bi++] = *p++;
            }
            if (*p == '"') p++;
            buf[bi] = '\0';
            fields[count++] = xstrdup(buf);
        } else {
            /* Unquoted field */
            char *start = p;
            while (*p && *p != ',') p++;
            char saved = *p; *p = '\0';
            fields[count++] = xstrdup(start);
            *p = saved;
        }
        if (*p == ',') p++;
        else break;
    }
    *out_fields = fields;
    return count;
}

/* ── Type inference ─────────────────────────────────────────────────── */
static int is_null_string(const char *s) {
    return s == NULL || s[0] == '\0' || str_iequal(s, "null");
}

static ValType infer_type(const char *s) {
    if (is_null_string(s)) return VAL_NULL;
    if (str_iequal(s, "true") || str_iequal(s, "false")) return VAL_BOOL;
    /* Try integer */
    char *end;
    strtoll(s, &end, 10);
    if (*end == '\0') return VAL_INT;
    /* Try float */
    strtod(s, &end);
    if (*end == '\0') return VAL_FLOAT;
    return VAL_STRING;
}

static Value parse_value(const char *s, ValType col_type) {
    if (is_null_string(s)) return val_null();
    switch (col_type) {
        case VAL_INT: {
            char *end;
            long long v = strtoll(s, &end, 10);
            return (*end == '\0') ? val_int(v) : val_string(s);
        }
        case VAL_FLOAT: {
            char *end;
            double v = strtod(s, &end);
            return (*end == '\0') ? val_float(v) : val_string(s);
        }
        case VAL_BOOL:
            if (str_iequal(s,"true"))  return val_bool(1);
            if (str_iequal(s,"false")) return val_bool(0);
            return val_null();
        case VAL_STRING:
            return val_string(s);
        default:
            return val_string(s);
    }
}

/* ── Catalog lifecycle ─────────────────────────────────────────────── */
Catalog *catalog_new(const char *data_dir) {
    Catalog *cat = xmalloc(sizeof(Catalog));
    cat->tables   = NULL;
    cat->ntables  = 0;
    cat->cap      = 0;
    cat->data_dir = xstrdup(data_dir);
    return cat;
}

void catalog_free(Catalog *cat) {
    if (!cat) return;
    for (int t = 0; t < cat->ntables; t++) {
        TableDesc *td = cat->tables[t];
        for (int j = 0; j < td->ncols; j++) free(td->cols[j].name);
        free(td->cols);
        for (int i = 0; i < td->nrows; i++) {
            for (int j = 0; j < td->ncols; j++) val_free(td->rows[i][j]);
            free(td->rows[i]);
        }
        free(td->rows);
        free(td->name);
        free(td);
    }
    free(cat->tables);
    free(cat->data_dir);
    free(cat);
}

/* ── Load table from CSV ────────────────────────────────────────────── */
TableDesc *catalog_load_table(Catalog *cat, const char *table_name) {
    /* Check if already loaded */
    TableDesc *existing = catalog_find_table(cat, table_name);
    if (existing) return existing;

    /* Build file path */
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.csv", cat->data_dir, table_name);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%sRuntime Error%s: Cannot open table file '%s'\n",
                COLOR_RED, COLOR_RESET, path);
        return NULL;
    }

    TableDesc *td = xmalloc(sizeof(TableDesc));
    td->name     = xstrdup(table_name);
    td->cols     = NULL;
    td->ncols    = 0;
    td->rows     = NULL;
    td->nrows    = 0;
    td->rows_cap = 0;

    char line[65536];
    int  row_idx = 0;

    /* ── Pass 1: read header and all raw data ── */
    /* We do a single-pass with type inference on the fly:
       start each column as VAL_INT, downgrade as needed. */

    /* Read header */
    if (!fgets(line, sizeof(line), f)) { fclose(f); free(td->name); free(td); return NULL; }
    char **header = NULL;
    int ncols = parse_csv_line(line, &header);
    td->ncols = ncols;
    td->cols  = xmalloc((size_t)ncols * sizeof(ColDesc));
    for (int j = 0; j < ncols; j++) {
        td->cols[j].name = xstrdup(header[j]);
        td->cols[j].type = VAL_INT;   /* start optimistic */
        free(header[j]);
    }
    free(header);

    /* Read raw strings into a temporary 2D array, updating type as we go */
    int raw_cap = 256;
    char ***raw = xmalloc((size_t)raw_cap * sizeof(char**));
    int *raw_ncols = xmalloc((size_t)raw_cap * sizeof(int));

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r') continue;
        if (row_idx == raw_cap) {
            raw_cap *= 2;
            raw      = xrealloc(raw,      (size_t)raw_cap * sizeof(char**));
            raw_ncols = xrealloc(raw_ncols,(size_t)raw_cap * sizeof(int));
        }
        char **fields = NULL;
        int nf = parse_csv_line(line, &fields);
        raw[row_idx]       = fields;
        raw_ncols[row_idx] = nf;

        /* Update column type downgrade: INT -> FLOAT -> STRING */
        for (int j = 0; j < nf && j < ncols; j++) {
            if (is_null_string(fields[j])) continue;
            ValType t = infer_type(fields[j]);
            ValType cur = td->cols[j].type;
            /* Downgrade rules */
            if (cur == VAL_INT && t != VAL_INT && t != VAL_NULL) {
                if (t == VAL_FLOAT) td->cols[j].type = VAL_FLOAT;
                else if (t == VAL_BOOL) td->cols[j].type = VAL_BOOL;
                else td->cols[j].type = VAL_STRING;
            } else if (cur == VAL_FLOAT && t == VAL_STRING) {
                td->cols[j].type = VAL_STRING;
            } else if (cur == VAL_BOOL && t != VAL_BOOL && t != VAL_NULL) {
                td->cols[j].type = VAL_STRING;
            }
        }
        row_idx++;
    }
    fclose(f);

    /* ── Pass 2: parse values with final inferred types ── */
    td->rows     = xmalloc((size_t)row_idx * sizeof(Value*));
    td->rows_cap = row_idx;
    td->nrows    = row_idx;
    for (int i = 0; i < row_idx; i++) {
        td->rows[i] = xmalloc((size_t)ncols * sizeof(Value));
        for (int j = 0; j < ncols; j++) {
            if (j < raw_ncols[i])
                td->rows[i][j] = parse_value(raw[i][j], td->cols[j].type);
            else
                td->rows[i][j] = val_null();
        }
        for (int j = 0; j < raw_ncols[i]; j++) free(raw[i][j]);
        free(raw[i]);
    }
    free(raw_ncols);
    free(raw);

    /* Register in catalog */
    if (cat->ntables == cat->cap) {
        cat->cap = cat->cap ? cat->cap * 2 : 8;
        cat->tables = xrealloc(cat->tables, (size_t)cat->cap * sizeof(TableDesc*));
    }
    cat->tables[cat->ntables++] = td;
    return td;
}

/* ── Lookup helpers ─────────────────────────────────────────────────── */
TableDesc *catalog_find_table(Catalog *cat, const char *name) {
    for (int i = 0; i < cat->ntables; i++)
        if (str_iequal(cat->tables[i]->name, name)) return cat->tables[i];
    return NULL;
}

int catalog_find_col(TableDesc *tbl, const char *col_name) {
    for (int j = 0; j < tbl->ncols; j++)
        if (str_iequal(tbl->cols[j].name, col_name)) return j;
    return -1;
}

long long catalog_row_estimate(TableDesc *tbl) {
    return (long long)tbl->nrows;
}
