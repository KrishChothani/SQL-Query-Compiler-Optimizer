-- Test 08: NULL checks with IS NULL
SELECT name, dept_id FROM students WHERE dept_id IS NULL LIMIT 5;
