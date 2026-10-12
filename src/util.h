/*
 * util.h — Dynamic arrays, string helpers, error reporting utilities.
 *
 * Role in compiler pipeline: Supporting infrastructure used by all phases.
 * Provides memory-safe dynamic arrays (Vec), string duplication, and coloured
 * terminal error output.
 */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdio.h>

/* ── Colour output ──────────────────────────────────────────────────── */
extern int g_use_color;   /* set to 0 by --no-color */

#define COLOR_RED     (g_use_color ? "\033[1;31m" : "")
#define COLOR_CYAN    (g_use_color ? "\033[1;36m" : "")
#define COLOR_YELLOW  (g_use_color ? "\033[1;33m" : "")
#define COLOR_GREEN   (g_use_color ? "\033[1;32m" : "")
#define COLOR_RESET   (g_use_color ? "\033[0m"    : "")

/* ── Generic dynamic array (Vec) ────────────────────────────────────── */
typedef struct {
    void  **data;
    int     len;
    int     cap;
} Vec;

Vec  *vec_new(void);
void  vec_push(Vec *v, void *item);
void  vec_free(Vec *v);          /* frees the Vec but NOT the items */
void  vec_free_deep(Vec *v, void (*item_free)(void*)); /* frees items too */

/* ── String helpers ─────────────────────────────────────────────────── */
char *str_dup(const char *s);
char *str_lower(const char *s);   /* returns newly allocated lowercase copy */
int   str_iequal(const char *a, const char *b); /* case-insensitive compare */

/* ── Token tracking for JSON/stage output ─────────────────────────── */
typedef struct {
    char *type;
    char *lexeme;
    int   line;
    int   col;
} TokenItem;

typedef struct {
    TokenItem *items;
    int        count;
    int        cap;
} TokenList;

extern TokenList g_tokens;
void token_list_init(void);
void token_list_add(const char *type, const char *lexeme, int line, int col);
void token_list_free(void);

/* ── Error reporting ────────────────────────────────────────────────── */
/* Each prints to stderr and returns -1 so callers can: return lex_error(...) */
extern int g_json_mode;
char *json_escape(const char *s);
void  json_reset_error(void);

int lex_error  (int line, int col, const char *fmt, ...);
int parse_error(int line, int col, const char *fmt, ...);
int sem_error  (int line, const char *fmt, ...);
int run_error  (const char *fmt, ...);

/* Levenshtein distance for "did you mean?" suggestions */
int  edit_distance(const char *a, const char *b);

/* xmalloc / xstrdup — abort on OOM */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

#endif /* UTIL_H */
