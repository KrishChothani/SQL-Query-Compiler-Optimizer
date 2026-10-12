-- Example 3: Optimizer Demonstrations
-- Run with: ./sqlc --compare -f examples/03_optimizer_demo.sql

-- Query 1: Constant folding & Boolean simplification
SELECT name, age FROM students WHERE 10 + 5 * 2 = 20 AND 1 = 1;

-- Query 2: Predicate pushdown through Join + Hash Join conversion
SELECT s.name, d.dname
FROM students s
JOIN dept d ON s.dept_id = d.id
WHERE s.age > 21;

-- Query 3: Multi-predicate pushdown with left join protection
SELECT s.name, d.dname
FROM students s
LEFT JOIN dept d ON s.dept_id = d.id
WHERE s.age < 20;
