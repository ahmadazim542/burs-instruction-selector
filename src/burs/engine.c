/*
 * engine.c - machine-independent BURS driver.
 *
 * Labeling is done by the burg-generated <prefix>_label(): a postorder
 * walk that looks up each node's state in precomputed tables, so every
 * node ends up knowing the cheapest rule for every nonterminal.  This
 * file does everything after that: cost queries, backtracking from the
 * root to recover the cover, and driving the target's emitter.
 */
#include <stdarg.h>
#include <string.h>

#include "engine.h"

int burs_label(const Target *t, NODEPTR_TYPE root)
{
	int state = t->label(root);
	return state != 0 && t->rule(state, t->start_nt) != 0;
}

int burs_cost(const Target *t, NODEPTR_TYPE p, int nt)
{
	int r = t->rule(STATE_LABEL(p), nt);
	short *nts;
	NODEPTR_TYPE kids[BURS_MAX_KIDS];
	int cost, i;

	if (r == 0)
		return -1;
	cost = t->rules[r].cost;
	nts = t->nts[r];
	t->kids(p, r, kids);
	for (i = 0; nts[i]; i++)
		cost += burs_cost(t, kids[i], nts[i]);
	return cost;
}

static void print_cover(const Target *t, NODEPTR_TYPE p, int nt, int depth, FILE *out)
{
	int r = t->rule(STATE_LABEL(p), nt);
	short *nts;
	NODEPTR_TYPE kids[BURS_MAX_KIDS];
	char where[48];
	int i, col;

	if (r == 0) {
		fprintf(out, "    <no cover for %s>\n", t->nt_names[nt]);
		return;
	}
	col = fprintf(out, "    ");
	for (i = 0; i < depth; i++)
		col += fprintf(out, ". ");
	col += fprintf(out, "%s", t->rules[r].pattern);
	snprintf(where, sizeof where, "n%d", p->id);
	fprintf(out, "%*s%-5s cost %d   %s\n", col < 44 ? 44 - col : 1, "",
		where, t->rules[r].cost, t->rules[r].asm_form);

	nts = t->nts[r];
	t->kids(p, r, kids);
	for (i = 0; nts[i]; i++)
		print_cover(t, kids[i], nts[i], depth + 1, out);
}

void burs_print_cover(const Target *t, NODEPTR_TYPE p, int nt, FILE *out)
{
	print_cover(t, p, nt, 0, out);
}

static void cost_table(const Target *t, NODEPTR_TYPE p, FILE *out)
{
	char label[48];
	int nt, first = 1;

	if (p == NULL)
		return;
	cost_table(t, p->left, out);
	cost_table(t, p->right, out);

	switch (p->op) {
	case CONST: snprintf(label, sizeof label, "n%d CONST %lld", p->id, p->ival); break;
	case VAR:   snprintf(label, sizeof label, "n%d VAR %s", p->id, p->name); break;
	default:    snprintf(label, sizeof label, "n%d %s", p->id, ir_opname(p->op)); break;
	}
	for (nt = 1; nt <= t->num_nts; nt++) {
		int r = t->rule(STATE_LABEL(p), nt);
		if (r == 0)
			continue;
		fprintf(out, "    %-18s %-5s %-22s %4d\n", first ? label : "",
			t->nt_names[nt], t->rules[r].pattern, burs_cost(t, p, nt));
		first = 0;
	}
	if (first)
		fprintf(out, "    %-18s (no nonterminal derivable)\n", label);
}

void burs_print_cost_table(const Target *t, NODEPTR_TYPE root, FILE *out)
{
	fprintf(out, "    %-18s %-5s %-22s %4s\n", "node", "NT", "best rule", "cost");
	fprintf(out, "    %-18s %-5s %-22s %4s\n", "----", "--", "---------", "----");
	cost_table(t, root, out);
}

Operand burs_reduce(const Target *t, NODEPTR_TYPE p, int nt, EmitCtx *ctx)
{
	int r = t->rule(STATE_LABEL(p), nt);
	short *nts;
	NODEPTR_TYPE kids[BURS_MAX_KIDS];
	Operand ops[BURS_MAX_KIDS];
	int i;

	if (r == 0)
		return burs_operand("?");
	nts = t->nts[r];
	t->kids(p, r, kids);
	/* children first, left to right: instructions come out in postorder */
	for (i = 0; nts[i]; i++)
		ops[i] = burs_reduce(t, kids[i], nts[i], ctx);
	return t->reduce(ctx, p, r, ops);
}

void burs_print_rules(const Target *t, FILE *out)
{
	int r;

	fprintf(out, "Target %s: %s\n", t->id, t->name);
	fprintf(out, "    %-3s %-24s %-4s %s\n", "#", "rule (pattern)", "cost", "instruction template");
	fprintf(out, "    %-3s %-24s %-4s %s\n", "--", "--------------", "----", "--------------------");
	for (r = 1; r <= t->num_rules; r++)
		if (t->rules[r].pattern != NULL)
			fprintf(out, "    %-3d %-24s %4d %s\n", r, t->rules[r].pattern,
				t->rules[r].cost, t->rules[r].asm_form);
}

/* ------------------------------------------------------------------ */
/* Helpers for target emitters                                         */
/* ------------------------------------------------------------------ */

void burs_emit(EmitCtx *ctx, const char *fmt, ...)
{
	va_list ap;

	if (ctx->ncode == BURS_MAX_INSTR) {
		ctx->overflow = 1;
		return;
	}
	va_start(ap, fmt);
	vsnprintf(ctx->code[ctx->ncode++], BURS_INSTR_LEN, fmt, ap);
	va_end(ap);
}

Operand burs_operand(const char *fmt, ...)
{
	Operand o;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(o.s, sizeof o.s, fmt, ap);
	va_end(ap);
	return o;
}

Operand burs_new_reg(EmitCtx *ctx)
{
	return burs_operand("r%d", ctx->next_reg++);
}

/* ------------------------------------------------------------------ */
/* Helpers for target simulators                                       */
/* ------------------------------------------------------------------ */

int burs_split(const char *line, char *op, size_t oplen,
	       char args[][BURS_INSTR_LEN], int maxargs)
{
	const char *p = line;
	size_t n = 0;
	int nargs = 0;

	while (*p == ' ')
		p++;
	while (*p && *p != ' ') {
		if (n + 1 >= oplen)
			return -1;
		op[n++] = *p++;
	}
	op[n] = '\0';
	while (*p == ' ')
		p++;
	while (*p) {
		n = 0;
		if (nargs == maxargs)
			return -1;
		while (*p && *p != ',') {
			if (*p != ' ') {
				if (n + 1 >= BURS_INSTR_LEN)
					return -1;
				args[nargs][n++] = *p;
			}
			p++;
		}
		args[nargs++][n] = '\0';
		if (*p == ',')
			p++;
	}
	return nargs;
}
