-- Test 06: BETWEEN ... AND range queries
SELECT name, age FROM students WHERE age BETWEEN 19 AND 21 LIMIT 5;
