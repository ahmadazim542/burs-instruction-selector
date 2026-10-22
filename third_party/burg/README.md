# BURG

This repository contains the BURG source distribution described in
[BURG: Fast Optimal Instruction Selection and Tree Parsing][burg].

Compared with the original distribution, this repository:

1. Modernizes old C constructs, including K&R-style function definitions.
2. Uses GNU Bison to generate `gram.tab.c` and `gram.tab.h` from `gram.y`, and
   includes the generated files in the repository.
3. Fixes generator and generated-selector correctness problems, including:
   - cost comparison between duplicate leaf rules;
   - state functions for grammars whose maximum arity is less than two;
   - declarations required by generated C code;
   - plank exception chains and filtered error states; and
   - type-safe list callbacks.
4. Preserves table compression by normalizing projected item sets and retains
   the original `%gram` state-filtering declaration.
5. Removes unused code and simplifies redundant state and table operations.
6. Replaces the original Makefile with a C-only CMake build using an explicit
   source list.

See [`README_ORIGINAL`](README_ORIGINAL) for the original README. To inspect the
unmodified imported source, check out the first commit in this repository.

## Building

BURG requires CMake 3.12 or newer and a C compiler:

```sh
cmake -S . -B build
cmake --build build
```

With a multi-configuration generator, such as Visual Studio, select a
configuration explicitly if needed:

```sh
cmake --build build --config Release
```

## Trying the sample grammar

Generate and compile the sample selector with:

```sh
burg -o sample.c sample.gr
cc sample.c -o sample
./sample
```

The `-I` option additionally emits internal tables and diagnostic helpers such
as `burm_string`, `burm_op_label`, and `burm_arity`. The sample does not depend
on this optional interface, so it compiles with or without `-I`.

### Filtering states with `%gram`

The optional `%gram` declaration lists nonterminals that should be retained by
the global state map. For example:

```text
%gram reg addr
```

After the rules have been read, a state that cannot derive any listed
nonterminal is mapped to the error state. This can reduce the generated
automaton, but the list must include every nonterminal whose states need to
remain available to later transitions.

## Understanding BURG

The following papers are useful background, roughly in the order in which they
are best read:

1. [Code Generation Using Tree Matching and Dynamic Programming][twig]
2. [Engineering a Simple, Efficient Code-Generator Generator][iburg]
3. [Simple and Efficient BURS Table Generation][simple-burs]
4. [Efficient Retargetable Code Generation Using Bottom-Up Tree Pattern
   Matching][retarget]
5. [Optimal Code Generation for Expression Trees: An Application of BURS
   Theory][burs-theory]

### Twig, iburg, and BURG

The first paper describes Twig, and the second describes iburg. All three tools
solve the minimum-cost tree-covering problem using the same dynamic-programming
principle, but they perform the work at different times and use substantially
different matchers.

Twig performs a depth-first traversal. On descent, a table-driven adaptation of
Aho-Corasick string matching recognizes path strings in parallel. After the
children of a node have been visited, Twig combines their partial matches to
recognize tree patterns and performs dynamic-programming cost calculation as
the traversal unwinds. It later uses the least-cost cover to execute actions.
iburg instead emits a hard-coded matcher that combines pattern matching and
dynamic-programming cost calculation in a bottom-up pass, followed by a
top-down reduction pass. iburg is usually the easiest implementation to study,
although the Twig paper gives a more detailed introduction to the
dynamic-programming formulation.

BURG moves the dynamic-programming decisions from compiler run time to
code-generator-generation time (sometimes called *compile-compile time*). It
constructs a finite-state bottom-up tree automaton, so a generated BURG matcher
can label a node from its operator and child states using table lookups. This
makes generated matchers fast, but requires rule costs to be static and makes
table generation more involved. iburg permits dynamic costs and accepts a larger
class of tree grammars.

Twig assumes that subject-tree and pattern nodes bearing the same symbol have
the same arity. This is the usual ranked-tree convention. Twig's bit-string
scheme relies on this assumption when it shifts the children's bit strings and
combines them with a logical AND to recognize a whole pattern. The issue is not
merely that Twig tests bit zero or cannot count successful subpatterns: the bits
represent matches at template depths, and fixed arity makes the recurrence over
all corresponding children exact.

### States and table generation

BURS stands for *bottom-up rewrite system*. BURG implements the tree-grammar
subset of the BURS model needed for fast instruction selection.

At run time, a generated matcher makes a bottom-up pass over the subject tree.
Each node receives a state determined by its operator and the states of its
children. Conceptually, a state is an item set indexed by nonterminal. For every
nonterminal to which the subtree can be reduced, the item records a least-cost
rule and its relative (delta) cost. A second, top-down pass uses the node's
state and a goal nonterminal to recover the selected rule and recursively visit
the subtrees named by that rule.

BURG builds the states and transition tables with a work-list algorithm:

1. Compute a state for each leaf operator.
2. Apply chain-rule closure.
3. Combine known child states with each non-leaf operator to compute transition
   results.
4. Trim items that cannot contribute to a least-cost cover, normalize the
   remaining costs, and merge equivalent states.
5. Repeat until no new states are found.

Normalizing a state subtracts the smallest finite cost from every finite cost in
that state. This preserves the relative ordering of its alternatives. When BURG
computes a transition, it compares rules for one fixed operator and one fixed
tuple of child states. The base cost omitted from each child state is therefore
common to all competing rules and cannot change which rule is cheapest. In this
repository, `zero` performs the normalization and the `addHP_*` functions build
transition results from normalized child states.

Projection requires another zero alignment. A full state may be normalized yet
lose its zero-cost item when `restrict_` keeps only the nonterminals relevant to
one table dimension. Re-normalizing the projection allows vectors that differ
only by a common offset to share the same projected state and plank field.

Normalization often makes the set of states finite, but it does not guarantee
finiteness. A cost-divergent grammar may cause BURG to keep creating states
until it runs out of memory. Figure 3 of the BURG paper gives an example. The
`-c N` option makes BURG stop when a relative cost exceeds `N`; using iburg is
another option for grammars that cannot be represented by a finite BURG
automaton.

### Mapping the table-generation paper to this implementation

The table-generation code closely follows *Simple and Efficient BURS Table
Generation*. The main correspondences are:

| Name in the paper | Name in this repository |
| --- | --- |
| `Main` | `build` |
| `NormalizeCosts` | `zero` |
| `Closure` | `closure` |
| `ComputeLeafStates` | `doLeaf` |
| `Project` | `restrict_` |
| `Triangle` | `siblings` |
| `Trim` | `trim` |
| `ComputeTransitions` | `addToTable`, `addHyperPlane`, and `addHP_*` |

The repository's C `main` function is the command-line driver; it parses the
grammar, performs preprocessing, calls `build`, and emits the generated matcher.

### The other BURS papers

*Efficient Retargetable Code Generation Using Bottom-Up Tree Pattern Matching*
describes a closely related and independently developed automaton construction.
It uses regular tree grammars and item sets containing `(rule, delta-cost)`
pairs, and it discusses table compression and the absence of run-time cost
analysis.

*Optimal Code Generation for Expression Trees: An Application of BURS Theory*
introduces the more general rewrite-system formulation of BURS and formalizes
the reachability and minimum-cost reachability problems. BURG deliberately uses
only the subset of that model needed by its tree-grammar interface.

## Further reading

Gabriel Hjort Blindell's book and dissertation provide broader surveys of
instruction selection:

1. [Instruction Selection: Principles, Methods, and Applications][book]
   surveys macro expansion, tree covering, DAG covering, and graph covering.
2. [Universal Instruction Selection][thesis]
   surveys the literature and presents an approach that integrates global
   instruction selection with global code motion and block ordering.

[burg]: https://doi.org/10.1145/131080.131089
[twig]: https://doi.org/10.1145/69558.75700
[iburg]: https://doi.org/10.1145/151640.151642
[simple-burs]: https://doi.org/10.1145/143095.143145
[retarget]: https://doi.org/10.1016/0096-0551%2890%2990006-B
[burs-theory]: https://doi.org/10.1145/73560.73586
[book]: https://doi.org/10.1007/978-3-319-34019-7
[thesis]: https://kth.diva-portal.org/smash/record.jsf?pid=diva2%3A1185339
