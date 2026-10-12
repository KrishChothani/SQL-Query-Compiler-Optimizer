# Makefile — SQL Query Compiler and Optimizer
# Targets: all  clean  test  demo
#
# Tools required: gcc, flex, bison
# Tested on: Linux/Debian with GCC 14, Flex 2.6.4, Bison 3.8.2

CC      = gcc
FLEX    = flex
BISON   = bison

CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic \
           -Wno-unused-function -Wno-unused-parameter \
           -D_POSIX_C_SOURCE=200809L \
           -g -O2 -Isrc

LDFLAGS = -lm

SRCDIR  = src
OBJDIR  = build
BINDIR  = .

TARGET  = sqlc

# ── Generated sources ────────────────────────────────────────────────
LEX_C   = $(OBJDIR)/lex.yy.c
PAR_C   = $(OBJDIR)/parser.tab.c
PAR_H   = $(OBJDIR)/parser.tab.h

# ── Hand-written sources ─────────────────────────────────────────────
HAND_C  = $(SRCDIR)/util.c    \
          $(SRCDIR)/value.c   \
          $(SRCDIR)/ast.c     \
          $(SRCDIR)/catalog.c \
          $(SRCDIR)/semantic.c\
          $(SRCDIR)/plan.c    \
          $(SRCDIR)/planner.c \
          $(SRCDIR)/optimizer.c\
          $(SRCDIR)/expr_eval.c\
          $(SRCDIR)/executor.c\
          $(SRCDIR)/main.c

# ── Object files ─────────────────────────────────────────────────────
HAND_O  = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(HAND_C))
GEN_O   = $(OBJDIR)/lex.yy.o $(OBJDIR)/parser.tab.o

ALL_O   = $(HAND_O) $(GEN_O)

# ════════════════════════════════════════════════════════════════════
.PHONY: all clean test demo conflicts

all: $(OBJDIR) $(TARGET)
	@echo ""
	@echo "Build successful!  Run:  ./$(TARGET) --help"

$(OBJDIR):
	mkdir -p $(OBJDIR)

# ── Link ─────────────────────────────────────────────────────────────
$(TARGET): $(ALL_O)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Bison ────────────────────────────────────────────────────────────
$(PAR_C) $(PAR_H): $(SRCDIR)/parser.y | $(OBJDIR)
	$(BISON) -d -Wall --report=all \
	         --report-file=$(OBJDIR)/bison.output \
	         -o $(PAR_C) $<
	@# Check for conflicts
	@if grep -q "conflicts:" $(OBJDIR)/bison.output 2>/dev/null; then \
	    echo "WARNING: Bison conflicts detected:"; \
	    grep "conflicts:" $(OBJDIR)/bison.output; \
	else \
	    echo "Bison: 0 conflicts (good)"; \
	fi

$(OBJDIR)/parser.tab.o: $(PAR_C) $(PAR_H)
	$(CC) $(CFLAGS) -I$(OBJDIR) -Wno-unused-variable -c -o $@ $<

# ── Flex ─────────────────────────────────────────────────────────────
$(LEX_C): $(SRCDIR)/lexer.l $(PAR_H) | $(OBJDIR)
	$(FLEX) -o $(LEX_C) $<

$(OBJDIR)/lex.yy.o: $(LEX_C) $(PAR_H)
	$(CC) $(CFLAGS) -I$(OBJDIR) -Wno-unused-function -Wno-sign-compare \
	      -Wno-pedantic -c -o $@ $<

# ── Hand-written objects ─────────────────────────────────────────────
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(PAR_H) | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(OBJDIR) -c -o $@ $<

# ── Bison conflict report ────────────────────────────────────────────
conflicts: $(PAR_C)
	@grep -E "conflict|State" $(OBJDIR)/bison.output | head -40

# ════════════════════════════════════════════════════════════════════
# Test target
# ════════════════════════════════════════════════════════════════════
test: all
	@echo ""
	@echo "Running test suite..."
	@bash tests/run_tests.sh

# ════════════════════════════════════════════════════════════════════
# Demo target
# ════════════════════════════════════════════════════════════════════
demo: all
	@echo ""
	@echo "════════════════════════════════════════════"
	@echo "  SQL Compiler Demo"
	@echo "════════════════════════════════════════════"
	@echo ""
	@echo "1. Simple SELECT with --verbose:"
	@./$(TARGET) --verbose "SELECT name, age FROM students WHERE age > 20 LIMIT 5;"
	@echo ""
	@echo "2. Constant folding: SELECT 2 + 3 * 4"
	@./$(TARGET) --ast "SELECT 2 + 3 * 4 AS x FROM students LIMIT 1;"
	@echo ""
	@echo "3. Two-table JOIN:"
	@./$(TARGET) "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id LIMIT 5;"
	@echo ""
	@echo "4. Optimizer --compare:"
	@./$(TARGET) --compare \
	  "SELECT s.name, d.dname FROM students s JOIN dept d ON s.dept_id = d.id WHERE s.age > 20 AND 1 = 1;"

# ════════════════════════════════════════════════════════════════════
clean:
	rm -rf $(OBJDIR) $(TARGET)
	@echo "Cleaned."
