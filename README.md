# SQL Query Compiler & Optimizer (`sqlc`)

A complete, working, demo-ready **SQL Query Compiler and Optimizer** built in C11 using **Flex** and **Bison** for university-level **Principles of Compiler Design**.

---

## 🚀 Features

- **Lexical Analysis (Flex):** Case-insensitive keywords, identifiers, string literals with quote escaping (`''`), exact line and column tracking.
- **Syntactic Analysis (Bison):** LALR(1) grammar with operator precedence, unambiguous `BETWEEN ... AND ...` rules, **0 conflicts**.
- **Abstract Syntax Tree (AST):** Strongly-typed AST nodes with complete tree-printer (`--ast`).
- **Semantic Analysis & Symbol Table:**
  - Catalog dynamically discovers CSV files and infers schemas.
  - Table and alias resolution, qualified and unqualified column resolution.
  - Suggests misspelled columns using Levenshtein distance (`Did you mean 'name'?`).
  - Strict type checking.
- **Relational Algebra Logical Planner:** Maps queries into Relational Algebra trees (`Scan`, `Join`, `Filter`, `Sort`, `Project`, `Distinct`, `Limit`).
- **Query Optimizer (Fixed-Point Rewrite Engine):**
  - **Constant Folding:** Compile-time arithmetic reduction (`2 + 3 * 4` $\to$ `14`).
  - **Boolean Simplification:** Tautology and contradiction reduction (`x AND TRUE` $\to$ `x`).
  - **Filter Removal:** Prunes redundant unconditional filters (`WHERE 1 = 1`).
  - **Predicate Splitting:** Decomposes conjunctions into individual filters.
  - **Predicate Pushdown:** Pushes filter conditions below joins down to the scanned table.
  - **Join Algorithm Selection:** Automatically converts equijoins from $O(N \times M)$ nested-loop joins to $O(N + M)$ in-memory hash joins.
- **Volcano Execution Engine:** Classic iterator model (`open`/`next`/`close`) pulling rows on demand.
- **Interactive REPL & CLI:** Supports single queries, files (`-f`), comparison mode (`--compare`), verbose phase tracing (`--verbose`), and interactive REPL.

---

## 📁 Directory Structure

```
sql_compiler/
├── Makefile                # Build targets: all, clean, test, demo, conflicts
├── README.md               # Project documentation
├── generate_data.py        # Realistic CSV data generator
├── data/                   # CSV tables (students, dept, courses, enrollments)
│   ├── courses.csv
│   ├── dept.csv
│   ├── enrollments.csv
│   └── students.csv
├── docs/                   # Course documentation
│   └── technical_report.md # Comprehensive Compiler Design Technical Report
├── examples/               # Example SQL queries
│   ├── 01_basic_queries.sql
│   ├── 02_joins.sql
│   └── 03_optimizer_demo.sql
├── src/                    # Compiler implementation
│   ├── ast.c / ast.h       # AST definitions & printers
│   ├── catalog.c / catalog.h # CSV catalog & table metadata
│   ├── executor.c / executor.h # Volcano iterator model execution engine
│   ├── expr_eval.c / expr_eval.h # Runtime row expression evaluation
│   ├── lexer.l             # Flex scanner
│   ├── main.c              # CLI orchestrator & REPL
│   ├── optimizer.c / optimizer.h # Rule-based rewrite engine
│   ├── parser.y            # Bison LALR(1) parser
│   ├── plan.c / plan.h     # Logical plan representation
│   ├── planner.c / planner.h # AST to Logical Plan translator
│   ├── semantic.c / semantic.h # Semantic analysis & type checking
│   ├── util.c / util.h     # Memory helpers & string utilities
│   └── value.c / value.h   # Dynamic Value runtime representation
└── tests/                  # Test suite
    ├── run_tests.sh        # Automated test runner (25 test cases)
    ├── valid/              # Positive unit tests
    └── invalid/            # Error handling & rejection tests
```

---

## 🛠️ Build & Quickstart

### Prerequisites
- Linux / WSL (Ubuntu, Debian, Fedora, Arch, etc.)
- `gcc` (with C11 support)
- `flex`
- `bison`
- `python3` (for generating sample CSV data)

### Build Commands
```bash
# 1. Compile the compiler binary
make

# 2. Run the full test suite (25 test cases)
make test

# 3. Run the interactive demo queries
make demo

# 4. Clean build artifacts
make clean
```

---

## 💻 CLI Usage

```
Usage: ./sqlc [flags] ["SQL" | -f file.sql]

Flags:
  --tokens        Print token stream produced by Flex
  --ast           Print Abstract Syntax Tree
  --plan          Print initial Logical Query Plan
  --optimize      Enable query optimizer (default: enabled)
  --no-optimize   Disable query optimizer
  --verbose       Print all compilation phases in detail
  --compare       Compare unoptimized vs optimized execution
  --no-color      Disable terminal color codes
  --data <dir>    Path to CSV directory (default: data)
  -f <file>       Execute SQL statements from file
```

### Examples

#### 1. Execute a Single Query
```bash
./sqlc "SELECT name, age, gpa FROM students WHERE gpa >= 3.8 LIMIT 5;"
```

#### 2. Inspect the Compiler Pipeline (`--verbose`)
```bash
./sqlc --verbose "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20 LIMIT 5;"
```

#### 3. Compare Optimizer Performance (`--compare`)
```bash
./sqlc --compare "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20 AND 1 = 1;"
```

#### 4. Run an Example File
```bash
./sqlc -f examples/01_basic_queries.sql
```

#### 5. Interactive REPL Mode
```bash
./sqlc
sql> SELECT * FROM dept;
sql> exit
```

---

## 🧪 Test Suite

Run all tests with:
```bash
make test
```
The test suite validates:
1. **Valid queries (Grammar & Execution):** projections, comparison filters, arithmetic, constant folding, boolean logic, `LIKE` patterns, `BETWEEN`, `IN`, `IS NULL`, `IS NOT NULL`, `INNER JOIN`, `LEFT JOIN`, three-table joins, `DISTINCT`, `ORDER BY ASC/DESC`, `LIMIT`, `EXPLAIN ANALYZE`.
2. **Invalid queries (Error Detection):** syntax errors, unknown tables, unknown columns, misspelled columns with suggestions, invalid join conditions, invalid aliases.
3. **Optimizer Rules:** constant folding, predicate pushdown, hash join selection, filter removal.

---

## 🌐 Web Interface (Flask + Jinja2 + Bootstrap)

A modern full-stack web application wrapping the C compiler binary `./sqlc` is included in the `webapp/` directory.

### Quick Start
```bash
./run_web.sh
```
Or start manually:
```bash
python3 webapp/app.py
```
Then navigate to: **`http://127.0.0.1:5000`** in your browser.

### Key Features
- **Interactive SQL Editor:** CodeMirror 5 with SQL syntax highlighting, line numbers, and `Ctrl+Enter` shortcut.
- **Per-Session Workspaces:** Each visitor gets an isolated workspace (`webapp/workspaces/<session_id>/`) pre-seeded with sample tables. Custom tables never leak across sessions.
- **Table Manager:**
  - Dynamic table list with inferred types (`INT`, `FLOAT`, `STRING`, `BOOL`) and row counts.
  - CSV file uploader (drag & drop, header validation, schema checking).
  - Manual table creator with interactive column builder and live table preview.
  - Table preview modal displaying the first 20 rows.
  - Workspace reset button restoring sample CSV tables.
- **Multi-Stage Compiler Inspection:**
  - **Result:** Formatted HTML data table with NULL badges, execution timing, and CSV export.
  - **Tokens:** Formatted lexer token stream (`TOKEN_TYPE | LEXEME | LINE:COL`).
  - **AST:** Monospace syntax tree from Bison parser.
  - **Logical Plan:** Relational algebra query plan tree.
  - **Optimized Plan:** Rewritten execution plan and ordered list of optimizer rules fired.
  - **Compare Mode:** Side-by-side unoptimized vs. optimized execution metrics with percentage reduction in rows processed.
  - **Error Panel:** Prominent alert for Lexical, Syntax, Semantic, and Runtime errors with line and column highlighting.

### REST API Reference

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/` | Main single-page web interface |
| `POST` | `/api/run` | Execute query: `{query, mode, optimize}` |
| `GET` | `/api/tables` | List tables with columns, inferred types, row count |
| `GET` | `/api/tables/<name>` | Get table schema and first 20 rows preview |
| `POST` | `/api/tables/upload` | Multipart CSV file upload (`file`, `name`) |
| `POST` | `/api/tables/create` | Manually create table: `{name, columns, rows}` |
| `DELETE` | `/api/tables/<name>` | Delete a table from the session workspace |
| `POST` | `/api/reset` | Reset session workspace to sample CSV tables |
| `GET` | `/api/examples` | Get list of ready-made example queries |

### Security & Robustness
1. **No Shell Invocations:** Subprocess execution calls `subprocess.run(..., shell=False, timeout=5)`.
2. **Path Traversal Protection:** Table names are strictly validated against `^[A-Za-z_][A-Za-z0-9_]*$`; file paths are strictly resolved inside the session workspace.
3. **Query Length Limit:** Maximum 5,000 characters per query.
4. **Crash & Timeout Containment:** Subprocess timeouts or abnormal terminations return formatted JSON errors; Flask never returns an unhandled 500 HTML page.
5. **Upload Safeguards:** Maximum 5 MB upload limit, max 20 columns, max 10,000 rows, and strict column count consistency per row.

---

## 📄 License
Academic Free License for educational use in Principles of Compiler Design.
