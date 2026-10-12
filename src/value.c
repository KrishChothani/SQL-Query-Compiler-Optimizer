/*
 * value.c — Implementation of the Value type: arithmetic, comparison, I/O.
 */
#include "value.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Constructors ──────────────────────────────────────────────────── */
Value val_null(void)            { Value v; v.type=VAL_NULL; v.ival=0; return v; }
Value val_int(long long i)      { Value v; v.type=VAL_INT;  v.ival=i; return v; }
Value val_float(double f)       { Value v; v.type=VAL_FLOAT;v.fval=f; return v; }
Value val_bool(int b)           { Value v; v.type=VAL_BOOL; v.bval=b?1:0; return v; }
Value val_string(const char *s) {
    Value v; v.type=VAL_STRING;
    v.sval = s ? xstrdup(s) : xstrdup("");
    return v;
}

/* ── Lifecycle ─────────────────────────────────────────────────────── */
Value val_copy(Value v) {
    if (v.type == VAL_STRING) return val_string(v.sval);
    return v;
}
void val_free(Value v) {
    if (v.type == VAL_STRING) free(v.sval);
}

/* ── NULL / truth ─────────────────────────────────────────────────── */
int val_is_null(Value v) { return v.type == VAL_NULL; }
int val_is_true(Value v) {
    switch (v.type) {
        case VAL_NULL:   return 0;
        case VAL_INT:    return v.ival != 0;
        case VAL_FLOAT:  return v.fval != 0.0;
        case VAL_STRING: return v.sval && v.sval[0] != '\0';
        case VAL_BOOL:   return v.bval;
    }
    return 0;
}
int val_equal(Value a, Value b) {
    if (a.type == VAL_NULL || b.type == VAL_NULL) return 0;
    return val_cmp(a,b) == 0;
}

/* ── Coercion ─────────────────────────────────────────────────────── */
Value val_coerce_float(Value v) {
    if (v.type == VAL_INT) return val_float((double)v.ival);
    return v;
}

/* ── Comparison ───────────────────────────────────────────────────── */
int val_cmp(Value a, Value b) {
    /* NULL comparisons yield unknown (-2) */
    if (a.type == VAL_NULL || b.type == VAL_NULL) return -2;

    /* Promote INT to FLOAT if mixed */
    if ((a.type == VAL_INT || a.type == VAL_FLOAT) &&
        (b.type == VAL_INT || b.type == VAL_FLOAT)) {
        double fa = (a.type == VAL_INT) ? (double)a.ival : a.fval;
        double fb = (b.type == VAL_INT) ? (double)b.ival : b.fval;
        return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
    }
    if (a.type == VAL_STRING && b.type == VAL_STRING)
        return strcmp(a.sval, b.sval);
    if (a.type == VAL_BOOL && b.type == VAL_BOOL)
        return a.bval - b.bval;
    /* Mixed types: compare type codes (stable but arbitrary) */
    return (int)a.type - (int)b.type;
}

/* ── Arithmetic ────────────────────────────────────────────────────── */
static int numeric(Value v) {
    return v.type == VAL_INT || v.type == VAL_FLOAT;
}

Value val_add(Value a, Value b) {
    if (!numeric(a) || !numeric(b)) return val_null();
    if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
        double fa = (a.type==VAL_INT)?(double)a.ival:a.fval;
        double fb = (b.type==VAL_INT)?(double)b.ival:b.fval;
        return val_float(fa+fb);
    }
    return val_int(a.ival + b.ival);
}
Value val_sub(Value a, Value b) {
    if (!numeric(a) || !numeric(b)) return val_null();
    if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
        double fa = (a.type==VAL_INT)?(double)a.ival:a.fval;
        double fb = (b.type==VAL_INT)?(double)b.ival:b.fval;
        return val_float(fa-fb);
    }
    return val_int(a.ival - b.ival);
}
Value val_mul(Value a, Value b) {
    if (!numeric(a) || !numeric(b)) return val_null();
    if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
        double fa = (a.type==VAL_INT)?(double)a.ival:a.fval;
        double fb = (b.type==VAL_INT)?(double)b.ival:b.fval;
        return val_float(fa*fb);
    }
    return val_int(a.ival * b.ival);
}
Value val_div(Value a, Value b) {
    if (!numeric(a) || !numeric(b)) return val_null();
    double fa = (a.type==VAL_INT)?(double)a.ival:a.fval;
    double fb = (b.type==VAL_INT)?(double)b.ival:b.fval;
    if (fb == 0.0) return val_null(); /* division by zero -> NULL */
    /* Integer division if both INT and exact */
    if (a.type == VAL_INT && b.type == VAL_INT && a.ival % b.ival == 0)
        return val_int(a.ival / b.ival);
    return val_float(fa/fb);
}
Value val_neg(Value a) {
    if (a.type == VAL_INT)   return val_int(-a.ival);
    if (a.type == VAL_FLOAT) return val_float(-a.fval);
    return val_null();
}

/* ── Printing ──────────────────────────────────────────────────────── */
void val_print(Value v) {
    char *s = val_to_string(v);
    fputs(s, stdout);
    free(s);
}

char *val_to_string(Value v) {
    char buf[64];
    switch (v.type) {
        case VAL_NULL:   return xstrdup("NULL");
        case VAL_BOOL:   return xstrdup(v.bval ? "TRUE" : "FALSE");
        case VAL_INT:    snprintf(buf, sizeof(buf), "%lld", v.ival); return xstrdup(buf);
        case VAL_FLOAT:
            /* Avoid trailing zeros for clean output */
            snprintf(buf, sizeof(buf), "%.6g", v.fval);
            return xstrdup(buf);
        case VAL_STRING: return xstrdup(v.sval);
    }
    return xstrdup("?");
}
