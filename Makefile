# BURS-based retargetable instruction selector
#
#   make            build build/burs (builds the burg generator first)
#   make demo       run the review sample expressions on both targets
#   make test       run the regression suite in tests/
#   make results    regenerate the evidence files in results/
#   make clean      remove build/

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra
BUILD   := build
GEN     := $(BUILD)/gen

BURG_DIR := third_party/burg
BURG_SRC := $(addprefix $(BURG_DIR)/, be.c burs.c closure.c delta.c fe.c \
	gram.tab.c item.c lex.c list.c main.c map.c nonterminal.c operator.c \
	pattern.c plank.c queue.c rule.c string.c symtab.c table.c trim.c zalloc.c)
BURG     := $(BUILD)/burg

INCLUDES := -Isrc/ir -Isrc/burs
CORE_SRC := src/ir/ir.c src/burs/engine.c src/main/main.c
CORE_HDR := src/ir/ir.h src/burs/target.h src/burs/engine.h
TARGETS_C := $(GEN)/targetA.c $(GEN)/targetB.c

BURS := $(BUILD)/burs

.PHONY: all demo test results clean

all: $(BURS)

# 1. The burg code-generator generator (third-party tool, built as-is).
$(BURG): $(BURG_SRC) | $(BUILD)
	$(CC) -O2 -w -o $@ $(BURG_SRC)

# 2. burg turns each target grammar into a C matcher with its own prefix,
#    so both targets link into one program.
$(GEN)/targetA.c: src/targets/targetA/targetA.gr $(BURG) | $(GEN)
	$(BURG) -p burmA -o $@ $<

$(GEN)/targetB.c: src/targets/targetB/targetB.gr $(BURG) | $(GEN)
	$(BURG) -p burmB -o $@ $<

# 3. Common IR + common engine + both generated targets.
$(BURS): $(CORE_SRC) $(TARGETS_C) $(CORE_HDR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(CORE_SRC) $(TARGETS_C)

$(BUILD) $(GEN):
	mkdir -p $@

demo: $(BURS)
	./$(BURS) --rules
	./$(BURS) --suite
	./$(BURS) "a + b * c"

test: $(BURS)
	sh tests/run_tests.sh

results: $(BURS)
	sh tests/make_results.sh

clean:
	rm -rf $(BUILD)
