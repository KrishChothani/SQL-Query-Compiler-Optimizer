-- Example 2: Joins (Inner, Left, and Multi-way)

-- 1. Inner Join between students and dept
SELECT s.name, s.gpa, d.dname, d.building
FROM students s
INNER JOIN dept d ON s.dept_id = d.id
WHERE s.gpa >= 3.5
LIMIT 10;

-- 2. Left Outer Join showing all students, including undeclared
SELECT s.name, d.dname
FROM students s
LEFT JOIN dept d ON s.dept_id = d.id
WHERE s.dept_id IS NULL;

-- 3. Three-way join: Students, Enrollments, and Courses
SELECT s.name, c.title, c.credits, e.grade
FROM students s
JOIN enrollments e ON s.id = e.student_id
JOIN courses c ON e.course_id = c.id
WHERE e.grade = 'A'
LIMIT 10;
