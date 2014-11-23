/*
 * $Id$
 *
 * Copyright (C) 2004-2013 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "duro.h"
#include <gen/strfns.h>
#include <string.h>
#include <rel/typeimpl.h>

static int
type_define_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    int i;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    int repc;
    RDB_possrep *repv = NULL;
    RDB_expression *initexp = NULL;
    RDB_expression *constraintp = NULL;

    if ((objc < 6) || (objc > 7)) {
        Tcl_WrongNumArgs(interp, 2, objv, "typename possreps ?constraint? initexp tx");
        return TCL_ERROR;
    }

    txstr = Tcl_GetString(objv[objc - 1]);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    ret = Tcl_ListObjLength(interp, objv[3], &repc);
    if (ret != TCL_OK)
        return ret;

    repv = (RDB_possrep *) Tcl_Alloc(sizeof(RDB_possrep) * repc);
    for (i = 0; i < repc; i++) {
        repv[i].compv = NULL;
    }

    /*
     * Read possible representations
     */
    for (i = 0; i < repc; i++) {
        Tcl_Obj *repobjp;
        int repobjlen;
        Tcl_Obj *repnameobjp, *repcompsobjp;
        int j;

        ret = Tcl_ListObjIndex(interp, objv[3], i, &repobjp);
        if (ret != TCL_OK)
            goto cleanup;
 
        ret = Tcl_ListObjLength(interp, repobjp, &repobjlen);
        if (ret != TCL_OK)
            goto cleanup;

        if (repobjlen != 2) {
            Tcl_SetResult(interp, "Invalid possible representation", TCL_STATIC);
            ret = TCL_ERROR;
            goto cleanup;
        }

        ret = Tcl_ListObjIndex(interp, repobjp, 0, &repnameobjp);
        if (ret != TCL_OK)
            goto cleanup;

        repv[i].name = Tcl_GetString(repnameobjp);

        ret = Tcl_ListObjIndex(interp, repobjp, 1, &repcompsobjp);
        if (ret != TCL_OK)
            goto cleanup;

        ret = Tcl_ListObjLength(interp, repcompsobjp, &repv[i].compc);
        if (ret != TCL_OK)
            goto cleanup;

        repv[i].compv = (RDB_attr *) Tcl_Alloc(sizeof (RDB_attr) * repv[i].compc);

        for (j = 0; j < repv[i].compc; j++) {
            Tcl_Obj *compobjp;
            Tcl_Obj *nameobjp;
            Tcl_Obj *typeobjp;
            int compobjlen;

            ret = Tcl_ListObjIndex(interp, repcompsobjp, j, &compobjp);
            if (ret != TCL_OK)
                goto cleanup;

            ret = Tcl_ListObjLength(interp, compobjp, &compobjlen);
            if (ret != TCL_OK)
                goto cleanup;

            if (compobjlen != 2) {
                Tcl_SetResult(interp, "Invalid component", TCL_STATIC);
                ret = TCL_ERROR;
                goto cleanup;
            }

            ret = Tcl_ListObjIndex(interp, compobjp, 0, &nameobjp);
            if (ret != TCL_OK)
                goto cleanup;

            ret = Tcl_ListObjIndex(interp, compobjp, 1, &typeobjp);
            if (ret != TCL_OK)
                goto cleanup;

            repv[i].compv[j].name = Tcl_GetString(nameobjp);
            repv[i].compv[j].typ = RDB_get_type(Tcl_GetString(typeobjp),
                    statep->current_ecp, txp);
            if (repv[i].compv[j].typ == NULL) {
                Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
                ret = TCL_ERROR;
                goto cleanup;
            }
        }
    }

    if (objc == 7) {
        /* Type constraint */
        constraintp = Duro_parse_expr_utf(interp, Tcl_GetString(objv[4]),
                statep, statep->current_ecp, txp);
        if (constraintp == NULL) {
            ret = TCL_ERROR;
            goto cleanup;
        }            
    }

    /* Init expression */
    initexp = Duro_parse_expr_utf(interp, Tcl_GetString(objv[objc - 2]),
            statep, statep->current_ecp, txp);
    if (initexp == NULL) {
        ret = TCL_ERROR;
        goto cleanup;
    }

    ret = RDB_define_type(Tcl_GetString(objv[2]), repc, repv, constraintp,
            initexp, 0, statep->current_ecp, txp);
    if (constraintp != NULL)
        RDB_del_expr(constraintp, statep->current_ecp);
    if (initexp != NULL)
        RDB_del_expr(initexp, statep->current_ecp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
        ret = TCL_ERROR;
        goto cleanup;
    }
    ret = TCL_OK;

cleanup:
    if (repv != NULL) {
        for (i = 0; i < repc; i++) {
            Tcl_Free((char *) repv[i].compv);
        }
        Tcl_Free((char *) repv);
    }

    return ret;
}

static int
type_drop_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "typename tx");
        return TCL_ERROR;
    }

    name = Tcl_GetString(objv[2]);
    txstr = Tcl_GetString(objv[3]);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    ret = RDB_drop_type(name, statep->current_ecp, txp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
        return TCL_ERROR;
    }

    return RDB_OK;
}

static int
type_implement_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_type *irep = NULL;

    if ((objc != 4) && (objc != 5)) {
        Tcl_WrongNumArgs(interp, 2, objv, "typename ?arep? tx");
        return TCL_ERROR;
    }

    txstr = Tcl_GetString(objv[objc - 1]);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    if (objc == 5) {
        irep = Duro_get_type(objv[3], interp, statep->current_ecp, txp);
        if (irep == NULL)
            return TCL_ERROR;
    }

    ret = RDB_implement_type(Tcl_GetString(objv[2]), irep, (size_t)-1,
            statep->current_ecp, txp);
    if (irep != NULL && !RDB_type_is_scalar(irep))
        RDB_del_nonscalar_type(irep, statep->current_ecp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
        return TCL_ERROR;
    }

    return TCL_OK;
}

int
Duro_type_cmd(ClientData data, Tcl_Interp *interp,
        int objc, Tcl_Obj *CONST objv[])
{
    TclState *statep = (TclState *) data;

    const char *sub_cmds[] = {
        "define", "drop", "implement", NULL
    };
    enum table_ix {
        define_ix, drop_ix, implement_ix
    };
    int index;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "arg ?arg ...?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], sub_cmds, "option", 0, &index)
            != TCL_OK) {
        return TCL_ERROR;
    }

    RDB_clear_err(statep->current_ecp);

    switch (index) {
        case define_ix:
            return type_define_cmd(statep, interp, objc, objv);
        case drop_ix:
            return type_drop_cmd(statep, interp, objc, objv);
        case implement_ix:
            return type_implement_cmd(statep, interp, objc, objv);
    }
    return TCL_ERROR;
}
