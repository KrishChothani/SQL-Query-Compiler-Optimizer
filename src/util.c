/*
 * util.c — Implementation of dynamic arrays, string helpers and error output.
 */
#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

int g_use_color = 1;

/* ── xmalloc/xrealloc/xstrdup ─────────────────────────────────────── */
void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    return p;
}
void *xrealloc(void *p, size_t n) {
    p = realloc(p, n);
    if (!p) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    return p;
}
char *xstrdup(const char *s) {
    char *d = strdup(s);
    if (!d) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    return d;
}

/* ── Vec ──────────────────────────────────────────────────────────── */
Vec *vec_new(void) {
    Vec *v = xmalloc(sizeof(Vec));
    v->data = NULL; v->len = 0; v->cap = 0;
    return v;
}
void vec_push(Vec *v, void *item) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = xrealloc(v->data, (size_t)v->cap * sizeof(void*));
    }
    v->data[v->len++] = item;
}
void vec_free(Vec *v) {
    if (!v) return;
    free(v->data);
    free(v);
}
void vec_free_deep(Vec *v, void (*item_free)(void*)) {
    if (!v) return;
    if (item_free)
        for (int i = 0; i < v->len; i++) item_free(v->data[i]);
    free(v->data);
    free(v);
}

/* ── String helpers ───────────────────────────────────────────────── */
char *str_dup(const char *s) { return s ? xstrdup(s) : NULL; }

char *str_lower(const char *s) {
    if (!s) return NULL;
    char *d = xstrdup(s);
    for (char *p = d; *p; p++) *p = (char)tolower((unsigned char)*p);
    return d;
}

int str_iequal(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b)
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    return *a == '\0' && *b == '\0';
}

/* ── Token tracking ───────────────────────────────────────────────── */
TokenList g_tokens = { NULL, 0, 0 };

void token_list_init(void) {
    token_list_free();
}

void token_list_add(const char *type, const char *lexeme, int line, int col) {
    if (g_tokens.count == g_tokens.cap) {
        g_tokens.cap = g_tokens.cap ? g_tokens.cap * 2 : 64;
        g_tokens.items = xrealloc(g_tokens.items, (size_t)g_tokens.cap * sizeof(TokenItem));
    }
    g_tokens.items[g_tokens.count].type = xstrdup(type);
    g_tokens.items[g_tokens.count].lexeme = xstrdup(lexeme);
    g_tokens.items[g_tokens.count].line = line;
    g_tokens.items[g_tokens.count].col = col;
    g_tokens.count++;
}

void token_list_free(void) {
    if (!g_tokens.items) return;
    for (int i = 0; i < g_tokens.count; i++) {
        free(g_tokens.items[i].type);
        free(g_tokens.items[i].lexeme);
    }
    free(g_tokens.items);
    g_tokens.items = NULL;
    g_tokens.count = 0;
    g_tokens.cap   = 0;
}

/* ── Error reporting ──────────────────────────────────────────────── */
int g_json_mode = 0;
static int g_json_error_emitted = 0;

void json_reset_error(void) {
    g_json_error_emitted = 0;
}

char *json_escape(const char *s) {
    if (!s) return xstrdup("");
    size_t len = strlen(s);
    char *out = xmalloc(len * 4 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            out[j++] = '\\'; out[j++] = '"';
        } else if (c == '\\') {
            out[j++] = '\\'; out[j++] = '\\';
        } else if (c == '\b') {
            out[j++] = '\\'; out[j++] = 'b';
        } else if (c == '\f') {
            out[j++] = '\\'; out[j++] = 'f';
        } else if (c == '\n') {
            out[j++] = '\\'; out[j++] = 'n';
        } else if (c == '\r') {
            out[j++] = '\\'; out[j++] = 'r';
        } else if (c == '\t') {
            out[j++] = '\\'; out[j++] = 't';
        } else if (c < 32) {
            j += (size_t)sprintf(&out[j], "\\u%04x", c);
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
    return out;
}

static void print_json_error(const char *category, const char *msg, int line, int col) {
    if (g_json_error_emitted) return;
    g_json_error_emitted = 1;
    char *esc = json_escape(msg);
    printf("{\"status\":\"error\",\"category\":\"%s\",\"message\":\"%s\",\"line\":%d,\"col\":%d}\n",
           category, esc, line, col);
    free(esc);
    fflush(stdout);
}

int lex_error(int line, int col, const char *fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (g_json_mode) {
        print_json_error("Lexical", buf, line, col);
        return -1;
    }
    fprintf(stderr, "%sLexical Error%s at line %d, col %d: %s\n",
            COLOR_RED, COLOR_RESET, line, col, buf);
    return -1;
}

int parse_error(int line, int col, const char *fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (g_json_mode) {
        print_json_error("Syntax", buf, line, col);
        return -1;
    }
    fprintf(stderr, "%sSyntax Error%s at line %d, col %d: %s\n",
            COLOR_RED, COLOR_RESET, line, col, buf);
    return -1;
}

int sem_error(int line, const char *fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (g_json_mode) {
        print_json_error("Semantic", buf, line, 1);
        return -1;
    }
    fprintf(stderr, "%sSemantic Error%s at line %d: %s\n",
            COLOR_RED, COLOR_RESET, line, buf);
    return -1;
}

int run_error(const char *fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (g_json_mode) {
        print_json_error("Runtime", buf, 0, 0);
        return -1;
    }
    fprintf(stderr, "%sRuntime Error%s: %s\n",
            COLOR_RED, COLOR_RESET, buf);
    return -1;
}

/* ── Levenshtein distance ─────────────────────────────────────────── */
int edit_distance(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    /* Use two-row DP to save memory */
    int *prev = xmalloc((size_t)(lb+1)*sizeof(int));
    int *curr = xmalloc((size_t)(lb+1)*sizeof(int));
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        curr[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (tolower((unsigned char)a[i-1]) ==
                        tolower((unsigned char)b[j-1])) ? 0 : 1;
            int del  = prev[j]   + 1;
            int ins  = curr[j-1] + 1;
            int sub  = prev[j-1] + cost;
            curr[j]  = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
        }
        int *tmp = prev; prev = curr; curr = tmp;
    }
    int dist = prev[lb];
    free(prev); free(curr);
    return dist;
}
