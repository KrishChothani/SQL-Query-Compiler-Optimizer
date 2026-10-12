/*
 * lexer.l — Flex lexer for the SQL subset.
 *
 * Role in compiler pipeline: Phase 1 – Lexical Analysis.
 * Implements a DFA (via Flex) that converts raw SQL text into a token
 * stream consumed by the Bison parser.
 *
 * Features:
 *  - All SQL keywords (case-insensitive via YY_USER_ACTION lowercasing).
 *  - Identifiers, integer and float literals, quoted string literals.
 *  - Single-line comments (-- to end-of-line).
 *  - Tracks line and column numbers for every token.
 *  - Reports invalid characters and unterminated strings with lex_error().
 *  - Provides a token-dump mode (g_token_mode) printing a formatted table.
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "value.h"
#include "util.h"
#include "parser.tab.h"   /* token codes from Bison */

/* ── Source position tracking ────────────────────────────────────── */
int yyline = 1;
int yycol  = 1;
int yytoken_col = 1;   /* column at start of current token */

/* called before every action */
#define YY_USER_ACTION \
    yytoken_col = yycol; \
    for (int _i = 0; yytext[_i]; _i++) { \
        if (yytext[_i] == '\n') { yyline++; yycol = 1; } \
        else yycol++; \
    }

/* ── Token dump mode ─────────────────────────────────────────────── */
extern int g_token_mode;

static void dump_token(const char *type, const char *lexeme, int line, int col) {
    if (g_json_mode) {
        token_list_add(type, lexeme, line, col);
    } else {
        printf("%-20s | %-30s | %d:%d\n", type, lexeme, line, col);
    }
}

/* Helper: return token and optionally dump it */
static int tok(int code, const char *type_name) {
    if (g_token_mode)
        dump_token(type_name, yytext, yyline, yytoken_col);
    return code;
}

/* ── String literal buffer ───────────────────────────────────────── */
static char  str_buf[65536];
static int   str_len;
static int   str_start_line, str_start_col;

%}

%option noyywrap nounput noinput
%option yylineno

/* ── Start conditions ────────────────────────────────────────────── */
%x STR_STATE

/* ── Helper patterns ─────────────────────────────────────────────── */
DIGIT       [0-9]
ALPHA       [A-Za-z_]
ALNUM       [A-Za-z0-9_]
INT_LIT     {DIGIT}+
FLOAT_LIT   {DIGIT}+\.{DIGIT}*|\.{DIGIT}+
IDENT       {ALPHA}{ALNUM}*
WS          [ \t\r]+
NEWLINE     \n

%%

 /* ── Whitespace & comments ─────────────────────────────────────── */
{WS}            { /* skip */ }
{NEWLINE}       { /* skip — position tracked by YY_USER_ACTION */ }
"--"[^\n]*      { /* single-line comment: skip */ }

 /* ── Keywords (case-insensitive via Flex option) ─────────────────
    Note: We match lowercase because the actual input is lowercased
    before the rules run via YY_USER_ACTION — but actually YY_USER_ACTION
    does not lowercase; we rely on case-insensitive patterns via 
    duplicate rules. Instead we use Flex %option caseless.             */

 /* We'll handle this via a catch-all IDENT rule that maps to keywords */

 /* ── Operators & punctuation ─────────────────────────────────────── */
"="     { return tok(EQ,     "EQ");     }
"!="    { return tok(NEQ,    "NEQ");    }
"<>"    { return tok(NEQ,    "NEQ");    }
"<="    { return tok(LE,     "LE");     }
">="    { return tok(GE,     "GE");     }
"<"     { return tok(LT,     "LT");     }
">"     { return tok(GT,     "GT");     }
"+"     { return tok(PLUS,   "PLUS");   }
"-"     { return tok(MINUS,  "MINUS");  }
"*"     { return tok(STAR,   "STAR");   }
"/"     { return tok(SLASH,  "SLASH");  }
"("     { return tok(LPAREN, "LPAREN"); }
")"     { return tok(RPAREN, "RPAREN"); }
","     { return tok(COMMA,  "COMMA");  }
"."     { return tok(DOT,    "DOT");    }
";"     { return tok(SEMI,   "SEMI");   }

 /* ── Numeric literals ─────────────────────────────────────────────── */
{FLOAT_LIT} {
    yylval.fval = atof(yytext);
    if (g_token_mode) dump_token("FLOAT_LIT", yytext, yyline, yytoken_col);
    return FLOAT_LIT;
}
{INT_LIT} {
    yylval.ival = atoll(yytext);
    if (g_token_mode) dump_token("INT_LIT", yytext, yyline, yytoken_col);
    return INT_LIT;
}

 /* ── String literals ──────────────────────────────────────────────── */
"'" {
    str_len = 0; str_buf[0] = '\0';
    str_start_line = yyline; str_start_col = yytoken_col;
    BEGIN(STR_STATE);
}
<STR_STATE>"''" { /* escaped single quote */
    if (str_len < (int)sizeof(str_buf)-1) str_buf[str_len++] = '\'';
}
<STR_STATE>"'" {
    str_buf[str_len] = '\0';
    yylval.sval = xstrdup(str_buf);
    if (g_token_mode) {
        char display[64];
        snprintf(display, sizeof(display), "'%.60s'", str_buf);
        dump_token("STRING_LIT", display, str_start_line, str_start_col);
    }
    BEGIN(INITIAL);
    return STRING_LIT;
}
<STR_STATE>\n {
    lex_error(str_start_line, str_start_col, "unterminated string literal");
    str_buf[str_len] = '\0';
    yylval.sval = xstrdup(str_buf);
    BEGIN(INITIAL);
    return STRING_LIT;
}
<STR_STATE><<EOF>> {
    lex_error(str_start_line, str_start_col, "unterminated string literal (EOF)");
    str_buf[str_len] = '\0';
    yylval.sval = xstrdup(str_buf);
    BEGIN(INITIAL);
    return STRING_LIT;
}
<STR_STATE>. {
    if (str_len < (int)sizeof(str_buf)-1) str_buf[str_len++] = yytext[0];
}

 /* ── Identifiers & keywords ───────────────────────────────────────── */
{IDENT} {
    /* Lowercase the lexeme for case-insensitive keyword matching */
    char *low = str_lower(yytext);

    /* Map to keyword tokens */
    int kw = 0;
    if      (!strcmp(low,"select"))   kw = SELECT;
    else if (!strcmp(low,"distinct")) kw = DISTINCT;
    else if (!strcmp(low,"from"))     kw = FROM;
    else if (!strcmp(low,"where"))    kw = WHERE;
    else if (!strcmp(low,"as"))       kw = AS;
    else if (!strcmp(low,"join"))     kw = JOIN;
    else if (!strcmp(low,"inner"))    kw = INNER;
    else if (!strcmp(low,"left"))     kw = LEFT;
    else if (!strcmp(low,"on"))       kw = ON;
    else if (!strcmp(low,"order"))    kw = ORDER;
    else if (!strcmp(low,"by"))       kw = BY;
    else if (!strcmp(low,"asc"))      kw = ASC;
    else if (!strcmp(low,"desc"))     kw = DESC;
    else if (!strcmp(low,"limit"))    kw = LIMIT;
    else if (!strcmp(low,"and"))      kw = AND;
    else if (!strcmp(low,"or"))       kw = OR;
    else if (!strcmp(low,"not"))      kw = NOT;
    else if (!strcmp(low,"like"))     kw = LIKE;
    else if (!strcmp(low,"between"))  kw = BETWEEN;
    else if (!strcmp(low,"in"))       kw = IN;
    else if (!strcmp(low,"is"))       kw = IS;
    else if (!strcmp(low,"null"))     kw = NULLTOK;
    else if (!strcmp(low,"true"))     kw = TRUE_LIT;
    else if (!strcmp(low,"false"))    kw = FALSE_LIT;
    else if (!strcmp(low,"explain"))  kw = EXPLAIN;
    else if (!strcmp(low,"analyze"))  kw = ANALYZE;

    free(low);

    if (kw) {
        if (g_token_mode) dump_token(yytext, yytext, yyline, yytoken_col);
        return kw;
    }
    /* Plain identifier */
    yylval.sval = xstrdup(yytext);
    if (g_token_mode) dump_token("IDENT", yytext, yyline, yytoken_col);
    return IDENT;
}

 /* ── Invalid characters ───────────────────────────────────────────── */
. {
    lex_error(yyline, yytoken_col, "unexpected character '%s'", yytext);
}

%%

/* ── Public interface ─────────────────────────────────────────────── */
int g_token_mode = 0;

void lexer_print_header(void) {
    printf("%-20s | %-30s | %s\n", "TOKEN_TYPE", "LEXEME", "LINE:COL");
    printf("%.20s-+-%s-+-%s\n",
           "--------------------",
           "------------------------------",
           "--------");
}
