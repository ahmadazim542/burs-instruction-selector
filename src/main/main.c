/*
 * main.c - console driver: parse an expression, run it through the BURS
 * engine for one or both targets, print cover / costs / code, and check
 * the generated code by simulating it against the IR's reference value.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "ir.h"

#define EXIT_USAGE	1
#define EXIT_PARSE	2
#define EXIT_CHECK	3

static const Target *const all_targets[] = { &targetA, &targetB };
#define NTARGETS (int)(sizeof all_targets / sizeof all_targets[0])

/* The sample expressions from the Review 1 / Review 2 documents. */
static const char *const suite[] = {
	"a + b * c",
	"(a + b) * c",
	"load(a) + 4",
	"a - b + c",
	"a + b + c",
	"a * (b + c)",
	"a",
	"7",
	"a * 3 - 1",
	"[a + 8] * [b]",
};
#define NSUITE (int)(sizeof suite / sizeof suite[0])

struct options {
	int use[NTARGETS];
	int verbose;
};

/* Result of running one tree through one target. */
struct run {
	int covered;
	int cost;
	int ninstr;
	int sim_ok;
	long long value;
	char err[160];
};

static void usage(FILE *out)
{
	fprintf(out,
		"usage: burs [options] \"expression\"   select instructions for an expression\n"
		"       burs [options] -i               interactive: one expression per line\n"
		"       burs [options] --suite          run the review sample expressions\n"
		"       burs [options] --exhaustive     check every small tree (depth <= 2)\n"
		"       burs [options] --bench          time labeling on growing random trees\n"
		"       burs [options] --rules          print the target rule tables\n"
		"\n"
		"options:\n"
		"  -t A|B|both     target(s) to generate code for (default: both)\n"
		"  -v              also print the per-node BURS cost table\n"
		"  -D name=value   set a variable's value for the simulation check\n"
		"  -h              this help\n"
		"\n"
		"expressions: + - *, integer constants, variables, ( ), and memory\n"
		"loads written [e] or load(e).   example: burs \"a + b * c\"\n");
}

static void collect_vars(NODEPTR_TYPE p, char seen[][IR_NAME_MAX], int *n, int max)
{
	int i;

	if (p == NULL)
		return;
	collect_vars(p->left, seen, n, max);
	collect_vars(p->right, seen, n, max);
	if (p->op != VAR)
		return;
	for (i = 0; i < *n; i++)
		if (strcmp(seen[i], p->name) == 0)
			return;
	if (*n < max)
		snprintf(seen[(*n)++], IR_NAME_MAX, "%s", p->name);
}

static void print_vars(NODEPTR_TYPE tree)
{
	char seen[32][IR_NAME_MAX];
	int i, n = 0;

	collect_vars(tree, seen, &n, 32);
	printf("Test values  :");
	for (i = 0; i < n; i++)
		printf(" %s=%lld", seen[i], ir_var_value(seen[i]));
	printf("%smem[x]=1000+x\n", n ? ", " : " ");
}

static void rule_line(char c)
{
	int i;
	for (i = 0; i < 72; i++)
		putchar(c);
	putchar('\n');
}

/* Label, select, emit and simulate one target.  quiet: no printing. */
static void run_target(const Target *t, NODEPTR_TYPE tree, long long expect,
		       int verbose, int quiet, struct run *out, EmitCtx *ctx)
{
	Operand res;
	int i;

	memset(out, 0, sizeof *out);
	memset(ctx, 0, sizeof *ctx);

	if (!quiet) {
		printf("\n");
		rule_line('=');
		printf("Target %s : %s\n", t->id, t->name);
		rule_line('=');
	}

	out->covered = burs_label(t, tree);	/* bottom-up DP labeling */
	if (!out->covered) {
		snprintf(out->err, sizeof out->err, "no cover: tree cannot be derived from '%s'",
			 t->nt_names[t->start_nt]);
		if (!quiet)
			printf("ERROR: %s\n", out->err);
		return;
	}

	if (verbose && !quiet) {
		printf("BURS cost table (bottom-up labeling, best rule per nonterminal):\n");
		burs_print_cost_table(t, tree, stdout);
		printf("\n");
	}

	out->cost = burs_cost(t, tree, t->start_nt);
	if (!quiet) {
		printf("Selected cover (backtracked from root, goal '%s'):\n",
		       t->nt_names[t->start_nt]);
		burs_print_cover(t, tree, t->start_nt, stdout);
	}

	res = burs_reduce(t, tree, t->start_nt, ctx);	/* top-down emission */
	out->ninstr = ctx->ncode;
	if (ctx->overflow) {
		snprintf(out->err, sizeof out->err, "too many instructions (limit %d)", BURS_MAX_INSTR);
		if (!quiet)
			printf("ERROR: %s\n", out->err);
		return;
	}

	if (!quiet) {
		printf("\nGenerated code:\n");
		for (i = 0; i < ctx->ncode; i++)
			printf("    %3d  %s\n", i + 1, ctx->code[i]);
		printf("\nTotal cover cost : %d   (%d instruction%s)\n", out->cost,
		       ctx->ncode, ctx->ncode == 1 ? "" : "s");
	}

	if (!t->simulate(ctx, &res, &out->value, out->err, sizeof out->err)) {
		if (!quiet)
			printf("Simulation       : FAILED -- %s\n", out->err);
		return;
	}
	out->sim_ok = out->value == expect;
	if (!out->sim_ok)
		snprintf(out->err, sizeof out->err, "simulated %lld, expected %lld",
			 out->value, expect);
	if (!quiet)
		printf("Simulation       : result %lld, reference %lld   %s\n",
		       out->value, expect, out->sim_ok ? "OK" : "MISMATCH");
}

/* Full report for one expression.  Returns an exit status. */
static int process(const char *src, const struct options *opt)
{
	char err[160];
	NODEPTR_TYPE tree = ir_parse(src, err, sizeof err);
	struct run runs[NTARGETS];
	static EmitCtx ctx;
	long long expect;
	int i, nused = 0, status = 0;

	printf("Expression   : %s\n", src);
	if (tree == NULL) {
		printf("ERROR: invalid expression, %s\n", err);
		return EXIT_PARSE;
	}
	printf("IR (prefix)  : ");
	printtree(tree, stdout);
	printf("\nIR tree      :\n");
	ir_draw(tree, stdout);
	print_vars(tree);
	expect = ir_eval(tree);
	printf("Reference    : %lld\n", expect);

	for (i = 0; i < NTARGETS; i++) {
		if (!opt->use[i])
			continue;
		nused++;
		run_target(all_targets[i], tree, expect, opt->verbose, 0, &runs[i], &ctx);
		if (!runs[i].covered || !runs[i].sim_ok)
			status = EXIT_CHECK;
	}

	if (nused > 1) {
		printf("\n");
		rule_line('-');
		printf("Retargeting summary (same IR tree, same BURS engine):\n");
		printf("    %-8s %-8s %-6s %s\n", "target", "instrs", "cost", "check");
		for (i = 0; i < NTARGETS; i++)
			if (opt->use[i])
				printf("    %-8s %-8d %-6d %s\n", all_targets[i]->id,
				       runs[i].ninstr, runs[i].cost,
				       runs[i].sim_ok ? "OK" : "FAIL");
		rule_line('-');
	}
	freetree(tree);
	return status;
}

static int run_suite(const struct options *opt)
{
	static EmitCtx ctx;
	int s, i, failures = 0;

	printf("Review sample expressions -- cover cost / instruction count per target\n");
	printf("(check = generated code simulated and compared with the IR reference value)\n\n");
	printf("%-3s %-16s", "#", "expression");
	for (i = 0; i < NTARGETS; i++)
		if (opt->use[i])
			printf("  %s:instr %s:cost", all_targets[i]->id, all_targets[i]->id);
	printf("  check\n");

	for (s = 0; s < NSUITE; s++) {
		char err[160];
		NODEPTR_TYPE tree = ir_parse(suite[s], err, sizeof err);
		long long expect;
		int ok = 1;

		printf("%-3d %-16s", s + 1, suite[s]);
		if (tree == NULL) {
			printf("  parse error: %s\n", err);
			failures++;
			continue;
		}
		expect = ir_eval(tree);
		for (i = 0; i < NTARGETS; i++) {
			struct run r;
			if (!opt->use[i])
				continue;
			run_target(all_targets[i], tree, expect, 0, 1, &r, &ctx);
			printf("  %7d %6d", r.ninstr, r.cost);
			ok &= r.covered && r.sim_ok;
		}
		printf("  %s\n", ok ? "PASS" : "FAIL");
		failures += !ok;
		freetree(tree);
	}
	printf("\n%d/%d expressions passed\n", NSUITE - failures, NSUITE);
	return failures ? EXIT_CHECK : 0;
}

/*
 * Baseline for comparison: a naive selector that emits one instruction per
 * IR node (LOAD/LOADI or PUSH/PUSHI for leaves, the plain operator form for
 * interior nodes), with the same cost model -- 1 per instruction, MUL 2.
 * It never uses immediate or memory operand forms.
 */
static int naive_cost(NODEPTR_TYPE p)
{
	if (p == NULL)
		return 0;
	return (p->op == MUL ? 2 : 1) + naive_cost(p->left) + naive_cost(p->right);
}

/*
 * Exhaustive small-tree check: every tree of depth <= 2 built from the
 * leaves {a, b, 3} and all four operators is run through every selected
 * target, and the simulated result must equal the IR reference value.
 * Subtrees are shared between trees; that is safe because a node's BURS
 * state depends only on the subtree below it.
 */
static int run_exhaustive(const struct options *opt)
{
	enum { DEPTH = 2, MAXT = 4096 };
	static NODEPTR_TYPE trees[MAXT];
	static EmitCtx ctx;
	int ntrees, prev, d, i, j, k, checked = 0, failures = 0;
	int maxcost[NTARGETS] = { 0 }, nfail[NTARGETS] = { 0 }, better[NTARGETS] = { 0 };
	long totcost[NTARGETS] = { 0 }, totnaive = 0;

	trees[0] = mk_var("a");
	trees[1] = mk_var("b");
	trees[2] = mk_const(3);
	ntrees = 3;
	for (d = 1; d <= DEPTH; d++) {
		prev = ntrees;
		for (i = 0; i < prev; i++) {
			trees[ntrees++] = mk_load(trees[i]);
			for (j = 0; j < prev; j++) {
				trees[ntrees++] = mk_add(trees[i], trees[j]);
				trees[ntrees++] = mk_sub(trees[i], trees[j]);
				trees[ntrees++] = mk_mul(trees[i], trees[j]);
			}
		}
	}

	for (i = 0; i < ntrees; i++) {
		long long expect = ir_eval(trees[i]);
		int naive = naive_cost(trees[i]);
		ir_number(trees[i]);
		totnaive += naive;
		for (k = 0; k < NTARGETS; k++) {
			struct run r;
			if (!opt->use[k])
				continue;
			run_target(all_targets[k], trees[i], expect, 0, 1, &r, &ctx);
			checked++;
			if (r.cost > maxcost[k])
				maxcost[k] = r.cost;
			totcost[k] += r.cost;
			better[k] += r.cost < naive;
			if (!r.covered || !r.sim_ok) {
				nfail[k]++;
				if (failures++ < 10) {
					printf("FAIL target %s: ", all_targets[k]->id);
					printtree(trees[i], stdout);
					printf("  -- %s\n", r.err);
				}
			}
		}
	}
	printf("Exhaustive check: %d trees (depth <= %d over a, b, 3 with ADD/SUB/MUL/LOAD)\n",
	       ntrees, DEPTH);
	for (k = 0; k < NTARGETS; k++)
		if (opt->use[k])
			printf("    target %s: %d failures, max cover cost %d\n",
			       all_targets[k]->id, nfail[k], maxcost[k]);
	printf("Cost vs naive one-instruction-per-node selection (same cost model):\n");
	printf("    naive baseline : average cost %.2f\n", (double)totnaive / ntrees);
	for (k = 0; k < NTARGETS; k++)
		if (opt->use[k])
			printf("    BURS target %s : average cost %.2f  (%.1f%% lower; cheaper on %d of %d trees)\n",
			       all_targets[k]->id, (double)totcost[k] / ntrees,
			       100.0 * (totnaive - totcost[k]) / totnaive, better[k], ntrees);
	printf("%d target runs, %d failures\n", checked, failures);
	return failures ? EXIT_CHECK : 0;
}

/*
 * Scaling benchmark: random balanced trees of growing size; time the
 * bottom-up labeling pass and the cover-cost backtrack.  Both should be
 * linear in the number of nodes.
 */
static NODEPTR_TYPE random_tree(int depth, unsigned *seed)
{
	static const char *const names[] = { "a", "b", "c", "d" };
	unsigned r;

	*seed = *seed * 1103515245u + 12345u;
	r = (*seed >> 16) & 0x7fff;
	if (depth == 0)
		return r % 3 == 0 ? mk_const(r % 10) : mk_var(names[r % 4]);
	if (r % 8 == 0)
		return mk_load(random_tree(depth - 1, seed));
	switch (r % 3) {
	case 0:  return mk_add(random_tree(depth - 1, seed), random_tree(depth - 1, seed));
	case 1:  return mk_sub(random_tree(depth - 1, seed), random_tree(depth - 1, seed));
	default: return mk_mul(random_tree(depth - 1, seed), random_tree(depth - 1, seed));
	}
}

static int count_nodes(NODEPTR_TYPE p)
{
	return p == NULL ? 0 : 1 + count_nodes(p->left) + count_nodes(p->right);
}

static int run_bench(const struct options *opt)
{
	int depth, k;

	printf("Scaling benchmark: random trees, time per pass (best of 5 runs)\n\n");
	printf("%-6s %9s", "depth", "nodes");
	for (k = 0; k < NTARGETS; k++)
		if (opt->use[k])
			printf("  %s:label_ms %s:ns/node %s:cost", all_targets[k]->id,
			       all_targets[k]->id, all_targets[k]->id);
	printf("\n");

	for (depth = 8; depth <= 18; depth += 2) {
		unsigned seed = 42;
		NODEPTR_TYPE tree = random_tree(depth, &seed);
		int nodes = count_nodes(tree);

		printf("%-6d %9d", depth, nodes);
		for (k = 0; k < NTARGETS; k++) {
			double best = -1;
			int run, cost = 0;
			if (!opt->use[k])
				continue;
			for (run = 0; run < 5; run++) {
				clock_t t0 = clock();
				double ms;
				burs_label(all_targets[k], tree);
				cost = burs_cost(all_targets[k], tree, all_targets[k]->start_nt);
				ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
				if (best < 0 || ms < best)
					best = ms;
			}
			printf("  %10.3f %10.1f %7d", best, best * 1e6 / nodes, cost);
		}
		printf("\n");
		freetree(tree);
	}
	printf("\n(label = burg-generated bottom-up pass + cover-cost backtrack; timings vary by machine)\n");
	return 0;
}

static int interactive(const struct options *opt)
{
	char line[1024];
	int status = 0;

	printf("BURS instruction selector -- enter an expression, or 'quit'.\n");
	for (;;) {
		size_t n;
		printf("burs> ");
		fflush(stdout);
		if (fgets(line, sizeof line, stdin) == NULL)
			break;
		n = strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' '))
			line[--n] = '\0';
		if (n == 0)
			continue;
		if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0)
			break;
		status |= process(line, opt);
		printf("\n");
	}
	return status;
}

static int set_var(const char *arg)
{
	const char *eq = strchr(arg, '=');
	char name[IR_NAME_MAX], *end;
	long long v;

	if (eq == NULL || eq == arg || (size_t)(eq - arg) >= sizeof name)
		return 0;
	snprintf(name, sizeof name, "%.*s", (int)(eq - arg), arg);
	v = strtoll(eq + 1, &end, 10);
	if (end == eq + 1 || *end != '\0')
		return 0;
	return ir_set_var(name, v);
}

int main(int argc, char **argv)
{
	struct options opt;
	const char *expr = NULL;
	int i, mode_rules = 0, mode_suite = 0, mode_inter = 0, mode_exh = 0, mode_bench = 0;

	memset(&opt, 0, sizeof opt);
	for (i = 0; i < NTARGETS; i++)
		opt.use[i] = 1;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(stdout);
			return 0;
		} else if (strcmp(a, "-v") == 0) {
			opt.verbose = 1;
		} else if (strcmp(a, "-i") == 0) {
			mode_inter = 1;
		} else if (strcmp(a, "--rules") == 0) {
			mode_rules = 1;
		} else if (strcmp(a, "--suite") == 0) {
			mode_suite = 1;
		} else if (strcmp(a, "--exhaustive") == 0) {
			mode_exh = 1;
		} else if (strcmp(a, "--bench") == 0) {
			mode_bench = 1;
		} else if (strcmp(a, "-t") == 0 && i + 1 < argc) {
			const char *t = argv[++i];
			int k, both = strcmp(t, "both") == 0, found = 0;
			for (k = 0; k < NTARGETS; k++) {
				int match = both || strcmp(t, all_targets[k]->id) == 0 ||
					    (t[1] == '\0' && (t[0] | 0x20) == (all_targets[k]->id[0] | 0x20));
				opt.use[k] = match;
				found |= match;
			}
			if (!found) {
				fprintf(stderr, "burs: unknown target '%s' (use A, B or both)\n", t);
				return EXIT_USAGE;
			}
		} else if (strcmp(a, "-D") == 0 && i + 1 < argc) {
			if (!set_var(argv[++i])) {
				fprintf(stderr, "burs: bad -D '%s' (expected name=integer)\n", argv[i]);
				return EXIT_USAGE;
			}
		} else if (strcmp(a, "-t") == 0 || strcmp(a, "-D") == 0) {
			fprintf(stderr, "burs: option %s needs an argument\n", a);
			return EXIT_USAGE;
		} else if (a[0] == '-' && isalpha((unsigned char)a[1])) {
			fprintf(stderr, "burs: unknown option '%s' (try -h)\n", a);
			return EXIT_USAGE;
		} else if (expr == NULL) {
			expr = a;
		} else {
			fprintf(stderr, "burs: more than one expression given; quote it: burs \"a + b\"\n");
			return EXIT_USAGE;
		}
	}

	if (mode_rules) {
		for (i = 0; i < NTARGETS; i++)
			if (opt.use[i]) {
				burs_print_rules(all_targets[i], stdout);
				printf("\n");
			}
		return 0;
	}
	if (mode_suite)
		return run_suite(&opt);
	if (mode_exh)
		return run_exhaustive(&opt);
	if (mode_bench)
		return run_bench(&opt);
	if (mode_inter)
		return interactive(&opt);
	if (expr == NULL) {
		usage(stderr);
		return EXIT_USAGE;
	}
	return process(expr, &opt);
}
