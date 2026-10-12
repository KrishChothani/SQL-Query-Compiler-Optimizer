-- Test 07: Set membership with IN (...)
SELECT name, dept_id FROM students WHERE dept_id IN (1, 3, 5) LIMIT 5;
