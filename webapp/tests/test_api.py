"""
test_api.py — Pytest test suite for Flask Web Application and Compiler Bridge.

Tests cover:
- Valid query execution and JSON result parsing
- Error categories: Syntax, Semantic, Lexical
- Table lifecycle: CSV upload, querying the custom table, deletion
- Multi-session isolation: two sessions do not see each other's tables
- Security constraints: query length limit, invalid table names, upload validation
- Compare mode: unoptimized vs optimized row count equality and metrics
- Workspace reset and table preview
"""

import io
import sys
from pathlib import Path
import pytest

# Add webapp directory to sys.path
BASE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BASE_DIR))

from app import app


@pytest.fixture
def client():
    """Flask test client fixture."""
    app.config["TESTING"] = True
    with app.test_client() as test_client:
        yield test_client


def test_index_page(client):
    """Checks that the main single-page interface loads with HTTP 200."""
    resp = client.get("/")
    assert resp.status_code == 200
    assert b"SQL Query Compiler & Optimizer" in resp.data


def test_valid_query(client):
    """Executes a valid query and verifies JSON structure and output."""
    resp = client.post(
        "/api/run",
        json={
            "query": "SELECT id, dname FROM dept WHERE id <= 2;",
            "mode": "result",
            "optimize": True,
        },
    )
    assert resp.status_code == 200
    data = resp.get_json()
    assert data["status"] == "ok"
    assert "columns" in data
    assert "rows" in data
    assert data["row_count"] == 2
    assert len(data["rows"]) == 2
    assert data["time_ms"] >= 0


def test_errors_categories(client):
    """Verifies that syntax, semantic, and lexical errors return proper categories and HTTP 400."""
    # 1. Syntax Error
    resp_syntax = client.post(
        "/api/run",
        json={"query": "SELECT FROM dept;", "mode": "result"},
    )
    assert resp_syntax.status_code == 400
    data_syntax = resp_syntax.get_json()
    assert data_syntax["status"] == "error"
    assert data_syntax["category"] == "Syntax"
    assert data_syntax["line"] > 0

    # 2. Semantic Error (Unknown Column)
    resp_semantic = client.post(
        "/api/run",
        json={"query": "SELECT invalid_col FROM dept;", "mode": "result"},
    )
    assert resp_semantic.status_code == 400
    data_semantic = resp_semantic.get_json()
    assert data_semantic["status"] == "error"
    assert data_semantic["category"] == "Semantic"

    # 3. Lexical Error (Invalid Character)
    resp_lexical = client.post(
        "/api/run",
        json={"query": "SELECT @#$ FROM dept;", "mode": "result"},
    )
    assert resp_lexical.status_code == 400
    data_lexical = resp_lexical.get_json()
    assert data_lexical["status"] == "error"
    assert data_lexical["category"] == "Lexical"


def test_upload_query_delete_csv(client):
    """Uploads a CSV, queries the new table, and then deletes it."""
    csv_content = b"id,name,salary\n101,Dr. Alice,95000\n102,Dr. Bob,88000\n"
    data = {
        "name": "professors",
        "file": (io.BytesIO(csv_content), "professors.csv"),
    }

    # 1. Upload CSV
    resp_upload = client.post(
        "/api/tables/upload",
        data=data,
        content_type="multipart/form-data",
    )
    assert resp_upload.status_code == 201
    upload_data = resp_upload.get_json()
    assert upload_data["status"] == "ok"
    assert upload_data["table"]["name"] == "professors"
    assert upload_data["table"]["row_count"] == 2

    # 2. Query the uploaded table
    resp_query = client.post(
        "/api/run",
        json={"query": "SELECT name, salary FROM professors WHERE salary > 90000;"},
    )
    assert resp_query.status_code == 200
    query_data = resp_query.get_json()
    assert query_data["row_count"] == 1
    assert query_data["rows"][0][0] == "Dr. Alice"

    # 3. Delete the table
    resp_delete = client.delete("/api/tables/professors")
    assert resp_delete.status_code == 200

    # 4. Querying deleted table should fail with semantic error
    resp_query_del = client.post(
        "/api/run",
        json={"query": "SELECT * FROM professors;"},
    )
    assert resp_query_del.status_code == 400
    assert resp_query_del.get_json()["category"] == "Semantic"


def test_two_sessions_isolation():
    """Confirms that two browser sessions do not share custom tables."""
    # Session 1 Client
    with app.test_client() as client1:
        client1.get("/")  # initialize session 1
        resp_create = client1.post(
            "/api/tables/create",
            json={
                "name": "session1_private",
                "columns": [{"name": "id", "type": "INT"}, {"name": "secret", "type": "STRING"}],
                "rows": [[1, "s1_data"]],
            },
        )
        assert resp_create.status_code == 201

        # Session 1 sees its table
        resp_list1 = client1.get("/api/tables")
        t_names1 = [t["name"] for t in resp_list1.get_json()["tables"]]
        assert "session1_private" in t_names1

    # Session 2 Client
    with app.test_client() as client2:
        client2.get("/")  # initialize session 2

        # Session 2 must NOT see session 1's table
        resp_list2 = client2.get("/api/tables")
        t_names2 = [t["name"] for t in resp_list2.get_json()["tables"]]
        assert "session1_private" not in t_names2

        # Session 2 querying session 1's table fails
        resp_q2 = client2.post(
            "/api/run",
            json={"query": "SELECT * FROM session1_private;"},
        )
        assert resp_q2.status_code == 400
        assert resp_q2.get_json()["category"] == "Semantic"


def test_security_limits_and_rejections(client):
    """Tests security constraints: query length limit, invalid table names, upload validation."""
    # 1. Oversized Query (> 5,000 characters)
    huge_query = "SELECT " + ("1+" * 2600) + "1;"
    resp_huge = client.post(
        "/api/run",
        json={"query": huge_query},
    )
    assert resp_huge.status_code == 400
    assert resp_huge.get_json()["category"] == "Validation"

    # 2. Invalid Table Name with Path Traversal
    resp_invalid_name = client.post(
        "/api/tables/create",
        json={
            "name": "../malicious",
            "columns": [{"name": "col", "type": "STRING"}],
            "rows": [["val"]],
        },
    )
    assert resp_invalid_name.status_code == 400

    # 3. Non-CSV Upload
    resp_bad_ext = client.post(
        "/api/tables/upload",
        data={
            "name": "bad_file",
            "file": (io.BytesIO(b"hello world"), "bad_file.txt"),
        },
        content_type="multipart/form-data",
    )
    assert resp_bad_ext.status_code == 400

    # 4. Inconsistent CSV Column Count
    bad_csv = b"col1,col2\nval1\n"
    resp_bad_csv = client.post(
        "/api/tables/upload",
        data={
            "name": "uneven_table",
            "file": (io.BytesIO(bad_csv), "uneven_table.csv"),
        },
        content_type="multipart/form-data",
    )
    assert resp_bad_csv.status_code == 400


def test_compare_mode(client):
    """Verifies that compare mode returns equal row counts for unoptimized and optimized plans."""
    query = (
        "SELECT s.name, d.dname FROM students s "
        "JOIN dept d ON s.dept_id = d.id "
        "WHERE s.age > 20 AND 1 = 1;"
    )
    resp = client.post(
        "/api/run",
        json={"query": query, "mode": "compare"},
    )
    assert resp.status_code == 200
    data = resp.get_json()
    assert data["status"] == "ok"
    assert "unoptimized" in data
    assert "optimized" in data

    # Compare mode returns the same row count for optimized and unoptimized
    assert data["unoptimized"]["rows_out"] == data["optimized"]["rows_out"]
    assert data["unoptimized"]["rows_out"] > 0
    assert data["reduction_pct"] >= 0.0
    assert len(data["optimized"]["rules_fired"]) > 0


def test_table_preview_and_reset(client):
    """Tests table preview endpoint and workspace reset."""
    # 1. Preview default students table
    resp_prev = client.get("/api/tables/students")
    assert resp_prev.status_code == 200
    data_prev = resp_prev.get_json()
    assert data_prev["name"] == "students"
    assert len(data_prev["rows"]) <= 20
    assert data_prev["row_count"] > 0

    # 2. Delete dept table
    resp_del = client.delete("/api/tables/dept")
    assert resp_del.status_code == 200

    # Verify dept is gone
    resp_check = client.get("/api/tables/dept")
    assert resp_check.status_code == 404

    # 3. Reset workspace
    resp_reset = client.post("/api/reset")
    assert resp_reset.status_code == 200

    # Verify dept is restored
    resp_restored = client.get("/api/tables/dept")
    assert resp_restored.status_code == 200
