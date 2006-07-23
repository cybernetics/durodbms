/*
 * $Id$
 *
 * Copyright (C) 2004-2006 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "rdb.h"
#include "internal.h"
#include <gen/strfns.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

RDB_bool
_RDB_index_sorts(struct _RDB_tbindex *indexp, int seqitc,
        const RDB_seq_item seqitv[])
{
    int i;

    if (indexp->idxp == NULL || !RDB_index_is_ordered(indexp->idxp)
            || indexp->attrc < seqitc)
        return RDB_FALSE;

    for (i = 0; i < seqitc; i++) {
        if (strcmp(indexp->attrv[i].attrname, seqitv[i].attrname) != 0
                || indexp->attrv[i].asc != seqitv[i].asc)
            return RDB_FALSE;
    }
    return RDB_TRUE;
}

enum {
    tbpv_cap = 256
};

static RDB_bool is_and(RDB_expression *exp) {
    return (RDB_bool) exp->kind == RDB_EX_RO_OP
            && strcmp (exp->var.op.name, "AND") == 0;
}

static void
unbalance_and(RDB_expression *exp)
{
    RDB_expression *axp;

    if (!is_and(exp))
        return;

    if (is_and(exp->var.op.argv[0]))
        unbalance_and(exp->var.op.argv[0]);
        
    if (is_and(exp->var.op.argv[1])) {
        unbalance_and(exp->var.op.argv[1]);
        if (is_and(exp->var.op.argv[0])) {
            RDB_expression *ax2p;

            /* Find leftmost factor */
            axp = exp->var.op.argv[0];
            while (is_and(axp->var.op.argv[0]))
                axp = axp->var.op.argv[0];

            /* Swap leftmost factor and right child */
            ax2p = exp->var.op.argv[1];
            exp->var.op.argv[1] = axp->var.op.argv[0];
            axp->var.op.argv[0] = ax2p;
        } else {
            /* Swap children */
            axp = exp->var.op.argv[0];
            exp->var.op.argv[0] = exp->var.op.argv[1];
            exp->var.op.argv[1] = axp;
        }
    }
}

/*
 * Check if the expression covers all index attributes.
 */
static int
expr_covers_index(RDB_expression *exp, _RDB_tbindex *indexp)
{
    int i;

    for (i = 0; i < indexp->attrc
            && _RDB_attr_node(exp, indexp->attrv[i].attrname, "=") != NULL;
            i++);
    return i;
}

#ifdef REMOVED
/*
 * Check if attrv covers all index attributes.
 */
static int
attrv_covers_index(int attrc, char *attrv[], _RDB_tbindex *indexp)
{
    int i;

    /* Check if all index attributes appear in attrv */
    for (i = 0; i < indexp->attrc; i++) {
        if (RDB_find_str(attrc, attrv, indexp->attrv[i].attrname) == -1)
            return RDB_FALSE;
    }
    return RDB_TRUE;
}
#endif

static int
move_node(RDB_expression *texp, RDB_expression **dstpp, RDB_expression *nodep,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_expression *prevp;

    /*
     * Move node
     */

    /* Get previous node */
    if (nodep == texp->var.op.argv[1]) {
        prevp = NULL;
    } else {
        prevp = texp->var.op.argv[1];
        while (prevp->var.op.argv[0] != nodep)
            prevp = prevp->var.op.argv[0];
    }

    if (!is_and(nodep)) {
        if (*dstpp == NULL)
            *dstpp = nodep;
        else {
            *dstpp = RDB_ro_op_va("AND", ecp, *dstpp, nodep,
                    (RDB_expression *) NULL);
            if (*dstpp == NULL)
                return RDB_ERROR;
        }
        if (prevp == NULL) {
            texp->var.op.argv[1] = NULL;
        } else {
            if (prevp == texp->var.op.argv[1]) {
                texp->var.op.argv[1] = prevp->var.op.argv[1];
            } else {
                RDB_expression *pprevp = texp->var.op.argv[1];

                while (pprevp->var.op.argv[0] != prevp)
                    pprevp = pprevp->var.op.argv[0];
                pprevp->var.op.argv[0] = prevp->var.op.argv[1];
            }
            free(prevp->var.op.name);
            free(prevp->var.op.argv);
            free(prevp);
        }
    } else {
        if (*dstpp == NULL)
            *dstpp = nodep->var.op.argv[1];
        else {
            *dstpp = RDB_ro_op_va("AND", ecp, *dstpp, nodep->var.op.argv[1],
                    (RDB_expression *) NULL);
            if (*dstpp == NULL)
                return RDB_ERROR;
        }
        if (prevp == NULL)
            texp->var.op.argv[1] = nodep->var.op.argv[0];
        else
            prevp->var.op.argv[0] = nodep->var.op.argv[0];
        free(nodep->var.op.name);
        free(nodep->var.op.argv);
        free(nodep);
    }
    return RDB_OK;
}

/*
 * Split a WHERE expression into two: one that uses the index specified by indexp
 * (the child) and one which does not (the parent)
 * If the parent condition becomes TRUE, simply convert
 * the selection into a selection which uses the index.
 */
static int
split_by_index(RDB_expression *texp, _RDB_tbindex *indexp,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i;
    RDB_expression *nodep;
    RDB_expression *ixexp = NULL;
    RDB_expression *stopexp = NULL;
    RDB_bool asc = RDB_TRUE;
    RDB_bool all_eq = RDB_TRUE;
    int objpc = 0;
    RDB_object **objpv;

    for (i = 0; i < indexp->attrc && all_eq; i++) {
        RDB_expression *attrexp;

        if (indexp->idxp != NULL && RDB_index_is_ordered(indexp->idxp)) {
            nodep = _RDB_attr_node(texp->var.op.argv[1],
                    indexp->attrv[i].attrname, "=");
            if (nodep == NULL) {
                nodep = _RDB_attr_node(texp->var.op.argv[1],
                        indexp->attrv[i].attrname, ">=");
                if (nodep == NULL) {
                    nodep = _RDB_attr_node(texp->var.op.argv[1],
                            indexp->attrv[i].attrname, ">");
                    if (nodep == NULL) {
                        nodep = _RDB_attr_node(texp->var.op.argv[1],
                                indexp->attrv[i].attrname, "<=");
                        if (nodep == NULL) {
                            nodep = _RDB_attr_node(texp->var.op.argv[1],
                                    indexp->attrv[i].attrname, "<");
                            if (nodep == NULL)
                                break;
                        }
                    }
                }
                if (strcmp(nodep->var.op.name, ">=") == 0
                        || strcmp(nodep->var.op.name, ">") == 0) {
                    stopexp = _RDB_attr_node(texp->var.op.argv[1],
                            indexp->attrv[i].attrname, "<=");
                    if (stopexp == NULL) {
                        stopexp = _RDB_attr_node(texp->var.op.argv[1],
                                indexp->attrv[i].attrname, "<");
                    }
                    if (stopexp != NULL) {
                        attrexp = stopexp;
                        if (is_and(attrexp))
                            attrexp = attrexp->var.op.argv[1];
                        if (move_node(texp, &ixexp, stopexp, ecp, txp) != RDB_OK)
                            return RDB_ERROR;
                        stopexp = attrexp;
                    }
                }
                all_eq = RDB_FALSE;
            }
        } else {
            nodep = _RDB_attr_node(texp->var.op.argv[1],
                    indexp->attrv[i].attrname, "=");
        }
        attrexp = nodep;
        if (is_and(attrexp))
            attrexp = attrexp->var.op.argv[1];

        objpc++;

        if (indexp->idxp != NULL && RDB_index_is_ordered(indexp->idxp)) {
            if (strcmp(attrexp->var.op.name, "=") == 0
                    || strcmp(attrexp->var.op.name, ">=") == 0
                    || strcmp(attrexp->var.op.name, ">") == 0)
                asc = indexp->attrv[i].asc;
            else
                asc = (RDB_bool) !indexp->attrv[i].asc;
        }
        
        if (move_node(texp, &ixexp, nodep, ecp, txp) != RDB_OK)
            return RDB_ERROR;
    }

    if (texp->var.op.argv[0]->kind == RDB_EX_TBP) {
        objpv = _RDB_index_objpv(indexp, ixexp, texp->var.op.argv[0]->var.tbref.tbp->typ,
                objpc, all_eq, asc);
    } else {
        objpv = _RDB_index_objpv(indexp, ixexp,
                texp->var.op.argv[0]->var.op.argv[0]->var.tbref.tbp->typ,
                objpc, all_eq, asc);        
    }
    if (objpv == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }

    if (texp->var.op.argv[1] != NULL) {
        RDB_expression *sitexp;

        /*
         * Split table into two
         */
        if (ixexp == NULL) {
            ixexp = RDB_bool_to_expr(RDB_TRUE, ecp);
            if (ixexp == NULL)
                return RDB_ERROR;
        }
        sitexp = RDB_ro_op("WHERE", 2, NULL, ecp);
        if (sitexp == NULL)
            return RDB_ERROR;
        RDB_add_arg(sitexp, texp->var.op.argv[0]);
        RDB_add_arg(sitexp, ixexp);
        
        sitexp->var.op.optinfo.objpc = objpc;
        sitexp->var.op.optinfo.objpv = objpv;
        sitexp->var.op.optinfo.asc = asc;
        sitexp->var.op.optinfo.all_eq = all_eq;
        sitexp->var.op.optinfo.stopexp = stopexp;
    } else {
        /*
         * Convert table to index select
         */
        texp->var.op.argv[1] = ixexp;
        texp->var.op.optinfo.objpc = objpc;
        texp->var.op.optinfo.objpv = objpv;
        texp->var.op.optinfo.asc = asc;
        texp->var.op.optinfo.all_eq = all_eq;
        texp->var.op.optinfo.stopexp = stopexp;
    }
    return RDB_OK;
}

static unsigned
table_cost(RDB_expression *texp)
{
    _RDB_tbindex *indexp;

    if (texp->kind == RDB_EX_TBP)
        return texp->var.tbref.tbp->var.tb.stp != NULL ?
                texp->var.tbref.tbp->var.tb.stp->est_cardinality : 0;

    if (texp->kind == RDB_EX_OBJ)
        return texp->var.obj.var.tb.stp->est_cardinality;

    if (strcmp(texp->var.op.name, "SEMIMINUS") == 0)
        return table_cost(texp->var.op.argv[0]); /* !! */

    if (strcmp(texp->var.op.name, "MINUS") == 0)
        return table_cost(texp->var.op.argv[0]); /* !! */

    if (strcmp(texp->var.op.name, "UNION") == 0)
        return table_cost(texp->var.op.argv[0])
                + table_cost(texp->var.op.argv[1]);

    if (strcmp(texp->var.op.name, "SEMIJOIN") == 0)
        return table_cost(texp->var.op.argv[0]); /* !! */

    if (strcmp(texp->var.op.name, "INTERSECT") == 0)
        return table_cost(texp->var.op.argv[0]); /* !! */

    if (strcmp(texp->var.op.name, "WHERE") == 0) {
        if (texp->var.op.optinfo.objpc == 0)
            return table_cost(texp->var.op.argv[0]);
        if (texp->var.op.argv[0]->kind == RDB_EX_TBP) {
            indexp = texp->var.op.argv[0]->var.tbref.indexp;
        } else {
            indexp = texp->var.op.argv[0]->var.op.argv[0]->var.tbref.indexp;
        }
        if (indexp->idxp == NULL)
            return 1;
        if (indexp->unique)
            return 2;
        if (!RDB_index_is_ordered(indexp->idxp))
            return 3;
        return 4;
    }
    if (strcmp(texp->var.op.name, "JOIN") == 0) {
        return table_cost(texp->var.op.argv[0])
                * table_cost(texp->var.op.argv[1]);
/* !!
            if (tbp->var.join.tb2p->kind != RDB_TB_PROJECT
                    || tbp->var.join.tb2p->var.project.indexp == NULL)
                return table_cost(tbp->var.join.tb1p)
                        * table_cost(tbp->var.join.tb2p);
            indexp = tbp->var.join.tb2p->var.project.indexp;
            if (indexp->idxp == NULL)
                return table_cost(tbp->var.join.tb1p);
            if (indexp->unique)
                return table_cost(tbp->var.join.tb1p) * 2;
            if (!RDB_index_is_ordered(indexp->idxp))
                return table_cost(tbp->var.join.tb1p) * 3;
            return table_cost(tbp->var.join.tb1p) * 4;
*/
    }
    if (strcmp(texp->var.op.name, "EXTEND") == 0
             || strcmp(texp->var.op.name, "PROJECT") == 0
             || strcmp(texp->var.op.name, "REMOVE") == 0
             || strcmp(texp->var.op.name, "SUMMARIZE") == 0
             || strcmp(texp->var.op.name, "RENAME") == 0
             || strcmp(texp->var.op.name, "WRAP") == 0
             || strcmp(texp->var.op.name, "UNWRAP") == 0
             || strcmp(texp->var.op.name, "GROUP") == 0
             || strcmp(texp->var.op.name, "UNGROUP") == 0)
        return table_cost(texp->var.op.argv[0]);
    if (strcmp(texp->var.op.name, "DIVIDE_BY_PER") == 0) {
        return table_cost(texp->var.op.argv[0])
                * table_cost(texp->var.op.argv[1]); /* !! */
    }
    abort();
}

static int
mutate(RDB_expression *texp, RDB_expression **tbpv, int cap, RDB_exec_context *,
        RDB_transaction *);

#ifdef REMOVED
/*
 * Add "null project" parent
 */
static RDB_object *
null_project(RDB_object *tbp, RDB_exec_context *ecp)
{
    RDB_object *ptbp = _RDB_new_table(ecp);
    if (tbp == NULL)
        return NULL;

    ptbp->is_user = RDB_TRUE;
    ptbp->is_persistent = RDB_FALSE;
    ptbp->kind = RDB_TB_PROJECT;
    ptbp->keyv = NULL;
    ptbp->var.project.tbp = tbp;
    ptbp->var.project.indexp = NULL;
    ptbp->var.project.keyloss = RDB_FALSE;

    /* Create type */
    ptbp->typ = RDB_create_relation_type(
            tbp->typ->var.basetyp->var.tuple.attrc,
            tbp->typ->var.basetyp->var.tuple.attrv, ecp);
    if (ptbp->typ == NULL) {
        free(ptbp);
        return NULL;
    }
    return ptbp;
}
#endif

static int
mutate_where(RDB_expression *texp, RDB_expression **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i;
    int tbc;
    RDB_expression *chexp = texp->var.op.argv[0];

    if (_RDB_transform(chexp, ecp, txp) != RDB_OK)
        return RDB_ERROR;

    if (chexp->kind == RDB_EX_TBP
        || (chexp->kind == RDB_EX_RO_OP
            && strcmp(chexp->var.op.name, "PROJECT") == 0
            && chexp->var.op.argv[0]->kind == RDB_EX_TBP)) {
        /* Convert condition into 'unbalanced' form */
        unbalance_and(texp->var.op.argv[1]);
    }

    tbc = mutate(chexp, tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;

    for (i = 0; i < tbc; i++) {
        RDB_expression *nexp;
        RDB_expression *exp = RDB_dup_expr(texp->var.op.argv[1], ecp);
        if (exp == NULL)
            return RDB_ERROR;

        nexp = RDB_ro_op("WHERE", 2, NULL, ecp);
        if (nexp == NULL) {
            RDB_drop_expr(exp, ecp);
            return RDB_ERROR;
        }
        RDB_add_arg(nexp, tbpv[i]);
        RDB_add_arg(nexp, exp);

        if (tbpv[i]->kind == RDB_EX_TBP
                && tbpv[i]->var.tbref.indexp != NULL)
        {
            _RDB_tbindex *indexp = tbpv[i]->var.tbref.indexp;
            if ((indexp->idxp != NULL && RDB_index_is_ordered(indexp->idxp))
                    || expr_covers_index(exp, indexp)
                            == indexp->attrc) {
                if (split_by_index(nexp, indexp, ecp, txp) != RDB_OK)
                    return RDB_ERROR;
            }
        } else if (tbpv[i]->kind == RDB_EX_RO_OP
                && strcmp(tbpv[i]->var.op.name, "PROJECT") == 0
                && tbpv[i]->var.op.argv[0]->kind == RDB_EX_TBP
                && tbpv[i]->var.op.argv[0]->var.tbref.indexp != NULL) {
            _RDB_tbindex *indexp = tbpv[i]->var.op.argv[0]->var.tbref.indexp;
            if ((indexp->idxp != NULL && RDB_index_is_ordered(indexp->idxp))
                    || expr_covers_index(exp, indexp)
                            == indexp->attrc) {
                if (split_by_index(nexp, indexp, ecp, txp) != RDB_OK)
                    return RDB_ERROR;
            }
        }
        tbpv[i] = nexp;
    }
    return tbc;
}

/*
 * Copy expression, making a copy of virtual tables
 */
static RDB_expression *
dup_expr_vt(const RDB_expression *exp, RDB_exec_context *ecp)
{
    RDB_expression *newexp;

    switch (exp->kind) {
        case RDB_EX_TUPLE_ATTR:
            newexp = dup_expr_vt(exp->var.op.argv[0], ecp);
            if (newexp == NULL)
                return NULL;
            return RDB_tuple_attr(newexp, exp->var.op.name, ecp);
        case RDB_EX_GET_COMP:
            newexp = dup_expr_vt(exp->var.op.argv[0], ecp);
            if (newexp == NULL)
                return NULL;
            return RDB_expr_comp(newexp, exp->var.op.name, ecp);
        case RDB_EX_RO_OP:
        {
            int i;
            RDB_expression **argexpv = (RDB_expression **)
                    malloc(sizeof (RDB_expression *) * exp->var.op.argc);

            if (argexpv == NULL)
                return NULL;

            for (i = 0; i < exp->var.op.argc; i++) {
                argexpv[i] = dup_expr_vt(exp->var.op.argv[i], ecp);
                if (argexpv[i] == NULL)
                    return NULL;
            }
            newexp = RDB_ro_op(exp->var.op.name, exp->var.op.argc, argexpv,
                    ecp);
            free(argexpv);
            return newexp;
        }
        case RDB_EX_OBJ:
            return RDB_obj_to_expr(&exp->var.obj, ecp);
        case RDB_EX_TBP:
            if (exp->var.tbref.tbp->var.tb.exp == NULL)
                return RDB_table_ref_to_expr(exp->var.tbref.tbp, ecp);
            return dup_expr_vt(exp->var.tbref.tbp->var.tb.exp, ecp);
        case RDB_EX_VAR:
            return RDB_expr_var(exp->var.varname, ecp);
    }
    abort();
}

static int
mutate_vt(RDB_expression *texp, int nargc, RDB_expression **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i, j, k;
    int ntbc;
    int otbc = 0;

    for (j = 0; j < nargc; j++) {
        ntbc = mutate(texp->var.op.argv[j], &tbpv[otbc], cap - otbc, ecp, txp);
        if (ntbc < 0)
            return ntbc;
        for (i = otbc; i < otbc + ntbc; i++) {
            RDB_expression *nexp = RDB_ro_op(texp->var.op.name,
                    texp->var.op.argc, NULL, ecp);
            if (nexp == NULL)
                return RDB_ERROR;

            for (k = 0; k < texp->var.op.argc; k++) {
                if (k == j) {
                    RDB_add_arg(nexp, tbpv[i]);
                } else {
                    RDB_expression *otexp = dup_expr_vt(texp->var.op.argv[k], ecp);
                    if (otexp == NULL)
                        return RDB_ERROR;
                    RDB_add_arg(nexp, otexp);
                }
            }
            tbpv[i] = nexp;
        }
        otbc += ntbc;
    }
    return otbc;
}

static int
mutate_full_vt(RDB_expression *texp, RDB_expression **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    return mutate_vt(texp, texp->var.op.argc, tbpv, cap, ecp, txp);
}

static void
table_to_empty(RDB_expression *exp, RDB_exec_context *ecp)
{
    /* !!
    switch (tbp->kind) {
        case RDB_TB_SELECT:
            RDB_drop_expr(tbp->var.select.exp, ecp);
            if (!tbp->var.select.tbp->is_persistent) {
                RDB_drop_table(tbp->var.select.tbp, ecp, NULL);
            }
            tbp->kind = RDB_TB_REAL;
            break;
        case RDB_TB_SEMIMINUS:
            if (!tbp->var.semiminus.tb1p->is_persistent) {
                RDB_drop_table(tbp->var.semiminus.tb1p, ecp, NULL);
            }
            if (!tbp->var.semiminus.tb2p->is_persistent) {
                RDB_drop_table(tbp->var.semiminus.tb2p, ecp, NULL);
            }
            tbp->kind = RDB_TB_REAL;
            break;
        default: ;
    }
    */
}

static int
replace_empty(RDB_expression *exp, RDB_exec_context *ecp, RDB_transaction *txp)
{
	int ret;
    struct _RDB_tx_and_ec te;

    te.txp = txp;
    te.ecp = ecp;

    /* Check if there is a constraint that says the table is empty */
    if (txp->dbp != NULL
            && RDB_hashtable_get(&txp->dbp->dbrootp->empty_tbtab,
                    exp, &te) != NULL) {
            table_to_empty(exp, ecp);
    } else if (exp->kind == RDB_EX_RO_OP) {
    	int i;
    	
    	for (i = 0; i < exp->var.op.argc; i++) {
            ret = replace_empty(exp->var.op.argv[0], ecp, txp);
            if (ret != RDB_OK)
                return ret;
    	}
    }
    return RDB_OK;
}

#ifdef REMOVED
static int
transform_semiminus_union(RDB_object *tbp, RDB_exec_context *ecp,
        RDB_transaction *txp, RDB_object **restbpp)
{
    int ret;
    RDB_object *tb1p, *tb2p, *tb3p, *tb4p;

    tb1p = _RDB_dup_vtable(tbp->var.semiminus.tb1p->var._union.tb1p, ecp);
    if (tb1p == NULL) {
        return RDB_ERROR;
    }

    tb2p = _RDB_dup_vtable(tbp->var.semiminus.tb2p, ecp);
    if (tb2p == NULL) {
        RDB_drop_table (tb1p, ecp, NULL);
        return RDB_ERROR;
    }

    tb3p = RDB_semiminus(tb1p, tb2p, ecp);
    if (tb3p == NULL) {
        RDB_drop_table(tb1p, ecp, NULL);
        RDB_drop_table(tb2p, ecp, NULL);
        return RDB_ERROR;
    }

    tb1p = _RDB_dup_vtable(tbp->var.semiminus.tb1p->var._union.tb2p, ecp);
    if (tb1p == NULL) {
        RDB_drop_table(tb3p, ecp, NULL);
        return RDB_ERROR;
    }
    tb2p = _RDB_dup_vtable(tbp->var.semiminus.tb2p, ecp);
    if (tb2p == NULL) {
        RDB_drop_table(tb1p, ecp, NULL);
        RDB_drop_table(tb3p, ecp, NULL);
        return RDB_ERROR;
    }

    tb4p = RDB_semiminus(tb1p, tb2p, ecp);
    if (tb4p == NULL) {
        RDB_drop_table(tb1p, ecp, NULL);
        RDB_drop_table(tb2p, ecp, NULL);
        RDB_drop_table(tb3p, ecp, NULL);        
        return RDB_ERROR;
    }

    *restbpp = RDB_union(tb3p, tb4p, ecp);
    if (*restbpp == NULL) {
        RDB_drop_table(tb3p, ecp, NULL);
        RDB_drop_table(tb4p, ecp, NULL);
        return RDB_ERROR;
    }
    ret = replace_empty(*restbpp, ecp, txp);
    if (ret != RDB_OK) {
        RDB_drop_table(*restbpp, ecp, NULL);
        return ret;
    }
    return RDB_OK;
}

static int
mutate_semiminus(RDB_expression *texp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i;
    int ret;

    tbc = mutate(tbp->var.semiminus.tb1p, tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;
    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;
        RDB_object *otbp = _RDB_dup_vtable(tbp->var.semiminus.tb2p, ecp);
        if (otbp == NULL)
            return RDB_ERROR;

        ntbp = RDB_semiminus(tbpv[i], otbp, ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    if (tbc < cap && tbp->var.semiminus.tb1p->kind == RDB_TB_UNION) {
        ret = transform_semiminus_union(tbp, ecp, txp, &tbpv[tbc]);
        if (ret != RDB_OK)
            return RDB_ERROR;
        tbc++;
        /* !! mutate_union */
    }
    return tbc;
}

static int
mutate_semijoin(RDB_expression *texp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc1, tbc2;
    int i;

    tbc1 = mutate(tbp->var.semijoin.tb1p, tbpv, cap, ecp, txp);
    if (tbc1 < 0)
        return tbc1;
    for (i = 0; i < tbc1; i++) {
        RDB_object *ntbp;
        RDB_object *otbp = _RDB_dup_vtable(tbp->var.semijoin.tb2p, ecp);
        if (otbp == NULL)
            return RDB_ERROR;

        ntbp = RDB_semijoin(tbpv[i], otbp, ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    tbc2 = mutate(tbp->var.semijoin.tb2p, &tbpv[tbc1], cap - tbc1, ecp, txp);
    if (tbc2 < 0)
        return tbc2;
    for (i = tbc1; i < tbc1 + tbc2; i++) {
        RDB_object *ntbp;
        RDB_object *otbp = _RDB_dup_vtable(tbp->var.semijoin.tb1p, ecp);
        if (otbp == NULL)
            return RDB_ERROR;

        ntbp = RDB_semijoin(otbp, tbpv[i], ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    return tbc1 + tbc2;
}

static int
mutate_rename(RDB_object *tbp, RDB_object **tbpv, int cap, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int tbc;
    int i;

    tbc = mutate(tbp->var.rename.tbp, tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;
    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;

        ntbp = RDB_rename(tbpv[i], tbp->var.rename.renc, tbp->var.rename.renv,
                ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    return tbc;
}

static int
mutate_extend(RDB_expression *texp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i, j;
    RDB_virtual_attr *extv;

    tbc = mutate(tbp->var.op.argv[0], tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;

    extv = malloc(sizeof (RDB_virtual_attr)
            * tbp->var.extend.attrc);
    if (extv == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }
    for (i = 0; i < tbp->var.extend.attrc; i++)
        extv[i].name = tbp->var.extend.attrv[i].name;

    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;

        for (j = 0; j < tbp->var.extend.attrc; j++) {
            extv[j].exp = RDB_dup_expr(tbp->var.extend.attrv[j].exp, ecp);
            if (extv[j].exp == NULL)
                return RDB_ERROR;
        }
        ntbp = _RDB_extend(tbpv[i], tbp->var.extend.attrc, extv, ecp, txp);
        if (ntbp == NULL) {
            for (j = 0; j < tbp->var.extend.attrc; j++)
                RDB_drop_expr(extv[j].exp, ecp);
            free(extv);
            return RDB_ERROR;
        }
        tbpv[i] = ntbp;
    }
    free(extv);
    return tbc;
}

static int
mutate_summarize(RDB_expression *texp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i, j;
    RDB_summarize_add *addv;

    tbc = mutate(tbp->var.op.argv[0], tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;

    addv = malloc(sizeof (RDB_summarize_add) * tbp->var.summarize.addc);
    if (addv == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }
    for (i = 0; i < tbp->var.summarize.addc; i++) {
        addv[i].op = tbp->var.summarize.addv[i].op;
        addv[i].name = tbp->var.summarize.addv[i].name;
    }

    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;
        RDB_object *otbp = _RDB_dup_vtable(tbp->var.summarize.tb2p, ecp);
        if (otbp == NULL) {
            free(addv);
            return RDB_ERROR;
        }

        for (j = 0; j < tbp->var.summarize.addc; j++) {
            if (tbp->var.summarize.addv[j].exp != NULL) {
                addv[j].exp = RDB_dup_expr(tbp->var.summarize.addv[j].exp, ecp);
                if (addv[j].exp == NULL)
                    return RDB_ERROR;
            } else {
                addv[j].exp = NULL;
            }
        }

        ntbp = RDB_summarize(tbpv[i], otbp, tbp->var.summarize.addc, addv,
                ecp, txp);
        if (ntbp == NULL) {
            for (j = 0; j < tbp->var.summarize.addc; j++)
                RDB_drop_expr(addv[j].exp, ecp);
            free(addv);
            return RDB_ERROR;
        }
        tbpv[i] = ntbp;
    }
    free(addv);
    return tbc;
}

static int
mutate_project(RDB_expression *texp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i;
    char **namev;

    if (tbp->var.project.tbp->kind == RDB_TB_REAL
            && tbp->var.project.tbp->stp != NULL
            && tbp->var.project.tbp->stp->indexc > 0) {
        tbc = tbp->var.project.tbp->stp->indexc;
        for (i = 0; i < tbc; i++) {
            _RDB_tbindex *indexp = &tbp->var.project.tbp->stp->indexv[i];
            RDB_object *ptbp = _RDB_dup_vtable(tbp, ecp);
            if (ptbp == NULL)
                return RDB_ERROR;
            ptbp->var.project.indexp = indexp;
            tbpv[i] = ptbp;
        }
    } else {            
        tbc = mutate(texp->var.op.argv[0], tbpv, cap, ecp, txp);
        if (tbc < 0)
            return tbc;

        namev = malloc(sizeof (char *) * tbp->typ->var.basetyp->var.tuple.attrc);
        if (namev == NULL) {
            RDB_raise_no_memory(ecp);
            return RDB_ERROR;
        }
        for (i = 0; i < tbp->typ->var.basetyp->var.tuple.attrc; i++)
            namev[i] = tbp->typ->var.basetyp->var.tuple.attrv[i].name;

        for (i = 0; i < tbc; i++) {
            RDB_object *ntbp;

            ntbp = RDB_project(tbpv[i], tbp->typ->var.basetyp->var.tuple.attrc,
                    namev, ecp);
            if (ntbp == NULL) {
                free(namev);
                return RDB_ERROR;
            }
            tbpv[i] = ntbp;
        }
        free(namev);
    }
    return tbc;
}

static int
common_attrs(RDB_type *tpltyp1, RDB_type *tpltyp2, char **cmattrv)
{
    int i, j;
    int cattrc = 0;

    for (i = 0; i < tpltyp1->var.tuple.attrc; i++) {
        for (j = 0;
             j < tpltyp2->var.tuple.attrc
                     && strcmp(tpltyp1->var.tuple.attrv[i].name,
                     tpltyp2->var.tuple.attrv[j].name) != 0;
             j++);
        if (j < tpltyp2->var.tuple.attrc)
            cmattrv[cattrc++] = tpltyp1->var.tuple.attrv[i].name;
    }
    return cattrc;
}

static int
mutate_join(RDB_expression *texp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc1, tbc2, tbc;
    int i;
    int cmattrc;
    char **cmattrv;

    cmattrv = malloc(sizeof(char *)
            * tbp->var.join.tb1p->typ->var.basetyp->var.tuple.attrc);
    if (cmattrv == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }
    cmattrc = common_attrs(tbp->var.join.tb1p->typ->var.basetyp,
            tbp->var.join.tb2p->typ->var.basetyp, cmattrv);

    tbc1 = mutate(texp->var.op.argv[0], tbpv, cap, ecp, txp);
    if (tbc1 < 0) {
        free(cmattrv);
        return tbc1;
    }
    tbc = 0;
    for (i = 0; i < tbc1; i++) {
        RDB_object *ntbp;

        /*
         * Take project with index only if the index covers the
         * common attributes
         */
        if (tbpv[i]->kind != RDB_TB_PROJECT
                || tbpv[i]->var.project.indexp == NULL
                || attrv_covers_index(cmattrc, cmattrv,
                        tbpv[i]->var.project.indexp)) {
            RDB_object *otbp = _RDB_dup_vtable(tbp->var.join.tb2p, ecp);
            if (otbp == NULL) {
                free(cmattrv);
                return RDB_ERROR;
            }

            ntbp = RDB_join(otbp, tbpv[i], ecp);
            if (ntbp == NULL) {
                free(cmattrv);
                return RDB_ERROR;
            }
            tbpv[tbc++] = ntbp;
        }
    }
    tbc1 = tbc;
    tbc2 = mutate(tbp->var.op.argv[1], &tbpv[tbc1], cap - tbc1, ecp, txp);
    if (tbc2 < 0) {
        free(cmattrv);
        return tbc2;
    }
    tbc = 0;
    for (i = tbc1; i < tbc1 + tbc2; i++) {
        RDB_object *ntbp;

        if (tbpv[i]->kind != RDB_TB_PROJECT
                || tbpv[i]->var.project.indexp == NULL
                || attrv_covers_index(cmattrc, cmattrv,
                        tbpv[i]->var.project.indexp)) {
            RDB_object *otbp = _RDB_dup_vtable(tbp->var.join.tb1p, ecp);
            if (otbp == NULL) {
                free(cmattrv);
                return RDB_ERROR;
            }
            ntbp = RDB_join(otbp, tbpv[i], ecp);
            if (ntbp == NULL) {
                free(cmattrv);
                return RDB_ERROR;
            }
            tbpv[tbc1 + tbc++] = ntbp;
        }
    }
    tbc = tbc1 + tbc;

    free(cmattrv);
    return tbc;
}

static int
mutate_wrap(RDB_object *tbp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i;

    tbc = mutate(tbp->var.wrap.tbp, tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;
    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;

        ntbp = RDB_wrap(tbpv[i], tbp->var.wrap.wrapc, tbp->var.wrap.wrapv, ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    return tbc;
}

static int
mutate_unwrap(RDB_object *tbp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i;

    tbc = mutate(tbp->var.unwrap.tbp, tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;
    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;

        ntbp = RDB_unwrap(tbpv[i], tbp->var.unwrap.attrc, tbp->var.unwrap.attrv,
                ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    return tbc;
}

static int
mutate_group(RDB_object *tbp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i;

    tbc = mutate(tbp->var.group.tbp, tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;
    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;

        ntbp = RDB_group(tbpv[i], tbp->var.group.attrc, tbp->var.group.attrv,
                tbp->var.group.gattr, ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    return tbc;
}

static int
mutate_ungroup(RDB_object *tbp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc;
    int i;

    tbc = mutate(tbp->var.ungroup.tbp, tbpv, cap, ecp, txp);
    if (tbc < 0)
        return tbc;
    for (i = 0; i < tbc; i++) {
        RDB_object *ntbp;

        ntbp = RDB_ungroup(tbpv[i], tbp->var.ungroup.attr, ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    return tbc;
}

static int
mutate_sdivide(RDB_object *tbp, RDB_object **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int tbc1, tbc2;
    int i;

    tbc1 = mutate(tbp->var.sdivide.tb1p, tbpv, cap, ecp, txp);
    if (tbc1 < 0)
        return tbc1;
    for (i = 0; i < tbc1; i++) {
        RDB_object *ntbp;
        RDB_object *otb1p = _RDB_dup_vtable(tbp->var.sdivide.tb2p, ecp);
        RDB_object *otb2p = _RDB_dup_vtable(tbp->var.sdivide.tb3p, ecp);
        if (otb1p == NULL || otb2p == NULL)
            return RDB_ERROR;

        ntbp = RDB_sdivide(tbpv[i], otb1p, otb2p, ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    tbc2 = mutate(tbp->var.sdivide.tb2p, &tbpv[tbc1], cap - tbc1, ecp, txp);
    if (tbc2 < 0)
        return tbc2;
    for (i = tbc1; i < tbc1 + tbc2; i++) {
        RDB_object *ntbp;
        RDB_object *otb1p = _RDB_dup_vtable(tbp->var.sdivide.tb1p, ecp);
        RDB_object *otb2p = _RDB_dup_vtable(tbp->var.sdivide.tb3p, ecp);
        if (otb1p == NULL || otb2p == NULL)
            return RDB_ERROR;

        ntbp = RDB_sdivide(otb1p, tbpv[i], otb2p, ecp);
        if (ntbp == NULL)
            return RDB_ERROR;
        tbpv[i] = ntbp;
    }
    return tbc1 + tbc2;
}
#endif

static int
mutate_tbref(RDB_expression *texp, RDB_expression **tbpv, int cap,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (texp->var.tbref.tbp->kind == RDB_OB_TABLE
            && texp->var.tbref.tbp->var.tb.stp != NULL
            && texp->var.tbref.tbp->var.tb.stp->indexc > 0) {
        int i;
        int tbc = texp->var.tbref.tbp->var.tb.stp->indexc;
        if (tbc > cap)
            tbc = cap;

        for (i = 0; i < tbc; i++) {
            _RDB_tbindex *indexp = &texp->var.tbref.tbp->var.tb.stp->indexv[i];
            RDB_expression *tiexp = RDB_table_ref_to_expr(
                    texp->var.tbref.tbp, ecp);
            if (tiexp == NULL)
                return RDB_ERROR;
            tiexp->var.tbref.indexp = indexp;
            tbpv[i] = tiexp;
        }
        return tbc;
    } else {
        return 0;
    }
}

static int
mutate(RDB_expression *texp, RDB_expression **tbpv, int cap, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    if (texp->kind == RDB_EX_TBP) {
        return mutate_tbref(texp, tbpv, cap, ecp, txp);
    }
    
    if (texp->kind != RDB_EX_RO_OP)
        return 0;

    if (strcmp(texp->var.op.name, "WHERE") == 0) {
        return mutate_where(texp, tbpv, cap, ecp, txp);
    }

    if (strcmp(texp->var.op.name, "UNION") == 0
            || strcmp(texp->var.op.name, "MINUS") == 0
            || strcmp(texp->var.op.name, "SEMIMINUS") == 0
            || strcmp(texp->var.op.name, "INTERSECT") == 0
            || strcmp(texp->var.op.name, "SEMIJOIN") == 0
            || strcmp(texp->var.op.name, "JOIN") == 0) {
        return mutate_full_vt(texp, tbpv, cap, ecp, txp);
    }
    if (strcmp(texp->var.op.name, "EXTEND") == 0
            || strcmp(texp->var.op.name, "PROJECT") == 0
            || strcmp(texp->var.op.name, "REMOVE") == 0
            || strcmp(texp->var.op.name, "RENAME") == 0
            || strcmp(texp->var.op.name, "SUMMARIZE") == 0
            || strcmp(texp->var.op.name, "WRAP") == 0
            || strcmp(texp->var.op.name, "UNWRAP") == 0
            || strcmp(texp->var.op.name, "GROUP") == 0
            || strcmp(texp->var.op.name, "UNGROUP") == 0) {
        return mutate_vt(texp, 1, tbpv, cap, ecp, txp);
    }
    if (strcmp(texp->var.op.name, "DIVIDE_BY_PER") == 0) {
        return mutate_vt(texp, 2, tbpv, cap, ecp, txp);
    }
#ifdef REMOVED    
    switch (tbp->kind) {
        case RDB_TB_REAL:
            return 0;
        case RDB_TB_UNION:
            return mutate_union(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_SEMIMINUS:
            return mutate_semiminus(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_SEMIJOIN:
            return mutate_semijoin(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_SELECT:
            /* Select over index is not further optimized */
            if (tbp->var.select.tbp->kind != RDB_TB_PROJECT
                    || tbp->var.select.tbp->var.project.indexp == NULL)
                return mutate_select(tbp, tbpv, cap, ecp, txp);
            return 0;
        case RDB_TB_JOIN:
            return mutate_join(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_EXTEND:
            return mutate_extend(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_PROJECT:
            return mutate_project(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_SUMMARIZE:
            return mutate_summarize(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_RENAME:
            return mutate_rename(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_WRAP:
            return mutate_wrap(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_UNWRAP:
            return mutate_unwrap(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_GROUP:
            return mutate_group(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_UNGROUP:
            return mutate_ungroup(tbp, tbpv, cap, ecp, txp);
        case RDB_TB_SDIVIDE:
            return mutate_sdivide(tbp, tbpv, cap, ecp, txp);
    }
#endif
    return 0;
}

/*
 * Estimate cost for reading all tuples of the table in the order
 * specified by seqitc/seqitv.
 */
static unsigned
sorted_table_cost(RDB_expression *texp, int seqitc,
        const RDB_seq_item seqitv[])
{
    int cost = table_cost(texp);

#ifdef REMOVED
    /* Check if the index must be sorted */
    if (seqitc > 0) {
        _RDB_tbindex *indexp = _RDB_sortindex(tbp);
        if (indexp == NULL || !_RDB_index_sorts(indexp, seqitc, seqitv))
        {
            int scost = (((double) cost) /* !! * log10(cost) */ / 7);

            if (scost == 0)
                scost = 1;
            cost += scost;
        }
    }
#endif

    return cost;
}

#ifdef REMOVED
static int
add_project(RDB_object *tbp, RDB_exec_context *ecp)
{
    RDB_object *ptbp;
    int ret;

    switch (tbp->kind) {
        case RDB_TB_REAL:
            break;
        case RDB_TB_SEMIMINUS:
            if (tbp->var.semiminus.tb1p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.semiminus.tb1p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.semiminus.tb1p = ptbp;
            } else {
                ret = add_project(tbp->var.semiminus.tb1p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            if (tbp->var.semiminus.tb2p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.semiminus.tb2p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.semiminus.tb2p = ptbp;
            } else {
                ret = add_project(tbp->var.semiminus.tb2p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_UNION:
            if (tbp->var._union.tb1p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var._union.tb1p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var._union.tb1p = ptbp;
            } else {
                ret = add_project(tbp->var._union.tb1p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            if (tbp->var._union.tb2p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var._union.tb2p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var._union.tb2p = ptbp;
            } else {
                ret = add_project(tbp->var._union.tb2p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_SEMIJOIN:
            if (tbp->var.semijoin.tb1p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.semijoin.tb1p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.semijoin.tb1p = ptbp;
            } else {
                ret = add_project(tbp->var.semijoin.tb1p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            if (tbp->var.semijoin.tb2p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.semijoin.tb2p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.semijoin.tb2p = ptbp;
            } else {
                ret = add_project(tbp->var.semijoin.tb2p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_SELECT:
            if (tbp->var.select.tbp->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.select.tbp, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.select.tbp = ptbp;
            } else {
                ret = add_project(tbp->var.select.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_JOIN:
            if (tbp->var.join.tb1p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.join.tb1p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.join.tb1p = ptbp;
            } else {
                ret = add_project(tbp->var.join.tb1p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            if (tbp->var.join.tb2p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.join.tb2p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.join.tb2p = ptbp;
            } else {
                ret = add_project(tbp->var.join.tb2p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_EXTEND:
            if (tbp->var.extend.tbp->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.extend.tbp, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.extend.tbp = ptbp;
            } else {
                ret = add_project(tbp->var.extend.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_PROJECT:
            if (tbp->var.project.tbp->kind != RDB_TB_REAL) {
                ret = add_project(tbp->var.project.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_SUMMARIZE:
            if (tbp->var.summarize.tb1p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.summarize.tb1p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.summarize.tb1p = ptbp;
            } else {
                ret = add_project(tbp->var.summarize.tb1p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_RENAME:
            if (tbp->var.rename.tbp->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.rename.tbp, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.rename.tbp = ptbp;
            } else {
                ret = add_project(tbp->var.rename.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_WRAP:
            if (tbp->var.wrap.tbp->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.wrap.tbp, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.wrap.tbp = ptbp;
            } else {
                ret = add_project(tbp->var.wrap.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_UNWRAP:
            if (tbp->var.unwrap.tbp->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.unwrap.tbp, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.unwrap.tbp = ptbp;
            } else {
                ret = add_project(tbp->var.unwrap.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_GROUP:
            if (tbp->var.group.tbp->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.group.tbp, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.group.tbp = ptbp;
            } else {
                ret = add_project(tbp->var.group.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_UNGROUP:
            if (tbp->var.ungroup.tbp->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.ungroup.tbp, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.ungroup.tbp = ptbp;
            } else {
                ret = add_project(tbp->var.ungroup.tbp, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_SDIVIDE:
            if (tbp->var.sdivide.tb1p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.sdivide.tb1p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.sdivide.tb1p = ptbp;
            } else {
                ret = add_project(tbp->var.sdivide.tb1p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            if (tbp->var.sdivide.tb2p->kind == RDB_TB_REAL) {
                ptbp = null_project(tbp->var.sdivide.tb2p, ecp);
                if (ptbp == NULL)
                    return RDB_ERROR;
                tbp->var.sdivide.tb2p = ptbp;
            } else {
                ret = add_project(tbp->var.sdivide.tb2p, ecp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
    }
    return RDB_OK;
}
#endif

int
_RDB_optimize(RDB_object *tbp, int seqitc, const RDB_seq_item seqitv[],
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object **ntbpp)
{
    int i;
    RDB_expression *nexp;

    if (tbp->var.tb.exp == NULL) {
        if (seqitc > 0 && tbp->var.tb.stp != NULL) {
            /*
             * Check if an index can be used for sorting
             */

            for (i = 0; i < tbp->var.tb.stp->indexc
                    && !_RDB_index_sorts(&tbp->var.tb.stp->indexv[i],
                            seqitc, seqitv);
                    i++);
            /* If yes, create reference */
            if (i < tbp->var.tb.stp->indexc) {
                nexp = RDB_table_ref_to_expr(tbp, ecp);
                if (nexp == NULL)
                    return RDB_ERROR;
                nexp->var.tbref.indexp = &tbp->var.tb.stp->indexv[i];
                *ntbpp = RDB_expr_to_vtable(nexp, ecp, txp);
                if (*ntbpp == NULL)
                    return RDB_ERROR;
                return RDB_OK;
            }
        }
        *ntbpp = tbp;
    } else {
        nexp = _RDB_optimize_expr(tbp->var.tb.exp,
                seqitc, seqitv, ecp, txp);
        if (nexp == NULL)
            return RDB_ERROR;
        *ntbpp = RDB_expr_to_vtable(nexp, ecp, txp);
        if (*ntbpp == NULL)
            return RDB_ERROR;
    }

    return RDB_OK;
}

RDB_expression *
_RDB_optimize_expr(RDB_expression *texp, int seqitc, const RDB_seq_item seqitv[],
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i;
    RDB_expression *nexp;
    unsigned obestcost, bestcost;
    int bestn;
    int tbc;
    RDB_expression *texpv[tbpv_cap];

    /*
     * Make a copy of the table, so it can be transformed freely
     */
    nexp = dup_expr_vt(texp, ecp);
    if (nexp == NULL)
        return NULL;

    /*
     * Algebraic optimization
     */

    if (_RDB_transform(nexp, ecp, txp) != RDB_OK)
        return NULL;

    /*
     * Replace tables which are declared to be empty
     * by a constraint
     */
    if (RDB_tx_db(txp) != NULL) {
        if (replace_empty(nexp, ecp, txp) != RDB_OK)
            return NULL;
    }

    /*
     * Add a project table above real tables
     * to prepare for indexes
     */

#ifdef REMOVED
        ret = add_project(nexp, ecp);
        if (ret != RDB_OK)
            return ret;
#endif

    /*
     * Try to find cheapest table
     */

    bestcost = sorted_table_cost(nexp, seqitc, seqitv);
    do {
        obestcost = bestcost;

        tbc = mutate(nexp, texpv, tbpv_cap, ecp, txp);
        if (tbc < 0)
            return NULL;

        bestn = -1;

        for (i = 0; i < tbc; i++) {
            int cost = sorted_table_cost(texpv[i], seqitc, seqitv);

            if (cost < bestcost) {
                bestcost = cost;
                bestn = i;
            }
        }
        if (bestn == -1) {
            for (i = 0; i < tbc; i++)
                RDB_drop_expr(texpv[i], ecp);
        } else {
            RDB_drop_expr(nexp, ecp);
            nexp = texpv[bestn];
            for (i = 0; i < tbc; i++) {
                if (i != bestn)
                    RDB_drop_expr(texpv[i], ecp);
            }
        }
    } while (bestcost < obestcost);
    return nexp;
}
