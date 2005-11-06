/*
 * $Id$
 *
 * Copyright (C) 2003-2005 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "duro.h"
#include <rel/internal.h>
#include <dli/parse.h>
#include <dli/tabletostr.h>
#include <gen/strfns.h>
#include <string.h>

RDB_table *
Duro_parse_table_utf(Tcl_Interp *interp, const char *s, void *arg,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_table *tbp;
    int srclen = strlen(s);
    int dstlen = (srclen + 1) * 2;
    char *dst = Tcl_Alloc(dstlen);

    ret = Tcl_UtfToExternal(interp, NULL, s, strlen(s), 0, NULL, dst, dstlen,
            NULL, NULL, NULL);
    if (ret != TCL_OK)
        return NULL;

    tbp = RDB_parse_table(dst, Duro_get_ltable, arg, ecp, txp);
    Tcl_Free(dst);
    if (tbp == NULL) {
        Duro_dberror(interp, ecp, txp);
        return NULL;
    }
    return tbp;
}

int
Duro_tcl_drop_ltable(table_entry *tbep, Tcl_HashEntry *entryp,
        RDB_exec_context *ecp)
{
    int ret;

    Tcl_DeleteHashEntry(entryp);
    ret = RDB_drop_table(tbep->tablep, ecp, NULL);
    Tcl_Free((char *) tbep);
    return ret;
}

static int
table_create_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    int attrc, keyc;
    int i, j;
    char *flagstr;
    char *txstr;
    RDB_transaction *txp;
    Tcl_HashEntry *entryp;
    RDB_table *tbp;
    RDB_attr *attrv = NULL;
    RDB_string_vec *keyv = NULL;
    RDB_bool persistent = RDB_TRUE;

    if (objc == 7) {
        /*
         * Read flag
         */
        flagstr = Tcl_GetString(objv[2]);
        if (strcmp(flagstr, "-global") == 0) {
        } else if (strcmp(flagstr, "-local") == 0) {
            persistent = RDB_FALSE;
        } else {
            Tcl_SetResult(interp,
                    "Wrong flag: must be -global or -local", TCL_STATIC);
            return TCL_ERROR;
        }
    } else if (objc != 6) {
        Tcl_WrongNumArgs(interp, 2, objv, "?flag? tablename attrs keys tx");
        return TCL_ERROR;
    }

    txstr = Tcl_GetStringFromObj(objv[objc - 1], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    ret = Tcl_ListObjLength(interp, objv[objc - 3], &attrc);
    if (ret != TCL_OK)
        goto cleanup;
    attrv = (RDB_attr *) Tcl_Alloc(sizeof (RDB_attr) * attrc);
    for (i = 0; i < attrc; i++) {
        attrv[i].name = NULL;
        attrv[i].defaultp = NULL;
    }
    /*
     * Read attribute defs
     */
    for (i = 0; i < attrc; i++) {
        Tcl_Obj *attrobjp, *nameobjp, *typeobjp;
        int llen;
    
        ret = Tcl_ListObjIndex(interp, objv[objc - 3], i, &attrobjp);
        if (ret != TCL_OK)
            goto cleanup;

        ret = Tcl_ListObjLength(interp, attrobjp, &llen);
        if (ret != TCL_OK)
            goto cleanup;
        if (llen < 2 || llen > 4) {
            Tcl_AppendResult(interp, "invalid attribute definition: ",
                    Tcl_GetString(attrobjp), NULL);
            ret = TCL_ERROR;
            goto cleanup;
        }
        
        Tcl_ListObjIndex(interp, attrobjp, 0, &nameobjp);
        Tcl_ListObjIndex(interp, attrobjp, 1, &typeobjp);
        attrv[i].name = RDB_dup_str(Tcl_GetStringFromObj(nameobjp, NULL));
        attrv[i].typ = Duro_get_type(typeobjp, interp, statep->current_ecp,
                txp);
        if (attrv[i].typ == NULL) {
            ret = TCL_ERROR;
            goto cleanup;
        }
        if (llen >= 3) {
            /*
             * Read default value
             */
            Tcl_Obj *defvalobjp;

            attrv[i].defaultp = (RDB_object *) Tcl_Alloc(sizeof (RDB_object));
            RDB_init_obj(attrv[i].defaultp);
            Tcl_ListObjIndex(interp, attrobjp, 2, &defvalobjp);
            ret = Duro_tcl_to_duro(interp, defvalobjp, attrv[i].typ,
                    attrv[i].defaultp, statep->current_ecp, txp);
            if (ret != RDB_OK) {
                goto cleanup;
            }
        } else {
            attrv[i].defaultp = NULL;
        }
        attrv[i].options = 0;
    }

    ret = Tcl_ListObjLength(interp, objv[objc - 2], &keyc);
    if (ret != TCL_OK)
        goto cleanup;
    keyv = (RDB_string_vec *) Tcl_Alloc(sizeof (RDB_string_vec) * keyc);
    for (i = 0; i < keyc; i++) {
        Tcl_Obj *keyobjp;
        Tcl_Obj *keyattrobjp;

        Tcl_ListObjIndex(interp, objv[objc - 2], i, &keyobjp);
        Tcl_ListObjLength(interp, keyobjp, &keyv[i].strc);
        keyv[i].strv = (char **) Tcl_Alloc(sizeof (char *) * keyv[i].strc);
        for (j = 0; j < keyv[i].strc; j++) {
            Tcl_ListObjIndex(interp, keyobjp, j, &keyattrobjp);
            keyv[i].strv[j] = RDB_dup_str(Tcl_GetStringFromObj(keyattrobjp, NULL));
        }
    }
    
    tbp = RDB_create_table(Tcl_GetStringFromObj(objv[objc - 4], NULL), persistent,
            attrc, attrv, keyc, keyv, statep->current_ecp, txp);
    if (tbp == NULL) {
        Duro_dberror(interp, statep->current_ecp, txp);
        ret = TCL_ERROR;
        goto cleanup;
    }

    if (!persistent) {
        ret = Duro_add_table(interp, statep, tbp, RDB_table_name(tbp),
                RDB_db_env(RDB_tx_db(txp)));
        if (ret != TCL_OK) {
            RDB_drop_table(tbp, statep->current_ecp, txp);
            goto cleanup;
        }
    }

cleanup:
    if (attrv != NULL) {
        for (i = 0; i < attrc; i++) {
            free(attrv[i].name);
            if (attrv[i].defaultp != NULL) {
                RDB_destroy_obj(attrv[i].defaultp, statep->current_ecp);
                Tcl_Free((char *) attrv[i].defaultp);
            }
        }
        Tcl_Free((char *) attrv);
    }
    if (keyv != NULL) {
        for (i = 0; i < keyc; i++) {
            for (j = 0; j < keyv[i].strc; j++)
                free(keyv[i].strv[j]);
            Tcl_Free((char *) keyv[i].strv);
        }
        Tcl_Free((char *) keyv);
    }
                
    return ret;
}

static int
table_expr_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *flagstr;
    char *txstr;
    RDB_transaction *txp;
    Tcl_HashEntry *entryp;
    RDB_table *tbp;
    RDB_bool persistent = RDB_FALSE;

    if (objc == 6) {
        flagstr = Tcl_GetString(objv[2]);
        if (strcmp(flagstr, "-global") == 0) {
            persistent = RDB_TRUE;
        } else if (strcmp(flagstr, "-local") == 0) {
            /* do nothing */
        } else {
            Tcl_SetResult(interp,
                    "Wrong flag: must be -global or -local", TCL_STATIC);
            return TCL_ERROR;
        }
    } else if (objc != 5) {
        Tcl_WrongNumArgs(interp, 2, objv, "?flag? tablename expression tx");
        return TCL_ERROR;
    }

    txstr = Tcl_GetStringFromObj(objv[objc - 1], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    tbp = Duro_parse_table_utf(interp, Tcl_GetString(objv[objc - 2]), statep,
            statep->current_ecp, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }

    ret = RDB_set_table_name(tbp, Tcl_GetStringFromObj(objv[objc - 3], NULL),
            statep->current_ecp, txp);
    if (ret != RDB_OK) {
        RDB_drop_table(tbp, statep->current_ecp, txp);
        Duro_dberror(interp, statep->current_ecp, txp);
        return TCL_ERROR;
    }

    if (persistent) {
        ret = RDB_add_table(tbp, statep->current_ecp, txp);
        if (ret != RDB_OK) {
            RDB_drop_table(tbp, statep->current_ecp, txp);
            Duro_dberror(interp, statep->current_ecp, txp);
            return TCL_ERROR;
        }            
    } else {
        ret = Duro_add_table(interp, statep, tbp, RDB_table_name(tbp),
                RDB_db_env(RDB_tx_db(txp)));
        if (ret != TCL_OK) {
            RDB_drop_table(tbp, statep->current_ecp, txp);
            return ret;
        }
    }

    return TCL_OK;
}

static int
table_drop_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename tx");
        return TCL_ERROR;
    }

    name = Tcl_GetStringFromObj(objv[2], NULL);
    txstr = Tcl_GetStringFromObj(objv[3], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }

    if (tbp->is_persistent) {
        ret = RDB_drop_table(tbp, statep->current_ecp, txp);
        if (ret != RDB_OK) {
            Duro_dberror(interp, statep->current_ecp, txp);
            return TCL_ERROR;
        }
    } else {
        Tcl_HashSearch search;

        /*
         * Check if there is another local table which depends on this table
         */
        entryp = Tcl_FirstHashEntry(&statep->ltables, &search);
        while (entryp != NULL) {
            table_entry *iep = (table_entry *) Tcl_GetHashValue(entryp);

            if (iep->tablep != tbp && _RDB_table_refers(iep->tablep, tbp)) {
                Tcl_AppendResult(interp, "Cannot drop table ", tbp->name,
                        ": ", iep->tablep->name, " depends on it", NULL);
                return TCL_ERROR;
            }
            entryp = Tcl_NextHashEntry(&search);
        }

        entryp = Tcl_FindHashEntry(&statep->ltables, name);
        ret = Duro_tcl_drop_ltable((table_entry *)Tcl_GetHashValue(entryp),
                entryp, statep->current_ecp);
        if (ret != RDB_OK) {
            Duro_dberror(interp, statep->current_ecp, txp);
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

int
Duro_delete_cmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;
    RDB_expression *wherep;
    TclState *statep = (TclState *) data;

    if (objc < 3 || objc > 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "table ?where? tx");
        return TCL_ERROR;
    }

    name = Tcl_GetStringFromObj(objv[1], NULL);

    txstr = Tcl_GetStringFromObj(objv[objc - 1], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }

    if (objc == 4) {
        /* Read where-conditon */
        wherep = Duro_parse_expr_utf(interp, Tcl_GetString(objv[2]), statep,
                statep->current_ecp, txp);
        if (wherep == NULL) {
            return TCL_ERROR;
        }
    } else {
        wherep = NULL;
    }

    ret = RDB_delete(tbp, wherep, statep->current_ecp, txp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, statep->current_ecp, txp);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static int
table_contains_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;
    int attrcount;
    int i;
    RDB_object tpl;
    RDB_type *typ;
    RDB_bool b;

    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename tuple tx");
        return TCL_ERROR;
    }

    name = Tcl_GetStringFromObj(objv[2], NULL);

    txstr = Tcl_GetStringFromObj(objv[4], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }
    typ = RDB_table_type(tbp);

    Tcl_ListObjLength(interp, objv[3], &attrcount);
    if (attrcount % 2 != 0) {
        Tcl_AppendResult(interp, "invalid tuple value: ",
                Tcl_GetString(objv[3]), NULL);
        return TCL_ERROR;
    } 

    RDB_init_obj(&tpl);
    for (i = 0; i < attrcount; i += 2) {
        Tcl_Obj *nameobjp, *valobjp;
        RDB_type *attrtyp;
        char *attrname;
        RDB_object obj;

        RDB_init_obj(&obj);

        Tcl_ListObjIndex(interp, objv[3], i, &nameobjp);
        attrname = Tcl_GetStringFromObj(nameobjp, NULL);
        attrtyp = RDB_type_attr_type(typ, attrname);
        if (attrtyp == NULL) {
            Tcl_AppendResult(interp, "invalid attribute: ", attrname, NULL);
            ret = TCL_ERROR;
            RDB_destroy_obj(&obj, statep->current_ecp);
            goto cleanup;
        }

        Tcl_ListObjIndex(interp, objv[3], i + 1, &valobjp);
        ret = Duro_tcl_to_duro(interp, valobjp, attrtyp, &obj,
                statep->current_ecp, txp);
        if (ret != TCL_OK) {
            RDB_destroy_obj(&obj, statep->current_ecp);
            goto cleanup;
        }

        RDB_tuple_set(&tpl, attrname, &obj, statep->current_ecp);

        RDB_destroy_obj(&obj, statep->current_ecp);
    }

    ret = RDB_table_contains(tbp, &tpl, statep->current_ecp, txp, &b);
    if (ret == RDB_OK) {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(b));
        ret = TCL_OK;        
    } else {
        Duro_dberror(interp, statep->current_ecp, txp);
        ret = TCL_ERROR;
    }

cleanup:
    RDB_destroy_obj(&tpl, statep->current_ecp);

    return ret;
}

static int
table_add_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename tx");
        return TCL_ERROR;
    }

    name = Tcl_GetString(objv[2]);

    txstr = Tcl_GetStringFromObj(objv[3], NULL);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }

    ret = RDB_add_table(tbp, statep->current_ecp, txp);
    if (ret != RDB_OK) {
        Duro_dberror(interp, statep->current_ecp, txp);
        return TCL_ERROR;
    }

    return TCL_OK;
}

static Tcl_Obj *
type_to_tobj(Tcl_Interp *interp, const RDB_type *typ)
{
    Tcl_Obj *listobjp;
    int i;
    RDB_type *tpltyp;
    Tcl_Obj *typobjp;

    if (RDB_type_is_scalar(typ))
        return Tcl_NewStringObj(RDB_type_name(typ), strlen(RDB_type_name(typ)));
    switch (typ->kind) {
        case RDB_TP_TUPLE:
            listobjp = Tcl_NewListObj(0, NULL);
            if (listobjp == NULL)
                return NULL;
            Tcl_ListObjAppendElement(interp, listobjp,
                    Tcl_NewStringObj("tuple", 5));
            for (i = 0; i < typ->var.tuple.attrc; i++) {
                Tcl_Obj *attrobjp = Tcl_NewListObj(0, NULL);

                Tcl_ListObjAppendElement(interp, attrobjp,
                        Tcl_NewStringObj(typ->var.tuple.attrv[i].name,
                                strlen(typ->var.tuple.attrv[i].name)));
                typobjp = type_to_tobj(interp, typ->var.tuple.attrv[i].typ);
                if (typobjp == NULL)
                    return NULL;
                Tcl_ListObjAppendElement(interp, attrobjp, typobjp);
                Tcl_ListObjAppendElement(interp, listobjp, attrobjp);
            }
            break;
        case RDB_TP_RELATION:
            tpltyp = typ->var.basetyp;
            listobjp = Tcl_NewListObj(0, NULL);
            if (listobjp == NULL)
                return NULL;
            Tcl_ListObjAppendElement(interp, listobjp,
                    Tcl_NewStringObj("relation", 8));
            for (i = 0; i < tpltyp->var.tuple.attrc; i++) {
                Tcl_Obj *attrobjp = Tcl_NewListObj(0, NULL);

                Tcl_ListObjAppendElement(interp, attrobjp,
                        Tcl_NewStringObj(tpltyp->var.tuple.attrv[i].name,
                                strlen(tpltyp->var.tuple.attrv[i].name)));
                typobjp = type_to_tobj(interp, tpltyp->var.tuple.attrv[i].typ);
                if (typobjp == NULL)
                    return NULL;
                Tcl_ListObjAppendElement(interp, attrobjp, typobjp);
                Tcl_ListObjAppendElement(interp, listobjp, attrobjp);
            }
            break;
        case RDB_TP_ARRAY:
            listobjp = Tcl_NewListObj(0, NULL);
            if (listobjp == NULL)
                return NULL;
            Tcl_ListObjAppendElement(interp, listobjp,
                    Tcl_NewStringObj("array", 5));
            typobjp = type_to_tobj(interp, typ->var.basetyp);
            if (typobjp == NULL)
                return NULL;
            Tcl_ListObjAppendElement(interp, listobjp, typobjp);
            break;
        default:
            return NULL;
    }
    return listobjp;
}

static int
table_attrs_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    int i;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;
    RDB_type *tuptyp;
    Tcl_Obj *listobjp;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename tx");
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

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }
    tuptyp = RDB_table_type(tbp)->var.basetyp;

    listobjp = Tcl_NewListObj(0, NULL);
    for (i = 0; i < tuptyp->var.tuple.attrc; i++) {
        Tcl_Obj *typobjp;
        Tcl_Obj *sublistobjp = Tcl_NewListObj(0, NULL);
        char *name = tuptyp->var.tuple.attrv[i].name;

        ret = Tcl_ListObjAppendElement(interp, sublistobjp,
                Tcl_NewStringObj(name, strlen(name)));
        if (ret != TCL_OK)
            return ret;
        typobjp = type_to_tobj(interp, tuptyp->var.tuple.attrv[i].typ);
        if (typobjp == NULL)
            return TCL_ERROR;
        ret = Tcl_ListObjAppendElement(interp, sublistobjp, typobjp);
        if (ret != TCL_OK)
            return ret;

        /* If there is a default value, add it to sublist */
        if (tuptyp->var.tuple.attrv[i].defaultp != NULL) {
            Tcl_Obj *defp = Duro_to_tcl(interp,
                    tuptyp->var.tuple.attrv[i].defaultp, statep->current_ecp,
                    txp);
            if (defp == NULL)
                return TCL_ERROR;
            ret = Tcl_ListObjAppendElement(interp, sublistobjp, defp);
            if (ret != TCL_OK)
                return ret;
        }

        ret = Tcl_ListObjAppendElement(interp, listobjp, sublistobjp);
        if (ret != TCL_OK)
            return ret;
    }

    Tcl_SetObjResult(interp, listobjp);
    return TCL_OK;
}

static int
table_def_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;
    RDB_object defobj;
    Tcl_Obj *deftobjp;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename tx");
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

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }

    RDB_init_obj(&defobj);
    ret = _RDB_table_to_str(&defobj, tbp, statep->current_ecp, txp, 0);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&defobj, statep->current_ecp);
        Duro_dberror(interp, statep->current_ecp, txp);
        return TCL_ERROR;
    }
    deftobjp = Duro_to_tcl(interp, &defobj, statep->current_ecp, txp);
    RDB_destroy_obj(&defobj, statep->current_ecp);
    if (deftobjp == NULL)
        return TCL_ERROR;

    Tcl_SetObjResult(interp, deftobjp);
    return TCL_OK;
}

static int
table_showplan_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp, *ntbp;
    RDB_object defobj;
    Tcl_Obj *deftobjp;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename tx");
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

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }

    ret = _RDB_optimize(tbp, 0, NULL, statep->current_ecp, txp, &ntbp);
    if (ret != RDB_OK) {
        /* !! Duro_dberror */
        return TCL_ERROR;
    }
    RDB_init_obj(&defobj);
    ret = _RDB_table_to_str(&defobj, ntbp, statep->current_ecp, txp,
            RDB_SHOW_INDEX);
    if (RDB_table_name(ntbp) == NULL)
        RDB_drop_table(ntbp, statep->current_ecp, txp);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&defobj, statep->current_ecp);
        Duro_dberror(interp, statep->current_ecp, txp);
        return TCL_ERROR;
    }
    deftobjp = Duro_to_tcl(interp, &defobj, statep->current_ecp, txp);
    RDB_destroy_obj(&defobj, statep->current_ecp);
    if (deftobjp == NULL)
        return TCL_ERROR;

    Tcl_SetObjResult(interp, deftobjp);
    return TCL_OK;
}

static int
table_keys_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    int i, j;
    char *name;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;
    int keyc;
    RDB_string_vec *keyv;
    Tcl_Obj *listobjp;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename tx");
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

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }
    keyc = RDB_table_keys(tbp, &keyv);
    if (keyc < 0) {
        /* Duro_dberror(interp, txp, keyc); !! */
        return TCL_ERROR;
    }

    listobjp = Tcl_NewListObj(0, NULL);
    for (i = 0; i < keyc; i++) {
        Tcl_Obj *ilistobjp = Tcl_NewListObj(0, NULL);

        for (j = 0; j < keyv[i].strc; j++) {
            ret = Tcl_ListObjAppendElement(interp, ilistobjp,
               Tcl_NewStringObj(keyv[i].strv[j], strlen(keyv[i].strv[j])));
            if (ret != TCL_OK)
                return ret;
        }
        ret = Tcl_ListObjAppendElement(interp, listobjp, ilistobjp);
        if (ret != TCL_OK)
            return ret;
    }

    Tcl_SetObjResult(interp, listobjp);
    return TCL_OK;
}

static int
table_rename_cmd(TclState *statep, Tcl_Interp *interp, int objc,
        Tcl_Obj *CONST objv[])
{
    int ret;
    char *name;
    char *newname;
    char *txstr;
    Tcl_HashEntry *entryp;
    RDB_transaction *txp;
    RDB_table *tbp;

    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 2, objv, "tablename newname tx");
        return TCL_ERROR;
    }

    name = Tcl_GetString(objv[2]);
    newname = Tcl_GetString(objv[3]);
    txstr = Tcl_GetString(objv[4]);
    entryp = Tcl_FindHashEntry(&statep->txs, txstr);
    if (entryp == NULL) {
        Tcl_AppendResult(interp, "Unknown transaction: ", txstr, NULL);
        return TCL_ERROR;
    }
    txp = Tcl_GetHashValue(entryp);

    tbp = Duro_get_table(statep, interp, name, txp);
    if (tbp == NULL) {
        return TCL_ERROR;
    }

    if (tbp->is_persistent) {
        ret = RDB_set_table_name(tbp, newname, statep->current_ecp, txp);
        if (ret != RDB_OK) {
            Duro_dberror(interp, statep->current_ecp, txp);
            return TCL_ERROR;
        }
    } else {
        Tcl_HashSearch search;
        table_entry *tbep;

        /*
         * Check if there is another local table which depends on this table
         */
        entryp = Tcl_FirstHashEntry(&statep->ltables, &search);
        while (entryp != NULL) {
            table_entry *iep = (table_entry *) Tcl_GetHashValue(entryp);

            if (iep->tablep != tbp && _RDB_table_refers(iep->tablep, tbp)) {
                Tcl_AppendResult(interp, "Cannot rename table ", tbp->name,
                        ": ", iep->tablep->name, " depends on it", NULL);
                return TCL_ERROR;
            }
            entryp = Tcl_NextHashEntry(&search);
        }

        entryp = Tcl_FindHashEntry(&statep->ltables, name);
        tbep = (table_entry *)Tcl_GetHashValue(entryp);
        Tcl_DeleteHashEntry(entryp);
        Tcl_Free((char *) tbep);

        ret = RDB_set_table_name(tbp, newname, statep->current_ecp, txp);
        if (ret != RDB_OK) {
            Duro_dberror(interp, statep->current_ecp, txp);
            return TCL_ERROR;
        }

        ret = Duro_add_table(interp, statep, tbp, newname, RDB_db_env(RDB_tx_db(txp)));
        if (ret != TCL_OK) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

int
Duro_table_cmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    TclState *statep = (TclState *) data;

    const char *sub_cmds[] = {
        "create", "drop", "expr", "contains", "add", "attrs", "keys", "rename",
        "def", "showplan", NULL
    };
    enum table_ix {
        create_ix, drop_ix, expr_ix, contains_ix, add_ix, attrs_ix, keys_ix,
        rename_ix, def_ix, showplan_ix
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

    switch (index) {
        case create_ix:
            return table_create_cmd(statep, interp, objc, objv);
        case drop_ix:
            return table_drop_cmd(statep, interp, objc, objv);
        case expr_ix:
            return table_expr_cmd(statep, interp, objc, objv);
        case contains_ix:
            return table_contains_cmd(statep, interp, objc, objv);
        case add_ix:
            return table_add_cmd(statep, interp, objc, objv);
        case attrs_ix:
            return table_attrs_cmd(statep, interp, objc, objv);
        case keys_ix:
            return table_keys_cmd(statep, interp, objc, objv);
        case rename_ix:
            return table_rename_cmd(statep, interp, objc, objv);
        case def_ix:
            return table_def_cmd(statep, interp, objc, objv);
        case showplan_ix:
            return table_showplan_cmd(statep, interp, objc, objv);
    }
    return TCL_ERROR;
}
