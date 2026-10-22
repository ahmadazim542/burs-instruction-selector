char rcsid_burs[] = "$Id$";

#include "b.h"

Item_Set errorState;

static void doLeaf(void *);

static void
doLeaf(void *x)
{
	Operator leaf = (Operator) x;
	int new;
	List pl;
	Item_Set ts;
	Item_Set tmp;

	assert(leaf->arity == 0);

	ts = newItem_Set(leaf->table->relevant);

	for (pl = leaf->table->rules; pl; pl = pl->next) {
		Rule p = (Rule) pl->x;
		Item *item = &ts->virgin[p->lhs->num];

		if (!item->rule || LESSCOST(p->delta, item->delta)) {
			item->rule = p;
			ASSIGNCOST(item->delta, p->delta);
			ts->op = leaf;
		}
	}
	trim(ts);
	zero(ts);
	tmp = encode(globalMap, ts, &new);
	if (new) {
		closure(ts);
		leaf->table->transition[0] = ts;
		addQ(globalQ, ts);
	} else {
		leaf->table->transition[0] = tmp;
		freeItem_Set(ts);
	}
}

void
build()
{
	int new;
	List ol;
	Item_Set ts;

	globalQ = newQ();
	globalMap = newMapping(GLOBAL_MAP_SIZE);

	ts = newItem_Set(0);
	errorState = encode(globalMap, ts, &new);
	ts->closed = ts->virgin;
	addQ(globalQ, ts);

	foreachList(doLeaf, leaves);

	debug(debugTables, printf("---initial set of states ---\n"));
	debug(debugTables, dumpMapping(globalMap));
	debug(debugTables, foreachList(dumpItem_Set, globalQ->head));
	
	for (ts = popQ(globalQ); ts; ts = popQ(globalQ)) {
		for (ol = operators; ol; ol = ol->next) {
			Operator op = (Operator) ol->x;
			addToTable(op->table, ts);
		}
	}
}
