-- Test 05: LIKE pattern matching with wildcards (% and _)
SELECT name FROM students WHERE name LIKE 'A%' OR name LIKE '%son' LIMIT 5;
