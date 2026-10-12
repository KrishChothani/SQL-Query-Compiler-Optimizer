/*
 * executor.c — Volcano iterator model implementation.
 *
 * Each operator is a struct with open/next/close function pointers.
 * All operators work on "wide" rows that contain columns from ALL joined
 * tables concatenated.  Only the final Project operator narrows them.
 *
 * Operators implemented:
 *   SeqScan, Filter, Project, NestedLoopJoin, HashJoin,
 *   LeftNLJoin, Sort, Limit, Distinct
 */
#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ══════════════════════════════════════════════════════════════════
   Row helpers
   ══════════════════════════════════════════════════════════════════ */
Row *row_alloc(int width) {
    Row *r = xmalloc(sizeof(Row));
    r->vals  = xmalloc((size_t)width * sizeof(Value));
    r->width = width;
    for (int i = 0; i < width; i++) r->vals[i] = val_null();
    return r;
}
Row *row_copy(Row *r) {
    Row *c = row_alloc(r->width);
    for (int i = 0; i < r->width; i++) c->vals[i] = val_copy(r->vals[i]);
    return c;
}
void row_free(Row *r) {
    if (!r) return;
    for (int i = 0; i < r->width; i++) val_free(r->vals[i]);
    free(r->vals);
    free(r);
}

/* ══════════════════════════════════════════════════════════════════
   Operator allocation helper
   ══════════════════════════════════════════════════════════════════ */
static Operator *alloc_op(SemCtx *ctx, PlanNode *plan) {
    Operator *op = xmalloc(sizeof(Operator));
    memset(op, 0, sizeof(Operator));
    op->ctx  = ctx;
    op->plan = plan;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   SeqScan operator
   ══════════════════════════════════════════════════════════════════ */
typedef struct {
    TableDesc *table;
    int        tbl_idx;
    int        row_idx;
    int        total_width;
} SeqScanState;

static void seqscan_open(Operator *self) {
    SeqScanState *s = self->state;
    s->row_idx = 0;
}
static Row *seqscan_next(Operator *self) {
    SeqScanState *s = self->state;
    if (s->row_idx >= s->table->nrows) return NULL;

    /* Build a "wide" row with NULLs for all other tables */
    Row *row = row_alloc(s->total_width);
    int off = col_offset(s->tbl_idx, 0, self->ctx);
    for (int c = 0; c < s->table->ncols; c++)
        row->vals[off + c] = val_copy(s->table->rows[s->row_idx][c]);
    s->row_idx++;
    self->stats.rows_out++;
    return row;
}
static void seqscan_close(Operator *self) { (void)self; }

static Operator *build_seqscan(PlanNode *plan, SemCtx *ctx) {
    Operator     *op = alloc_op(ctx, plan);
    SeqScanState *s  = xmalloc(sizeof(SeqScanState));
    s->table       = plan->scan.table;
    s->tbl_idx     = plan->scan.tbl_idx;
    s->total_width = row_width(ctx);
    s->row_idx     = 0;
    op->state      = s;
    op->op_open    = seqscan_open;
    op->op_next    = seqscan_next;
    op->op_close   = seqscan_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Filter operator
   ══════════════════════════════════════════════════════════════════ */
typedef struct {
    AstNode *predicate;
} FilterState;

static void filter_open(Operator *self) {
    if (self->left) self->left->op_open(self->left);
}
static Row *filter_next(Operator *self) {
    FilterState *s = self->state;
    while (1) {
        Row *row = self->left->op_next(self->left);
        if (!row) return NULL;
        self->stats.rows_in++;
        Value v = expr_eval(s->predicate, row->vals, self->ctx);
        int pass = val_is_true(v);
        val_free(v);
        if (pass) { self->stats.rows_out++; return row; }
        row_free(row);
    }
}
static void filter_close(Operator *self) {
    if (self->left) self->left->op_close(self->left);
}

static Operator *build_filter(PlanNode *plan, SemCtx *ctx) {
    Operator    *op = alloc_op(ctx, plan);
    FilterState *s  = xmalloc(sizeof(FilterState));
    s->predicate = plan->filter.predicate;
    op->state    = s;
    op->op_open  = filter_open;
    op->op_next  = filter_next;
    op->op_close = filter_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Project operator
   ══════════════════════════════════════════════════════════════════ */
typedef struct {
    AstNode **exprs;
    int       count;
} ProjectState;

static void project_open(Operator *self) {
    if (self->left) self->left->op_open(self->left);
}
static Row *project_next(Operator *self) {
    ProjectState *s = self->state;
    Row *in = self->left->op_next(self->left);
    if (!in) return NULL;
    self->stats.rows_in++;
    Row *out = row_alloc(s->count);
    for (int i = 0; i < s->count; i++) {
        out->vals[i] = expr_eval(s->exprs[i], in->vals, self->ctx);
    }
    row_free(in);
    self->stats.rows_out++;
    return out;
}
static void project_close(Operator *self) {
    if (self->left) self->left->op_close(self->left);
}

static Operator *build_project(PlanNode *plan, SemCtx *ctx) {
    Operator     *op = alloc_op(ctx, plan);
    ProjectState *s  = xmalloc(sizeof(ProjectState));
    s->exprs    = plan->project.exprs;
    s->count    = plan->project.count;
    op->state   = s;
    op->op_open = project_open;
    op->op_next = project_next;
    op->op_close= project_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Nested-Loop Join operator  (INNER and LEFT)
   ══════════════════════════════════════════════════════════════════ */
typedef struct {
    AstNode  *condition;
    JoinType  join_type;
    Row      *left_row;         /* current left row */
    int       right_exhausted;  /* right child returned NULL */
    int       left_matched;     /* current left row matched at least once */
} NLJoinState;

static void nljoin_open(Operator *self) {
    if (self->left)  self->left->op_open(self->left);
    if (self->right) self->right->op_open(self->right);
    NLJoinState *s = self->state;
    s->left_row       = NULL;
    s->right_exhausted= 0;
    s->left_matched   = 0;
}

/* Combine two wide rows into one */
static Row *combine_rows(Row *l, Row *r) {
    /* Both are "wide" rows of the same total width.
       For NL join the right row has NULLs in the left columns —
       we overlay: take non-null values from each. */
    int w = l->width;
    Row *out = row_alloc(w);
    for (int i = 0; i < w; i++) {
        if (!val_is_null(l->vals[i]))
            out->vals[i] = val_copy(l->vals[i]);
        else
            out->vals[i] = val_copy(r->vals[i]);
    }
    return out;
}

static Row *nljoin_next(Operator *self) {
    NLJoinState *s = self->state;
    while (1) {
        /* Get a new left row if needed */
        if (!s->left_row) {
            if (s->left_row) row_free(s->left_row);
            s->left_row = self->left->op_next(self->left);
            if (!s->left_row) return NULL;  /* left exhausted */
            self->stats.rows_in++;
            /* Reset right child */
            self->right->op_close(self->right);
            self->right->op_open(self->right);
            s->right_exhausted = 0;
            s->left_matched    = 0;
        }

        if (s->right_exhausted) {
            /* LEFT JOIN: emit left row with NULLs if no match */
            if (s->join_type == JOIN_LEFT && !s->left_matched) {
                Row *out = row_copy(s->left_row);
                row_free(s->left_row);
                s->left_row = NULL;
                self->stats.rows_out++;
                return out;
            }
            row_free(s->left_row);
            s->left_row = NULL;
            continue;
        }

        Row *right_row = self->right->op_next(self->right);
        if (!right_row) {
            s->right_exhausted = 1;
            continue;
        }

        /* Evaluate join condition */
        Row *combined = combine_rows(s->left_row, right_row);
        row_free(right_row);

        int pass = 1;
        if (s->condition) {
            Value v = expr_eval(s->condition, combined->vals, self->ctx);
            pass = val_is_true(v);
            val_free(v);
        }
        if (pass) {
            s->left_matched = 1;
            self->stats.rows_out++;
            return combined;
        }
        row_free(combined);
    }
}

static void nljoin_close(Operator *self) {
    NLJoinState *s = self->state;
    if (s->left_row) { row_free(s->left_row); s->left_row = NULL; }
    if (self->left)  self->left->op_close(self->left);
    if (self->right) self->right->op_close(self->right);
}

static Operator *build_nljoin(PlanNode *plan, SemCtx *ctx) {
    Operator    *op = alloc_op(ctx, plan);
    NLJoinState *s  = xmalloc(sizeof(NLJoinState));
    s->condition      = plan->join.condition;
    s->join_type      = plan->join.join_type;
    s->left_row       = NULL;
    s->right_exhausted= 0;
    s->left_matched   = 0;
    op->state    = s;
    op->op_open  = nljoin_open;
    op->op_next  = nljoin_next;
    op->op_close = nljoin_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Hash Join operator  (INNER equi-join)
   Bucket chain: hash on right (build), probe with left
   ══════════════════════════════════════════════════════════════════ */
#define HASH_BUCKETS 1024

typedef struct HashEntry {
    Row              *row;
    struct HashEntry *next;
} HashEntry;

typedef struct {
    AstNode    *condition;
    HashEntry  *buckets[HASH_BUCKETS];
    Row        *left_row;         /* current probe row */
    HashEntry  *probe_entry;      /* current bucket position */
    int         built;
    /* For equi-join: which column offsets to hash on */
    int         left_key_off;
    int         right_key_off;
} HashJoinState;

static unsigned int hash_value(Value v) {
    unsigned int h = 5381;
    char *s = val_to_string(v);
    for (char *p = s; *p; p++) h = h * 33 + (unsigned char)*p;
    free(s);
    return h % HASH_BUCKETS;
}

/* Extract equi-join key offsets from the join condition */
static void extract_equijoin_keys(AstNode *cond, SemCtx *ctx,
                                   int *left_off, int *right_off,
                                   Operator *left_child,
                                   Operator *right_child) {
    *left_off = *right_off = -1;
    if (!cond || cond->kind != AST_BINOP || cond->binop.op != OP_EQ) return;
    AstNode *la = cond->binop.left, *ra = cond->binop.right;
    if (la->kind != AST_COLUMN || ra->kind != AST_COLUMN) return;

    /* Figure out which side each column belongs to */
    int lo1 = col_offset(la->col.tbl_idx, la->col.col_idx, ctx);
    int lo2 = col_offset(ra->col.tbl_idx, ra->col.col_idx, ctx);

    /* The left child scans certain table indices */
    /* Check which table index is in left vs right subtree */
    /* We use tbl_idx to determine: right scan starts after left scans */
    (void)left_child; (void)right_child;
    /* Simple heuristic: the column with the smaller tbl_idx is on the left */
    if (la->col.tbl_idx <= ra->col.tbl_idx) {
        *left_off  = lo1;
        *right_off = lo2;
    } else {
        *left_off  = lo2;
        *right_off = lo1;
    }
}

static void hashjoin_open(Operator *self) {
    HashJoinState *s = self->state;
    /* Clear buckets */
    memset(s->buckets, 0, sizeof(s->buckets));
    s->left_row   = NULL;
    s->probe_entry= NULL;
    s->built      = 0;

    if (self->left)  self->left->op_open(self->left);
    if (self->right) self->right->op_open(self->right);

    /* Build phase: materialise right child into hash table */
    Row *r;
    while ((r = self->right->op_next(self->right)) != NULL) {
        self->stats.rows_in++;
        Value key = (s->right_key_off >= 0) ?
                    val_copy(r->vals[s->right_key_off]) : val_null();
        unsigned int bucket = hash_value(key);
        val_free(key);
        HashEntry *e = xmalloc(sizeof(HashEntry));
        e->row    = r;
        e->next   = s->buckets[bucket];
        s->buckets[bucket] = e;
    }
    s->built = 1;
}

static Row *hashjoin_next(Operator *self) {
    HashJoinState *s = self->state;

    while (1) {
        /* Try current probe bucket */
        while (s->probe_entry) {
            Row *right = s->probe_entry->row;
            s->probe_entry = s->probe_entry->next;

            Row *combined = combine_rows(s->left_row, right);
            int pass = 1;
            if (s->condition) {
                Value v = expr_eval(s->condition, combined->vals, self->ctx);
                pass = val_is_true(v);
                val_free(v);
            }
            if (pass) {
                self->stats.rows_out++;
                return combined;
            }
            row_free(combined);
        }

        /* Get next left row */
        if (s->left_row) { row_free(s->left_row); s->left_row = NULL; }
        s->left_row = self->left->op_next(self->left);
        if (!s->left_row) return NULL;
        self->stats.rows_in++;

        /* Probe bucket */
        Value key = (s->left_key_off >= 0) ?
                    val_copy(s->left_row->vals[s->left_key_off]) : val_null();
        unsigned int bucket = hash_value(key);
        val_free(key);
        s->probe_entry = s->buckets[bucket];
    }
}

static void hashjoin_close(Operator *self) {
    HashJoinState *s = self->state;
    if (s->left_row) { row_free(s->left_row); s->left_row = NULL; }
    for (int i = 0; i < HASH_BUCKETS; i++) {
        HashEntry *e = s->buckets[i];
        while (e) {
            HashEntry *next = e->next;
            row_free(e->row);
            free(e);
            e = next;
        }
        s->buckets[i] = NULL;
    }
    if (self->left)  self->left->op_close(self->left);
    if (self->right) self->right->op_close(self->right);
}

static Operator *build_hashjoin(PlanNode *plan, SemCtx *ctx) {
    Operator      *op = alloc_op(ctx, plan);
    HashJoinState *s  = xmalloc(sizeof(HashJoinState));
    memset(s, 0, sizeof(HashJoinState));
    s->condition = plan->join.condition;
    op->state    = s;
    op->op_open  = hashjoin_open;
    op->op_next  = hashjoin_next;
    op->op_close = hashjoin_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Sort operator  (materialise all rows, qsort, emit)
   ══════════════════════════════════════════════════════════════════ */
typedef struct {
    AstNode **keys;
    int      *asc;
    int       nkeys;
    Row     **rows;
    int       nrows;
    int       cap;
    int       cur;
    SemCtx   *ctx;
} SortState;

static SortState *g_sort_state;   /* thread-unsafe but fine for single-threaded use */

static int sort_cmp(const void *a, const void *b) {
    Row *ra = *(Row**)a, *rb = *(Row**)b;
    SortState *s = g_sort_state;
    for (int k = 0; k < s->nkeys; k++) {
        Value va = expr_eval(s->keys[k], ra->vals, s->ctx);
        Value vb = expr_eval(s->keys[k], rb->vals, s->ctx);
        int cmp;
        if (va.type == VAL_NULL && vb.type == VAL_NULL) {
            cmp = 0;
        } else if (va.type == VAL_NULL) {
            cmp = 1;  /* NULLs sort last in ASC */
        } else if (vb.type == VAL_NULL) {
            cmp = -1; /* non-NULL before NULL in ASC */
        } else {
            cmp = val_cmp(va, vb);
        }
        val_free(va); val_free(vb);
        if (!s->asc[k]) cmp = -cmp;
        if (cmp != 0) return cmp;
    }
    return 0;
}

static void sort_open(Operator *self) {
    SortState *s = self->state;
    s->cur   = 0;
    s->nrows = 0;

    if (self->left) self->left->op_open(self->left);

    /* Materialise all rows */
    Row *r;
    while ((r = self->left->op_next(self->left)) != NULL) {
        self->stats.rows_in++;
        if (s->nrows == s->cap) {
            s->cap  = s->cap ? s->cap * 2 : 64;
            s->rows = xrealloc(s->rows, (size_t)s->cap * sizeof(Row*));
        }
        s->rows[s->nrows++] = r;
    }

    /* Sort */
    g_sort_state = s;
    if (s->nrows > 1)
        qsort(s->rows, (size_t)s->nrows, sizeof(Row*), sort_cmp);
}
static Row *sort_next(Operator *self) {
    SortState *s = self->state;
    if (s->cur >= s->nrows) return NULL;
    self->stats.rows_out++;
    return row_copy(s->rows[s->cur++]);
}
static void sort_close(Operator *self) {
    SortState *s = self->state;
    for (int i = 0; i < s->nrows; i++) row_free(s->rows[i]);
    free(s->rows);
    s->rows = NULL; s->nrows = 0; s->cap = 0; s->cur = 0;
    if (self->left) self->left->op_close(self->left);
}

static Operator *build_sort(PlanNode *plan, SemCtx *ctx) {
    Operator  *op = alloc_op(ctx, plan);
    SortState *s  = xmalloc(sizeof(SortState));
    s->keys  = plan->sort.keys;
    s->asc   = plan->sort.asc;
    s->nkeys = plan->sort.count;
    s->rows  = NULL; s->nrows = 0; s->cap = 0; s->cur = 0;
    s->ctx   = ctx;
    op->state    = s;
    op->op_open  = sort_open;
    op->op_next  = sort_next;
    op->op_close = sort_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Limit operator
   ══════════════════════════════════════════════════════════════════ */
typedef struct { long long n; long long emitted; } LimitState;

static void limit_open(Operator *self) {
    LimitState *s = self->state; s->emitted = 0;
    if (self->left) self->left->op_open(self->left);
}
static Row *limit_next(Operator *self) {
    LimitState *s = self->state;
    if (s->emitted >= s->n) return NULL;
    Row *r = self->left->op_next(self->left);
    if (!r) return NULL;
    self->stats.rows_in++;
    self->stats.rows_out++;
    s->emitted++;
    return r;
}
static void limit_close(Operator *self) {
    if (self->left) self->left->op_close(self->left);
}

static Operator *build_limit(PlanNode *plan, SemCtx *ctx) {
    Operator   *op = alloc_op(ctx, plan);
    LimitState *s  = xmalloc(sizeof(LimitState));
    s->n = plan->limit.n; s->emitted = 0;
    op->state    = s;
    op->op_open  = limit_open;
    op->op_next  = limit_next;
    op->op_close = limit_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Distinct operator  (full deduplication of emitted rows)
   ══════════════════════════════════════════════════════════════════ */
typedef struct {
    Row **seen;
    int   nseen;
    int   cap;
} DistinctState;

static void distinct_open(Operator *self) {
    DistinctState *s = self->state;
    for (int i = 0; i < s->nseen; i++) row_free(s->seen[i]);
    free(s->seen);
    s->seen = NULL; s->nseen = 0; s->cap = 0;
    if (self->left) self->left->op_open(self->left);
}
static int rows_equal(Row *a, Row *b) {
    if (!a || !b || a->width != b->width) return 0;
    for (int i = 0; i < a->width; i++) {
        if (a->vals[i].type == VAL_NULL && b->vals[i].type == VAL_NULL)
            continue;
        int cmp = val_cmp(a->vals[i], b->vals[i]);
        if (cmp != 0) return 0;
    }
    return 1;
}
static Row *distinct_next(Operator *self) {
    DistinctState *s = self->state;
    while (1) {
        Row *r = self->left->op_next(self->left);
        if (!r) return NULL;
        self->stats.rows_in++;
        int found = 0;
        for (int i = 0; i < s->nseen; i++) {
            if (rows_equal(r, s->seen[i])) {
                found = 1;
                break;
            }
        }
        if (found) {
            row_free(r);
            continue;
        }
        if (s->nseen == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 64;
            s->seen = xrealloc(s->seen, (size_t)s->cap * sizeof(Row*));
        }
        s->seen[s->nseen++] = row_copy(r);
        self->stats.rows_out++;
        return r;
    }
}
static void distinct_close(Operator *self) {
    DistinctState *s = self->state;
    for (int i = 0; i < s->nseen; i++) row_free(s->seen[i]);
    free(s->seen);
    s->seen = NULL; s->nseen = 0; s->cap = 0;
    if (self->left) self->left->op_close(self->left);
}

static Operator *build_distinct(PlanNode *plan, SemCtx *ctx) {
    Operator      *op = alloc_op(ctx, plan);
    DistinctState *s  = xmalloc(sizeof(DistinctState));
    s->seen      = NULL;
    s->nseen     = 0;
    s->cap       = 0;
    op->state    = s;
    op->op_open  = distinct_open;
    op->op_next  = distinct_next;
    op->op_close = distinct_close;
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Build dispatcher
   ══════════════════════════════════════════════════════════════════ */
Operator *exec_build(PlanNode *plan, SemCtx *ctx) {
    if (!plan) return NULL;
    Operator *op = NULL;
    switch (plan->type) {
        case PLAN_SCAN:
            op = build_seqscan(plan, ctx);
            break;
        case PLAN_FILTER:
            op = build_filter(plan, ctx);
            op->left = exec_build(plan->left, ctx);
            break;
        case PLAN_PROJECT:
            op = build_project(plan, ctx);
            op->left = exec_build(plan->left, ctx);
            break;
        case PLAN_JOIN:
            if (plan->join.algo == JOIN_ALGO_HASH &&
                plan->join.join_type == JOIN_INNER) {
                op = build_hashjoin(plan, ctx);
                op->left  = exec_build(plan->left,  ctx);
                op->right = exec_build(plan->right, ctx);
                /* Set key offsets */
                HashJoinState *hs = op->state;
                extract_equijoin_keys(plan->join.condition, ctx,
                                      &hs->left_key_off, &hs->right_key_off,
                                      op->left, op->right);
            } else {
                op = build_nljoin(plan, ctx);
                op->left  = exec_build(plan->left,  ctx);
                op->right = exec_build(plan->right, ctx);
            }
            break;
        case PLAN_SORT:
            op = build_sort(plan, ctx);
            op->left = exec_build(plan->left, ctx);
            break;
        case PLAN_LIMIT:
            op = build_limit(plan, ctx);
            op->left = exec_build(plan->left, ctx);
            break;
        case PLAN_DISTINCT:
            op = build_distinct(plan, ctx);
            op->left = exec_build(plan->left, ctx);
            break;
    }
    return op;
}

/* ══════════════════════════════════════════════════════════════════
   Result materialisation & execution helpers
   ══════════════════════════════════════════════════════════════════ */
ExecResult exec_collect(Operator *root) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    root->op_open(root);

    Row **rows = NULL;
    int   nrows = 0, rcap = 0;
    Row  *r;
    while ((r = root->op_next(root)) != NULL) {
        if (nrows == rcap) {
            rcap = rcap ? rcap * 2 : 64;
            rows = xrealloc(rows, (size_t)rcap * sizeof(Row*));
        }
        rows[nrows++] = r;
    }
    root->op_close(root);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed_ms = ((t1.tv_sec - t0.tv_sec) * 1000.0) +
                        ((t1.tv_nsec - t0.tv_nsec) / 1e6);

    ExecResult res;
    res.rows = rows;
    res.nrows = nrows;
    res.elapsed_ms = elapsed_ms;
    return res;
}

void exec_result_free(ExecResult *res) {
    if (!res || !res->rows) return;
    for (int i = 0; i < res->nrows; i++) {
        row_free(res->rows[i]);
    }
    free(res->rows);
    res->rows = NULL;
    res->nrows = 0;
}

/* ══════════════════════════════════════════════════════════════════
   Result printer  (aligned ASCII table)
   ══════════════════════════════════════════════════════════════════ */
long long exec_run(Operator *root, int ncols, char **col_names, int show_stats) {
    ExecResult res = exec_collect(root);
    int nrows = res.nrows;
    Row **rows = res.rows;
    double elapsed = res.elapsed_ms / 1000.0;

    /* Compute column widths */
    int *widths = xmalloc((size_t)ncols * sizeof(int));
    for (int c = 0; c < ncols; c++) widths[c] = (int)strlen(col_names[c]);
    for (int i = 0; i < nrows; i++) {
        for (int c = 0; c < ncols && c < rows[i]->width; c++) {
            char *s = val_to_string(rows[i]->vals[c]);
            int len = (int)strlen(s);
            if (len > widths[c]) widths[c] = len;
            free(s);
        }
    }

    /* Print header */
    printf("%s", COLOR_CYAN);
    printf("+");
    for (int c = 0; c < ncols; c++) {
        for (int w = 0; w < widths[c]+2; w++) printf("-");
        printf("+");
    }
    printf("\n| ");
    for (int c = 0; c < ncols; c++) {
        printf("%-*s | ", widths[c], col_names[c]);
    }
    printf("\n+");
    for (int c = 0; c < ncols; c++) {
        for (int w = 0; w < widths[c]+2; w++) printf("-");
        printf("+");
    }
    printf("%s\n", COLOR_RESET);

    /* Print rows */
    for (int i = 0; i < nrows; i++) {
        printf("| ");
        for (int c = 0; c < ncols && c < rows[i]->width; c++) {
            char *s = val_to_string(rows[i]->vals[c]);
            printf("%-*s | ", widths[c], s);
            free(s);
        }
        printf("\n");
    }

    /* Footer */
    printf("+");
    for (int c = 0; c < ncols; c++) {
        for (int w = 0; w < widths[c]+2; w++) printf("-");
        printf("+");
    }
    printf("\n");
    free(widths);

    printf("%s%d row%s (%.4f s)%s\n",
           COLOR_GREEN, nrows, nrows==1?"":"s", elapsed, COLOR_RESET);

    if (show_stats) {
        printf("\n%sExecution statistics:%s\n", COLOR_CYAN, COLOR_RESET);
        exec_print_stats(root, 0);
    }
    exec_result_free(&res);
    return nrows;
}

/* ══════════════════════════════════════════════════════════════════
   Stats printer
   ══════════════════════════════════════════════════════════════════ */
static const char *plan_type_name(PlanNodeType t) {
    switch(t){
        case PLAN_SCAN:     return "SeqScan";
        case PLAN_FILTER:   return "Filter";
        case PLAN_PROJECT:  return "Project";
        case PLAN_JOIN:     return "Join";
        case PLAN_SORT:     return "Sort";
        case PLAN_LIMIT:    return "Limit";
        case PLAN_DISTINCT: return "Distinct";
    }
    return "?";
}

void exec_print_stats(Operator *op, int indent) {
    if (!op) return;
    for (int i=0;i<indent;i++) printf("  ");
    const char *name = op->plan ? plan_type_name(op->plan->type) : "?";
    printf("%-12s  rows_in=%-8lld rows_out=%lld\n",
           name, op->stats.rows_in, op->stats.rows_out);
    exec_print_stats(op->left,  indent+1);
    exec_print_stats(op->right, indent+1);
}

void exec_stats_to_json(Operator *op, FILE *f, int *is_first) {
    if (!op) return;
    const char *name = op->plan ? plan_type_name(op->plan->type) : "?";
    if (!*is_first) fprintf(f, ",");
    *is_first = 0;
    fprintf(f, "{\"operator\":\"%s\",\"rows_in\":%lld,\"rows_out\":%lld}",
            name, op->stats.rows_in, op->stats.rows_out);
    exec_stats_to_json(op->left, f, is_first);
    exec_stats_to_json(op->right, f, is_first);
}

long long exec_total_rows_processed(Operator *op) {
    if (!op) return 0;
    return op->stats.rows_in + op->stats.rows_out +
           exec_total_rows_processed(op->left) +
           exec_total_rows_processed(op->right);
}

/* ══════════════════════════════════════════════════════════════════
   Free operator tree
   ══════════════════════════════════════════════════════════════════ */
void exec_free(Operator *op) {
    if (!op) return;
    exec_free(op->left);
    exec_free(op->right);
    free(op->state);
    free(op);
}
