/*
 * Copyright (C) 2003 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

/* $Id$ */

#include "duro.h"
#include <rel/internal.h>
#include <string.h>

int
Duro_operator_cmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    RDB_bool update;
    int ret;
    int i;
    char *txstr;
    RDB_transaction *txp;
    Tcl_HashEntry *entryp;
    int argc;
    RDB_type **argtv;
    char *txtp;
    int len;
    Tcl_Obj *namelistp, *opdatap;
    TclState *statep = (TclState *) data;

    if (objc != 7) {
        Tcl_WrongNumArgs(interp, 1, objv,
                "name [-returns rtype | -updates updlist] tx args body");
        return TCL_ERROR;
    }
    if (strcmp (Tcl_GetStringFromObj(objv[2], NULL), "-updates") == 0)
        update = RDB_TRUE;
    if (strcmp (Tcl_GetStringFromObj(objv[2], NULL), "-returns") == 0)
        update = RDB_FALSE;
    else
        Tcl_SetResult(interp, "invalid option, must be -updates or -returns",
                TCL_STATIC);

    txstr = Tcl_GetStringFromObj(objv[4], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    namelistp = Tcl_NewListObj(0, NULL);

    Tcl_ListObjLength(interp, objv[5], &argc);
    argtv = (RDB_type **) Tcl_Alloc(sizeof(RDB_type *) * argc);

    for (i = 0; i < argc; i++) {
        Tcl_Obj *argtobjp;
        Tcl_Obj *typeobjp;
        Tcl_Obj *nameobjp;

        ret = Tcl_ListObjIndex(interp, objv[5], i, &argtobjp);
        if (ret != TCL_OK)
            goto cleanup;

        /*
         * Get argument and append it to list
         */
        ret = Tcl_ListObjIndex(interp, argtobjp, 0, &nameobjp);
        if (ret != TCL_OK) {
            goto cleanup;
        }
        ret = Tcl_ListObjAppendElement(interp, namelistp, nameobjp);
        if (ret != TCL_OK)
            goto cleanup;

        ret = Tcl_ListObjIndex(interp, argtobjp, 1, &typeobjp);
        if (ret != TCL_OK) {
            goto cleanup;
        }
        if (typeobjp == NULL) {
            Tcl_SetResult(interp, "Type missing", TCL_STATIC);
            ret = TCL_ERROR;
            goto cleanup;
        }

        ret = RDB_get_type(Tcl_GetStringFromObj(typeobjp, NULL), txp,
                &argtv[i]);
        if (ret != RDB_OK) {
            Duro_dberror(interp, ret);
            ret = TCL_ERROR;
            goto cleanup;
        }
    }

    /*
     * Create list of argument names and op body
     */
    opdatap = Tcl_NewListObj(1, &namelistp);
    ret = Tcl_ListObjAppendElement(interp, opdatap, objv[6]);
    if (ret != TCL_OK)
            goto cleanup;

    txtp = Tcl_GetStringFromObj(opdatap, &len);

    if (update) {
        int updlen;
        Tcl_Obj *argobjp;
        RDB_bool *updv = (RDB_bool *) Tcl_Alloc(argc);

        for (i = 0; i < argc; i++)
            updv[i] = RDB_FALSE;

        ret = Tcl_ListObjLength(interp, objv[3], &updlen);
        if (ret != TCL_OK) {
            Tcl_Free(updv);
            return ret;
        }

        for (i = 0; i < updlen; i++) {
            int j;
            char *argp;

            Tcl_ListObjIndex(interp, objv[3], i, &argobjp);
            if (ret != TCL_OK) {
                Tcl_Free(updv);
                return ret;
            }
            argp = Tcl_GetString(argobjp);

            /*
             * Search arg name in list, if found, set updv[i] to RDB_TRUE.
             */
            for (j = 0; j < argc; j++) {
                Tcl_Obj *updobjp;

                Tcl_ListObjIndex(interp, namelistp, i, &updobjp);
                if (ret != TCL_OK) {
                    Tcl_Free(updv);
                    return ret;
                }
                if (strcmp(Tcl_GetString(updobjp), argp) == 0) {
                    updv[i] = RDB_TRUE;
                    break;
                }
            }
            if (j >= argc) {
                Tcl_AppendResult(interp, "Invalid argument: ", argp, NULL);
                Tcl_Free(updv);
                return ret;
            }
        }

        ret = RDB_create_update_op(Tcl_GetStringFromObj(objv[1], NULL),
                argc, argtv, updv, "libdurotcl", "Duro_invoke_update_op",
                txtp, (size_t) len, txp);
        Tcl_Free(updv);
    } else {
        RDB_type *rtyp;

        ret = RDB_get_type(Tcl_GetStringFromObj(objv[3], NULL), txp,
                &rtyp);
        if (ret != RDB_OK) {
            Duro_dberror(interp, ret);
            return TCL_ERROR;
        }

        ret = RDB_create_ro_op(Tcl_GetStringFromObj(objv[1], NULL),
                 argc, argtv, rtyp, "libdurotcl", "Duro_invoke_ro_op",
                 txtp, (size_t) len, txp);
    }
    
    if (ret != RDB_OK) {
        Duro_dberror(interp, ret);
        return TCL_ERROR;
    }

    ret = TCL_OK;

cleanup:
    Tcl_Free((char *) argtv);
    return ret;
}

int
Duro_call_cmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int ret;
    int i;
    int argc;
    RDB_type **argtv;
    char *txstr;
    RDB_transaction *txp;
    Tcl_HashEntry *entryp;
    RDB_upd_op *op;
    RDB_object **argv = NULL;
    TclState *statep = (TclState *) data;

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv,
                "name tx arg argtype ?arg argtype ...?");
        return TCL_ERROR;
    }

    txstr = Tcl_GetStringFromObj(objv[2], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    argc = (objc - 3) / 2;
    argtv = (RDB_type **) Tcl_Alloc(sizeof (RDB_type *) * argc);

    for (i = 0; i < argc; i++) {
         ret = RDB_get_type(Tcl_GetString(objv[4 + i * 2]), txp, &argtv[i]);
         if (ret != RDB_OK) {
             Tcl_Free((char *) argtv);
             return TCL_ERROR;
         }
    }

    ret = _RDB_get_upd_op(Tcl_GetString(objv[1]), argc, argtv, txp, &op);
    if (ret != RDB_OK) {
        Tcl_Free((char *) argtv);
        Duro_dberror(interp, ret);
        return TCL_ERROR;
    }

    argv = (RDB_object **) Tcl_Alloc(sizeof (RDB_object *) * argc);

    for (i = 0; i < argc; i++) {
        Tcl_Obj *valobjp = objv[3 + i * 2];

        if (op->updv[i]) {
            /* Update argument - read variable */
            valobjp = Tcl_ObjGetVar2(interp, valobjp, NULL, 0);
        }

        argv[i] = (RDB_object *) Tcl_Alloc(sizeof (RDB_object));
        ret = Duro_tcl_to_duro(interp, valobjp, argtv[i], argv[i]);
        if (ret != TCL_OK) {
            Tcl_Free((char *) argtv);
            Tcl_Free((char *) argv);
            return ret;
        }
    }

    Tcl_Free((char *) argtv);

    ret = RDB_call_update_op(Tcl_GetStringFromObj(objv[1], NULL),
            argc, argv, txp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, ret);
        return TCL_ERROR;
    }

    /* Store updated values */
    for (i = 0; i < argc; i++) {
        if (op->updv[i]) {
            Tcl_Obj *valobjp = Duro_to_tcl(interp, argv[i]);

            if (valobjp == NULL)
                return TCL_ERROR;
            Tcl_ObjSetVar2(interp, objv[3 + i * 2], NULL, valobjp, 0);
        }
    }   
    for (i = 0; i < argc; i++) {
        RDB_destroy_obj(argv[i]);
        Tcl_Free((char *) argv[i]);
    }
    Tcl_Free((char *) argv);

    return TCL_OK;
}

int
Duro_invoke_update_op(const char *name, int argc, RDB_object *argv[],
        RDB_bool updv[], const void *iargp, size_t iarglen,
        RDB_transaction *txp)
{
    int ret;
    int i;
    Tcl_Obj *opdatap;
    Tcl_Obj *namelistp;
    Tcl_Obj *bodyp;
    RDB_environment *envp = RDB_db_env(RDB_tx_db(txp));
    Tcl_Interp *interp = (Tcl_Interp *) envp->user_data;

    /* Get operator data (arg names + body) */
    opdatap = Tcl_NewStringObj((CONST char *) iargp, iarglen);

    /* Get argument names and set arguments */

    ret = Tcl_ListObjIndex(interp, opdatap, 0, &namelistp);
    if (ret != TCL_OK)
        return RDB_INTERNAL;

    for (i = 0; i < argc; i++) {
        Tcl_Obj *argnamep;

        ret = Tcl_ListObjIndex(interp, namelistp, i, &argnamep);
        if (ret != TCL_OK)
            return RDB_INTERNAL;

        if (Tcl_ObjSetVar2(interp, argnamep, NULL,
                Duro_to_tcl(interp, argv[i]), 0) == NULL)
            return RDB_INTERNAL;
        if (ret != TCL_OK)
            return RDB_INTERNAL;
    }

    /* Get body */
    ret = Tcl_ListObjIndex(interp, opdatap, 1, &bodyp);
    if (ret != TCL_OK)
        return RDB_INTERNAL;

    /* Execute operator */
    ret = Tcl_EvalObjEx(interp, bodyp, 0);
    switch (ret) {
        case TCL_ERROR:
            RDB_errmsg(envp, "%s", Tcl_GetStringResult(interp));
            return RDB_INVALID_ARGUMENT;
        case TCL_BREAK:
            RDB_errmsg(envp, "invoked \"break\" outside of a loop");
            return RDB_INVALID_ARGUMENT;
        case TCL_CONTINUE:
            RDB_errmsg(envp, "invoked \"continue\" outside of a loop");
            return RDB_INVALID_ARGUMENT;
    }

    /* Store updated values */
    for (i = 0; i < argc; i++) {
        if (updv[i]) {
            Tcl_Obj *argnamep;
            Tcl_Obj *valobjp;

            ret = Tcl_ListObjIndex(interp, namelistp, i, &argnamep);
            if (ret != TCL_OK)
                return RDB_INTERNAL;

            valobjp = Tcl_ObjGetVar2(interp, argnamep, NULL, 0);

            ret = Duro_tcl_to_duro(interp, valobjp, RDB_obj_type(argv[i]),
                    argv[i]);
            if (ret != TCL_OK)
                return RDB_INTERNAL;
        }
    }

    return RDB_OK;
}

int
Duro_invoke_ro_op(const char *name, int argc, RDB_object *argv[],
        const void *iargp, size_t iarglen,
        RDB_transaction *txp, RDB_object *retvalp)
{
    int ret;
    int i;
    Tcl_Obj *opdatap;
    Tcl_Obj *namelistp;
    Tcl_Obj *bodyp;
    RDB_environment *envp = RDB_db_env(RDB_tx_db(txp));
    Tcl_Interp *interp = (Tcl_Interp *) envp->user_data;

    /* Get operator data (arg names + body) */
    opdatap = Tcl_NewStringObj((CONST char *) iargp, iarglen);

    /*
     * Get argument names and set arguments
     */

    ret = Tcl_ListObjIndex(interp, opdatap, 0, &namelistp);
    if (ret != TCL_OK) {
        RDB_errmsg(envp, "%s", Tcl_GetStringResult(interp));
        return RDB_INTERNAL;
    }

    for (i = 0; i < argc; i++) {
        Tcl_Obj *argnamep;

        ret = Tcl_ListObjIndex(interp, namelistp, i, &argnamep);
        if (ret != TCL_OK) {
            RDB_errmsg(envp, "%s", Tcl_GetStringResult(interp));
            return RDB_INTERNAL;
        }

        if (Tcl_ObjSetVar2(interp, argnamep, NULL,
                Duro_to_tcl(interp, argv[i]), 0) == NULL) {
            RDB_errmsg(envp, "%s", Tcl_GetStringResult(interp));
            return RDB_INTERNAL;
        }
        if (ret != TCL_OK) {
            RDB_errmsg(envp, "%s", Tcl_GetStringResult(interp));
            return RDB_INTERNAL;
        }
    }

    /* Get body */
    ret = Tcl_ListObjIndex(interp, opdatap, 1, &bodyp);
    if (ret != TCL_OK) {
        RDB_errmsg(envp, "%s", Tcl_GetStringResult(interp));
        return RDB_INTERNAL;
    }

    /* Execute operator */
    ret = Tcl_EvalObjEx(interp, bodyp, 0);
    switch (ret) {
        case TCL_ERROR:
            RDB_errmsg(envp, "%s", Tcl_GetStringResult(interp));
            return RDB_INVALID_ARGUMENT;
        case TCL_BREAK:
            RDB_errmsg(envp, "invoked \"break\" outside of a loop");
            return RDB_INVALID_ARGUMENT;
        case TCL_CONTINUE:
            RDB_errmsg(envp, "invoked \"continue\" outside of a loop");
            return RDB_INVALID_ARGUMENT;
    }

    /* Convert result */
    ret = Duro_tcl_to_duro(interp, Tcl_GetObjResult(interp),
            RDB_obj_type(retvalp), retvalp);
    if (ret != RDB_OK)
        return ret;

    return RDB_OK;
}
