/*
 * $Id$
 *
 * Copyright (C) 2005-2012 Rene Hartmann.
 * See the file COPYING for redistribution information.
 *
 *
 * Functions for declarative integrity constraints
 */

#include "rdb.h"
#include "transform.h"
#include "internal.h"
#include "catalog.h"
#include "serialize.h"
#include <gen/strfns.h>
#include <obj/objinternal.h>

#include <string.h>

static int
optimize_constr_expr(RDB_expression *exp, RDB_exec_context *ecp)
{
    if (RDB_expr_is_op(exp, "subset_of") && exp->def.op.args.firstp != NULL
            && exp->def.op.args.firstp->nextp != NULL
            && exp->def.op.args.firstp->nextp->nextp == NULL) {
        /* Convert T1 subset_of T2 to is_empty(T1 minus T2) */
        RDB_expression *minusexp;
        char *isenamp = RDB_dup_str("is_empty");
        if (isenamp == NULL) {
            RDB_raise_no_memory(ecp);
            return RDB_ERROR;
        }
        minusexp = RDB_ro_op("minus", ecp);
        if (minusexp == NULL) {
            RDB_free(isenamp);
            return RDB_ERROR;
        }

        minusexp->def.op.args.firstp = exp->def.op.args.firstp;
        minusexp->def.op.args.lastp = exp->def.op.args.lastp;
        minusexp->nextp = NULL;
        RDB_free(exp->def.op.name);
        exp->def.op.name = isenamp;
        exp->def.op.args.firstp = exp->def.op.args.lastp = minusexp;
    }
    return RDB_OK;
}

/*
 * Add constraint to the linked list of constraints managed by dbroot
 */
static int
add_constraint(RDB_constraint *constrp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_dbroot *dbrootp = RDB_tx_db(txp)->dbrootp;

    if (optimize_constr_expr(constrp->exp, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_expr_resolve_tbnames(constrp->exp, ecp, txp) != RDB_OK)
        return RDB_ERROR;

    constrp->nextp = dbrootp->first_constrp;
    dbrootp->first_constrp = constrp;

    return RDB_OK;
}

/*
 * Read constraints from catalog
 */
int
RDB_read_constraints(RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    int i;
    RDB_object constrs;
    RDB_object *tplp;
    RDB_dbroot *dbrootp = RDB_tx_db(txp)->dbrootp;

    RDB_init_obj(&constrs);

    ret = RDB_table_to_array(&constrs, dbrootp->constraints_tbp, 0, NULL,
            0, ecp, txp);
    if (ret != RDB_OK)
        goto cleanup;

    for (i = 0; (tplp = RDB_array_get(&constrs, i, ecp)) != NULL; i++) {
        RDB_constraint *constrp = RDB_alloc(sizeof(RDB_constraint), ecp);

        if (constrp == NULL) {
            ret = RDB_ERROR;
            goto cleanup;
        }
        constrp->name = RDB_dup_str(RDB_obj_string(RDB_tuple_get(tplp,
                "constraintname")));
        if (constrp->name == NULL) {
            RDB_free(constrp);
            RDB_raise_no_memory(ecp);
            ret = RDB_ERROR;
            goto cleanup;
        }
        constrp->exp = RDB_binobj_to_expr(RDB_tuple_get(tplp, "i_expr"), ecp,
                txp);
        if (constrp->exp == NULL) {
            RDB_free(constrp->name);
            RDB_free(constrp);
            ret = RDB_ERROR;
            goto cleanup;
        }

        /* Add to list */
        if (add_constraint(constrp, ecp, txp) != RDB_OK)
            return RDB_ERROR;
    }
    if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_NOT_FOUND_ERROR) {
        RDB_clear_err(ecp);
        ret = RDB_OK;
    } else {
        ret = RDB_ERROR;
    }
    if (ret == RDB_OK) {
        RDB_tx_db(txp)->dbrootp->constraints_read = RDB_TRUE;
    }

cleanup:
    RDB_destroy_obj(&constrs, ecp);
    return ret;
}

/** @defgroup constr Constraint functions 
 * @{
 */

/**
 * 
RDB_create_constraint creates a constraint with the name <var>name</var>
on the database the transaction specified by <var>txp</var> interacts with.

@returns RDB_OK on success, RDB_ERROR if an error occurred.

@par Errors:
<dl>
<dt>no_running_tx_error
<dd><var>txp</var> does not point to a running transaction.
<dt>type_mismatch_error
<dd>The *<var>constrp</var> is not of type BOOLEAN.
<dt>predicate_violation_error
<dd>The *<var>constrp</var> does not evaluate to TRUE.
</dl>

The call may also fail for a @ref system-errors "system error",
in which case the transaction may be implicitly rolled back.
 */
int
RDB_create_constraint(const char *name, RDB_expression *exp,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_bool res;
    RDB_constraint *constrp;

    /* Check constraint */
    ret = RDB_evaluate_bool(exp, NULL, NULL, NULL, ecp, txp, &res);
    if (ret != RDB_OK)
        return ret;
    if (!res) {
        RDB_raise_predicate_violation(name, ecp);
        return RDB_ERROR;
    }

    if (!RDB_tx_db(txp)->dbrootp->constraints_read) {
        if (RDB_read_constraints(ecp, txp) != RDB_OK)
            return RDB_ERROR;
    }

    constrp = RDB_alloc(sizeof (RDB_constraint), ecp);
    if (constrp == NULL) {
        return RDB_ERROR;
    }

    constrp->exp = exp;

    constrp->name = RDB_dup_str(name);
    if (constrp->name == NULL) {
        RDB_raise_no_memory(ecp);
        goto error;
    }

    /* Create constraint in catalog */
    ret = RDB_cat_create_constraint(name, exp, ecp, txp);
    if (ret != RDB_OK)
        goto error;

    /* Insert constraint into list */
    if (add_constraint(constrp, ecp, txp) != RDB_OK)
        return RDB_ERROR;
    
    return RDB_OK;

error:
    RDB_free(constrp->name);
    RDB_free(constrp);

    return RDB_ERROR;
}

/**
 * RDB_drop_constraint deletes the constraint with the name <var>name</var>.

@returns RDB_OK on success, RDB_ERROR if an error occurred.

@par Errors:
<dl>
<dt>no_running_tx_error
<dd><var>txp</var> does not point to a running transaction.
<dt>not_found_error
<dd>A constraint with the name <var>name</var> could not be found.
</dl>

The call may also fail for a @ref system-errors "system error",
in which case the transaction may be implicitly rolled back.
 */
int
RDB_drop_constraint(const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int ret;
    RDB_expression *condp;
    RDB_constraint *constrp;
    RDB_constraint *prevconstrp = NULL;
    RDB_dbroot *dbrootp = RDB_tx_db(txp)->dbrootp;

    /*
     * Read constraints because otherwise the RDB_delete() call
     * may read the constraint that is meant to be deleted.
     */
    if (!dbrootp->constraints_read) {
        if (RDB_read_constraints(ecp, txp) != RDB_OK) {
            return RDB_ERROR;
        }
    }

    /* Delete constraint from list */
    constrp = dbrootp->first_constrp;
    while (constrp != NULL && strcmp(constrp->name, name) != 0) {
        prevconstrp = constrp;
        constrp = constrp->nextp;
    }
    if (constrp != NULL) {
        if (prevconstrp == NULL) {
            dbrootp->first_constrp = constrp->nextp;
        } else {
            prevconstrp->nextp = constrp->nextp;
        }

        RDB_del_expr(constrp->exp, ecp);
        RDB_free(constrp->name);
        RDB_free(constrp);
    }

    /* Delete constraint from catalog */
    condp = RDB_eq(RDB_var_ref("constraintname", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (condp == NULL) {
        return RDB_ERROR;
    }
    ret = RDB_delete(dbrootp->constraints_tbp, condp, ecp, txp);
    RDB_del_expr(condp, ecp);
    if (ret == 0) {
        RDB_raise_not_found(name, ecp);
        return RDB_ERROR;
    }

    return ret == RDB_ERROR ? RDB_ERROR : RDB_OK;
}

/*@}*/
