/*
 * ir.c - expression-tree IR: builders, printing, parser and reference
 * evaluator.  Nothing in this file knows about any target machine.
 */
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"

static const char *const opnames[] = {
	"?", "CONST", "VAR", "ADD", "SUB", "MUL", "LOAD"
};
static const int arities[] = { 0, 0, 0, 2, 2, 2, 1 };

const char *ir_opname(int op)
{
	return (op >= 1 && op <= IR_NUM_OPS) ? opnames[op] : "?";
}

int ir_arity(int op)
{
	return (op >= 1 && op <= IR_NUM_OPS) ? arities[op] : 0;
}

/* ------------------------------------------------------------------ */
/* Builders                                                            */
/* ------------------------------------------------------------------ */

static NODEPTR_TYPE mk_node(int op, NODEPTR_TYPE l, NODEPTR_TYPE r)
{
	NODEPTR_TYPE p = calloc(1, sizeof *p);

	if (p == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	p->op = op;
	p->left = l;
	p->right = r;
	return p;
}

NODEPTR_TYPE mk_const(long long v)
{
	NODEPTR_TYPE p = mk_node(CONST, NULL, NULL);
	p->ival = v;
	return p;
}

NODEPTR_TYPE mk_var(const char *name)
{
	NODEPTR_TYPE p = mk_node(VAR, NULL, NULL);
	snprintf(p->name, sizeof p->name, "%s", name);
	return p;
}

NODEPTR_TYPE mk_add(NODEPTR_TYPE l, NODEPTR_TYPE r) { return mk_node(ADD, l, r); }
NODEPTR_TYPE mk_sub(NODEPTR_TYPE l, NODEPTR_TYPE r) { return mk_node(SUB, l, r); }
NODEPTR_TYPE mk_mul(NODEPTR_TYPE l, NODEPTR_TYPE r) { return mk_node(MUL, l, r); }
NODEPTR_TYPE mk_load(NODEPTR_TYPE a) { return mk_node(LOAD, a, NULL); }

void freetree(NODEPTR_TYPE p)
{
	if (p == NULL)
		return;
	freetree(p->left);
	freetree(p->right);
	free(p);
}

static int number(NODEPTR_TYPE p, int next)
{
	if (p == NULL)
		return next;
	next = number(p->left, next);
	next = number(p->right, next);
	p->id = ++next;
	return next;
}

int ir_number(NODEPTR_TYPE p)
{
	return number(p, 0);
}

/* ------------------------------------------------------------------ */
/* Printing                                                            */
/* ------------------------------------------------------------------ */

void ir_print_node(NODEPTR_TYPE p, FILE *out)
{
	switch (p->op) {
	case CONST: fprintf(out, "CONST %lld", p->ival); break;
	case VAR:   fprintf(out, "VAR %s", p->name); break;
	default:    fprintf(out, "%s", ir_opname(p->op)); break;
	}
}

void printtree(NODEPTR_TYPE p, FILE *out)
{
	switch (ir_arity(p->op)) {
	case 0:
		ir_print_node(p, out);
		break;
	case 1:
		fprintf(out, "%s(", ir_opname(p->op));
		printtree(p->left, out);
		fprintf(out, ")");
		break;
	default:
		fprintf(out, "%s(", ir_opname(p->op));
		printtree(p->left, out);
		fprintf(out, ", ");
		printtree(p->right, out);
		fprintf(out, ")");
		break;
	}
}

static void draw(NODEPTR_TYPE p, FILE *out, char *prefix, size_t plen, int last, int root)
{
	size_t n;

	fprintf(out, "    %s%s", prefix, root ? "" : (last ? "`-- " : "|-- "));
	ir_print_node(p, out);
	fprintf(out, "   [n%d]\n", p->id);

	n = strlen(prefix);
	if (!root && n + 5 < plen)
		strcat(prefix, last ? "    " : "|   ");
	if (p->left && p->right) {
		draw(p->left, out, prefix, plen, 0, 0);
		draw(p->right, out, prefix, plen, 1, 0);
	} else if (p->left) {
		draw(p->left, out, prefix, plen, 1, 0);
	}
	prefix[n] = '\0';
}

void ir_draw(NODEPTR_TYPE p, FILE *out)
{
	char prefix[256] = "";
	draw(p, out, prefix, sizeof prefix, 1, 1);
}

/* ------------------------------------------------------------------ */
/* Parser (recursive descent)                                          */
/* ------------------------------------------------------------------ */

struct parser {
	const char *s;		/* whole input   */
	const char *p;		/* current char  */
	char *err;
	size_t errlen;
	int failed;
};

static void fail(struct parser *ps, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (ps->failed)
		return;
	ps->failed = 1;
	n = snprintf(ps->err, ps->errlen, "column %d: ", (int)(ps->p - ps->s) + 1);
	if (n < 0 || (size_t)n >= ps->errlen)
		return;
	va_start(ap, fmt);
	vsnprintf(ps->err + n, ps->errlen - n, fmt, ap);
	va_end(ap);
}

static void skip_ws(struct parser *ps)
{
	while (isspace((unsigned char)*ps->p))
		ps->p++;
}

static int peek(struct parser *ps)
{
	skip_ws(ps);
	return (unsigned char)*ps->p;
}

static void describe(int c, char *buf, size_t len)
{
	if (c == '\0')
		snprintf(buf, len, "end of input");
	else
		snprintf(buf, len, "'%c'", c);
}

static int expect(struct parser *ps, int c)
{
	char what[32];

	if (peek(ps) == c) {
		ps->p++;
		return 1;
	}
	describe(peek(ps), what, sizeof what);
	fail(ps, "expected '%c' but found %s", c, what);
	return 0;
}

static NODEPTR_TYPE parse_expr(struct parser *ps);

static NODEPTR_TYPE parse_primary(struct parser *ps)
{
	int c = peek(ps);
	char what[32];

	if (isdigit(c)) {
		long long v = 0;
		while (isdigit((unsigned char)*ps->p)) {
			int d = *ps->p - '0';
			if (v > (LLONG_MAX - d) / 10) {
				fail(ps, "integer constant too large");
				return NULL;
			}
			v = v * 10 + d;
			ps->p++;
		}
		return mk_const(v);
	}
	if (isalpha(c) || c == '_') {
		char name[IR_NAME_MAX];
		size_t n = 0;
		while (isalnum((unsigned char)*ps->p) || *ps->p == '_') {
			if (n + 1 >= sizeof name) {
				fail(ps, "identifier longer than %d characters", IR_NAME_MAX - 1);
				return NULL;
			}
			name[n++] = *ps->p++;
		}
		name[n] = '\0';
		if (strcmp(name, "load") == 0 && peek(ps) == '(') {
			NODEPTR_TYPE e;
			ps->p++;
			e = parse_expr(ps);
			if (e == NULL || !expect(ps, ')')) {
				freetree(e);
				return NULL;
			}
			return mk_load(e);
		}
		return mk_var(name);
	}
	if (c == '(' || c == '[') {
		int close = c == '(' ? ')' : ']';
		NODEPTR_TYPE e;
		ps->p++;
		e = parse_expr(ps);
		if (e == NULL || !expect(ps, close)) {
			freetree(e);
			return NULL;
		}
		return c == '[' ? mk_load(e) : e;
	}
	if (c == '/' || c == '%' || c == '^' || c == '&' || c == '|') {
		fail(ps, "operator '%c' is not supported (IR has ADD, SUB, MUL, LOAD only)", c);
		return NULL;
	}
	if (c == '-') {
		fail(ps, "unary minus is not supported; write (0 - x)");
		return NULL;
	}
	describe(c, what, sizeof what);
	fail(ps, "expected a number, variable, '(' or '[' but found %s", what);
	return NULL;
}

static NODEPTR_TYPE parse_term(struct parser *ps)
{
	NODEPTR_TYPE l = parse_primary(ps);

	while (l != NULL && peek(ps) == '*') {
		NODEPTR_TYPE r;
		ps->p++;
		r = parse_primary(ps);
		if (r == NULL) {
			freetree(l);
			return NULL;
		}
		l = mk_mul(l, r);
	}
	return l;
}

static NODEPTR_TYPE parse_expr(struct parser *ps)
{
	NODEPTR_TYPE l = parse_term(ps);

	while (l != NULL && (peek(ps) == '+' || peek(ps) == '-')) {
		int op = *ps->p++;
		NODEPTR_TYPE r = parse_term(ps);
		if (r == NULL) {
			freetree(l);
			return NULL;
		}
		l = op == '+' ? mk_add(l, r) : mk_sub(l, r);
	}
	return l;
}

NODEPTR_TYPE ir_parse(const char *src, char *err, size_t errlen)
{
	struct parser ps;
	NODEPTR_TYPE t;
	char what[32];

	ps.s = ps.p = src;
	ps.err = err;
	ps.errlen = errlen;
	ps.failed = 0;
	if (errlen > 0)
		err[0] = '\0';

	if (peek(&ps) == '\0') {
		fail(&ps, "empty expression");
		return NULL;
	}
	t = parse_expr(&ps);
	if (t != NULL && peek(&ps) != '\0') {
		describe(peek(&ps), what, sizeof what);
		if (strchr("/%^&|", *ps.p))
			fail(&ps, "operator '%c' is not supported (IR has ADD, SUB, MUL, LOAD only)", *ps.p);
		else
			fail(&ps, "unexpected %s after a complete expression", what);
		freetree(t);
		return NULL;
	}
	if (t != NULL)
		ir_number(t);
	return t;
}

/* ------------------------------------------------------------------ */
/* Reference semantics                                                 */
/* ------------------------------------------------------------------ */

#define MAX_OVERRIDES 32

static struct {
	char name[IR_NAME_MAX];
	long long value;
} overrides[MAX_OVERRIDES];
static int noverrides;

int ir_set_var(const char *name, long long value)
{
	int i;

	for (i = 0; i < noverrides; i++)
		if (strcmp(overrides[i].name, name) == 0) {
			overrides[i].value = value;
			return 1;
		}
	if (noverrides == MAX_OVERRIDES)
		return 0;
	snprintf(overrides[noverrides].name, IR_NAME_MAX, "%s", name);
	overrides[noverrides++].value = value;
	return 1;
}

long long ir_var_value(const char *name)
{
	unsigned sum = 0;
	int i;

	for (i = 0; i < noverrides; i++)
		if (strcmp(overrides[i].name, name) == 0)
			return overrides[i].value;
	if (name[0] >= 'a' && name[0] <= 'z' && name[1] == '\0')
		return name[0] - 'a' + 2;
	for (i = 0; name[i]; i++)
		sum += (unsigned char)name[i];
	return sum % 50 + 2;
}

long long ir_mem_read(long long addr)
{
	return 1000 + addr;
}

long long ir_eval(NODEPTR_TYPE p)
{
	switch (p->op) {
	case CONST: return p->ival;
	case VAR:   return ir_var_value(p->name);
	case ADD:   return ir_eval(p->left) + ir_eval(p->right);
	case SUB:   return ir_eval(p->left) - ir_eval(p->right);
	case MUL:   return ir_eval(p->left) * ir_eval(p->right);
	case LOAD:  return ir_mem_read(ir_eval(p->left));
	}
	return 0;
}
