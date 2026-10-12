-- Test 16: EXPLAIN ANALYZE execution pipeline profiling
EXPLAIN ANALYZE SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 22 LIMIT 5;
