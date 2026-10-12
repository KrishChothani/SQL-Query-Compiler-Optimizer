-- Invalid Test 04: Unknown column in JOIN ON clause
SELECT s.name FROM students s JOIN dept d ON s.nonexistent_id = d.id;
