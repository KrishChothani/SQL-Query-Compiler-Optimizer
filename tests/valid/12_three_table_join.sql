-- Test 12: Three-table multi-way JOIN
SELECT s.name, c.title, e.grade
FROM students s
JOIN enrollments e ON s.id = e.student_id
JOIN courses c ON e.course_id = c.id
LIMIT 5;
