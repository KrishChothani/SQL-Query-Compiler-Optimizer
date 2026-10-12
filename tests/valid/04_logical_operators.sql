-- Test 04: Boolean logical expressions (AND, OR, NOT)
SELECT name, age, gpa FROM students WHERE (age > 22 AND gpa > 3.0) OR NOT (age >= 20) LIMIT 5;
