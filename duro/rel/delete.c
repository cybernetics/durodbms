/*
 * $Id$
 *
 * Copyright (C) 2004-2005 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "rdb.h"
#include "typeimpl.h"
#include "internal.h"
#include <gen/strfns.h>
#include <string.h>

static int
delete_by_uindex(RDB_table *tbp, RDB_object *objpv[], _RDB_tbindex *indexp,
        RDB_transaction *txp)
{
    RDB_field *fv;
    int i;
    int ret;
    int keylen = indexp->attrc;

    fv = malloc(sizeof (RDB_field) * keylen);
    if (fv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }
    
    for (i = 0; i < keylen; i++) {
        ret = _RDB_obj_to_field(&fv[i], objpv[i]);
        if (ret != RDB_OK)
            goto cleanup;
    }

    if (indexp->idxp == NULL) {
        ret = RDB_delete_rec(tbp->stp->recmapp, fv,
                tbp->is_persistent ? txp->txid : NULL);
    } else {
        ret = RDB_index_delete_rec(indexp->idxp, fv,
                tbp->is_persistent ? txp->txid : NULL);
    }
    if (ret != RDB_OK) {
        goto cleanup;
    }

    ret = RDB_OK;

cleanup:
    free(fv);
    return ret;
}

int
_RDB_delete_real(RDB_table *tbp, RDB_expression *condp, RDB_transaction *txp)
{
    int ret;
    int i;
    RDB_cursor *curp;
    RDB_object tpl;
    void *datap;
    size_t len;
    RDB_bool b;
    RDB_type *tpltyp = tbp->typ->var.basetyp;

    ret = RDB_recmap_cursor(&curp, tbp->stp->recmapp, RDB_TRUE,
            tbp->is_persistent ? txp->txid : NULL);
    if (ret != RDB_OK)
        return ret;
    ret = RDB_cursor_first(curp);
    if (ret == RDB_NOT_FOUND) {
        RDB_destroy_cursor(curp);
        return RDB_OK;
    }
    
    do {
        RDB_init_obj(&tpl);
        if (condp != NULL) {
            for (i = 0; i < tpltyp->var.tuple.attrc; i++) {
                RDB_object val;

                ret = RDB_cursor_get(curp,
                        *(int*) RDB_hashmap_get(
                                &tbp->stp->attrmap,
                                tpltyp->var.tuple.attrv[i].name, NULL),
                        &datap, &len);
                if (ret != 0) {
                   RDB_destroy_obj(&tpl);
                   goto error;
                }
                RDB_init_obj(&val);
                ret = RDB_irep_to_obj(&val, tpltyp->var.tuple.attrv[i].typ,
                                 datap, len);
                if (ret != RDB_OK) {
                   RDB_destroy_obj(&val);
                   RDB_destroy_obj(&tpl);
                   goto error;
                }
                ret = RDB_tuple_set(&tpl, tpltyp->var.tuple.attrv[i].name, &val);
                RDB_destroy_obj(&val);
                if (ret != RDB_OK) {
                   RDB_destroy_obj(&tpl);
                   goto error;
                }
            }

            ret = RDB_evaluate_bool(condp, &tpl, txp, &b);
            if (ret != RDB_OK)
                 goto error;
        } else {
            b = RDB_TRUE;
        }
        if (b) {
            ret = RDB_cursor_delete(curp);
            if (ret != RDB_OK) {
                RDB_errmsg(txp->dbp->dbrootp->envp, "cannot delete record: %s",
                        RDB_strerror(ret));
                RDB_destroy_obj(&tpl);
                goto error;
            }
        }
        RDB_destroy_obj(&tpl);
        ret = RDB_cursor_next(curp, 0);
    } while (ret == RDB_OK);
    if (ret != RDB_NOT_FOUND)
        goto error;
    return RDB_destroy_cursor(curp);

error:
    RDB_destroy_cursor(curp);
    return ret;
}  

int
_RDB_delete_select_uindex(RDB_table *tbp, RDB_expression *condp,
        RDB_transaction *txp)
{
    int ret;

    if (condp != NULL) {
        RDB_object tpl;
        RDB_bool b;

        RDB_init_obj(&tpl);

        /*
         * Read tuple and check condition
         */
        ret = _RDB_get_by_uindex(tbp->var.select.tbp->var.project.tbp,
                tbp->var.select.objpv, tbp->var.select.tbp->var.project.indexp,
                tbp->typ->var.basetyp, txp, &tpl);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&tpl);
            goto cleanup;
        }
        ret = RDB_evaluate_bool(condp, &tpl, txp, &b);
        RDB_destroy_obj(&tpl);
        if (ret != RDB_OK)
            goto cleanup;

        if (!b)
            return RDB_OK;
    }

    ret = delete_by_uindex(tbp->var.select.tbp->var.project.tbp,
            tbp->var.select.objpv, tbp->var.select.tbp->var.project.indexp,
            txp);
    if (ret == RDB_NOT_FOUND)
        ret = RDB_OK;

cleanup:
    return ret;
}

int
_RDB_delete_select_index(RDB_table *tbp, RDB_expression *condp,
        RDB_transaction *txp)
{
    int ret, ret2;
    int i;
    int flags;
    RDB_cursor *curp = NULL;
    RDB_field *fv = NULL;
    _RDB_tbindex *indexp = tbp->var.select.tbp->var.project.indexp;
    int keylen = indexp->attrc;

    ret = RDB_index_cursor(&curp, indexp->idxp, RDB_TRUE,
            tbp->var.select.tbp->var.project.tbp->is_persistent ?
            txp->txid : NULL);
    if (ret != RDB_OK) {
        if (txp != NULL) {
            RDB_errmsg(txp->dbp->dbrootp->envp, "cannot create cursor: %s",
                    RDB_strerror(ret));
        }
        return ret;
    }

    fv = malloc(sizeof (RDB_field) * keylen);
    if (fv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }

    for (i = 0; i < keylen; i++) {
        ret =_RDB_obj_to_field(&fv[i], tbp->var.select.objpv[i]);
        if (ret != RDB_OK)
            goto cleanup;
    }

    if (tbp->var.select.objpc != indexp->attrc
            || !tbp->var.select.all_eq)
        flags = RDB_REC_RANGE;
    else
        flags = 0;

    ret = RDB_cursor_seek(curp, tbp->var.select.objpc, fv, flags);
    if (ret == RDB_NOT_FOUND) {
        ret = RDB_OK;
        goto cleanup;
    }

    do {
        RDB_bool del = RDB_TRUE;
        RDB_bool b;

        RDB_object tpl;

        RDB_init_obj(&tpl);

        /*
         * Read tuple and check condition
         */
        ret = _RDB_get_by_cursor(tbp->var.select.tbp->var.project.tbp,
                curp, tbp->typ->var.basetyp, &tpl);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&tpl);
            goto cleanup;
        }
        if (tbp->var.select.stopexp != NULL) {
            ret = RDB_evaluate_bool(tbp->var.select.stopexp, &tpl,
                    txp, &b);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl);
                goto cleanup;
            }
            if (!b) {
                ret = RDB_OK;
                RDB_destroy_obj(&tpl);
                goto cleanup;
            }
        }
        if (condp != NULL) {
            ret = RDB_evaluate_bool(condp, &tpl, txp, &del);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl);
                goto cleanup;
            }
        }
        ret = RDB_evaluate_bool(tbp->var.select.exp, &tpl, txp, &b);
        RDB_destroy_obj(&tpl);
        if (ret != RDB_OK)
            goto cleanup;
        del = (RDB_bool) (del && b);

        if (del) {
            ret = RDB_cursor_delete(curp);
            if (ret != RDB_OK)
                goto cleanup;
        }

        if (tbp->var.select.objpc == indexp->attrc
                && tbp->var.select.all_eq)
            flags = RDB_REC_DUP;
        else
            flags = 0;
        ret = RDB_cursor_next(curp, flags);
    } while (ret == RDB_OK);

    if (ret == RDB_NOT_FOUND)
        ret = RDB_OK;

cleanup:
    if (curp != NULL) {
        ret2 = RDB_destroy_cursor(curp);
        if (ret2 != RDB_OK && ret == RDB_OK)
            ret = ret2;
    }
    free(fv);
    return ret;
}

int
RDB_delete(RDB_table *tbp, RDB_expression *condp, RDB_transaction *txp)
{
    RDB_ma_delete del;

    del.tbp = tbp;
    del.condp = condp;
    return RDB_multi_assign(0, NULL, 0, NULL, 1, &del, 0, NULL, txp);
}
