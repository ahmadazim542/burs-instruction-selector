/*
 * ir.h - machine-independent expression-tree IR.
 *
 * This header is the contract shared by every module: the parser builds
 * these nodes, the burg-generated matchers label them, the emitters walk
 * them.  The operator numbers below MUST match the %term line in every
 * target grammar (src/targets/<target>/<target>.gr).
 */
#ifndef IR_H
#define IR_H

#include <stdio.h>

enum {
	CONST = 1,	/* integer constant      (leaf)  ival */
	VAR   = 2,	/* named variable        (leaf)  name */
	ADD   = 3,	/* left + right                        */
	SUB   = 4,	/* left - right                        */
	MUL   = 5,	/* left * right                        */
	LOAD  = 6,	/* memory[left]          (unary)       */
	IR_NUM_OPS = 6
};

#define IR_NAME_MAX 32

typedef struct node *NODEPTR_TYPE;

struct node {
	int op;			/* one of CONST..LOAD                        */
	int state_label;	/* BURS state written by <target>_label()    */
	int id;			/* postorder number n1, n2, ... (for output) */
	long long ival;		/* CONST value                               */
	char name[IR_NAME_MAX];	/* VAR name                                  */
	NODEPTR_TYPE left, right;
};

/* Accessors required by the burg-generated matchers. */
#define OP_LABEL(p)	((p)->op)
#define STATE_LABEL(p)	((p)->state_label)
#define LEFT_CHILD(p)	((p)->left)
#define RIGHT_CHILD(p)	((p)->right)

/* Tree builders. */
NODEPTR_TYPE mk_const(long long v);
NODEPTR_TYPE mk_var(const char *name);
NODEPTR_TYPE mk_add(NODEPTR_TYPE l, NODEPTR_TYPE r);
NODEPTR_TYPE mk_sub(NODEPTR_TYPE l, NODEPTR_TYPE r);
NODEPTR_TYPE mk_mul(NODEPTR_TYPE l, NODEPTR_TYPE r);
NODEPTR_TYPE mk_load(NODEPTR_TYPE addr);
void freetree(NODEPTR_TYPE p);

/* Number nodes n1..nN in postorder; returns N. */
int ir_number(NODEPTR_TYPE p);

const char *ir_opname(int op);
int ir_arity(int op);

/* Printing: one-line prefix form, and an indented tree drawing. */
void printtree(NODEPTR_TYPE p, FILE *out);
void ir_print_node(NODEPTR_TYPE p, FILE *out);	/* "ADD", "VAR a", "CONST 4" */
void ir_draw(NODEPTR_TYPE p, FILE *out);

/*
 * Parse an infix expression into IR.
 *   expr    := term (('+' | '-') term)*
 *   term    := primary ('*' primary)*
 *   primary := NUMBER | IDENT | '(' expr ')' | '[' expr ']' | load '(' expr ')'
 * '[e]' and 'load(e)' both build LOAD(e).  On failure returns NULL and
 * writes a message into err.
 */
NODEPTR_TYPE ir_parse(const char *src, char *err, size_t errlen);

/*
 * Reference semantics, used as the test oracle for generated code.
 * Variables a..z default to 2..27; other names hash to a small value.
 * Memory is modelled as mem[addr] = 1000 + addr.
 */
int ir_set_var(const char *name, long long value);
long long ir_var_value(const char *name);
long long ir_mem_read(long long addr);
long long ir_eval(NODEPTR_TYPE p);

#endif /* IR_H */
