-- Example 1: Basic SELECT Queries and Filtering

-- 1. All departments
SELECT * FROM dept;

-- 2. Projected columns with simple predicate
SELECT name, age, gpa FROM students WHERE gpa >= 3.8;

-- 3. Range check with BETWEEN
SELECT name, age FROM students WHERE age BETWEEN 18 AND 20 LIMIT 10;

-- 4. Pattern matching with LIKE
SELECT name, gpa FROM students WHERE name LIKE 'A%' LIMIT 5;

-- 5. Null checks
SELECT name, dept_id FROM students WHERE dept_id IS NULL;
