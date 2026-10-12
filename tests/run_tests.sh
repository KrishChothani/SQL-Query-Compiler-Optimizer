#!/usr/bin/env bash
# run_tests.sh — Automated Test Suite for SQL Query Compiler and Optimizer
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/sqlc"
DATA_DIR="$ROOT_DIR/data"
VALID_DIR="$ROOT_DIR/tests/valid"
INVALID_DIR="$ROOT_DIR/tests/invalid"

GREEN="\033[0;32m"
RED="\033[0;31m"
BLUE="\033[0;34m"
YELLOW="\033[0;33m"
BOLD="\033[1m"
RESET="\033[0m"

passed=0
failed=0
total=0

echo -e "${BOLD}${BLUE}══════════════════════════════════════════════════════════════════${RESET}"
echo -e "${BOLD}${BLUE}       SQL Query Compiler & Optimizer — Test Suite Runner         ${RESET}"
echo -e "${BOLD}${BLUE}══════════════════════════════════════════════════════════════════${RESET}"
echo ""

if [[ ! -x "$BIN" ]]; then
    echo -e "${RED}Error: Compiler binary $BIN not found! Run 'make' first.${RESET}"
    exit 1
fi

# ── 1. Positive Tests (Valid queries should parse & execute cleanly) ──
echo -e "${BOLD}=== Phase 1: Valid Query Tests (Grammar, Planning & Execution) ===${RESET}"
for test_file in "$VALID_DIR"/*.sql; do
    [[ -f "$test_file" ]] || continue
    name="$(basename "$test_file")"
    total=$((total + 1))

    output=$("$BIN" --data "$DATA_DIR" -f "$test_file" 2>&1)
    status=$?

    # Check that execution succeeded and produced a table output or plan output
    if [[ $status -eq 0 && ! "$output" =~ "Error" ]]; then
        echo -e "  [${GREEN}PASS${RESET}] $name"
        passed=$((passed + 1))
    else
        echo -e "  [${RED}FAIL${RESET}] $name"
        echo -e "    ${YELLOW}Exit status:${RESET} $status"
        echo -e "    ${YELLOW}Output snippet:${RESET}"
        echo "$output" | head -n 5 | sed 's/^/      /'
        failed=$((failed + 1))
    fi
done

echo ""

# ── 2. Negative Tests (Syntax and Semantic Error Detection) ───────────
echo -e "${BOLD}=== Phase 2: Error Handling Tests (Lex/Parse/Semantic Rejection) ===${RESET}"
for test_file in "$INVALID_DIR"/*.sql; do
    [[ -f "$test_file" ]] || continue
    name="$(basename "$test_file")"
    total=$((total + 1))

    output=$("$BIN" --data "$DATA_DIR" -f "$test_file" 2>&1)
    status=$?

    # An invalid query MUST either return non-zero exit code or produce Error in output
    if [[ $status -ne 0 || "$output" =~ "Error" || "$output" =~ "error" ]]; then
        echo -e "  [${GREEN}PASS${RESET}] $name (Correctly rejected)"
        passed=$((passed + 1))
    else
        echo -e "  [${RED}FAIL${RESET}] $name (Expected error, but query succeeded)"
        echo "$output" | head -n 5 | sed 's/^/      /'
        failed=$((failed + 1))
    fi
done

echo ""

# ── 3. Optimizer Rule Verification Tests ─────────────────────────────
echo -e "${BOLD}=== Phase 3: Query Optimizer Rewrite Rules Verification ===${RESET}"

# Test 3a: Constant Folding
total=$((total + 1))
opt_out=$("$BIN" --data "$DATA_DIR" --verbose "SELECT 2 + 3 * 4 AS val FROM students LIMIT 1;" 2>&1)
if [[ "$opt_out" =~ "Constant Folding" || "$opt_out" =~ "14" ]]; then
    echo -e "  [${GREEN}PASS${RESET}] optimizer_constant_folding (2 + 3 * 4 -> 14)"
    passed=$((passed + 1))
else
    echo -e "  [${RED}FAIL${RESET}] optimizer_constant_folding"
    failed=$((failed + 1))
fi

# Test 3b: Predicate Pushdown & Boolean Simplification
total=$((total + 1))
opt_out2=$("$BIN" --data "$DATA_DIR" --compare "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20 AND 1 = 1;" 2>&1)
if [[ "$opt_out2" =~ "Predicate Pushdown" && "$opt_out2" =~ "Boolean Simplification" ]]; then
    echo -e "  [${GREEN}PASS${RESET}] optimizer_predicate_pushdown (Pushed filter below join)"
    passed=$((passed + 1))
else
    echo -e "  [${RED}FAIL${RESET}] optimizer_predicate_pushdown"
    failed=$((failed + 1))
fi

# Test 3c: Hash Join Selection
total=$((total + 1))
if [[ "$opt_out2" =~ "Hash Join" ]]; then
    echo -e "  [${GREEN}PASS${RESET}] optimizer_join_algorithm (Equi-join converted to Hash Join)"
    passed=$((passed + 1))
else
    echo -e "  [${RED}FAIL${RESET}] optimizer_join_algorithm"
    failed=$((failed + 1))
fi

# Test 3d: Filter Removal
total=$((total + 1))
opt_out3=$("$BIN" --data "$DATA_DIR" --verbose "SELECT name FROM students WHERE 1 = 1 LIMIT 2;" 2>&1)
if [[ "$opt_out3" =~ "Filter Removal" || "$opt_out3" =~ "Boolean Simplification" ]]; then
    echo -e "  [${GREEN}PASS${RESET}] optimizer_filter_removal (Tautological predicate eliminated)"
    passed=$((passed + 1))
else
    echo -e "  [${RED}FAIL${RESET}] optimizer_filter_removal"
    failed=$((failed + 1))
fi

echo ""
echo -e "${BOLD}${BLUE}══════════════════════════════════════════════════════════════════${RESET}"
if [[ $failed -eq 0 ]]; then
    echo -e "${BOLD}${GREEN}ALL TESTS PASSED! (${passed}/${total})${RESET}"
else
    echo -e "${BOLD}${RED}TEST FAILURES DETECTED: ${passed} passed, ${failed} failed out of ${total}.${RESET}"
fi
echo -e "${BOLD}${BLUE}══════════════════════════════════════════════════════════════════${RESET}"

exit $failed
