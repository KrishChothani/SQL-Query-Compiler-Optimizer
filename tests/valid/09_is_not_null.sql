-- Test 09: NULL checks with IS NOT NULL
SELECT name, dept_id FROM students WHERE dept_id IS NOT NULL LIMIT 5;
