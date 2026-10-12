-- Test 11: LEFT OUTER JOIN (includes students with NULL dept_id)
SELECT s.name, d.dname FROM students s LEFT JOIN dept d ON s.dept_id = d.id WHERE s.dept_id IS NULL LIMIT 5;
