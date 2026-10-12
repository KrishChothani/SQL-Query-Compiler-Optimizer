"""
app.py — Flask web application for SQL Query Compiler and Optimizer.

Provides session-isolated workspaces, REST API endpoints, and Jinja2 frontend routes.
"""

from __future__ import annotations

import os
from pathlib import Path
import uuid
from flask import (
    Flask,
    jsonify,
    render_template,
    request,
    session,
)

from table_manager import (
    clean_old_workspaces,
    create_table_manual,
    delete_table,
    get_table_preview,
    get_workspace_dir,
    list_tables,
    reset_workspace,
    save_csv_upload,
)
from compiler_bridge import run_compiler

# Initialize Flask app
BASE_DIR = Path(__file__).resolve().parent
app = Flask(
    __name__,
    template_folder=str(BASE_DIR / "templates"),
    static_folder=str(BASE_DIR / "static"),
)

# App configuration
app.config["SECRET_KEY"] = os.environ.get("SECRET_KEY", "sqlc-compiler-secret-key-2026")
app.config["MAX_CONTENT_LENGTH"] = 5 * 1024 * 1024  # 5 MB max request size

# Clean old workspaces (> 24 hours) on startup
try:
    clean_old_workspaces(max_age_hours=24.0)
except Exception:
    pass


def get_current_session_id() -> str:
    """Ensures a session ID exists in the user's session cookie and returns it."""
    if "session_id" not in session:
        session["session_id"] = str(uuid.uuid4())
    session_id = session["session_id"]
    # Ensure workspace directory exists and sample data is seeded
    get_workspace_dir(session_id)
    return session_id


# ── Ready-made example queries ──────────────────────────────────────────
EXAMPLES = [
    {
        "title": "1. Student GPA Filter (Simple)",
        "mode": "result",
        "description": "Basic SELECT with comparison predicate on floating point GPA",
        "query": "SELECT id, name, age, gpa FROM students WHERE gpa >= 3.5;"
    },
    {
        "title": "2. Constant Folding & Arithmetic",
        "mode": "verbose",
        "description": "Evaluates constant expressions at compile-time (2 + 3 * 4 -> 14)",
        "query": "SELECT name, age, 2 + 3 * 4 AS calc FROM students WHERE age > 18 + 2;"
    },
    {
        "title": "3. Hash Join on Departments",
        "mode": "verbose",
        "description": "Equi-join automatically converted from Nested Loop to Hash Join",
        "query": "SELECT s.name, s.gpa, d.dname FROM students s JOIN dept d ON s.dept_id = d.id;"
    },
    {
        "title": "4. Left Outer Join",
        "mode": "result",
        "description": "Preserves all departments even with no enrolled courses",
        "query": "SELECT d.dname, c.cname FROM dept d LEFT JOIN courses c ON d.id = c.dept_id;"
    },
    {
        "title": "5. Three-Table Join & Predicate Pushdown",
        "mode": "compare",
        "description": "Pushes WHERE filter below joins and drops tautologies (1 = 1)",
        "query": "SELECT s.name, c.cname, e.grade FROM students s JOIN enrollments e ON s.id = e.student_id JOIN courses c ON e.course_id = c.id WHERE s.age > 20 AND 1 = 1;"
    },
    {
        "title": "6. Top 5 Students (ORDER BY + LIMIT)",
        "mode": "result",
        "description": "Sorts descending by GPA and limits execution output to 5 rows",
        "query": "SELECT name, gpa FROM students ORDER BY gpa DESC LIMIT 5;"
    },
    {
        "title": "7. Distinct Buildings",
        "mode": "result",
        "description": "Deduplicates result values using hash-based distinct elimination",
        "query": "SELECT DISTINCT building FROM dept;"
    },
    {
        "title": "8. Pattern Matching (LIKE)",
        "mode": "result",
        "description": "Prefix string matching on names starting with 'A%'",
        "query": "SELECT name, email FROM students WHERE name LIKE 'A%';"
    },
    {
        "title": "9. EXPLAIN Query Plan",
        "mode": "explain",
        "description": "Inspects relational algebra logical plan without running the query",
        "query": "EXPLAIN SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id;"
    },
    {
        "title": "10. EXPLAIN ANALYZE Execution Stats",
        "mode": "explain_analyze",
        "description": "Executes query and displays per-operator row input and output counts",
        "query": "EXPLAIN ANALYZE SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20;"
    }
]


# ── Frontend Routes ─────────────────────────────────────────────────────

@app.route("/")
def index():
    """Renders the main single-page application."""
    get_current_session_id()
    return render_template("index.html")


# ── Backend API Endpoints ───────────────────────────────────────────────

@app.route("/api/run", methods=["POST"])
def api_run():
    """Runs a query via the compiler bridge in the session's isolated workspace."""
    session_id = get_current_session_id()
    ws_dir = get_workspace_dir(session_id)

    data = request.get_json(silent=True) or {}
    query = data.get("query", "")
    mode = data.get("mode", "result")
    optimize = data.get("optimize", True)

    result, status_code = run_compiler(
        workspace_path=ws_dir,
        query=query,
        mode=mode,
        optimize=bool(optimize)
    )
    return jsonify(result), status_code


@app.route("/api/tables", methods=["GET"])
def api_list_tables():
    """Lists all tables in the session's workspace with schema and row counts."""
    session_id = get_current_session_id()
    try:
        tables = list_tables(session_id)
        return jsonify({"tables": tables, "count": len(tables)})
    except Exception as exc:
        return jsonify({
            "status": "error",
            "category": "Runtime",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 500


@app.route("/api/tables/<name>", methods=["GET"])
def api_get_table(name: str):
    """Returns the schema and first 20 rows of a specific table."""
    session_id = get_current_session_id()
    try:
        preview = get_table_preview(session_id, name)
        return jsonify(preview)
    except ValueError as exc:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 400
    except FileNotFoundError as exc:
        return jsonify({
            "status": "error",
            "category": "NotFound",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 404
    except Exception as exc:
        return jsonify({
            "status": "error",
            "category": "Runtime",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 500


@app.route("/api/tables/upload", methods=["POST"])
def api_upload_table():
    """Uploads a CSV file to create a table in the session workspace."""
    session_id = get_current_session_id()

    table_name = request.form.get("name", "").strip()
    if not table_name:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": "Table name is required",
            "line": 0,
            "col": 0
        }), 400

    if "file" not in request.files:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": "No file uploaded",
            "line": 0,
            "col": 0
        }), 400

    file = request.files["file"]
    if not file.filename:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": "No selected file",
            "line": 0,
            "col": 0
        }), 400

    try:
        result = save_csv_upload(session_id, table_name, file)
        return jsonify({"status": "ok", "table": result}), 201
    except ValueError as exc:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 400
    except Exception as exc:
        return jsonify({
            "status": "error",
            "category": "Runtime",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 500


@app.route("/api/tables/create", methods=["POST"])
def api_create_table():
    """Manually creates a table from column specifications and row data."""
    session_id = get_current_session_id()
    data = request.get_json(silent=True) or {}

    name = data.get("name", "").strip()
    columns = data.get("columns", [])
    rows = data.get("rows", [])

    if not name:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": "Table name is required",
            "line": 0,
            "col": 0
        }), 400

    try:
        result = create_table_manual(session_id, name, columns, rows)
        return jsonify({"status": "ok", "table": result}), 201
    except ValueError as exc:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 400
    except Exception as exc:
        return jsonify({
            "status": "error",
            "category": "Runtime",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 500


@app.route("/api/tables/<name>", methods=["DELETE"])
def api_delete_table(name: str):
    """Deletes a table from the session workspace."""
    session_id = get_current_session_id()
    try:
        delete_table(session_id, name)
        return jsonify({"status": "ok", "message": f"Table '{name}' deleted successfully"})
    except ValueError as exc:
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 400
    except FileNotFoundError as exc:
        return jsonify({
            "status": "error",
            "category": "NotFound",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 404
    except Exception as exc:
        return jsonify({
            "status": "error",
            "category": "Runtime",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 500


@app.route("/api/reset", methods=["POST"])
def api_reset():
    """Resets the session workspace to initial sample data."""
    session_id = get_current_session_id()
    try:
        reset_workspace(session_id)
        tables = list_tables(session_id)
        return jsonify({"status": "ok", "message": "Workspace reset to sample data", "tables": tables})
    except Exception as exc:
        return jsonify({
            "status": "error",
            "category": "Runtime",
            "message": str(exc),
            "line": 0,
            "col": 0
        }), 500


@app.route("/api/examples", methods=["GET"])
def api_examples():
    """Returns the list of ready-made example queries."""
    return jsonify({"examples": EXAMPLES})


# ── Error Handlers ──────────────────────────────────────────────────────

@app.errorhandler(413)
def request_entity_too_large(error):
    if request.path.startswith("/api/"):
        return jsonify({
            "status": "error",
            "category": "Validation",
            "message": "File size exceeds the 5 MB limit",
            "line": 0,
            "col": 0
        }), 413
    return "File too large (max 5 MB)", 413


@app.errorhandler(404)
def not_found_error(error):
    if request.path.startswith("/api/"):
        return jsonify({
            "status": "error",
            "category": "NotFound",
            "message": "Resource not found",
            "line": 0,
            "col": 0
        }), 404
    return "Page not found", 404


@app.errorhandler(500)
def internal_error(error):
    if request.path.startswith("/api/"):
        return jsonify({
            "status": "error",
            "category": "Runtime",
            "message": "Internal server error",
            "line": 0,
            "col": 0
        }), 500
    return "Internal server error", 500


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=True)
