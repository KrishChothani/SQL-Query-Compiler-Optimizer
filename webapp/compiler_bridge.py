"""
compiler_bridge.py — Executes ./sqlc safely via subprocess and parses output.

Ensures strict security:
- shell=False always
- 5-second execution timeout
- Query length <= 5,000 characters
- Queries passed via temporary file (-f)
- Clean JSON parsing and graceful error handling on compiler crash
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any

# Path to sqlc binary
BASE_DIR = Path(__file__).resolve().parent
SQLC_BIN = (BASE_DIR.parent / "sqlc").resolve()

MAX_QUERY_LENGTH = 5_000
TIMEOUT_SECONDS = 5


def run_compiler(
    workspace_path: Path,
    query: str,
    mode: str = "result",
    optimize: bool = True
) -> tuple[dict[str, Any], int]:
    """Runs ./sqlc with the given parameters and returns (json_data, http_status_code)."""
    # 1. Query validation
    if not isinstance(query, str) or not query.strip():
        return {
            "status": "error",
            "category": "Validation",
            "message": "Query cannot be empty",
            "line": 0,
            "col": 0
        }, 400

    if len(query) > MAX_QUERY_LENGTH:
        return {
            "status": "error",
            "category": "Validation",
            "message": f"Query exceeds maximum length of {MAX_QUERY_LENGTH} characters (length was {len(query)})",
            "line": 0,
            "col": 0
        }, 400

    # Ensure compiler binary exists
    if not SQLC_BIN.exists() or not os.access(SQLC_BIN, os.X_OK):
        return {
            "status": "error",
            "category": "Runtime",
            "message": f"Compiler binary not found or not executable at {SQLC_BIN}. Run 'make' first.",
            "line": 0,
            "col": 0
        }, 500

    # 2. Build command arguments safely
    cmd: list[str] = [
        str(SQLC_BIN),
        "--data-dir",
        str(workspace_path),
        "--json"
    ]

    if not optimize:
        cmd.append("--no-optimize")

    clean_query = query.strip()
    clean_mode = (mode or "result").strip().lower()

    if clean_mode == "compare":
        cmd.append("--compare")
    elif clean_mode == "verbose":
        cmd.append("--verbose")
    elif clean_mode == "tokens":
        cmd.append("--tokens")
    elif clean_mode == "ast":
        cmd.append("--ast")
    elif clean_mode == "plan":
        cmd.append("--plan")
    elif clean_mode == "explain":
        if not clean_query.upper().startswith("EXPLAIN"):
            clean_query = f"EXPLAIN {clean_query}"
    elif clean_mode == "explain_analyze":
        upper = clean_query.upper()
        if not upper.startswith("EXPLAIN ANALYZE"):
            if upper.startswith("EXPLAIN"):
                clean_query = "EXPLAIN ANALYZE " + clean_query[7:].strip()
            else:
                clean_query = f"EXPLAIN ANALYZE {clean_query}"

    # 3. Write query to a secure temporary file
    tmp_path: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            suffix=".sql",
            delete=False,
            encoding="utf-8"
        ) as tmp_file:
            tmp_file.write(clean_query)
            tmp_path = tmp_file.name

        cmd.extend(["-f", tmp_path])

        # 4. Execute safely without shell=True
        proc = subprocess.run(
            cmd,
            timeout=TIMEOUT_SECONDS,
            capture_output=True,
            text=True,
            shell=False
        )

        stdout = proc.stdout.strip()
        stderr = proc.stderr.strip()

        # Parse JSON output if present
        if stdout:
            # Search for JSON object in stdout in case any warning preceded it
            json_start = stdout.find("{")
            json_end = stdout.rfind("}")
            if json_start != -1 and json_end != -1 and json_end >= json_start:
                json_str = stdout[json_start : json_end + 1]
                try:
                    data = json.loads(json_str)
                    http_code = 200 if data.get("status") == "ok" else 400
                    return data, http_code
                except json.JSONDecodeError:
                    pass

        # If execution failed and no valid JSON was output
        if proc.returncode != 0:
            msg = stderr or stdout or f"Process exited with code {proc.returncode}"
            return {
                "status": "error",
                "category": "Runtime",
                "message": msg,
                "line": 0,
                "col": 0
            }, 400

        # Successful exit but unexpected output format
        return {
            "status": "error",
            "category": "Runtime",
            "message": f"Unexpected compiler output: {stdout[:200]}",
            "line": 0,
            "col": 0
        }, 500

    except subprocess.TimeoutExpired:
        return {
            "status": "error",
            "category": "Runtime",
            "message": f"Query execution timed out after {TIMEOUT_SECONDS} seconds",
            "line": 0,
            "col": 0
        }, 504

    except Exception as exc:
        return {
            "status": "error",
            "category": "Runtime",
            "message": f"Compiler execution error: {str(exc)}",
            "line": 0,
            "col": 0
        }, 500

    finally:
        if tmp_path and os.path.exists(tmp_path):
            try:
                os.remove(tmp_path)
            except OSError:
                pass
