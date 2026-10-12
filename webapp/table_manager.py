"""
table_manager.py — Per-session workspace and CSV table management.

Handles session workspace isolation, sample data seeding, table schema inference,
table previews, CSV uploads, manual table creation, and table deletion.
"""

from __future__ import annotations

import csv
import io
import os
import re
from pathlib import Path
import shutil
import time
from typing import Any

# Directory paths
BASE_DIR = Path(__file__).resolve().parent
WORKSPACE_BASE_DIR = BASE_DIR / "workspaces"
SAMPLE_DATA_DIR = BASE_DIR.parent / "data"

# Validation regex
TABLE_NAME_REGEX = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SESSION_ID_REGEX = re.compile(r"^[a-zA-Z0-9_\-]+$")

# Constraints
MAX_COLUMNS = 20
MAX_ROWS = 10_000
MAX_FILE_SIZE = 5 * 1024 * 1024  # 5 MB


def clean_old_workspaces(max_age_hours: float = 24.0) -> None:
    """Removes workspace directories that haven't been modified in over max_age_hours."""
    if not WORKSPACE_BASE_DIR.exists():
        return

    now = time.time()
    cutoff = now - (max_age_hours * 3600.0)

    for entry in WORKSPACE_BASE_DIR.iterdir():
        if entry.is_dir():
            try:
                mtime = entry.stat().st_mtime
                if mtime < cutoff:
                    shutil.rmtree(entry, ignore_errors=True)
            except OSError:
                pass


def validate_session_id(session_id: str) -> str:
    if not session_id or not SESSION_ID_REGEX.match(session_id):
        raise ValueError(f"Invalid session ID: {session_id}")
    return session_id


def validate_table_name(table_name: str) -> str:
    if not table_name or not TABLE_NAME_REGEX.match(table_name):
        raise ValueError(
            f"Invalid table name '{table_name}'. Names must match ^[A-Za-z_][A-Za-z0-9_]*$"
        )
    return table_name


def get_workspace_dir(session_id: str) -> Path:
    """Returns the workspace directory for the session, seeding sample tables if new."""
    valid_id = validate_session_id(session_id)
    ws_dir = (WORKSPACE_BASE_DIR / valid_id).resolve()

    # Safety check against path traversal
    if not str(ws_dir).startswith(str(WORKSPACE_BASE_DIR.resolve())):
        raise ValueError("Invalid workspace path")

    if not ws_dir.exists():
        ws_dir.mkdir(parents=True, exist_ok=True)
        seed_sample_data(ws_dir)

    return ws_dir


def seed_sample_data(target_dir: Path) -> None:
    """Copies all sample CSV files from data/ into target_dir."""
    if not SAMPLE_DATA_DIR.exists():
        return

    for src_file in SAMPLE_DATA_DIR.glob("*.csv"):
        dest_file = target_dir / src_file.name
        shutil.copy2(src_file, dest_file)


def reset_workspace(session_id: str) -> None:
    """Clears and re-seeds the workspace for session_id."""
    ws_dir = get_workspace_dir(session_id)
    for f in ws_dir.glob("*.csv"):
        try:
            f.unlink()
        except OSError:
            pass
    seed_sample_data(ws_dir)


def infer_val_type(val_str: str) -> str:
    s = val_str.strip()
    if not s or s.lower() == "null":
        return "NULL"
    if s.lower() in ("true", "false"):
        return "BOOL"
    try:
        int(s)
        return "INT"
    except ValueError:
        pass
    try:
        float(s)
        return "FLOAT"
    except ValueError:
        pass
    return "STRING"


def infer_column_types(sample_rows: list[list[str]], ncols: int) -> list[str]:
    types = ["NULL"] * ncols
    for row in sample_rows:
        for c in range(min(ncols, len(row))):
            t = infer_val_type(row[c])
            if t == "NULL":
                continue
            cur = types[c]
            if cur == "NULL":
                types[c] = t
            elif cur == "INT" and t == "FLOAT":
                types[c] = "FLOAT"
            elif cur != t and not (cur == "FLOAT" and t == "INT"):
                types[c] = "STRING"

    # Default remaining NULL columns to STRING
    return [t if t != "NULL" else "STRING" for t in types]


def list_tables(session_id: str) -> list[dict[str, Any]]:
    """Returns a list of tables in the workspace with inferred schema and row count."""
    ws_dir = get_workspace_dir(session_id)
    tables: list[dict[str, Any]] = []

    for csv_file in sorted(ws_dir.glob("*.csv")):
        tname = csv_file.stem
        try:
            with open(csv_file, "r", encoding="utf-8", errors="replace") as f:
                reader = csv.reader(f)
                header = next(reader, None)
                if not header:
                    continue
                header = [c.strip() for c in header]
                sample_rows = []
                total_rows = 0
                for row in reader:
                    total_rows += 1
                    if len(sample_rows) < 50:
                        sample_rows.append(row)

                col_types = infer_column_types(sample_rows, len(header))
                columns = [
                    {"name": header[i], "type": col_types[i]}
                    for i in range(len(header))
                ]
                tables.append({
                    "name": tname,
                    "columns": columns,
                    "row_count": total_rows
                })
        except Exception:
            continue

    return tables


def get_table_preview(session_id: str, table_name: str) -> dict[str, Any]:
    """Returns schema and first 20 rows of the table."""
    validate_table_name(table_name)
    ws_dir = get_workspace_dir(session_id)
    csv_file = (ws_dir / f"{table_name}.csv").resolve()

    if not str(csv_file).startswith(str(ws_dir)):
        raise ValueError("Path traversal attempt detected")
    if not csv_file.exists():
        raise FileNotFoundError(f"Table '{table_name}' does not exist")

    with open(csv_file, "r", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if not header:
            raise ValueError(f"Table '{table_name}' has no header")
        header = [c.strip() for c in header]

        first_20: list[list[str]] = []
        sample_rows: list[list[str]] = []
        total_rows = 0
        for row in reader:
            total_rows += 1
            if len(first_20) < 20:
                first_20.append(row)
            if len(sample_rows) < 50:
                sample_rows.append(row)

        col_types = infer_column_types(sample_rows, len(header))
        columns = [
            {"name": header[i], "type": col_types[i]}
            for i in range(len(header))
        ]

        return {
            "name": table_name,
            "columns": columns,
            "rows": first_20,
            "row_count": total_rows,
            "preview_count": len(first_20)
        }


def save_csv_upload(session_id: str, table_name: str, file_storage) -> dict[str, Any]:
    """Validates and saves an uploaded CSV file."""
    validate_table_name(table_name)
    ws_dir = get_workspace_dir(session_id)
    dest_path = (ws_dir / f"{table_name}.csv").resolve()

    if not str(dest_path).startswith(str(ws_dir)):
        raise ValueError("Path traversal attempt detected")

    filename = getattr(file_storage, "filename", "")
    if not filename.lower().endswith(".csv"):
        raise ValueError("Only .csv files are supported")

    content_bytes = file_storage.read()
    if len(content_bytes) > MAX_FILE_SIZE:
        raise ValueError(f"File size exceeds maximum allowed of 5 MB ({len(content_bytes)} bytes)")

    text = content_bytes.decode("utf-8", errors="replace")
    stream = io.StringIO(text)
    reader = csv.reader(stream)

    header = next(reader, None)
    if not header:
        raise ValueError("CSV file must contain a header row")

    header = [c.strip() for c in header]
    if len(header) == 0:
        raise ValueError("CSV header cannot be empty")
    if len(header) > MAX_COLUMNS:
        raise ValueError(f"Too many columns ({len(header)}). Max allowed is {MAX_COLUMNS}")

    # Validate column names
    seen_cols = set()
    for col in header:
        if not TABLE_NAME_REGEX.match(col):
            raise ValueError(f"Invalid column name '{col}'. Must match ^[A-Za-z_][A-Za-z0-9_]*$")
        if col in seen_cols:
            raise ValueError(f"Duplicate column name '{col}' in header")
        seen_cols.add(col)

    rows: list[list[str]] = []
    row_count = 0
    for row_idx, row in enumerate(reader, start=2):
        if not row:
            continue
        if len(row) != len(header):
            raise ValueError(
                f"Row {row_idx} has {len(row)} columns, expected {len(header)}"
            )
        row_count += 1
        if row_count > MAX_ROWS:
            raise ValueError(f"CSV file exceeds maximum allowed of {MAX_ROWS} rows")
        rows.append(row)

    # Write validated CSV
    with open(dest_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(rows)

    col_types = infer_column_types(rows[:50], len(header))
    columns = [
        {"name": header[i], "type": col_types[i]}
        for i in range(len(header))
    ]

    return {
        "name": table_name,
        "row_count": row_count,
        "columns": columns
    }


def create_table_manual(
    session_id: str, table_name: str, columns: list[dict[str, str]], rows: list[list[Any]]
) -> dict[str, Any]:
    """Creates a table from column definitions and user-provided row data."""
    validate_table_name(table_name)
    ws_dir = get_workspace_dir(session_id)
    dest_path = (ws_dir / f"{table_name}.csv").resolve()

    if not str(dest_path).startswith(str(ws_dir)):
        raise ValueError("Path traversal attempt detected")

    if not columns:
        raise ValueError("At least one column is required")
    if len(columns) > MAX_COLUMNS:
        raise ValueError(f"Too many columns ({len(columns)}). Max allowed is {MAX_COLUMNS}")

    col_names: list[str] = []
    seen = set()
    for c in columns:
        cname = c.get("name", "").strip()
        if not TABLE_NAME_REGEX.match(cname):
            raise ValueError(f"Invalid column name '{cname}'")
        if cname in seen:
            raise ValueError(f"Duplicate column name '{cname}'")
        seen.add(cname)
        col_names.append(cname)

    if len(rows) > MAX_ROWS:
        raise ValueError(f"Too many rows ({len(rows)}). Max allowed is {MAX_ROWS}")

    formatted_rows: list[list[str]] = []
    for r_idx, r in enumerate(rows, start=1):
        if len(r) != len(col_names):
            raise ValueError(f"Row {r_idx} has {len(r)} values, expected {len(col_names)}")
        formatted_rows.append([str(v) if v is not None else "" for v in r])

    with open(dest_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(col_names)
        writer.writerows(formatted_rows)

    return {
        "name": table_name,
        "row_count": len(rows),
        "columns": [{"name": c["name"], "type": c.get("type", "STRING")} for c in columns]
    }


def delete_table(session_id: str, table_name: str) -> bool:
    """Deletes a table from the workspace."""
    validate_table_name(table_name)
    ws_dir = get_workspace_dir(session_id)
    csv_file = (ws_dir / f"{table_name}.csv").resolve()

    if not str(csv_file).startswith(str(ws_dir)):
        raise ValueError("Path traversal attempt detected")
    if not csv_file.exists():
        raise FileNotFoundError(f"Table '{table_name}' does not exist")

    csv_file.unlink()
    return True
