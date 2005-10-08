/*
 * Copyright (C) 2003-2005 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

/* $Id$ */

#include "rdb.h"
#include "typeimpl.h"
#include "internal.h"
#include <string.h>

static int
project_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    RDB_bool result;
    int ret;
    RDB_type *tpltyp = tbp->typ->var.basetyp;

    if (tpltyp->var.tuple.attrc ==
            tbp->var.project.tbp->typ->var.basetyp->var.tuple.attrc) {
        /* Null project */
        return RDB_table_contains(tbp->var.project.tbp, tplp, txp);
    } else if (tpltyp->var.tuple.attrc > 0) {
        RDB_expression *condp;
        RDB_table *seltbp;
        RDB_object *objp;
        int i, ret;

        /* create where-condition */
        objp = RDB_tuple_get(tplp, tpltyp->var.tuple.attrv[0].name);
        if (objp == NULL)
            return RDB_INVALID_ARGUMENT;
        condp = RDB_ro_op_va("=", RDB_expr_attr(tpltyp->var.tuple.attrv[0].name),
                RDB_obj_to_expr(objp), (RDB_expression *) NULL);
        if (condp == NULL)
            return RDB_NO_MEMORY;
        for (i = 1; i < tpltyp->var.tuple.attrc; i++) {
            objp = RDB_tuple_get(tplp, tpltyp->var.tuple.attrv[i].name);
            if (objp == NULL) {
                if (condp != NULL)
                    RDB_drop_expr(condp);
                return RDB_INVALID_ARGUMENT;
            }
            
            condp = RDB_ro_op_va("AND", condp,
                    RDB_ro_op_va("=", RDB_expr_attr(
                            tpltyp->var.tuple.attrv[i].name),
                            RDB_obj_to_expr(objp), (RDB_expression *) NULL),
                    (RDB_expression *) NULL);
            if (condp == NULL)
                return RDB_NO_MEMORY;
        }
        if (condp == NULL)
            return RDB_NO_MEMORY;

        /* create selection table */
        ret = RDB_select(tbp, condp, txp, &seltbp);
        if (ret != RDB_OK) {
            RDB_drop_expr(condp);
            return ret;
        }

        /* check if selection is empty */
        ret = RDB_table_is_empty(seltbp, txp, &result);
        _RDB_free_table(seltbp);
        if (ret != RDB_OK)
            return ret;
        return result ? RDB_NOT_FOUND : RDB_OK;
    } else {
        /* projection with no attributes */

        ret = RDB_table_is_empty(tbp->var.project.tbp, txp, &result);
        if (ret != RDB_OK)
            return ret;
        return result ? RDB_NOT_FOUND : RDB_OK;
    }
}

static int
rename_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    RDB_object tpl;
    int ret;

    RDB_init_obj(&tpl);
    ret = _RDB_invrename_tuple(tplp, tbp->var.rename.renc, tbp->var.rename.renv,
            &tpl);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_table_contains(tbp->var.rename.tbp, &tpl, txp);

cleanup:
    RDB_destroy_obj(&tpl);
    return ret;
}

static int
ungroup_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    int ret, ret2;
    RDB_object tpl;
    RDB_qresult *qrp;
    RDB_table *seltbp = NULL;
    RDB_type *tpltyp = tbp->var.ungroup.tbp->typ->var.basetyp;

    if (tpltyp->var.tuple.attrc > 1) {
        RDB_expression *condp;
        RDB_object *objp;
        int i;

        /* create where-condition */
        i = 0;
        if (i < tpltyp->var.tuple.attrc
                && strcmp(tpltyp->var.tuple.attrv[i].name,
                          tbp->var.ungroup.attr) == 0)
            i++;
        objp = RDB_tuple_get(tplp, tpltyp->var.tuple.attrv[i].name);
        if (objp == NULL)
            return RDB_INVALID_ARGUMENT;
        condp = RDB_ro_op_va("=", RDB_expr_attr(tpltyp->var.tuple.attrv[i++].name),
                RDB_obj_to_expr(objp), (RDB_expression *) NULL);
        while (i < tpltyp->var.tuple.attrc) {
            if (strcmp(tpltyp->var.tuple.attrv[i].name,
                          tbp->var.ungroup.attr) != 0) {
                objp = RDB_tuple_get(tplp, tpltyp->var.tuple.attrv[i].name);
                if (objp == NULL) {
                    if (condp != NULL)
                        RDB_drop_expr(condp);
                    return RDB_INVALID_ARGUMENT;
                }
                condp = RDB_ro_op_va("AND", condp,
                        RDB_ro_op_va("=",
                                RDB_expr_attr(tpltyp->var.tuple.attrv[i].name),
                                RDB_obj_to_expr(objp), (RDB_expression *) NULL),
                        (RDB_expression *) NULL);
                if (condp == NULL)
                    return RDB_NO_MEMORY;
            }
            i++;
        }
        if (condp == NULL) {
            return RDB_NO_MEMORY;
        }

        /* create selection table */
        ret = RDB_select(tbp->var.ungroup.tbp, condp, txp, &seltbp);
        if (ret != RDB_OK) {
            RDB_drop_expr(condp);
            return ret;
        }
        ret = _RDB_table_qresult(seltbp, txp, &qrp);
        if (ret != RDB_OK) {
            RDB_drop_table(seltbp, txp);
            _RDB_handle_syserr(txp, ret);
            return ret;
        }
    } else {
        /*
         * Only one attribute (the UNGROUPed attribute)
         */
        ret = _RDB_table_qresult(tbp->var.ungroup.tbp, txp, &qrp);
        if (ret != RDB_OK) {
            _RDB_handle_syserr(txp, ret);
            return ret;
        }
    }
    /*
     * Try to find a tuple where the UNGROUPed attribute contains the
     * tuple specified by tbp, projected over the attributes of the
     * UNGROUPed attribute.
     */
    RDB_init_obj(&tpl);
    while ((ret = _RDB_next_tuple(qrp, &tpl, txp)) == RDB_OK) {
        /* The additional attributes in tpl are ignored */
        ret = RDB_table_contains(RDB_obj_table(RDB_tuple_get(&tpl,
                tbp->var.ungroup.attr)), tplp, txp);
        if (ret != RDB_NOT_FOUND) {
            /* Found or Error */
            break;
        }
    }
    if (ret != RDB_OK && ret != RDB_NOT_FOUND) {
        _RDB_handle_syserr(txp, ret);
        goto error;
    }

    RDB_destroy_obj(&tpl);
    ret2 = _RDB_drop_qresult(qrp, txp);
    if (ret2 != RDB_OK) {
        _RDB_handle_syserr(txp, ret2);
        return ret2;
    }    
    if (seltbp != NULL) {
        ret2 = RDB_drop_table(seltbp, txp);
        if (ret2 != RDB_OK) {
            _RDB_handle_syserr(txp, ret);
            return ret2;
        }
    }
    return ret;

error:
    RDB_destroy_obj(&tpl);
    _RDB_drop_qresult(qrp, txp);
    if (seltbp != NULL)
        RDB_drop_table(seltbp, txp);
    return ret;
}

static int
wrap_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;

    /*
     * Check if unwrapped tuple is in base table
     */

    RDB_init_obj(&tpl);

    ret = _RDB_invwrap_tuple(tplp, tbp->var.wrap.wrapc, tbp->var.wrap.wrapv,
            &tpl);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&tpl);
        return ret;
    }

    ret = RDB_table_contains(tbp->var.wrap.tbp, &tpl, txp);
    RDB_destroy_obj(&tpl);
    return ret;
}

static int
unwrap_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;

    /*
     * Check if wrapped tuple is in base table
     */

    RDB_init_obj(&tpl);
    ret = _RDB_invunwrap_tuple(tplp, tbp->var.unwrap.attrc,
            tbp->var.unwrap.attrv, tbp->var.unwrap.tbp->typ->var.basetyp, &tpl);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&tpl);
        return ret;
    }

    ret = RDB_table_contains(tbp->var.unwrap.tbp, &tpl, txp);
    RDB_destroy_obj(&tpl);
    return ret;
}

static int
sdivide_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    int ret;

    ret = RDB_table_contains(tbp->var.sdivide.tb1p, tplp, txp);
    if (ret != RDB_OK)
        return ret;

    return _RDB_sdivide_preserves(tbp, tplp, NULL, txp);
}

static int
stored_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    int i;
    int ret;
    RDB_field *fvp;
    RDB_type *tpltyp = tbp->typ->var.basetyp;
    int attrcount = tpltyp->var.tuple.attrc;

    if (tbp->stp == NULL) {
        /* Physical table representation has not been created, so table is empty */
        return RDB_NOT_FOUND;
    }

    fvp = malloc(sizeof(RDB_field) * attrcount);
    if (fvp == NULL)
        return RDB_NO_MEMORY;
    for (i = 0; i < attrcount; i++) {
        RDB_object *objp;
        int fno = *_RDB_field_no(tbp->stp, tpltyp->var.tuple.attrv[i].name);

        objp = RDB_tuple_get(tplp, tpltyp->var.tuple.attrv[i].name);
        if (objp == NULL) {
            free(fvp);
            return RDB_INVALID_ARGUMENT;
        }
        if (objp->typ != NULL && !RDB_type_equals (RDB_obj_type(objp),
                tpltyp->var.tuple.attrv[i].typ)) {
            free(fvp);
            return RDB_TYPE_MISMATCH;
        }

        /* Set type - needed for tuples */
        if (objp->typ == NULL
                && (objp->kind == RDB_OB_TUPLE
                || objp->kind == RDB_OB_TABLE)) {
            objp->typ = tpltyp->var.tuple.attrv[i].typ;
        }
        ret = _RDB_obj_to_field(&fvp[fno], objp);
        if (ret != RDB_OK) {
            free(fvp);
            return ret;
        }
    }

    /* Don't use tx if table is local */
    ret = RDB_contains_rec(tbp->stp->recmapp, fvp,
            tbp->is_persistent ? txp->txid : NULL);
    free(fvp);
    if (RDB_is_syserr(ret)) {
        RDB_errmsg(txp->dbp->dbrootp->envp, RDB_strerror(ret));
        _RDB_handle_syserr(txp, ret);
    }
    return ret;
}

int
RDB_table_contains(RDB_table *tbp, const RDB_object *tplp, RDB_transaction *txp)
{
    int ret;
    RDB_bool b;

    if (txp != NULL && !RDB_tx_is_running(txp))
        return RDB_INVALID_TRANSACTION;

    switch (tbp->kind) {
        case RDB_TB_REAL:
            return stored_contains(tbp, tplp, txp);
        case RDB_TB_SELECT:
            ret = RDB_evaluate_bool(tbp->var.select.exp, tplp, txp, &b);
            if (ret != RDB_OK)
                return ret;
            if (!b)
                return RDB_NOT_FOUND;
            return RDB_table_contains(tbp->var.select.tbp, tplp, txp);
        case RDB_TB_UNION:
            ret = RDB_table_contains(tbp->var._union.tb1p, tplp, txp);
            if (ret == RDB_OK)
                return RDB_OK;
            return RDB_table_contains(tbp->var._union.tb2p, tplp, txp);
        case RDB_TB_MINUS:
            ret = RDB_table_contains(tbp->var.minus.tb1p, tplp, txp);
            if (ret != RDB_OK)
                return ret;
            ret = RDB_table_contains(tbp->var.minus.tb2p, tplp, txp);
            if (ret == RDB_OK)
                return RDB_NOT_FOUND;
            if (ret == RDB_NOT_FOUND)
                return RDB_OK;
            return ret;
        case RDB_TB_INTERSECT:
            ret = RDB_table_contains(tbp->var.intersect.tb1p, tplp, txp);
            if (ret != RDB_OK)
                return ret;
            return RDB_table_contains(tbp->var.intersect.tb2p, tplp, txp);
        case RDB_TB_JOIN:
            ret = RDB_table_contains(tbp->var.join.tb1p, tplp, txp);
            if (ret != RDB_OK)
                return ret;
            return RDB_table_contains(tbp->var.join.tb2p, tplp, txp);
        case RDB_TB_EXTEND:
            return RDB_table_contains(tbp->var.extend.tbp, tplp, txp);
        case RDB_TB_PROJECT:
            return project_contains(tbp, tplp, txp);
        case RDB_TB_SUMMARIZE:
        case RDB_TB_GROUP:
            /*
             * Create qresult and check if it contains the tuple
             */
            {
                int ret2;

                RDB_qresult *qrp;
                ret = _RDB_table_qresult(tbp, txp, &qrp);
                if (ret != RDB_OK) {
                    RDB_errmsg(txp->dbp->dbrootp->envp,
                            "Unable to create qresult: %s", RDB_strerror(ret));
                    _RDB_handle_syserr(txp, ret);
                    return ret;
                }

                ret2 = _RDB_qresult_contains(qrp, tplp, txp);
                if (ret2 != RDB_OK && ret2 != RDB_NOT_FOUND
                        && ret2 != RDB_INVALID_ARGUMENT) {
                    _RDB_drop_qresult(qrp, txp);
                    RDB_errmsg(txp->dbp->dbrootp->envp,
                            "_RDB_qresult_contains() failed: %s",
                            RDB_strerror(ret2));
                    _RDB_handle_syserr(txp, ret2);
                    return ret2;
                }

                ret = _RDB_drop_qresult(qrp, txp);
                if (ret != RDB_OK) {
                    RDB_errmsg(txp->dbp->dbrootp->envp,
                            "Unable to drop qresult: %s", RDB_strerror(ret));
                    _RDB_handle_syserr(txp, ret);
                    return ret;
                }
                return ret2;
            }
        case RDB_TB_UNGROUP:
            return ungroup_contains(tbp, tplp, txp);
        case RDB_TB_RENAME:
            return rename_contains(tbp, tplp, txp);
        case RDB_TB_WRAP:
            return wrap_contains(tbp, tplp, txp);
        case RDB_TB_UNWRAP:
            return unwrap_contains(tbp, tplp, txp);;
        case RDB_TB_SDIVIDE:
            return sdivide_contains(tbp, tplp, txp);
    }
    /* should never be reached */
    abort();
}
