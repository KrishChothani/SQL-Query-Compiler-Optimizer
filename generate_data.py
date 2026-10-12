#!/usr/bin/env python3
"""
generate_data.py — Generate realistic CSV data for the SQL compiler demo.
Run:  python3 generate_data.py
Output files go to data/
"""
import random, os, math

random.seed(42)
os.makedirs("data", exist_ok=True)

# ── dept.csv ─────────────────────────────────────────────────────────
depts = [
    (1, "CS",      "Tech Building"),
    (2, "Math",    "Science Hall"),
    (3, "Physics", "Science Hall"),
    (4, "English", "Arts Building"),
    (5, "History", "Arts Building"),
]
with open("data/dept.csv", "w") as f:
    f.write("id,dname,building\n")
    for row in depts:
        f.write(",".join(str(x) for x in row) + "\n")
print(f"dept.csv: {len(depts)} rows")

# ── courses.csv ───────────────────────────────────────────────────────
courses = [
    (1,  "Intro to CS",         1, 3),
    (2,  "Data Structures",     1, 3),
    (3,  "Algorithms",          1, 4),
    (4,  "Databases",           1, 3),
    (5,  "Compilers",           1, 4),
    (6,  "Calculus I",          2, 3),
    (7,  "Linear Algebra",      2, 3),
    (8,  "Discrete Math",       2, 3),
    (9,  "Mechanics",           3, 4),
    (10, "Electromagnetism",    3, 4),
    (11, "Quantum Physics",     3, 3),
    (12, "Technical Writing",   4, 2),
    (13, "World Literature",    4, 3),
    (14, "American History",    5, 3),
    (15, "World History",       5, 3),
    (16, "Operating Systems",   1, 4),
    (17, "Computer Networks",   1, 3),
    (18, "Machine Learning",    1, 4),
    (19, "Statistics",          2, 3),
    (20, "Number Theory",       2, 3),
]
with open("data/courses.csv", "w") as f:
    f.write("id,title,dept_id,credits\n")
    for row in courses:
        f.write(",".join(str(x) for x in row) + "\n")
print(f"courses.csv: {len(courses)} rows")

# ── students.csv ─────────────────────────────────────────────────────
first_names = ["Alice","Bob","Carol","Dave","Eve","Frank","Grace","Hank",
               "Iris","Jack","Karen","Leo","Mia","Ned","Olivia","Pete",
               "Quinn","Rose","Sam","Tara","Uma","Vince","Wendy","Xander",
               "Yara","Zoe","Aaron","Beth","Carl","Diana"]
last_names  = ["Smith","Jones","Taylor","Brown","Wilson","Evans","Thomas",
               "Roberts","Johnson","White","Hall","Martin","Garcia","Lee",
               "Anderson","Jackson","Harris","Clark","Lewis","Young"]

students = []
used_names = set()
for i in range(1, 201):
    while True:
        fn = random.choice(first_names)
        ln = random.choice(last_names)
        full = f"{fn} {ln}"
        if full not in used_names:
            used_names.add(full)
            break
    age     = random.randint(18, 30)
    dept_id = random.randint(1, 5)
    gpa     = round(random.uniform(1.5, 4.0), 2)
    # Insert some NULLs
    if random.random() < 0.03:   gpa     = ""   # NULL gpa
    if random.random() < 0.02:   dept_id = ""   # NULL dept
    students.append((i, full, age, dept_id, gpa))

with open("data/students.csv", "w") as f:
    f.write("id,name,age,dept_id,gpa\n")
    for row in students:
        f.write(",".join(str(x) for x in row) + "\n")
print(f"students.csv: {len(students)} rows")

# ── enrollments.csv ───────────────────────────────────────────────────
grades = ["A","A-","B+","B","B-","C+","C","C-","D","F","W"]
weights = [10,8,8,12,8,7,6,4,3,2,2]

enrollments = []
used_pairs = set()
for _ in range(1000):
    sid = random.randint(1, 200)
    cid = random.randint(1, 20)
    if (sid, cid) in used_pairs:
        continue
    used_pairs.add((sid, cid))
    g = random.choices(grades, weights=weights)[0]
    if random.random() < 0.02: g = ""  # NULL grade
    enrollments.append((sid, cid, g))

with open("data/enrollments.csv", "w") as f:
    f.write("student_id,course_id,grade\n")
    for row in enrollments:
        f.write(",".join(str(x) for x in row) + "\n")
print(f"enrollments.csv: {len(enrollments)} rows")

print("\nAll data files generated in data/")
