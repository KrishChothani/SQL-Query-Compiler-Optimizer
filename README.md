# SQL Query Compiler & Optimizer (`sqlc`)

> A complete relational database query compiler, rule-based cost optimizer, and Volcano iterator execution engine written in C11 (Flex & Bison) with an interactive Flask web interface.

**Course**: Principles of Compiler Design  
**Tech Stack**: C11, Flex 2.6, Bison 3.8, Python 3.10+, Flask 3.0, Jinja2, Bootstrap 5, CodeMirror 5  
**Author**: `[Author Name]` | **Enrollment No.**: `[Enrollment No.]` | **Instructor**: `[Instructor Name]` | **Date**: October 2026  

---

## Table of Contents

- [1. Overview](#1-overview)
- [2. Features](#2-features)
- [3. Architecture](#3-architecture)
- [4. Project Structure](#4-project-structure)
- [5. Prerequisites and Installation](#5-prerequisites-and-installation)
- [6. Build and Run (C Compiler)](#6-build-and-run-c-compiler)
- [7. Run the Web Interface](#7-run-the-web-interface)
- [8. Supported SQL Subset](#8-supported-sql-subset)
- [9. Sample Data](#9-sample-data)
- [10. Examples](#10-examples)
- [11. Optimizer Showcase](#11-optimizer-showcase)
- [12. Web API Reference](#12-web-api-reference)
- [13. Testing](#13-testing)
- [14. Error Handling](#14-error-handling)
- [15. Security Notes (Web Application)](#15-security-notes-web-application)
- [16. Demo Guide for Evaluation](#16-demo-guide-for-evaluation)
- [17. Limitations and Future Work](#17-limitations-and-future-work)
- [18. Troubleshooting](#18-troubleshooting)
- [19. License and Acknowledgements](#19-license-and-acknowledgements)

---

## 1. Overview

`sqlc` is an educational, production-style SQL query compiler and relational query optimizer built for university-level **Principles of Compiler Design**. The system implements the classic six-phase compiler pipeline: scanning raw SQL text into tokens, parsing into an Abstract Syntax Tree (AST), performing semantic verification and schema catalog lookup against CSV tables, constructing a relational algebra Intermediate Representation (IR), executing a fixed-point rule-based query optimizer, and evaluating physical plans using the classic Volcano iterator model.

Unlike toy parsers that stop at syntax trees, `sqlc` compiles declarative SQL queries directly into executable algebraic operator trees. The project demonstrates core computer science compiler principles including deterministic finite automata (DFA), LALR(1) grammars with zero shift/reduce conflicts, symbol table type inference, relational tree rewriting, compile-time constant folding, and in-memory hash join algorithms. A companion Flask web application wraps the native binary, providing an interactive query laboratory with session-isolated workspaces.

### Compiler Phase to Module Mapping

| Compiler Phase | Implementation Module | Primary Responsibilities |
|---|---|---|
| **Phase 1: Lexical Analysis** | `src/lexer.l` | DFA tokenization via Flex; keyword normalization, literals, line:col tracking. |
| **Phase 2: Syntax Analysis** | `src/parser.y` | Bison LALR(1) grammar; operator precedence, AST construction, 0 conflicts. |
| **Phase 3: Semantic Analysis** | `src/semantic.c`, `src/catalog.c` | Symbol table, CSV schema discovery, type checking, Levenshtein suggestions. |
| **Phase 4: Intermediate Representation** | `src/plan.c`, `src/planner.c` | Translates AST into a Relational Algebra plan tree (Scan, Filter, Project, Join). |
| **Phase 5: Query Optimization** | `src/optimizer.c` | Fixed-point relational rewrites: constant folding, pushdown, hash join selection. |
| **Phase 6: Code Generation & Execution** | `src/executor.c`, `src/expr_eval.c` | Volcano iterator model (`open`/`next`/`close`), materialization, stats tracking. |

---

## 2. Features

- **SQL Subset Support:** `SELECT [DISTINCT]`, column aliasing, multi-table `INNER JOIN` and `LEFT JOIN`, `WHERE`, `ORDER BY [ASC|DESC]`, and `LIMIT`.
- **Expression Evaluation:** Arithmetic (`+`, `-`, `*`, `/`), comparisons (`=`, `!=`, `<>`, `<`, `>`, `<=`, `>=`), boolean logic (`AND`, `OR`, `NOT`), `LIKE` prefix/suffix matching, `BETWEEN ... AND ...`, `IN (...)`, and `IS NULL` / `IS NOT NULL`.
- **Semantic Verification & Catalog:** Automatic CSV header parsing and type inference (`INT`, `FLOAT`, `STRING`, `BOOL`), table/alias scoping, and Levenshtein typo suggestions (`Did you mean 'name'?`).
- **Relational Optimizer:**
  - Compile-time Constant Folding (`2 + 3 * 4` $\to$ `14`).
  - Boolean Simplification & Tautology Pruning (`WHERE x AND 1 = 1` $\to$ `WHERE x`).
  - Filter Removal & Predicate Splitting.
  - Predicate Pushdown (pushes selection below joins directly into scan filters).
  - Join Algorithm Selection (automatically converts equijoins from $O(N \times M)$ Nested-Loop to $O(N + M)$ Hash Join).
- **Execution Engine:** Pull-based Volcano iterator model with memory-safe value representations and execution statistics (`rows_in`, `rows_out`, timing).
- **CLI & Output Modes:** Supports single query strings, script files (`-f`), interactive REPL, `--tokens`, `--ast`, `--plan`, `--verbose`, `--compare`, `EXPLAIN`, `EXPLAIN ANALYZE`, and clean `--json`.
- **Web Application:** Dual-column UI with CodeMirror 5 editor, per-session data directories, CSV upload, manual table builder with live preview, multi-stage tabbed outputs, and side-by-side comparison.

---

## 3. Architecture

### Compiler Pipeline

```text
       Raw SQL Text
            │
            ▼
┌───────────────────────┐
│     Flex Scanner      │  Phase 1: Lexical Analysis (src/lexer.l)
│       (lexer.l)       │  DFA token stream with line & column tracking
└───────────┬───────────┘
            │ Token Stream
            ▼
┌───────────────────────┐
│     Bison Parser      │  Phase 2: Syntax Analysis (src/parser.y)
│       (parser.y)      │  LALR(1) grammar; constructs strongly-typed AST
└───────────┬───────────┘
            │ Abstract Syntax Tree (AST)
            ▼
┌───────────────────────┐
│   Semantic Analyzer   │  Phase 3: Semantic Analysis (src/semantic.c, src/catalog.c)
│  (semantic.c/catalog) │  Resolves tables, columns, aliases, types & CSV schemas
└───────────┬───────────┘
            │ Annotated AST + Symbol Table Context
            ▼
┌───────────────────────┐
│    Logical Planner    │  Phase 4: Intermediate Representation (src/planner.c)
│      (planner.c)      │  Constructs Relational Algebra Tree (Scan, Join, Filter, Project)
└───────────┬───────────┘
            │ Unoptimized Logical Plan
            ▼
┌───────────────────────┐
│    Query Optimizer    │  Phase 5: Optimization (src/optimizer.c)
│     (optimizer.c)     │  Fixed-point rewrite passes: pushdown, folding, hash join
└───────────┬───────────┘
            │ Optimized Physical Plan
            ▼
┌───────────────────────┐
│   Volcano Executor    │  Phase 6: Execution Engine (src/executor.c, src/expr_eval.c)
│     (executor.c)      │  Demand-driven open() / next() / close() iterator model
└───────────┬───────────┘
            │ Result Tuples
            ▼
       Output Table / JSON
```

### Web Integration Architecture

```text
Browser Client (HTML5 / Vanilla JS / Bootstrap 5 / CodeMirror 5)
  │
  ├── [POST /api/run]  {query, mode, optimize}
  ├── [GET  /api/tables]
  └── [POST /api/tables/upload]
  │
  ▼
Flask Web Server (Python 3.10+, webapp/app.py)
  │
  ├── table_manager.py     ──> Isolates workspace at webapp/workspaces/<session_id>/
  └── compiler_bridge.py   ──> Enforces timeout=5s, max_len=5000, shell=False
        │
        ▼ (subprocess.run)
Native C Binary (./sqlc --data-dir <ws> --json -f /tmp/query.sql)
        │
        ▼ (stdout JSON)
Flask Router (app.py) ──> Formatted JSON Response ──> Browser Rendering
```

### Architectural Concepts by Phase

- **Lexical Analysis (Flex):** Implements regular expressions compiled into a Deterministic Finite Automaton (DFA) that converts raw source text into integer token identifiers, recording file line and column coordinates for precise diagnostics.
- **Syntactic Analysis (Bison):** Implements a bottom-up LALR(1) parser enforcing a context-free grammar (CFG). Shift/reduce conflicts are completely eliminated using explicit operator precedence (`%left`, `%right`, `%nonassoc`).
- **Semantic Analysis & Symbol Table:** Traverses the AST to bind table references and unqualified attributes to concrete schemas loaded from CSV headers. Performs static type checking and suggests misspelled column names using Levenshtein edit distance.
- **Intermediate Representation (Relational Algebra):** Maps declarative SQL syntax into procedural relational algebra trees comprised of relational primitives: selection ($\sigma$), projection ($\pi$), cartesian product / join ($\bowtie$), distinct ($\delta$), sort ($\tau$), and limit ($\lambda$).
- **Rule-Based Optimizer:** Applies algebraic transformation equivalences iteratively until reaching a fixed point (no further rules fire), reducing intermediate tuple volumes prior to expensive joins.
- **Volcano Execution Engine:** Implements the classic demand-driven iterator model where parent operators invoke `next()` on child operators, streaming tuples pipeline-fashion with minimal memory overhead.

---

## 4. Project Structure

```text
sql_compiler/
├── Makefile                       # GNU Make build file (targets: all, clean, test, demo)
├── README.md                      # Comprehensive project documentation
├── generate_data.py               # Generates sample student/department CSV data
├── run_web.sh                     # Automated startup script for web application
├── sqlc                           # Native compiler executable (generated by make)
├── data/                          # Sample CSV relational tables
│   ├── courses.csv                # 20 rows (id, title, dept_id, credits)
│   ├── dept.csv                   # 5 rows (id, dname, building)
│   ├── enrollments.csv            # 884 rows (student_id, course_id, grade)
│   └── students.csv               # 200 rows (id, name, age, dept_id, gpa)
├── docs/                          # Course and architecture documentation
│   ├── demo_script.md             # 5-step evaluation script for examiners
│   ├── screenshots/               # Directory for UI screenshots (.gitkeep)
│   ├── technical_report.md        # Compiler design report & grammar specification
│   └── web_architecture.md        # Web layer architecture & dataflow specifications
├── examples/                      # Reference SQL scripts
│   ├── 01_basic_queries.sql       # Selection, filtering, arithmetic, and limits
│   ├── 02_joins.sql               # Inner, left outer, and three-table joins
│   └── 03_optimizer_demo.sql      # Queries demonstrating rewrite optimizations
├── src/                           # Compiler C implementation
│   ├── ast.c / ast.h              # AST constructors, destructors, and visual tree printer
│   ├── catalog.c / catalog.h      # CSV catalog loader, schema discovery, type inference
│   ├── executor.c / executor.h    # Volcano iterator operators (Scan, Filter, Join, Sort)
│   ├── expr_eval.c / expr_eval.h  # Runtime expression evaluator for WHERE and ON clauses
│   ├── lexer.l                    # Flex scanner specification (regex rules, tokens)
│   ├── main.c                     # CLI orchestrator, flag handler, REPL, JSON emitter
│   ├── optimizer.c / optimizer.h  # Fixed-point relational algebra rewrite rules
│   ├── parser.y                   # Bison LALR(1) grammar specification and actions
│   ├── plan.c / plan.h            # Logical plan representation and relational tree printer
│   ├── planner.c / planner.h      # Translator from AST to logical relational plan
│   ├── semantic.c / semantic.h    # Type checking, symbol scoping, and typo suggestions
│   ├── util.c / util.h            # Dynamic array (Vec), string helpers, JSON escaping
│   └── value.c / value.h          # Tagged union runtime values (INT, FLOAT, STRING, BOOL, NULL)
├── tests/                         # Automated test suite
│   ├── run_tests.sh               # Bash test harness (25 automated compiler tests)
│   ├── invalid/                   # Negative test queries (syntax and semantic errors)
│   └── valid/                     # Positive test queries (features and execution)
└── webapp/                        # Flask Web Application
    ├── app.py                     # Flask backend, REST API routes, session management
    ├── compiler_bridge.py         # Subprocess execution bridge with security guards
    ├── requirements.txt           # Python dependencies (Flask, pytest)
    ├── table_manager.py           # Per-session workspace isolation & CSV validation
    ├── static/                    # Static frontend assets
    │   ├── css/style.css          # Modern dark/light styling, tree views, badges
    │   └── js/app.js              # Vanilla JS frontend client logic
    ├── templates/                 # Jinja2 HTML templates
    │   ├── base.html              # Base layout, CDN libraries, navigation
    │   ├── index.html             # Two-column layout (editor + table sidebar)
    │   └── partials/              # Modular template components
    │       ├── stages_panel.html  # Output tabs (Result, Tokens, AST, Plan, Compare)
    │       └── tables_panel.html  # Table manager sidebar and modal forms
    └── tests/                     # Web API test suite
        └── test_api.py            # 8 Pytest integration test cases
```

---

## 5. Prerequisites and Installation

### Linux / WSL / Ubuntu / Debian

Install the C toolchain, Flex, Bison, and Python:
```bash
sudo apt update
sudo apt install -y gcc make flex bison python3 python3-venv python3-pip
```

### Windows (WSL Recommended)

We recommend using **WSL2 (Windows Subsystem for Linux)** with Ubuntu:
```powershell
wsl --install -d Ubuntu
```
Once inside Ubuntu, execute the Linux installation command above. Alternatively, MSYS2 (`pacman -S mingw-w64-x86_64-gcc make flex bison python`) can be used.

### macOS (Homebrew)

```bash
brew install gcc make flex bison python3
export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:$PATH"
```

### Verifying Toolchain Versions

Confirm that your installed versions meet the minimum requirements:
```bash
gcc --version     # Requires GCC 9+ (tested on GCC 14.2)
flex --version    # Requires Flex 2.6+ (tested on Flex 2.6.4)
bison --version   # Requires Bison 3.5+ (tested on Bison 3.8.2)
python3 --version # Requires Python 3.10+ (tested on Python 3.12.7)
```

---

## 6. Build and Run (C Compiler)

### Build Targets

```bash
make           # Compiles Flex, Bison, and C sources into executable ./sqlc
make test      # Runs the 25-case compiler test suite
make demo      # Runs a curated demonstration of compiler features
make clean     # Removes build directory and the compiled binary
make conflicts # Inspects the Bison conflict report (verifies 0 conflicts)
```

### Command-Line Usage

The compiler accepts SQL directly from command-line arguments, from files (`-f`), or interactively:
```bash
./sqlc [flags] ["SQL QUERY" | -f file.sql]
```

### CLI Flags Reference

| Flag | Purpose | Example |
|---|---|---|
| `--tokens` | Prints the token stream produced by Flex | `./sqlc --tokens "SELECT * FROM dept;"` |
| `--ast` | Prints the indented Abstract Syntax Tree | `./sqlc --ast "SELECT 2 + 3 * 4;"` |
| `--plan` | Prints the logical query plan before optimization | `./sqlc --plan "SELECT * FROM dept;"` |
| `--optimize` | Enables query optimization passes (default: on) | `./sqlc --optimize "SELECT * FROM dept;"` |
| `--no-optimize` | Disables all query optimizer passes | `./sqlc --no-optimize "SELECT * FROM dept;"` |
| `--verbose` | Dumps token stream, AST, logical plan, and execution stats | `./sqlc --verbose "SELECT * FROM dept;"` |
| `--compare` | Runs unoptimized vs. optimized side by side | `./sqlc --compare "SELECT * FROM dept;"` |
| `--no-color` | Disables ANSI color sequences in output | `./sqlc --no-color "SELECT * FROM dept;"` |
| `--json` | Outputs execution results and stages as JSON | `./sqlc --json "SELECT * FROM dept;"` |
| `--data <dir>` | Specifies path to CSV table directory (default: `data`) | `./sqlc --data data "SELECT * FROM dept;"` |
| `--data-dir <dir>` | Alias for `--data` | `./sqlc --data-dir data "SELECT * FROM dept;"` |
| `-f <file>` | Reads and executes SQL queries from a file | `./sqlc -f examples/01_basic_queries.sql` |
| `--help` | Displays command-line syntax and options | `./sqlc --help` |

### Interactive REPL Commands

Running `./sqlc` without arguments opens the interactive shell:
```text
sql> .help
Commands:
  .tables          list all available tables
  .schema <table>  show table schema
  .quit            exit
```

---

## 7. Run the Web Interface

### Quick Start via Script

The project provides an automated launcher script that verifies the binary, installs Python dependencies if required, and starts the server:
```bash
chmod +x run_web.sh
./run_web.sh
```

### Manual Setup

```bash
# 1. Compile the C binary if not already built
make

# 2. (Optional) Create and activate a Python virtual environment
python3 -m venv venv
source venv/bin/activate

# 3. Install Flask and Pytest
pip install -r webapp/requirements.txt

# 4. Start the Flask application
python3 webapp/app.py
```
Open **`http://127.0.0.1:5000`** in your web browser.

### Web Interface Overview

- **SQL Editor:** CodeMirror 5 with SQL syntax highlighting, keyboard shortcuts (`Ctrl+Enter` to execute), and an **Examples** dropdown.
- **Left Sidebar ("Tables"):** Displays all tables in the current session with inferred column types and row counts. Includes **Preview** and **Delete** actions.
- **Add Table Modal:**
  - *(a) Upload CSV:* Uploads custom `.csv` files with schema checking.
  - *(b) Create Manually:* Interactive dynamic column builder (`INT`, `FLOAT`, `STRING`, `BOOL`) with live data preview.
- **Multi-Stage Output Tabs:**
  - **Result:** HTML table with greyed `NULL` values, execution time, and CSV export.
  - **Tokens:** Formatted `TOKEN_TYPE | LEXEME | LINE:COL` stream.
  - **AST:** Monospace syntax tree generated by the Bison parser.
  - **Logical Plan:** Relational algebra tree.
  - **Optimized Plan:** Rewritten plan alongside badges of optimizer rules fired.
  - **Compare:** Side-by-side unoptimized vs. optimized operator execution metrics with percentage reduction in rows processed.
  - **Error Panel:** Prominent alert displaying error category (`Lexical`, `Syntax`, `Semantic`, `Runtime`), error location, and line highlighting.

### Screenshots

*Placeholders have been prepared in `docs/screenshots/` (`.gitkeep`). Capture and save the following files when taking project screenshots:*
1. `![Main Interface](docs/screenshots/main.png)` — Query editor and tables sidebar.
2. `![Query Result](docs/screenshots/result_table.png)` — Result tab displaying formatted rows.
3. `![AST and Plan](docs/screenshots/ast_plan.png)` — Monospace AST and Logical Plan view.
4. `![Compare Mode](docs/screenshots/compare_mode.png)` — Side-by-side optimization reduction metrics.
5. `![Error Reporting](docs/screenshots/error_panel.png)` — Red alert panel with highlighted editor line.

---

## 8. Supported SQL Subset

### Grammar Summary

```sql
SELECT [DISTINCT] <col_list | *>
FROM <table_name> [[AS] <alias>]
[ [INNER] JOIN <table_name> [[AS] <alias>] ON <expr> ]*
[ LEFT JOIN <table_name> [[AS] <alias>] ON <expr> ]*
[ WHERE <expr> ]
[ ORDER BY <col_name> [ASC | DESC] [, ...] ]
[ LIMIT <integer> ] ;
```

### Operator Precedence (Lowest to Highest)

| Precedence Level | Operators | Associativity | Description |
|:---:|:---:|:---:|---|
| 1 | `OR` | Left | Logical disjunction |
| 2 | `AND` | Left | Logical conjunction |
| 3 | `NOT` | Right | Logical negation |
| 4 | `=`, `!=`, `<>`, `<`, `<=`, `>`, `>=` | None | Relational comparisons |
| 5 | `LIKE`, `BETWEEN ... AND ...`, `IN (...)`, `IS NULL`, `IS NOT NULL` | None | Pattern matching and set membership |
| 6 | `+`, `-` | Left | Addition and subtraction |
| 7 | `*`, `/` | Left | Multiplication and division |
| 8 | `-` (Unary) | Right | Unary arithmetic negation |

### Supported Literals & Types

- **Integer (`VAL_INT`):** `64-bit` signed integers (`42`, `-10`).
- **Float (`VAL_FLOAT`):** Double-precision floating point (`3.1415`, `.5`).
- **String (`VAL_STRING`):** Single-quoted strings with quote escaping (`'Alice'`, `'O''Connor'`).
- **Boolean (`VAL_BOOL`):** `TRUE` and `FALSE` (case-insensitive).
- **Null (`VAL_NULL`):** `NULL` (case-insensitive).

### Unsupported Features (Explicit Scope Limits)

- Subqueries (`WHERE id IN (SELECT ...)`)
- `GROUP BY` and `HAVING`
- Aggregate functions (`COUNT()`, `SUM()`, `AVG()`, `MIN()`, `MAX()`)
- Multi-table `UPDATE`, `INSERT`, `DELETE`, `CREATE TABLE` DDL (tables are loaded from CSV)
- Window functions (`OVER (PARTITION BY ...)`)
- `RIGHT JOIN` and `FULL OUTER JOIN`

---

## 9. Sample Data

The project ships with four normalized sample CSV tables in `data/`:

| Table | File | Rows | Columns & Inferred Types | Description |
|---|---|:---:|---|---|
| `students` | `data/students.csv` | 200 | `id (INT)`, `name (STRING)`, `age (INT)`, `dept_id (INT)`, `gpa (FLOAT)` | University students |
| `dept` | `data/dept.csv` | 5 | `id (INT)`, `dname (STRING)`, `building (STRING)` | Academic departments |
| `courses` | `data/courses.csv` | 20 | `id (INT)`, `title (STRING)`, `dept_id (INT)`, `credits (INT)` | Course offerings |
| `enrollments` | `data/enrollments.csv` | 884 | `student_id (INT)`, `course_id (INT)`, `grade (STRING)` | Student course enrollments |

---

## 10. Examples

*All examples below show real captured output from `./sqlc`.*

### 1. Simple SELECT with Filtering and LIMIT

```bash
./sqlc "SELECT name, age, gpa FROM students WHERE gpa >= 3.8 LIMIT 5;"
```
```text
+-----------------+-----+------+
| name            | age | gpa  | 
+-----------------+-----+------+
| Xander Anderson | 26  | 3.93 | 
| Sam Garcia      | 23  | 3.99 | 
| Uma Clark       | 18  | 3.82 | 
| Beth Harris     | 20  | 3.9  | 
| Beth Lee        | 27  | 3.81 | 
+-----------------+-----+------+
5 rows (0.0000 s)
```

### 2. Operator Precedence and AST Verification

Evaluating arithmetic precedence (`*` over `+`):
```bash
./sqlc --ast "SELECT 2 + 3 * 4 AS val FROM students LIMIT 1;"
```
```text
── Abstract Syntax Tree ──
SELECT
  Columns:
    List[1]
      AS val  [line 1]
        BinOp(+)  [line 1]
          Literal(2)  [line 1]
          BinOp(*)  [line 1]
            Literal(3)  [line 1]
            Literal(4)  [line 1]
  FROM: students
  LIMIT: 1

+-----+
| val | 
+-----+
| 14  | 
+-----+
1 row (0.0000 s)
```

Evaluating boolean precedence (`AND` binds tighter than `OR`):
```bash
./sqlc --ast "SELECT name FROM students WHERE age > 20 OR gpa > 3.5 AND age < 25 LIMIT 1;"
```
```text
── Abstract Syntax Tree ──
SELECT
  Columns:
    List[1]
      name  [line 1]
  FROM: students
  WHERE:
    BinOp(OR)  [line 1]
      BinOp(>)  [line 1]
        age  [line 1]
        Literal(20)  [line 1]
      BinOp(AND)  [line 1]
        BinOp(>)  [line 1]
          gpa  [line 1]
          Literal(3.5)  [line 1]
        BinOp(<)  [line 1]
          age  [line 1]
          Literal(25)  [line 1]
  LIMIT: 1

+---------------+
| name          | 
+---------------+
| Hank Anderson | 
+---------------+
1 row (0.0000 s)
```

### 3. Two-Table and Three-Table JOIN

Two-table equijoin between `students` and `dept`:
```bash
./sqlc "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id LIMIT 5;"
```
```text
+-------------+---------+
| s.name      | d.dname | 
+-------------+---------+
| Jack Brown  | CS      | 
| Ned Smith   | CS      | 
| Karen Lewis | CS      | 
| Tara White  | CS      | 
| Eve Jackson | CS      | 
+-------------+---------+
5 rows (0.0001 s)
```

Three-table equijoin joining `students`, `enrollments`, and `courses`:
```bash
./sqlc "SELECT s.name, c.title, e.grade FROM students s JOIN enrollments e ON s.id = e.student_id JOIN courses c ON e.course_id = c.id LIMIT 5;"
```
```text
+-----------+-------------------+---------+
| s.name    | c.title           | e.grade | 
+-----------+-------------------+---------+
| Uma Brown | Computer Networks | C       | 
| Uma Brown | Data Structures   | B       | 
| Uma Brown | Quantum Physics   | D       | 
| Uma Brown | American History  | A-      | 
| Uma Brown | Algorithms        | C+      | 
+-----------+-------------------+---------+
5 rows (0.0008 s)
```

### 4. LEFT OUTER JOIN with NULLs

Departments without courses exceeding 3 credits emit `NULL`:
```bash
./sqlc "SELECT d.dname, c.title FROM dept d LEFT JOIN courses c ON d.id = c.dept_id AND c.credits > 3;"
```
```text
+---------+-------------------+
| d.dname | c.title           | 
+---------+-------------------+
| CS      | Algorithms        | 
| CS      | Compilers         | 
| CS      | Operating Systems | 
| CS      | Machine Learning  | 
| Math    | NULL              | 
| Physics | Mechanics         | 
| Physics | Electromagnetism  | 
| English | NULL              | 
| History | NULL              | 
+---------+-------------------+
9 rows (0.0000 s)
```

### 5. ORDER BY, LIMIT, and DISTINCT

```bash
./sqlc "SELECT DISTINCT building FROM dept ORDER BY building ASC;"
```
```text
+---------------+
| building      | 
+---------------+
| Arts Building | 
| Science Hall  | 
| Tech Building | 
+---------------+
3 rows (0.0000 s)
```

```bash
./sqlc "SELECT name, gpa FROM students WHERE gpa IS NOT NULL ORDER BY gpa DESC LIMIT 5;"
```
```text
+-----------------+------+
| name            | gpa  | 
+-----------------+------+
| Beth Lewis      | 4    | 
| Sam Garcia      | 3.99 | 
| Olivia Anderson | 3.98 | 
| Sam Harris      | 3.95 | 
| Mia Anderson    | 3.95 | 
+-----------------+------+
5 rows (0.0003 s)
```

### 6. Lexer Token Stream (`--tokens`)

```bash
./sqlc --tokens "SELECT id, dname FROM dept WHERE id = 1;"
```
```text
── Token Stream ──
TOKEN_TYPE           | LEXEME                         | LINE:COL
---------------------+--------------------------------+---------
SELECT               | SELECT                         | 1:1
IDENT                | id                             | 1:8
COMMA                | ,                              | 1:10
IDENT                | dname                          | 1:12
FROM                 | FROM                           | 1:18
IDENT                | dept                           | 1:23
WHERE                | WHERE                          | 1:28
IDENT                | id                             | 1:34
EQ                   | =                              | 1:37
INT_LIT              | 1                              | 1:39
SEMI                 | ;                              | 1:40

+----+-------+
| id | dname | 
+----+-------+
| 1  | CS    | 
+----+-------+
1 row (0.0000 s)
```

### 7. EXPLAIN and EXPLAIN ANALYZE

Inspect logical relational plan without running execution:
```bash
./sqlc "EXPLAIN SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id;"
```
```text
── Query Plan ──
Project [s.name, d.dname]
└─ Join INNER [NL (s.dept_id = d.id)]
   ├─ Scan students AS s  [200 rows]
   └─ Scan dept AS d  [5 rows]
```

Execute query and print per-operator runtime tuple statistics:
```bash
./sqlc "EXPLAIN ANALYZE SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20;"
```
```text
+-----------------+---------+
| s.name          | d.dname | 
+-----------------+---------+
| Hank Anderson   | Physics | 
| ... (152 rows omitted)    |
| Jack Brown      | CS      | 
+-----------------+---------+
154 rows (0.0006 s)

Execution statistics:
Project       rows_in=154      rows_out=154
  Join          rows_in=163      rows_out=154
    Filter        rows_in=200      rows_out=158
      SeqScan       rows_in=0        rows_out=200
    SeqScan       rows_in=0        rows_out=5
```

### 8. Error Reporting Across All Compiler Phases

#### Lexical Error (Invalid Character)
```bash
./sqlc --no-color "SELECT @#$ FROM dept;"
```
```text
Lexical Error at line 1, col 8: unexpected character '@'
Lexical Error at line 1, col 9: unexpected character '#'
Lexical Error at line 1, col 10: unexpected character '$'
Syntax Error at line 1, col 12: syntax error, unexpected FROM (near 'FROM')
```

#### Syntax Error (Grammar Violation)
```bash
./sqlc --no-color "SELECT FROM dept;"
```
```text
Syntax Error at line 1, col 8: syntax error, unexpected FROM (near 'FROM')
```

#### Semantic Error (Unknown Column with Typo Suggestion)
```bash
./sqlc --no-color "SELECT nam FROM students;"
```
```text
Semantic Error at line 1: unknown column 'nam'. Did you mean 'name'?
```

#### Runtime Error (Missing Data File)
```bash
./sqlc --no-color --data nonexistent_dir "SELECT * FROM dept;"
```
```text
Runtime Error: Cannot open table file 'nonexistent_dir/dept.csv'
Semantic Error at line 1: unknown table 'dept'
```

---

## 11. Optimizer Showcase

### Optimization Rules Reference

1. **Constant Folding:** Replaces static constant subexpressions with literal values at compile time.
   *Example:* `WHERE age > 18 + 2` $\to$ `WHERE age > 20`.
2. **Boolean Simplification:** Simplifies tautological or contradictory predicates using identity laws.
   *Example:* `WHERE x AND TRUE` $\to$ `WHERE x`.
3. **Filter Removal:** Prunes unconditional tautological filters entirely.
   *Example:* `WHERE 1 = 1` $\to$ `[Filter removed]`.
4. **Predicate Splitting:** Decomposes complex conjunctive predicates into individual filters for independent optimization.
   *Example:* `WHERE a = 1 AND b = 2` $\to$ `Filter [a = 1] -> Filter [b = 2]`.
5. **Predicate Pushdown:** Pushes filter operators below relational joins directly onto table scan nodes, dramatically reducing join input cardinality.
   *Example:* `(Students JOIN Dept) WHERE Students.age > 20` $\to$ `(Filter[age > 20](Students)) JOIN Dept`.
6. **Projection Pushdown:** Prunes unreferenced attributes early in the plan pipeline to minimize tuple widths.
7. **Join Algorithm Selection:** Analyzes join predicates and replaces $O(N \times M)$ Nested-Loop joins with $O(N + M)$ in-memory Hash Joins for equijoins.

### Benchmark: `--compare` Execution

```bash
./sqlc --compare "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20 AND 1 = 1;"
```

#### Unoptimized Execution Plan (Volcano Model)
```text
Project [s.name, d.dname]
└─ Filter [((s.age > 20) AND (1 = 1))]
   └─ Join INNER [NL (s.dept_id = d.id)]
      ├─ Scan students AS s  [200 rows]
      └─ Scan dept AS d  [5 rows]
```

#### Optimization Rules Fired in Order
```text
── Rules fired ──
  Rule applied: Constant Folding
  Rule applied: Boolean Simplification
  Rule applied: Predicate Pushdown
  Rule applied: Join Algorithm Selection (Hash Join)
```

#### Optimized Execution Plan
```text
Project [s.name, d.dname]
└─ Join INNER [HASH (s.dept_id = d.id)]
   ├─ Filter [(s.age > 20)]
      └─ Scan students AS s  [200 rows]
   └─ Scan dept AS d  [5 rows]
```

#### Operator Workload Comparison Table

| Operator Stage | Unoptimized `rows_in` | Unoptimized `rows_out` | Optimized `rows_in` | Optimized `rows_out` | Notes |
|---|:---:|:---:|:---:|:---:|---|
| **SeqScan (dept)** | 0 | 1,000 | 0 | 5 | Unoptimized rescanned dept for every student |
| **SeqScan (students)** | 0 | 200 | 0 | 200 | Scans base student table |
| **Filter** | 195 | 154 | 200 | 158 | Filter executed *before* join in optimized plan |
| **Join** | 200 | 195 | 163 | 154 | Hash Join replaces Nested Loop |
| **Project** | 154 | 154 | 154 | 154 | Identical final projection |
| **Total Rows Processed** | \multicolumn{2}{c|}{**2,252 rows**} | \multicolumn{2}{c|}{**1,188 rows**} | **47.2% Reduction in Workload** |

Both physical execution plans return the exact same **154 rows**, demonstrating algebraic correctness.

---

## 12. Web API Reference

The Flask backend exposes clean JSON REST endpoints:

| HTTP Method | Path | Request Body | Response Format | Status Codes |
|---|---|---|---|:---:|
| `GET` | `/` | None | HTML | `200` |
| `POST` | `/api/run` | `{"query": str, "mode": str, "optimize": bool}` | JSON compiler output | `200`, `400`, `500`, `504` |
| `GET` | `/api/tables` | None | `{"tables": [...], "count": N}` | `200`, `500` |
| `GET` | `/api/tables/<name>` | None | `{"name": str, "columns": [...], "rows": [...]}` | `200`, `404`, `500` |
| `POST` | `/api/tables/upload` | Multipart form: `name` (text), `file` (.csv) | `{"status": "ok", "table": {...}}` | `201`, `400`, `413`, `500` |
| `POST` | `/api/tables/create` | `{"name": str, "columns": [...], "rows": [...]}` | `{"status": "ok", "table": {...}}` | `201`, `400`, `500` |
| `DELETE` | `/api/tables/<name>` | None | `{"status": "ok", "message": str}` | `200`, `404`, `500` |
| `POST` | `/api/reset` | None | `{"status": "ok", "tables": [...]}` | `200`, `500` |
| `GET` | `/api/examples` | None | `{"examples": [...]}` | `200` |

### Sample `curl` Invocation

```bash
curl -X POST http://127.0.0.1:5000/api/run \
     -H "Content-Type: application/json" \
     -d '{"query": "SELECT id, dname FROM dept WHERE id <= 2;", "mode": "result", "optimize": true}'
```

```json
{
  "columns": [
    "dept.id",
    "dept.dname"
  ],
  "row_count": 2,
  "rows": [
    [
      1,
      "CS"
    ],
    [
      2,
      "Math"
    ]
  ],
  "status": "ok",
  "time_ms": 0.02
}
```

---

## 13. Testing

### 1. Compiler Core Test Suite (`make test`)

The native compiler test suite validates all compilation phases and optimizer rewrites:
```bash
make test
```

```text
══════════════════════════════════════════════════════════════════
       SQL Query Compiler & Optimizer — Test Suite Runner         
══════════════════════════════════════════════════════════════════

=== Phase 1: Valid Query Tests (Grammar, Planning & Execution) ===
  [PASS] 01_simple_select.sql
  [PASS] 02_where_comparison.sql
  [PASS] 03_arithmetic_constant_folding.sql
  [PASS] 04_logical_operators.sql
  [PASS] 05_like_matching.sql
  [PASS] 06_between_and.sql
  [PASS] 07_in_list.sql
  [PASS] 08_is_null.sql
  [PASS] 09_is_not_null.sql
  [PASS] 10_inner_join.sql
  [PASS] 11_left_join.sql
  [PASS] 12_three_table_join.sql
  [PASS] 13_distinct.sql
  [PASS] 14_order_by_asc_desc.sql
  [PASS] 15_limit.sql
  [PASS] 16_explain_analyze.sql

=== Phase 2: Error Handling Tests (Lex/Parse/Semantic Rejection) ===
  [PASS] 01_syntax_error.sql (Correctly rejected)
  [PASS] 02_unknown_table.sql (Correctly rejected)
  [PASS] 03_unknown_column.sql (Correctly rejected)
  [PASS] 04_bad_join_condition.sql (Correctly rejected)
  [PASS] 05_bad_alias.sql (Correctly rejected)

=== Phase 3: Query Optimizer Rewrite Rules Verification ===
  [PASS] optimizer_constant_folding (2 + 3 * 4 -> 14)
  [PASS] optimizer_predicate_pushdown (Pushed filter below join)
  [PASS] optimizer_join_algorithm (Equi-join converted to Hash Join)
  [PASS] optimizer_filter_removal (Tautological predicate eliminated)

══════════════════════════════════════════════════════════════════
ALL TESTS PASSED! (25/25)
══════════════════════════════════════════════════════════════════
```

### 2. Web API Integration Test Suite (`pytest`)

```bash
python3 -m pytest -p no:typeguard webapp/tests/test_api.py -v
```

```text
============================= test session starts ==============================
rootdir: /media/sf_KaliSharedStorage/PCD/Assignment/sql_compiler
collected 8 items

webapp/tests/test_api.py::test_index_page PASSED                         [ 12%]
webapp/tests/test_api.py::test_valid_query PASSED                        [ 25%]
webapp/tests/test_api.py::test_errors_categories PASSED                  [ 37%]
webapp/tests/test_api.py::test_upload_query_delete_csv PASSED            [ 50%]
webapp/tests/test_api.py::test_two_sessions_isolation PASSED             [ 62%]
webapp/tests/test_api.py::test_security_limits_and_rejections PASSED     [ 75%]
webapp/tests/test_api.py::test_compare_mode PASSED                       [ 87%]
webapp/tests/test_api.py::test_table_preview_and_reset PASSED            [100%]

============================== 8 passed in 24.11s ==============================
```

### Algebraic Invariant Verification

The test harness enforces the fundamental theorem of query optimization: **an optimized plan must produce the exact same result relation as the unoptimized plan**.
`test_compare_mode` verifies:
$$\text{Rows}_{\text{unoptimized}} = \text{Rows}_{\text{optimized}} \quad \land \quad \text{Workload}_{\text{optimized}} < \text{Workload}_{\text{unoptimized}}$$

---

## 14. Error Handling

Errors are trapped across all phases and returned in structured format:

| Category | Compiler Phase | Typical Cause | Diagnostic Message Format |
|---|---|---|---|
| **Lexical** | Phase 1 (`lexer.l`) | Unrecognized characters, unterminated strings | `Lexical Error at line L, col C: unexpected character 'X'` |
| **Syntax** | Phase 2 (`parser.y`) | Grammar violations, unexpected tokens | `Syntax Error at line L, col C: syntax error, unexpected X (near 'Y')` |
| **Semantic** | Phase 3 (`semantic.c`) | Unknown tables, missing columns, type mismatch | `Semantic Error at line L: unknown column 'X'. Did you mean 'Y'?` |
| **Runtime** | Phase 6 (`executor.c`) | Missing CSV files, division by zero, memory limits | `Runtime Error: Cannot open table file 'data/X.csv'` |
| **Validation** | Web API (`table_manager.py`) | Oversized query, invalid table names, corrupted CSV | `{"status": "error", "category": "Validation", "message": "..."}` |

---

## 15. Security Notes (Web Application)

1. **Subprocess Isolation (`shell=False`):** Query commands are executed strictly via `subprocess.run(["./sqlc", ...], shell=False)`. Shell command-string concatenation is prohibited, preventing shell injection vulnerabilities.
2. **Execution Timeout:** All compiler invocations are capped with `timeout=5`. Long-running commands are terminated and return HTTP 504.
3. **Query Length Limitation:** Queries are strictly limited to **5,000 characters**.
4. **Per-Session Workspaces:** Each browser session is assigned an isolated workspace directory (`webapp/workspaces/<uuid>/`). Users cannot access, modify, or delete another session's tables.
5. **Path Traversal Protection:** Table names are validated against `^[A-Za-z_][A-Za-z0-9_]*$`. Path resolution ensures all file operations remain within the session's workspace directory.
6. **Upload Constraints:** Flask enforces `MAX_CONTENT_LENGTH = 5 * 1024 * 1024` (5 MB). CSV uploads are restricted to `.csv`, validated for row column-count consistency, and capped at **20 columns** and **10,000 rows**.
7. **Crash Containment:** Non-zero compiler exits or segfaults are caught and serialized into JSON error bodies; Flask never outputs raw 500 stack traces to the client.

---

## 16. Demo Guide for Evaluation

Follow this 8-step script to demonstrate all compiler features in 10 minutes:

1. **Build and Test Verification (1 min):**
   ```bash
   make clean && make
   make test
   ```
   Show that all 25 compiler tests pass.
2. **Inspect Lexer Token Stream (1 min):**
   ```bash
   ./sqlc --tokens "SELECT id, name FROM dept WHERE id = 1;"
   ```
   Show token classification (`TOKEN_TYPE | LEXEME | LINE:COL`).
3. **Inspect AST & Operator Precedence (1 min):**
   ```bash
   ./sqlc --ast "SELECT 2 + 3 * 4 AS val FROM students LIMIT 1;"
   ```
   Point out that `3 * 4` is nested deeper than `2 +` in the AST.
4. **Demonstrate Typo Suggestion (1 min):**
   ```bash
   ./sqlc "SELECT nam FROM students;"
   ```
   Show the Levenshtein suggestion: `Did you mean 'name'?`.
5. **Demonstrate Query Optimization (2 mins):**
   ```bash
   ./sqlc --compare "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20 AND 1 = 1;"
   ```
   Show that Predicate Pushdown and Hash Join reduce workload by **47.2%** while producing identical output.
6. **Launch Web Interface (1 min):**
   ```bash
   ./run_web.sh
   ```
   Open `http://127.0.0.1:5000` in the browser.
7. **Run Query in Web UI & Export CSV (2 mins):**
   - Click **Run Query** (`Ctrl+Enter`).
   - Switch between **Result**, **AST**, **Logical Plan**, and **Optimized Plan** tabs.
   - Click **Export CSV** to download the query results.
8. **Upload Custom Table & Error Handling (1 min):**
   - Click **Add Table** $\to$ Upload or manually create a new table.
   - Run a query against the new table.
   - Type an invalid query (e.g. `SELECT FROM dept;`) to trigger the red error alert panel.

---

## 17. Limitations and Future Work

- **No Subqueries:** Nested subqueries (`WHERE id IN (SELECT ...)`) are not currently parsed.
- **No Aggregate Functions or GROUP BY:** `SUM()`, `AVG()`, `COUNT()`, and `GROUP BY` are not implemented.
- **Heuristic Rather Than Cost-Based:** Optimization uses rule-based heuristic rewriting rather than dynamic programming dynamic cost enumeration (System R style).
- **In-Memory Hash Table:** Hash joins require the inner table to fit in system RAM.
- **CSV Data Source:** Indexes (B-Trees) and write transactions (ACID / WAL) are not implemented.

### Future Work Directions

1. Implement aggregate operator (`AggregateHash`) for `GROUP BY` and aggregate functions.
2. Introduce a Cost-Based Optimizer (CBO) utilizing table histogram statistics.
3. Add support for correlated and uncorrelated subquery flattening.
4. Add index scan operator utilizing on-disk B+ Trees.

---

## 18. Troubleshooting

| Problem | Cause | Solution |
|---|---|---|
| `bison: command not found` | Bison is not installed | Run `sudo apt install bison` (or `brew install bison`). |
| `flex: command not found` | Flex is not installed | Run `sudo apt install flex` (or `brew install flex`). |
| `Address already in use: 5000` | Port 5000 is occupied | Kill existing process: `fuser -k 5000/tcp` or `pkill -f "webapp/app.py"`. |
| `Permission denied: ./run_web.sh` | Execution bit not set | Run `chmod +x run_web.sh` and try again. |
| `Clock skew detected` during `make` | Shared VM filesystem timestamp drift | Run `make clean && make`. |
| `ModuleNotFoundError: No module named 'flask'` | Flask not installed | Run `pip install -r webapp/requirements.txt`. |
| `CRLF line terminators error on WSL` | Windows Git checked out files with CRLF | Run `dos2unix run_web.sh tests/run_tests.sh`. |

---

## 19. License and Acknowledgements

This project is licensed under the **Academic Free License (AFL 3.0)** for educational use in university courses on Principles of Compiler Design.

### Acknowledgements

- **Flex & Bison**: The classic Lex & Yacc compiler construction tools.
- **Volcano Iterator Model**: Goetz Graefe (1994), *Volcano - An Extensible and Parallel Query Evaluation System*.
- **Relational Algebra Optimizations**: Database Management Systems (Ramakrishnan & Gehrke).
