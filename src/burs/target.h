/*
 * target.h - the interface between the machine-independent BURS engine
 * and a target description.
 *
 * A target is one burg grammar (src/targets/<t>/<t>.gr).  burg turns the
 * grammar into a matcher (<prefix>_label/_rule/_nts/_kids); the grammar's
 * C footer adds the rule table and a reduce() function that turns one
 * selected rule into target instructions.  The footer then exports a
 * single `const Target` object, and the engine only ever talks to that.
 */
#ifndef TARGET_H
#define TARGET_H

#include <stdio.h>

#include "ir.h"

#define BURS_MAX_KIDS	10
#define BURS_MAX_INSTR	512
#define BURS_INSTR_LEN	48

/* Called by the generated matcher on an impossible operator. */
#define PANIC(...)	fprintf(stderr, __VA_ARGS__)

/* What a reduced subtree evaluates to: "r3", "#4", "[a]", or "" (stack). */
typedef struct {
	char s[40];
} Operand;

/* Emission buffer shared by engine and target. */
typedef struct {
	char code[BURS_MAX_INSTR][BURS_INSTR_LEN];
	int ncode;
	int next_reg;		/* virtual register counter (register targets) */
	int overflow;
} EmitCtx;

/* One grammar rule, indexed by its external rule number. */
typedef struct {
	const char *pattern;	/* "reg: ADD(reg,imm)"         */
	int cost;		/* must equal the cost in the .gr */
	const char *asm_form;	/* "ADDI rd, rs, #imm"         */
} RuleInfo;

typedef struct Target {
	const char *id;			/* "A" / "B"                         */
	const char *name;		/* one-line description              */
	int start_nt;			/* goal nonterminal                  */
	int num_nts;
	const char *const *nt_names;	/* [1..num_nts]                      */
	int num_rules;
	const RuleInfo *rules;		/* [1..num_rules]                    */

	/* burg-generated matcher */
	int (*label)(NODEPTR_TYPE);
	int (*rule)(int state, int goalnt);
	short **nts;
	NODEPTR_TYPE *(*kids)(NODEPTR_TYPE, int, NODEPTR_TYPE *);

	/* target-specific code generation and checking */
	Operand (*reduce)(EmitCtx *ctx, NODEPTR_TYPE p, int rule, const Operand *kids);
	int (*simulate)(const EmitCtx *ctx, const Operand *result, long long *value,
			char *err, size_t errlen);
} Target;

extern const Target targetA;
extern const Target targetB;

/* Helpers for target emitters. */
void burs_emit(EmitCtx *ctx, const char *fmt, ...);
Operand burs_operand(const char *fmt, ...);
Operand burs_new_reg(EmitCtx *ctx);

/*
 * Helpers for target simulators.  burs_split() breaks "ADDI r2,r1,#4" into
 * opcode "ADDI" and args {"r2","r1","#4"}; returns the arg count or -1.
 */
#define BURS_MAX_ARGS 4
int burs_split(const char *line, char *op, size_t oplen,
	       char args[][BURS_INSTR_LEN], int maxargs);

#endif /* TARGET_H */
