-- Test 10: Two-table INNER JOIN with ON predicate
SELECT s.name, d.dname FROM students s INNER JOIN dept d ON s.dept_id = d.id LIMIT 5;
