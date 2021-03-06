/*
 * Copyright (C) 2003-2009, 2012 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "duro.h"
#include <rel/typeimpl.h>
#include <string.h>

static int
operator_create_cmd(ClientData data, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    RDB_bool update;
    int ret;
    int i;
    char *txstr;
    RDB_transaction *txp;
    Tcl_HashEntry *entryp;
    int argc;
    RDB_parameter *paramv;
    char *txtp;
    int len;
    Tcl_Obj *namelistp, *opdatap;
    TclState *statep = (TclState *) data;

    if (objc != 8) {
        Tcl_WrongNumArgs(interp, 2, objv,
                "name [-returns rtype | -updates updlist] arglist body txId");
        return TCL_ERROR;
    }

    RDB_clear_err(statep->current_ecp);

    if (strcmp (Tcl_GetString(objv[3]), "-updates") == 0) {
        update = RDB_TRUE;
    } else if (strcmp (Tcl_GetString(objv[3]), "-returns") == 0) {
        update = RDB_FALSE;
    } else {
        Tcl_SetResult(interp, "invalid option, must be -updates or -returns",
                TCL_STATIC);
        return TCL_ERROR;
    }

    txstr = Tcl_GetString(objv[7]);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    namelistp = Tcl_NewListObj(0, NULL);

    Tcl_ListObjLength(interp, objv[5], &argc);
    if (argc % 2 > 0) {
        Tcl_SetResult(interp, "invalid argument list", TCL_STATIC);
        return TCL_ERROR;
    }
    argc /= 2;
        
    paramv = (RDB_parameter *) Tcl_Alloc(sizeof(RDB_parameter) * argc);

    for (i = 0; i < argc; i++) {
        Tcl_Obj *typeobjp;
        Tcl_Obj *nameobjp;

        /*
         * Get argument and append it to list
         */
        ret = Tcl_ListObjIndex(interp, objv[5], i * 2, &nameobjp);
        if (ret != TCL_OK) {
            goto cleanup;
        }
        ret = Tcl_ListObjAppendElement(interp, namelistp, nameobjp);
        if (ret != TCL_OK)
            goto cleanup;

        ret = Tcl_ListObjIndex(interp, objv[5], i * 2 + 1, &typeobjp);
        if (ret != TCL_OK) {
            goto cleanup;
        }
        if (typeobjp == NULL) {
            Tcl_SetResult(interp, "Type missing", TCL_STATIC);
            ret = TCL_ERROR;
            goto cleanup;
        }

        paramv[i].typ = Duro_get_type(typeobjp, interp, statep->current_ecp, txp);
        if (paramv[i].typ == NULL) {
            ret = TCL_ERROR;
            goto cleanup;
        }
    }

    /*
     * Create list of argument names and op body. This is then stored
     * in the catalog.
     */
    opdatap = Tcl_NewListObj(1, &namelistp);
    ret = Tcl_ListObjAppendElement(interp, opdatap, objv[6]);
    if (ret != TCL_OK)
            goto cleanup;

    txtp = Tcl_GetStringFromObj(opdatap, &len);

    if (update) {
        int updlen;
        Tcl_Obj *argobjp;

        for (i = 0; i < argc; i++)
            paramv[i].update = RDB_FALSE;

        ret = Tcl_ListObjLength(interp, objv[4], &updlen);
        if (ret != TCL_OK) {
            goto cleanup;
        }

        for (i = 0; i < updlen; i++) {
            int j;
            char *argp;

            ret = Tcl_ListObjIndex(interp, objv[4], i, &argobjp);
            if (ret != TCL_OK) {
                goto cleanup;
            }
            argp = Tcl_GetString(argobjp);

            /*
             * Search arg name in list, set paramv[j].update to RDB_TRUE if found.
             */
            for (j = 0; j < argc; j++) {
                Tcl_Obj *updobjp;

                ret = Tcl_ListObjIndex(interp, namelistp, i, &updobjp);
                if (ret != TCL_OK) {
                    goto cleanup;
                }
                if (strcmp(Tcl_GetString(updobjp), argp) == 0) {
                    paramv[j].update = RDB_TRUE;
                    break;
                }
            }
            if (j >= argc) {
                Tcl_AppendResult(interp, "invalid argument: ", argp, NULL);
                ret = TCL_ERROR;
                goto cleanup;
            }
        }

        ret = RDB_create_update_op(Tcl_GetString(objv[2]),
                argc, paramv,
#ifdef _WIN32
                "durotcl",
#else
                "libdurotcl",
#endif
                "Duro_invoke_update_op",
                txtp, statep->current_ecp, txp);
    } else {
        RDB_type *rtyp = Duro_get_type(objv[4], interp, statep->current_ecp,
                txp);
        if (rtyp == NULL) {
            ret = TCL_ERROR;
            goto cleanup;
        }

        ret = RDB_create_ro_op(Tcl_GetString(objv[2]), argc, paramv, rtyp,
#ifdef _WIN32
                "durotcl",
#else
                "libdurotcl",
#endif
                 "Duro_invoke_ro_op", txtp,
                statep->current_ecp, txp);
    }    
    if (ret != RDB_OK) {
        Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
        ret = TCL_ERROR;
        goto cleanup;
    }

    ret = TCL_OK;

cleanup:
    Tcl_Free((char *) paramv);
    return ret;
}

static int
operator_drop_cmd(ClientData data, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *txstr;
    RDB_transaction *txp;
    Tcl_HashEntry *entryp;
    TclState *statep = (TclState *) data;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "name txId");
        return TCL_ERROR;
    }

    txstr = Tcl_GetString(objv[3]);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    ret = RDB_drop_op(Tcl_GetString(objv[2]), statep->current_ecp, txp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
        return TCL_ERROR;
    }

    return TCL_OK;
}

int
Duro_operator_cmd(ClientData data, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    TclState *statep = (TclState *) data;

    const char *sub_cmds[] = {
        "create", "drop", NULL
    };
    enum table_ix {
        create_ix, drop_ix
    };
    int index;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "arg ?arg ...?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], sub_cmds, "option", 0, &index)
            != RDB_OK) {
        return TCL_ERROR;
    }

    switch (index) {
        case create_ix:
            return operator_create_cmd(statep, interp, objc, objv);
        case drop_ix:
            return operator_drop_cmd(statep, interp, objc, objv);
    }
    return TCL_ERROR;
}

int
Duro_call_cmd(ClientData data, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    int i;
    int argc;
    char *txstr;
    RDB_transaction *txp;
    Tcl_HashEntry *entryp;
    RDB_operator *op;
    RDB_type **argtv = NULL;
    RDB_object **argv = NULL;
    TclState *statep = (TclState *) data;

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "name ?arg argtype ...? txId");
        return TCL_ERROR;
    }

    txstr = Tcl_GetString(objv[objc - 1]);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    argc = (objc - 3) / 2;
    argtv = (RDB_type **) Tcl_Alloc(sizeof (RDB_type *) * argc);

    for (i = 0; i < argc; i++) {
        argtv[i] = Duro_get_type(objv[3 + i * 2], interp, statep->current_ecp,
                txp);
        if (argtv[i] == NULL) {
            ret = TCL_ERROR;
            goto cleanup;
        }
    }

    op = RDB_get_update_op(Tcl_GetString(objv[1]), argc, argtv, NULL,
            statep->current_ecp, txp);
    if (op == NULL) {
        Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
        ret = TCL_ERROR;
        goto cleanup;
    }

    argv = (RDB_object **) Tcl_Alloc(sizeof (RDB_object *) * argc);
    for (i = 0; i < argc; i++)
        argv[i] = NULL;

    for (i = 0; i < argc; i++) {
        Tcl_Obj *valobjp = objv[2 + i * 2];

        if (RDB_get_parameter(op, i)->update && !RDB_type_is_relation(argtv[i])) {
            /* It's a non-relation update argument - read variable */
            valobjp = Tcl_ObjGetVar2(interp, valobjp, NULL, 0);
            if (valobjp == NULL) {
                ret = TCL_ERROR;
                Tcl_AppendResult(interp, "can't read \"",
                        Tcl_GetString(objv[2 + i * 2]),
                        "\": no such variable", NULL);
                goto cleanup;
            }
        }

        if (RDB_get_parameter(op, i)->update && RDB_type_is_relation(argtv[i])) {
            /* Updated relation argument - pass table */
            argv[i] = Duro_get_table(statep, interp, Tcl_GetString(valobjp), txp);
            if (argv[i] == NULL) {
                ret = TCL_ERROR;
                goto cleanup;
            }
        } else {
            argv[i] = (RDB_object *) Tcl_Alloc(sizeof (RDB_object));
            RDB_init_obj(argv[i]);
            ret = Duro_tcl_to_duro(interp, valobjp, argtv[i], argv[i],
                    statep->current_ecp, txp);
            if (ret != TCL_OK) {
                RDB_destroy_obj(argv[i], statep->current_ecp);
                Tcl_Free((char *) argv[i]);
                argv[i] = NULL;
                ret = TCL_ERROR;
                goto cleanup;
            }
            if (RDB_type_is_tuple(argtv[i]))
                argv[i]->typ = argtv[i];
        }
    }

    ret = RDB_call_update_op(op, argc, argv, statep->current_ecp, txp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, RDB_get_err(statep->current_ecp), txp);
        return TCL_ERROR;
    }

    /* Store updated values */
    for (i = 0; i < argc; i++) {
        if (RDB_get_parameter(op, i)->update && !RDB_type_is_relation(argtv[i])) {
            Tcl_Obj *valobjp = Duro_to_tcl(interp, argv[i],
                    statep->current_ecp, txp);
            if (valobjp == NULL) {
                ret = TCL_ERROR;
                goto cleanup;
            }
            Tcl_ObjSetVar2(interp, objv[2 + i * 2], NULL, valobjp, 0);
        }
    }

    ret = TCL_OK;

cleanup:
    if (argv != NULL) {
        for (i = 0; i < argc; i++) {
            if (argv[i] != NULL
                    && (!RDB_get_parameter(op, i)->update || !RDB_type_is_relation(argtv[i]))) {
                RDB_destroy_obj(argv[i], statep->current_ecp);
                Tcl_Free((char *) argv[i]);
            }
            if (!RDB_type_is_scalar(argtv[i])) {
                ret = RDB_del_nonscalar_type(argtv[i], statep->current_ecp);
            }
        }
        Tcl_Free((char *) argv);
    }
    Tcl_Free((char *) argtv);

    return ret;
}

static Tcl_Obj *
find_txid(RDB_transaction *txp, TclState *statep)
{
    Tcl_HashSearch search;

    Tcl_HashEntry *entryp = Tcl_FirstHashEntry(&statep->txs, &search);
    while (entryp != NULL) {
        if ((RDB_transaction *) Tcl_GetHashValue(entryp) == txp) {
            char *txid = Tcl_GetHashKey(&statep->txs, entryp);
            return Tcl_NewStringObj(txid, -1);
        }
        entryp = Tcl_NextHashEntry(&search);
    }
    return NULL;
}

int
Duro_invoke_update_op(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    int i;
    Tcl_Obj *opdatap;
    Tcl_Obj *nametop;
    Tcl_Obj *namelistp;
    Tcl_Obj *bodytop;
    Tcl_Obj *txtop;
    Tcl_Obj *procargv[5];
    Tcl_Obj **opargv;
    Tcl_Obj *msgobjp;
    Tcl_CmdInfo cmdinfo;
    TclState *statep;
    RDB_exec_context *oldecp;
    RDB_bool issetter = RDB_is_setter(op);
    Tcl_Interp *interp = RDB_ec_property(ecp, "TCL_INTERP");
    if (interp == NULL) {
        RDB_raise_resource_not_found("Tcl interpreter not found", ecp);
        return RDB_ERROR;
    }

    nametop = Tcl_NewStringObj(RDB_operator_name(op), -1);

    /* Get operator data (arg names + body) */
    opdatap = Tcl_NewStringObj((CONST char *) RDB_operator_source(op),
            strlen(RDB_operator_source(op)));

    /* Get argument name list */
    ret = Tcl_ListObjIndex(interp, opdatap, 0, &namelistp);
    if (ret != TCL_OK) {
        RDB_raise_internal("invalid argument names", ecp);
        return RDB_ERROR;
    }

    /* Add tx argument */
    ret = Tcl_ListObjAppendElement(interp, namelistp,
            Tcl_NewStringObj("tx", -1));
    if (ret != TCL_OK) {
        RDB_raise_internal("could not add tx", ecp);
        return RDB_ERROR;
    }

    if (!Tcl_GetCommandInfo(interp, "duro::operator", &cmdinfo)) {
        RDB_raise_internal("could not get command info", ecp);
        return RDB_ERROR;
    }
    statep = (TclState *) cmdinfo.objClientData;

    /* Find tx id */
    txtop = find_txid(txp, statep);
    if (txtop == NULL) {
        RDB_raise_internal("transaction not found", ecp);
        return RDB_ERROR;
    }

    /* Get body */
    ret = Tcl_ListObjIndex(interp, opdatap, 1, &bodytop);
    if (ret != TCL_OK) {
        RDB_raise_internal("could not get operator body", ecp);
        return RDB_ERROR;
    }

    /*
     * Create a command by executing the 'proc' command
     * Increase reference counters before passing strings
     * to Tcl_EvalObjv().
     */
    procargv[0] = Tcl_NewStringObj("proc", -1);
    Tcl_IncrRefCount(procargv[0]);
    procargv[1] = nametop;
    Tcl_IncrRefCount(nametop);
    procargv[2] = namelistp;
    procargv[3] = bodytop;

    ret = Tcl_EvalObjv(interp, 4, procargv, 0);

    Tcl_DecrRefCount(procargv[0]);

    if (ret != TCL_OK) {
        Tcl_DecrRefCount(nametop);
        msgobjp = Tcl_NewStringObj("proc command failed: ", -1);
        Tcl_AppendObjToObj(msgobjp, Tcl_GetObjResult(interp));
        RDB_raise_internal(Tcl_GetString(msgobjp), ecp);
        return RDB_ERROR;
    }

    /*
     * Set arguments
     */

    opargv = (Tcl_Obj **) Tcl_Alloc(sizeof (Tcl_Obj *) * (argc + 2));

    opargv[0] = nametop;

    for (i = 0; i < argc; i++) {
        RDB_type *typ = RDB_obj_type(argv[i]);
        if ((RDB_type_is_relation(typ)) && RDB_get_parameter(op, i)->update) {
            if (RDB_table_name(argv[i]) == NULL) {
                /* Only possible when it's a setter */

                ret = RDB_set_table_name(argv[i], "duro_t", ecp, txp);
                if (ret != RDB_OK) {
                    Tcl_Free((char *) opargv);
                    Tcl_DecrRefCount(nametop);
                    return RDB_ERROR;
                }
                ret = Duro_add_table(interp, statep, argv[i], "duro_t",
                        RDB_db_env(RDB_tx_db(txp)));
                if (ret != TCL_OK) {
                    Tcl_Free((char *) opargv);
                    RDB_raise_internal("passing table arg failed", ecp);
                    Tcl_DecrRefCount(nametop);
                    return RDB_ERROR;
                }
            }
            opargv[i + 1] = Tcl_NewStringObj(RDB_table_name(argv[i]), -1);
        } else {
            Tcl_Obj *argtop = issetter && i == 0 ?
                    Duro_irep_to_tcl(interp, argv[i], ecp, txp)
                    : Duro_to_tcl(interp, argv[i], ecp, txp);

            if (RDB_get_parameter(op, i)->update) {
                char buf[11];
                Tcl_Obj *varnamep;

                sprintf(buf, "duro_%05d", i);
                varnamep = Tcl_NewStringObj(buf, 10);
                if (Tcl_ObjSetVar2(interp, varnamep, NULL, argtop, 0) == NULL) {
                    Tcl_Free((char *) opargv);
                    RDB_raise_internal("passing arg failed", ecp);
                    Tcl_DecrRefCount(nametop);
                    return RDB_ERROR;
                }
                opargv[i + 1] = varnamep;
            } else {
                opargv[i + 1] = argtop;
            }
        }
    }

    opargv[argc + 1] = txtop;

    /* Set execution context, store old one */
    oldecp = statep->current_ecp;
    statep->current_ecp = ecp;

    /* Execute operator by invoking the Tcl procedure just created */
    ret = Tcl_EvalObjv(interp, argc + 2, opargv, 0);

    Tcl_DecrRefCount(nametop);

    statep->current_ecp = oldecp;

    Tcl_Free((char *) opargv);
    
    switch (ret) {
        case TCL_ERROR:
            msgobjp  = Tcl_NewStringObj("invoking Tcl procedure failed: ", -1);
            Tcl_AppendObjToObj(msgobjp, Tcl_GetObjResult(interp));
            RDB_raise_invalid_argument(Tcl_GetString(msgobjp), ecp);
            return RDB_ERROR;
        case TCL_BREAK:
            RDB_raise_invalid_argument("invoked \"break\" outside of a loop", ecp);
            return RDB_ERROR;
        case TCL_CONTINUE:
            RDB_raise_invalid_argument("invoked \"continue\" outside of a loop", ecp);
            return RDB_ERROR;
    }

    /* Store updated values */
    for (i = 0; i < argc; i++) {
        if (RDB_get_parameter(op, i)->update && !RDB_type_is_relation(RDB_obj_type(argv[i]))) {
            Tcl_Obj *valobjp;
            RDB_type *convtyp;
            RDB_type *argtyp = RDB_obj_type(argv[i]);
            char buf[11];
            Tcl_Obj *varnamep;

            sprintf(buf, "duro_%05d", i);
            varnamep = Tcl_NewStringObj(buf, 10);

            valobjp = Tcl_ObjGetVar2(interp, varnamep, NULL, 0);

            if (RDB_type_is_scalar(argtyp)) {
                if (issetter && i == 0) {
                    /* It's the argument of setter, so use internal rep */
                    convtyp = argtyp->def.scalar.arep;
                } else {
                    convtyp = argtyp;
                }
            } else {
                convtyp = argtyp;
            }

            ret = Duro_tcl_to_duro(interp, valobjp, convtyp, argv[i], ecp, txp);
            argv[i]->typ = argtyp;

            Tcl_UnsetVar(interp, Tcl_GetString(varnamep), 0);

            if (ret != TCL_OK) {
                RDB_raise_internal("storing updated arg failed", ecp);
                return RDB_ERROR;
            }
        }
    }

    return RDB_OK;
}

int
Duro_invoke_ro_op(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    int ret;
    int i;
    Tcl_Obj *opdatap;
    Tcl_Obj *nametop;
    Tcl_Obj *namelistp;
    Tcl_Obj *bodytop;
    Tcl_Obj *procargv[4];
    Tcl_Obj **opargv;
    RDB_type *convtyp;
    Tcl_CmdInfo cmdinfo;
    TclState *statep;
    RDB_exec_context *oldecp;
    Tcl_Obj *msgobjp;
    Tcl_Obj *txtop = NULL;
    RDB_type *rtyp = RDB_return_type(op);
    Tcl_Interp *interp = RDB_ec_property(ecp, "TCL_INTERP");
    if (interp == NULL) {
        RDB_raise_resource_not_found("Tcl interpreter not found", ecp);
        return RDB_ERROR;
    }

    nametop = Tcl_NewStringObj(RDB_operator_name(op), -1);

    /* Get operator data (arg names + body) */
    opdatap = Tcl_NewStringObj((CONST char *) RDB_operator_source(op),
            strlen(RDB_operator_source(op)));

    /* Get argument name list */
    ret = Tcl_ListObjIndex(interp, opdatap, 0, &namelistp);
    if (ret != TCL_OK) {
        RDB_raise_internal("invalid argument names", ecp);
        return RDB_ERROR;
    }

    if (!Tcl_GetCommandInfo(interp, "duro::operator", &cmdinfo)) {
        RDB_raise_internal("could not get command info", ecp);
        return RDB_ERROR;
    }
    statep = (TclState *) cmdinfo.objClientData;

    /* Try to find tx id */
    txtop = find_txid(txp, statep);
    if (txtop != NULL) {
        /* Add tx argument */
        ret = Tcl_ListObjAppendElement(interp, namelistp, Tcl_NewStringObj("tx", -1));
        if (ret != TCL_OK) {
            RDB_raise_internal("could not add tx", ecp);
            return RDB_ERROR;
        }
    }

    /* Get body */
    ret = Tcl_ListObjIndex(interp, opdatap, 1, &bodytop);
    if (ret != TCL_OK) {
        RDB_raise_internal("could not get operator body", ecp);
        return RDB_ERROR;
    }

    /*
     * Create a command by executing the 'proc' command
     * Increase reference counters before passing strings
     * to Tcl_EvalObjv().
     */
    procargv[0] = Tcl_NewStringObj("proc", -1);
    Tcl_IncrRefCount(procargv[0]);
    procargv[1] = nametop;
    Tcl_IncrRefCount(nametop);
    procargv[2] = namelistp;
    procargv[3] = bodytop;

    ret = Tcl_EvalObjv(interp, 4, procargv, 0);

    Tcl_DecrRefCount(procargv[0]);

    if (ret != TCL_OK) {
        Tcl_DecrRefCount(nametop);
        msgobjp = Tcl_NewStringObj("proc command failed: ", -1);
        Tcl_AppendObjToObj(msgobjp, Tcl_GetObjResult(interp));
        RDB_raise_internal(Tcl_GetString(msgobjp), ecp);
        return RDB_ERROR;
    }

    /*
     * Set arguments
     */

    opargv = (Tcl_Obj **) Tcl_Alloc(sizeof (Tcl_Obj *) * (argc + 2));

    opargv[0] = nametop;

    for (i = 0; i < argc; i++) {
        /* If the operator is a getter or cmp, pass actual rep */
        opargv[i + 1] = (RDB_is_getter(op) && i == 0)
                || RDB_is_comparer(op) ?
                Duro_irep_to_tcl(interp, argv[i], ecp, txp)
                : Duro_to_tcl(interp, argv[i], ecp, txp);
    }

    if (txtop != NULL) {
        opargv[argc + 1] = txtop;
    }

    /* Set execution context, store old one */
    oldecp = statep->current_ecp;
    statep->current_ecp = ecp;

    /* Execute operator by invoking the Tcl procedure just created */
    ret = Tcl_EvalObjv(interp, txtop != NULL ? argc + 2 : argc + 1, opargv, 0);

    Tcl_DecrRefCount(nametop);
    Tcl_Free((char *) opargv);

    statep->current_ecp = oldecp;

    switch (ret) {
        case TCL_ERROR:
            msgobjp  = Tcl_NewStringObj("invoking Tcl procedure failed: ", -1);
            Tcl_AppendObjToObj(msgobjp, Tcl_GetObjResult(interp));
            RDB_raise_invalid_argument(Tcl_GetString(msgobjp), ecp);
            return RDB_ERROR;
        case TCL_BREAK:
            RDB_raise_invalid_argument("invoked \"break\" outside of a loop",
                    ecp);
            return RDB_ERROR;
        case TCL_CONTINUE:
            RDB_raise_invalid_argument(
                    "invoked \"continue\" outside of a loop", ecp);
            return RDB_ERROR;
    }

    /*
     * Convert result
     */
    if (RDB_type_is_scalar(rtyp)) {
        if (RDB_is_selector(op)) {
            /* It's a selector, so use internal rep */
            convtyp = rtyp->def.scalar.arep;
        } else {
            convtyp = rtyp;
        }
    } else {
        convtyp = rtyp;
    }
    ret = Duro_tcl_to_duro(interp, Tcl_GetObjResult(interp),
            convtyp, retvalp, ecp, txp);
    if (!RDB_type_is_relation(rtyp))
        retvalp->typ = rtyp;
    if (ret != TCL_OK) {
        RDB_raise_invalid_argument("converting return value failed", ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}
