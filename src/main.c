/*
 * main.c — CLI entry point, REPL, and query pipeline orchestrator.
 *
 * Modes:
 *   ./sqlc "SELECT ..."         single query from command line
 *   ./sqlc -f file.sql          read query from file
 *   ./sqlc                      interactive REPL (sql> prompt)
 *
 * Flags:
 *   --tokens      show lexer token stream
 *   --ast         show parsed AST
 *   --plan        show logical plan (before optimization)
 *   --optimize / --no-optimize
 *   --verbose     show every phase
 *   --compare     run unoptimized vs optimized side by side
 *   --no-color    disable ANSI color
 *
 * REPL commands: .tables  .schema <tbl>  .help  .quit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* All modules */
#include "util.h"
#include "value.h"
#include "ast.h"
#include "catalog.h"
#include "semantic.h"
#include "plan.h"
#include "planner.h"
#include "optimizer.h"
#include "executor.h"
#include "expr_eval.h"

/* Defined in lexer.l */
extern int   g_token_mode;
extern void  lexer_print_header(void);
extern void *yy_scan_string(const char *);
extern void  yy_delete_buffer(void *);
extern int   yyparse(void);
extern int   yylex(void);
extern int   yyline;
extern int   yytoken_col;

/* Defined in parser.y */
extern AstNode *g_parse_result;
extern int      g_parse_error;
extern int      g_explain_mode;
extern int      g_explain_analyze;

extern char *yytext;

/* ── Global flags ──────────────────────────────────────────────────── */
static int g_show_tokens   = 0;
static int g_show_ast      = 0;
static int g_show_plan     = 0;
static int g_do_optimize   = 1;
static int g_verbose       = 0;
static int g_compare       = 0;
static char *g_data_dir    = "data";

/* ── Pipeline ──────────────────────────────────────────────────────── */
static int run_query(const char *sql, Catalog *cat) {
    token_list_init();
    json_reset_error();

    /* ── Phase 1: Lex ────────────────────────────────────────────── */
    yyline = 1; yytoken_col = 1;
    g_parse_result = NULL;
    g_parse_error  = 0;
    g_explain_mode = 0;
    g_explain_analyze = 0;

    if (g_show_tokens || g_verbose || g_json_mode) {
        if (!g_json_mode) {
            printf("%s── Token Stream ──%s\n", COLOR_CYAN, COLOR_RESET);
            lexer_print_header();
        }
        g_token_mode = 1;
    }

    void *buf = yy_scan_string(sql);
    int parse_ret = yyparse();
    yy_delete_buffer(buf);
    g_token_mode = 0;

    if (!g_json_mode && (g_show_tokens || g_verbose)) printf("\n");

    if (parse_ret != 0 || g_parse_error || !g_parse_result) {
        if (g_json_mode) {
            parse_error(yyline, yytoken_col, "syntax error near '%s'", yytext ? yytext : "");
        }
        if (g_parse_result) ast_free(g_parse_result);
        token_list_free();
        return -1;
    }

    /* ── Phase 2: Show AST ───────────────────────────────────────── */
    if (!g_json_mode && (g_show_ast || g_verbose)) {
        printf("%s── Abstract Syntax Tree ──%s\n", COLOR_CYAN, COLOR_RESET);
        ast_print(g_parse_result, 0);
        printf("\n");
    }

    /* ── Phase 3: Semantic Analysis ─────────────────────────────── */
    SemCtx ctx;
    if (semantic_check(g_parse_result, cat, &ctx) != 0) {
        ast_free(g_parse_result);
        semctx_free(&ctx);
        token_list_free();
        return -1;
    }

    if (!g_json_mode && g_verbose) {
        printf("%s── Semantic Info ──%s\n", COLOR_CYAN, COLOR_RESET);
        for (int i = 0; i < ctx.ntables; i++)
            printf("  Table[%d]: %s (%s) — %d cols, %d rows\n",
                   i,
                   ctx.tables[i].table->name,
                   ctx.tables[i].alias,
                   ctx.tables[i].table->ncols,
                   ctx.tables[i].table->nrows);
        printf("\n");
    }

    /* ── Phase 4: Build Logical Plan ─────────────────────────────── */
    PlanNode *plan = planner_build(g_parse_result, &ctx);
    if (!plan) {
        if (g_json_mode) run_error("failed to build logical plan");
        ast_free(g_parse_result);
        semctx_free(&ctx);
        token_list_free();
        return -1;
    }

    if (!g_json_mode && (g_show_plan || g_verbose)) {
        printf("%s── Logical Plan (before optimization) ──%s\n",
               COLOR_CYAN, COLOR_RESET);
        plan_print(plan, 0, 1);
        printf("\n");
    }

    /* EXPLAIN without ANALYZE: just print plan and exit */
    if (g_explain_mode && !g_explain_analyze) {
        if (g_json_mode) {
            char *pstr = plan_to_str(plan);
            char *esc_p = json_escape(pstr ? pstr : "");
            printf("{\"status\":\"ok\",\"plan\":\"%s\"}\n", esc_p);
            free(esc_p);
            free(pstr);
        } else if (!g_show_plan && !g_verbose) {
            printf("%s── Query Plan ──%s\n", COLOR_CYAN, COLOR_RESET);
            plan_print(plan, 0, 1);
        }
        plan_free(plan);
        ast_free(g_parse_result);
        semctx_free(&ctx);
        token_list_free();
        return 0;
    }

    /* ── Phase 5: Optimize ───────────────────────────────────────── */
    OptLog log;
    optlog_init(&log);

    PlanNode *opt_plan = plan;
    if (g_do_optimize) {
        opt_plan = optimizer_run(plan, &ctx, &log);

        if (!g_json_mode && g_verbose) {
            printf("%s── Optimizer Rules Fired ──%s\n", COLOR_CYAN, COLOR_RESET);
            optlog_print(&log);
            printf("\n%s── Optimized Plan ──%s\n", COLOR_CYAN, COLOR_RESET);
            plan_print(opt_plan, 0, 1);
            printf("\n");
        }
    }

    /* ── Phase 6: Execute ────────────────────────────────────────── */
    int ncols;
    char **col_names = plan_output_names(opt_plan, &ctx, &ncols);

    Operator *exec_root = exec_build(opt_plan, &ctx);
    if (!exec_root) {
        run_error("failed to build executor");
        for (int i = 0; i < ncols; i++) free(col_names[i]);
        free(col_names);
        plan_free(opt_plan);
        ast_free(g_parse_result);
        semctx_free(&ctx);
        optlog_free(&log);
        token_list_free();
        return -1;
    }

    if (!g_json_mode) {
        int show_stats = g_explain_analyze || g_verbose;
        exec_run(exec_root, ncols, col_names, show_stats);
    } else {
        ExecResult res = exec_collect(exec_root);
        printf("{\"status\":\"ok\",\"columns\":[");
        for (int i = 0; i < ncols; i++) {
            char *esc = json_escape(col_names[i]);
            printf("%s\"%s\"", i > 0 ? "," : "", esc);
            free(esc);
        }
        printf("],\"rows\":[");
        for (int i = 0; i < res.nrows; i++) {
            if (i > 0) printf(",");
            printf("[");
            for (int c = 0; c < ncols && c < res.rows[i]->width; c++) {
                if (c > 0) printf(",");
                Value val = res.rows[i]->vals[c];
                if (val.type == VAL_INT) {
                    printf("%lld", val.ival);
                } else if (val.type == VAL_FLOAT) {
                    printf("%f", val.fval);
                } else if (val.type == VAL_STRING) {
                    char *esc = json_escape(val.sval);
                    printf("\"%s\"", esc);
                    free(esc);
                } else if (val.type == VAL_BOOL) {
                    printf("%s", val.bval ? "true" : "false");
                } else {
                    printf("null");
                }
            }
            printf("]");
        }
        printf("],\"row_count\":%d,\"time_ms\":%.2f", res.nrows, res.elapsed_ms);

        if (g_show_tokens || g_verbose) {
            printf(",\"tokens\":[");
            for (int i = 0; i < g_tokens.count; i++) {
                char *et = json_escape(g_tokens.items[i].type);
                char *el = json_escape(g_tokens.items[i].lexeme);
                printf("%s{\"type\":\"%s\",\"lexeme\":\"%s\",\"line\":%d,\"col\":%d}",
                       i > 0 ? "," : "", et, el,
                       g_tokens.items[i].line, g_tokens.items[i].col);
                free(et); free(el);
            }
            printf("]");
        }

        if (g_show_ast || g_verbose) {
            char *astr = ast_to_str(g_parse_result);
            char *esc = json_escape(astr ? astr : "");
            printf(",\"ast\":\"%s\"", esc);
            free(esc); free(astr);
        }

        if (g_show_plan || g_verbose || g_explain_mode) {
            char *pstr = plan_to_str(plan);
            char *esc = json_escape(pstr ? pstr : "");
            printf(",\"plan\":\"%s\"", esc);
            free(esc); free(pstr);
        }

        if (g_do_optimize && (g_verbose || g_show_plan)) {
            char *ostr = plan_to_str(opt_plan);
            char *esc = json_escape(ostr ? ostr : "");
            printf(",\"optimized_plan\":\"%s\"", esc);
            free(esc); free(ostr);

            printf(",\"rules_fired\":[");
            for (int i = 0; i < log.count; i++) {
                char *er = json_escape(log.rules[i]);
                printf("%s\"%s\"", i > 0 ? "," : "", er);
                free(er);
            }
            printf("]");
        }

        if (g_explain_analyze || g_verbose) {
            printf(",\"stats\":[");
            int first = 1;
            exec_stats_to_json(exec_root, stdout, &first);
            printf("]");
        }

        printf("}\n");
        fflush(stdout);
        exec_result_free(&res);
    }

    /* Cleanup */
    for (int i = 0; i < ncols; i++) free(col_names[i]);
    free(col_names);
    exec_free(exec_root);
    plan_free(opt_plan);
    ast_free(g_parse_result);
    semctx_free(&ctx);
    optlog_free(&log);
    token_list_free();
    return 0;
}

/* ── Compare mode ──────────────────────────────────────────────────── */
static void run_compare(const char *sql, Catalog *cat) {
    if (!g_json_mode) {
        printf("%s═══ UNOPTIMIZED ═══%s\n", COLOR_YELLOW, COLOR_RESET);
        g_do_optimize = 0; g_show_plan = 1;
        run_query(sql, cat);

        printf("\n%s═══ OPTIMIZED ═══%s\n", COLOR_YELLOW, COLOR_RESET);
        g_do_optimize = 1; g_show_plan = 1; g_verbose = 0;

        /* Need fresh parse */
        OptLog log; optlog_init(&log);
        yyline = 1; yytoken_col = 1;
        g_parse_result = NULL; g_parse_error = 0;
        void *buf = yy_scan_string(sql);
        yyparse();
        yy_delete_buffer(buf);
        if (!g_parse_result) return;

        SemCtx ctx;
        if (semantic_check(g_parse_result, cat, &ctx) != 0) {
            ast_free(g_parse_result); semctx_free(&ctx); return;
        }
        PlanNode *plan = planner_build(g_parse_result, &ctx);
        if (!plan) { ast_free(g_parse_result); semctx_free(&ctx); return; }

        PlanNode *opt_plan = optimizer_run(plan, &ctx, &log);

        printf("%s── Rules fired ──%s\n", COLOR_CYAN, COLOR_RESET);
        optlog_print(&log);
        printf("\n%s── Optimized plan ──%s\n", COLOR_CYAN, COLOR_RESET);
        plan_print(opt_plan, 0, 1);
        printf("\n");

        int ncols;
        char **col_names = plan_output_names(opt_plan, &ctx, &ncols);
        Operator *exec_root = exec_build(opt_plan, &ctx);
        if (exec_root) exec_run(exec_root, ncols, col_names, 1);

        for (int i=0;i<ncols;i++) free(col_names[i]);
        free(col_names);
        if (exec_root) exec_free(exec_root);
        plan_free(opt_plan);
        ast_free(g_parse_result);
        semctx_free(&ctx);
        optlog_free(&log);
        g_do_optimize = 1; g_show_plan = 0;
        return;
    }

    /* JSON compare mode */
    yyline = 1; yytoken_col = 1;
    g_parse_result = NULL; g_parse_error = 0;
    void *buf1 = yy_scan_string(sql);
    int p1 = yyparse();
    yy_delete_buffer(buf1);
    if (p1 != 0 || g_parse_error || !g_parse_result) {
        parse_error(yyline, yytoken_col, "syntax error near '%s'", yytext ? yytext : "");
        if (g_parse_result) ast_free(g_parse_result);
        return;
    }

    SemCtx ctx1;
    if (semantic_check(g_parse_result, cat, &ctx1) != 0) {
        ast_free(g_parse_result); semctx_free(&ctx1); return;
    }
    PlanNode *plan1 = planner_build(g_parse_result, &ctx1);
    if (!plan1) {
        run_error("failed to build unoptimized plan");
        ast_free(g_parse_result); semctx_free(&ctx1); return;
    }
    char *unopt_plan_str = plan_to_str(plan1);
    int ncols1;
    char **col_names1 = plan_output_names(plan1, &ctx1, &ncols1);
    Operator *exec_root1 = exec_build(plan1, &ctx1);
    if (!exec_root1) {
        run_error("failed to build unoptimized executor");
        for (int i=0;i<ncols1;i++) free(col_names1[i]);
        free(col_names1);
        plan_free(plan1); free(unopt_plan_str);
        ast_free(g_parse_result); semctx_free(&ctx1); return;
    }
    ExecResult unopt_res = exec_collect(exec_root1);
    long long unopt_rows_proc = exec_total_rows_processed(exec_root1);

    /* 2. Optimized run */
    OptLog log2; optlog_init(&log2);
    yyline = 1; yytoken_col = 1;
    g_parse_result = NULL; g_parse_error = 0;
    void *buf2 = yy_scan_string(sql);
    yyparse();
    yy_delete_buffer(buf2);
    SemCtx ctx2;
    semantic_check(g_parse_result, cat, &ctx2);
    PlanNode *plan2 = planner_build(g_parse_result, &ctx2);
    PlanNode *opt_plan2 = optimizer_run(plan2, &ctx2, &log2);
    char *opt_plan_str = plan_to_str(opt_plan2);
    int ncols2;
    char **col_names2 = plan_output_names(opt_plan2, &ctx2, &ncols2);
    Operator *exec_root2 = exec_build(opt_plan2, &ctx2);
    ExecResult opt_res = exec_collect(exec_root2);
    long long opt_rows_proc = exec_total_rows_processed(exec_root2);

    double reduction_pct = 0.0;
    if (unopt_rows_proc > 0) {
        reduction_pct = ((double)(unopt_rows_proc - opt_rows_proc) / (double)unopt_rows_proc) * 100.0;
        if (reduction_pct < 0.0) reduction_pct = 0.0;
    }

    printf("{\"status\":\"ok\",\"columns\":[");
    for (int i = 0; i < ncols2; i++) {
        char *esc = json_escape(col_names2[i]);
        printf("%s\"%s\"", i > 0 ? "," : "", esc);
        free(esc);
    }
    printf("],\"rows\":[");
    for (int i = 0; i < opt_res.nrows; i++) {
        if (i > 0) printf(",");
        printf("[");
        for (int c = 0; c < ncols2 && c < opt_res.rows[i]->width; c++) {
            if (c > 0) printf(",");
            Value val = opt_res.rows[i]->vals[c];
            if (val.type == VAL_INT) {
                printf("%lld", val.ival);
            } else if (val.type == VAL_FLOAT) {
                printf("%f", val.fval);
            } else if (val.type == VAL_STRING) {
                char *esc = json_escape(val.sval);
                printf("\"%s\"", esc);
                free(esc);
            } else if (val.type == VAL_BOOL) {
                printf("%s", val.bval ? "true" : "false");
            } else {
                printf("null");
            }
        }
        printf("]");
    }
    printf("],\"row_count\":%d,\"time_ms\":%.2f,", opt_res.nrows, opt_res.elapsed_ms);

    char *esc_unopt_p = json_escape(unopt_plan_str ? unopt_plan_str : "");
    printf("\"unoptimized\":{\"plan\":\"%s\",\"rows_out\":%d,\"rows_processed\":%lld,\"time_ms\":%.2f,\"stats\":[",
           esc_unopt_p, unopt_res.nrows, unopt_rows_proc, unopt_res.elapsed_ms);
    free(esc_unopt_p);
    int first1 = 1;
    exec_stats_to_json(exec_root1, stdout, &first1);
    printf("]},");

    char *esc_opt_p = json_escape(opt_plan_str ? opt_plan_str : "");
    printf("\"optimized\":{\"plan\":\"%s\",\"rows_out\":%d,\"rows_processed\":%lld,\"time_ms\":%.2f,\"rules_fired\":[",
           esc_opt_p, opt_res.nrows, opt_rows_proc, opt_res.elapsed_ms);
    free(esc_opt_p);
    for (int i = 0; i < log2.count; i++) {
        char *er = json_escape(log2.rules[i]);
        printf("%s\"%s\"", i > 0 ? "," : "", er);
        free(er);
    }
    printf("],\"stats\":[");
    int first2 = 1;
    exec_stats_to_json(exec_root2, stdout, &first2);
    printf("]},\"reduction_pct\":%.1f}\n", reduction_pct);
    fflush(stdout);

    for (int i=0;i<ncols1;i++) free(col_names1[i]);
    free(col_names1);
    exec_result_free(&unopt_res);
    exec_free(exec_root1);
    plan_free(plan1);
    free(unopt_plan_str);
    semctx_free(&ctx1);

    for (int i=0;i<ncols2;i++) free(col_names2[i]);
    free(col_names2);
    exec_result_free(&opt_res);
    exec_free(exec_root2);
    plan_free(opt_plan2);
    free(opt_plan_str);
    ast_free(g_parse_result);
    semctx_free(&ctx2);
    optlog_free(&log2);
}

/* ── REPL ──────────────────────────────────────────────────────────── */
static void repl(Catalog *cat) {
    printf("%sSQL Query Compiler v1.0  —  type .help for commands%s\n",
           COLOR_CYAN, COLOR_RESET);
    char line[8192];
    char query[65536];
    query[0] = '\0';

    while (1) {
        printf("sql> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

        /* REPL commands */
        if (strcmp(line, ".quit") == 0 || strcmp(line, ".exit") == 0) break;
        if (strcmp(line, ".help") == 0) {
            printf("Commands:\n"
                   "  .tables          list all available tables\n"
                   "  .schema <table>  show table schema\n"
                   "  .quit            exit\n"
                   "  .help            show this help\n"
                   "SQL flags (prepend to query):\n"
                   "  --tokens --ast --plan --verbose --compare\n");
            continue;
        }
        if (strcmp(line, ".tables") == 0) {
            printf("Available tables (in data/):\n");
            /* List .csv files */
            char cmd[512];
            snprintf(cmd, sizeof(cmd),
                     "ls %s/*.csv 2>/dev/null | sed 's|.*/||;s|\\.csv||'",
                     g_data_dir);
            system(cmd);
            continue;
        }
        if (strncmp(line, ".schema ", 8) == 0) {
            const char *tname = line + 8;
            TableDesc *td = catalog_load_table(cat, tname);
            if (!td) { fprintf(stderr,"Table '%s' not found\n",tname); continue; }
            printf("Table: %s  (%d rows)\n", td->name, td->nrows);
            for (int c=0;c<td->ncols;c++) {
                const char *ty = td->cols[c].type==VAL_INT    ? "INT"    :
                                 td->cols[c].type==VAL_FLOAT  ? "FLOAT"  :
                                 td->cols[c].type==VAL_STRING ? "STRING" :
                                 td->cols[c].type==VAL_BOOL   ? "BOOL"   : "NULL";
                printf("  %-20s %s\n", td->cols[c].name, ty);
            }
            continue;
        }

        /* Accumulate multi-line query */
        if (query[0]) strncat(query, " ", sizeof(query)-strlen(query)-1);
        strncat(query, line, sizeof(query)-strlen(query)-1);

        /* Run when we see a semicolon or non-empty single line */
        if (strchr(line, ';') || (line[0] && query[0])) {
            if (query[0]) {
                if (g_compare) run_compare(query, cat);
                else           run_query(query, cat);
            }
            query[0] = '\0';
        }
    }
    printf("Bye.\n");
}

/* ── Usage ─────────────────────────────────────────────────────────── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [flags] [\"SQL\"|  -f file.sql]\n"
        "Flags:\n"
        "  --tokens        print token stream\n"
        "  --ast           print AST\n"
        "  --plan          print logical plan\n"
        "  --optimize      enable optimizer (default)\n"
        "  --no-optimize   disable optimizer\n"
        "  --verbose       print all phases\n"
        "  --compare       run unoptimized vs optimized\n"
        "  --no-color      disable colour output\n"
        "  --json          output result as JSON\n"
        "  --data <dir>    directory for CSV files (default: data)\n"
        "  --data-dir <dir>directory for CSV files (alias for --data)\n"
        "  -f <file>       read SQL from file\n",
        prog);
}

/* ── main ──────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    char *sql_arg     = NULL;
    char *file_arg    = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tokens"))     g_show_tokens = 1;
        else if (!strcmp(argv[i], "--ast"))   g_show_ast    = 1;
        else if (!strcmp(argv[i], "--plan"))  g_show_plan   = 1;
        else if (!strcmp(argv[i], "--optimize"))    g_do_optimize = 1;
        else if (!strcmp(argv[i], "--no-optimize")) g_do_optimize = 0;
        else if (!strcmp(argv[i], "--verbose"))     g_verbose     = 1;
        else if (!strcmp(argv[i], "--compare"))     g_compare     = 1;
        else if (!strcmp(argv[i], "--no-color"))    g_use_color   = 0;
        else if (!strcmp(argv[i], "--json")) {
            g_json_mode = 1;
            g_use_color = 0;
        }
        else if ((!strcmp(argv[i], "--data") || !strcmp(argv[i], "--data-dir")) && i+1 < argc)
            g_data_dir = argv[++i];
        else if (!strcmp(argv[i], "-f") && i+1 < argc)
            file_arg = argv[++i];
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]); return 0;
        } else if (argv[i][0] != '-') {
            sql_arg = argv[i];
        }
    }

    Catalog *cat = catalog_new(g_data_dir);
    int rc = 0;

    if (file_arg) {
        FILE *f = fopen(file_arg, "r");
        if (!f) { fprintf(stderr, "Cannot open '%s'\n", file_arg); return 1; }
        char buf[65536]; size_t n = fread(buf, 1, sizeof(buf)-1, f);
        buf[n] = '\0'; fclose(f);
        /* Run each statement split by ';' */
        char *p = buf;
        while (*p) {
            char *semi = strchr(p, ';');
            if (!semi) { /* last statement without semicolon */
                if (g_compare) run_compare(p, cat);
                else if (run_query(p, cat) != 0) rc = 1;
                break;
            }
            char saved = *(semi+1); *(semi+1) = '\0';
            if (g_compare) run_compare(p, cat);
            else if (run_query(p, cat) != 0) rc = 1;
            *(semi+1) = saved;
            p = semi + 1;
            while (*p == '\n' || *p == '\r' || *p == ' ') p++;
        }
    } else if (sql_arg) {
        if (g_compare) run_compare(sql_arg, cat);
        else if (run_query(sql_arg, cat) != 0) rc = 1;
    } else {
        repl(cat);
    }

    catalog_free(cat);
    return rc;
}
