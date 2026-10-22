/*
 * engine.h - machine-independent BURS driver.  Every function takes the
 * target as a parameter; none of them knows any instruction set.
 */
#ifndef ENGINE_H
#define ENGINE_H

#include <stdio.h>

#include "target.h"

/* Bottom-up labeling pass.  Returns 1 if the root can derive the goal. */
int burs_label(const Target *t, NODEPTR_TYPE root);

/* Cost of the best derivation of nonterminal nt at node p. */
int burs_cost(const Target *t, NODEPTR_TYPE p, int nt);

/* Backtrack from (p, nt) and print the selected rule cover, top-down. */
void burs_print_cover(const Target *t, NODEPTR_TYPE p, int nt, FILE *out);

/* Per-node table of every derivable nonterminal, its best rule and cost. */
void burs_print_cost_table(const Target *t, NODEPTR_TYPE root, FILE *out);

/* Backtrack from (p, nt) and emit instructions; returns the result operand. */
Operand burs_reduce(const Target *t, NODEPTR_TYPE p, int nt, EmitCtx *ctx);

/* Print the target's rule table. */
void burs_print_rules(const Target *t, FILE *out);

#endif /* ENGINE_H */
