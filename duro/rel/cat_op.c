/*
 * Catalog functions for user-defined operators.
 *
 * Copyright (C) 2012-2016 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "cat_op.h"
#include "serialize.h"
#include "internal.h"
#include "qresult.h"

#include <gen/releaseno.h>
#include <obj/objinternal.h>

#include <string.h>
#include <stdio.h>

/*
 * Convert tuple to operator (without return type)
 */
static RDB_operator *
tuple_to_operator(const char *name, const RDB_object *tplp,
        RDB_exec_context *ecp, RDB_transaction *txp) {
    int i;
    const char *libname;
    RDB_object *typobjp;
    RDB_operator *op;
    RDB_int argc;
    RDB_type **argtv = NULL;
    RDB_object *typarrp = RDB_tuple_get(tplp, "argtypes");

    argc = RDB_array_length(typarrp, ecp);
    if (argc == (RDB_int) RDB_ERROR)
        return NULL;

    /* Read types from tuple */
    if (argc > 0) {
        argtv = RDB_alloc(sizeof(RDB_type *) * argc, ecp);
        if (argtv == NULL)
            return NULL;
        for (i = 0; i < argc; i++) {
            typobjp = RDB_array_get(typarrp, i, ecp);
            if (typobjp == NULL)
                goto error;
            argtv[i] = RDB_bin_to_type(typobjp, ecp, txp);
            if (argtv[i] == NULL)
                goto error;
        }
    }
    op = RDB_new_op_data(name, NULL, argc, argtv, NULL, ecp);
    if (op == NULL)
        goto error;

    if (RDB_copy_obj(&op->source, RDB_tuple_get(tplp, "source"), ecp) != RDB_OK)
        goto error;

    if (RDB_copy_obj(&op->cretime, RDB_tuple_get(tplp, "creation_time"), ecp)
            != RDB_OK)
        goto error;

    libname = RDB_tuple_get_string(tplp, "lib");
    if (libname[0] != '\0') {
#ifndef _WIN32
        op->modhdl = lt_dlopenext(libname);
        if (op->modhdl == NULL) {
            char buf[64];

            /* Try again with .so suffix and version number */
            snprintf(buf, sizeof(buf), "%s.so.%s", libname, RDB_release_number);
            op->modhdl = lt_dlopen(buf);
        }
#else
        op->modhdl = LoadLibraryA(libname);
#endif
        if (op->modhdl == NULL) {
            RDB_raise_resource_not_found(libname, ecp);
            goto error;
        }
    } else {
        op->modhdl = NULL;
    }
    if (argtv != NULL) {
        for (i = 0; i < argc; i++) {
            if (argtv[i] != NULL && !RDB_type_is_scalar(argtv[i]))
                RDB_del_nonscalar_type(argtv[i], ecp);
        }
        RDB_free(argtv);
    }
    return op;

error:
    if (argtv != NULL) {
        for (i = 0; i < argc; i++) {
            if (argtv[i] != NULL && !RDB_type_is_scalar(argtv[i]))
                RDB_del_nonscalar_type(argtv[i], ecp);
        }
        RDB_free(argtv);
    }
    return NULL;
} /* tuple_to_operator */

int
cat_load_ro_op(const char *name, RDB_object *tbp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_expression *exp, *wexp, *argp;
    const char *symname;
    RDB_object tpl;
    RDB_object typesobj;
    int ret;
    RDB_operator *op;
    RDB_qresult *qrp = NULL;
    RDB_object *vtbp = NULL;
    RDB_int opcount = 0;

    /*
     * Create virtual table <optable> WHERE opname=<name>
     */
    exp = RDB_eq(RDB_var_ref("opname", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (exp == NULL) {
        RDB_del_expr(exp, ecp);
        RDB_destroy_obj(&typesobj, ecp);
        return RDB_ERROR;
    }

    wexp = RDB_ro_op("where", ecp);
    if (wexp == NULL) {
        RDB_del_expr(exp, ecp);
        return RDB_ERROR;
    }
    argp = RDB_table_ref(tbp, ecp);
    if (argp == NULL) {
        RDB_del_expr(wexp, ecp);
        RDB_del_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(wexp, argp);
    RDB_add_arg(wexp, exp);

    vtbp = RDB_expr_to_vtable(wexp, ecp, txp);
    if (vtbp == NULL) {
        RDB_del_expr(wexp, ecp);
        return RDB_ERROR;
    }
    qrp = RDB_table_qresult(vtbp, ecp, txp);
    if (qrp == NULL) {
        RDB_drop_table(vtbp, ecp, txp);
        return RDB_ERROR;
    }
    RDB_init_obj(&tpl);

    /* Read tuples and convert them to operators */
    while (RDB_next_tuple(qrp, &tpl, ecp, txp) == RDB_OK) {
        op = tuple_to_operator(name, &tpl, ecp, txp);
        if (op == NULL)
            goto error;

        /* Return type */
        op->rtyp = RDB_bin_to_type(RDB_tuple_get(&tpl, "rtype"), ecp, txp);
        if (op->rtyp == NULL) {
            RDB_free_op_data(op, ecp);
            goto error;
        }
        symname = RDB_tuple_get_string(&tpl, "symbol");

        /* Special handling of selectors and comparison for user-defined types */
        if (strcmp(symname, "RDB_op_sys_select") == 0) {
            op->opfn.ro_fp = &RDB_op_sys_select;
        } else if (strcmp(symname, "RDB_sys_lt") == 0) {
                op->opfn.ro_fp = &RDB_sys_lt;
        } else if (strcmp(symname, "RDB_sys_let") == 0) {
                op->opfn.ro_fp = &RDB_sys_let;
        } else if (strcmp(symname, "RDB_sys_gt") == 0) {
                op->opfn.ro_fp = &RDB_sys_gt;
        } else if (strcmp(symname, "RDB_sys_get") == 0) {
                op->opfn.ro_fp = &RDB_sys_get;
        } else if (symname[0] == '\0') {
            op->opfn.ro_fp = NULL;
        } else {
#ifdef _WIN32
            op->opfn.ro_fp = (RDB_ro_op_func *) GetProcAddress(op->modhdl, symname);
#else
            op->opfn.ro_fp = (RDB_ro_op_func *) lt_dlsym(op->modhdl, symname);
#endif
            if (op->opfn.ro_fp == NULL) {
                RDB_raise_resource_not_found(symname, ecp);
                RDB_free_op_data(op, ecp);
                goto error;
            }
        }

        if (RDB_put_op(&txp->dbp->dbrootp->ro_opmap, op, ecp) != RDB_OK) {
            RDB_free_op_data(op, ecp);
            goto error;
        }
        if (RDB_env_trace(txp->envp) > 0) {
            fputs("Read-only operator ", stderr);
            fputs(name, stderr);
            fputs(" loaded from catalog\n", stderr);
        }
        opcount++;
    }
    if (RDB_obj_type(RDB_get_err(ecp)) != &RDB_NOT_FOUND_ERROR) {
        return RDB_ERROR;
    }
    RDB_clear_err(ecp);

    ret = RDB_del_qresult(qrp, ecp, txp);
    qrp = NULL;
    if (ret != RDB_OK)
        goto error;

    ret = RDB_drop_table(vtbp, ecp, txp);
    vtbp = NULL;
    if (ret != RDB_OK)
        goto error;

    RDB_destroy_obj(&tpl, ecp);
    return opcount;

error:
    if (qrp != NULL)
        RDB_del_qresult(qrp, ecp, txp);
    if (vtbp != NULL)
        RDB_drop_table(vtbp, ecp, txp);
    RDB_destroy_obj(&tpl, ecp);
    return RDB_ERROR;
}

/*
 * Read all read-only operators with specified name from database.
 * Return the number of operators loaded or RDB_ERROR if an error occured.
 */
RDB_int
RDB_cat_load_ro_op(const char *name, RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_int cnt;
    RDB_int opcount = cat_load_ro_op(name, txp->dbp->dbrootp->ro_ops_tbp,
            ecp, txp);
    if (opcount == RDB_ERROR)
        return RDB_ERROR;
    cnt = cat_load_ro_op(name, txp->dbp->dbrootp->ro_op_versions_tbp, ecp, txp);
    if (cnt == RDB_ERROR)
        return RDB_ERROR;
    return opcount + cnt;
} /* RDB_cat_load_ro_op */

int
cat_load_upd_op(const char *name, RDB_object *tbp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_expression *exp, *wexp, *argp;
    RDB_qresult *qrp;
    const char *symname;
    RDB_object *vtbp;
    RDB_object tpl;
    RDB_object typesobj;
    RDB_object *updobjp, *updvobjp;
    int i;
    RDB_operator *op;
    RDB_int opcount = 0;

    /*
     * Create virtual table sys_upd_ops WHERE name=<name>
     */
    exp = RDB_eq(RDB_var_ref("opname", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (exp == NULL) {
        RDB_del_expr(exp, ecp);
        RDB_destroy_obj(&typesobj, ecp);
        return RDB_ERROR;
    }

    wexp = RDB_ro_op("where", ecp);
    if (wexp == NULL) {
        RDB_del_expr(exp, ecp);
        return RDB_ERROR;
    }
    argp = RDB_table_ref(tbp, ecp);
    if (argp == NULL) {
        RDB_del_expr(wexp, ecp);
        RDB_del_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(wexp, argp);
    RDB_add_arg(wexp, exp);

    vtbp = RDB_expr_to_vtable(wexp, ecp, txp);
    if (vtbp == NULL) {
        RDB_del_expr(wexp, ecp);
        return RDB_ERROR;
    }
    qrp = RDB_table_qresult(vtbp, ecp, txp);
    if (qrp == NULL) {
        RDB_drop_table(vtbp, ecp, txp);
        return RDB_ERROR;
    }
    RDB_init_obj(&tpl);

    /* Read tuples and convert them to operators */
    while (RDB_next_tuple(qrp, &tpl, ecp, txp) == RDB_OK) {
        op = tuple_to_operator(name, &tpl, ecp, txp);
        if (op == NULL)
            goto error;

        updvobjp = RDB_tuple_get(&tpl, "updv");
        for (i = 0; i < op->paramc; i++) {
            updobjp = RDB_array_get(updvobjp, (RDB_int) i, ecp);
            if (updobjp == NULL)
                goto error;
            op->paramv[i].update = RDB_obj_bool(updobjp);
        }

        symname = RDB_tuple_get_string(&tpl, "symbol");
        if (symname[0] != '\0') {
#ifdef _WIN32
            op->opfn.upd_fp = (RDB_upd_op_func *) GetProcAddress(op->modhdl, symname);
#else
            op->opfn.upd_fp = (RDB_upd_op_func *) lt_dlsym(op->modhdl, symname);
#endif
            if (op->opfn.upd_fp == NULL) {
                RDB_raise_resource_not_found(symname, ecp);
                RDB_free_op_data(op, ecp);
                goto error;
            }
        } else {
            op->opfn.upd_fp = NULL;
        }

        if (RDB_put_op(&txp->dbp->dbrootp->upd_opmap, op, ecp) != RDB_OK) {
            RDB_free_op_data(op, ecp);
            goto error;
        }

        if (RDB_env_trace(txp->envp) > 0) {
            fputs("Update operator ", stderr);
            fputs(name, stderr);
            fputs(" loaded from catalog\n", stderr);
        }
        opcount++;
    }
    if (RDB_obj_type(RDB_get_err(ecp)) != &RDB_NOT_FOUND_ERROR) {
        return RDB_ERROR;
    }
    RDB_clear_err(ecp);

    if (RDB_del_qresult(qrp, ecp, txp) != RDB_OK) {
        qrp = NULL;
        goto error;
    }

    if (RDB_drop_table(vtbp, ecp, txp) != RDB_OK) {
        vtbp = NULL;
        goto error;
    }

    RDB_destroy_obj(&tpl, ecp);
    return opcount;

error:
    if (qrp != NULL)
        RDB_del_qresult(qrp, ecp, txp);
    if (vtbp != NULL)
        RDB_drop_table(vtbp, ecp, txp);
    RDB_destroy_obj(&tpl, ecp);
    return RDB_ERROR;
}

/* Read all read-only operators with specified name from database */
RDB_int
RDB_cat_load_upd_op(const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_int cnt;
    RDB_int opcount = cat_load_upd_op(name, txp->dbp->dbrootp->upd_ops_tbp,
            ecp, txp);
    if (opcount == RDB_ERROR)
        return RDB_ERROR;
    cnt = cat_load_upd_op(name, txp->dbp->dbrootp->upd_op_versions_tbp, ecp, txp);
    if (cnt == RDB_ERROR)
        return RDB_ERROR;
    return opcount + cnt;
}

RDB_int
RDB_cat_del_ops(const char *opname,
        RDB_object *ops_tbp, RDB_object *versions_tbp,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_int cnt1, cnt2;
    RDB_expression *wherep;

    /* Delete all versions from the database */
    wherep = RDB_attr_eq_strval("opname", opname, ecp);
    if (wherep == NULL) {
        RDB_raise_no_memory(ecp);
        return (RDB_int) RDB_ERROR;
    }
    cnt1 = RDB_delete(ops_tbp, wherep, ecp, txp);
    if (cnt1 == (RDB_int) RDB_ERROR) {
        RDB_del_expr(wherep, ecp);
        return (RDB_int) RDB_ERROR;
    }
    cnt2 = RDB_delete(versions_tbp, wherep, ecp, txp);
    RDB_del_expr(wherep, ecp);
    if (cnt2 == (RDB_int) RDB_ERROR) {
        return (RDB_int) RDB_ERROR;
    }
    return cnt1 + cnt2;
}

RDB_int
RDB_cat_del_op_version(const char *opname, const char *version,
        RDB_object *ops_tbp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_int cnt;
    RDB_expression *nexp, *vexp, *wherep;

    /* Delete all versions from the database */
    nexp = RDB_attr_eq_strval("opname", opname, ecp);
    if (nexp == NULL) {
        RDB_raise_no_memory(ecp);
        return (RDB_int) RDB_ERROR;
    }
    vexp = RDB_attr_eq_strval("version", version, ecp);
    if (vexp == NULL) {
        RDB_del_expr(nexp, ecp);
        RDB_raise_no_memory(ecp);
        return (RDB_int) RDB_ERROR;
    }
    wherep = RDB_ro_op("and", ecp);
    if (wherep == NULL) {
        RDB_del_expr(nexp, ecp);
        RDB_del_expr(vexp, ecp);
        return (RDB_int) RDB_ERROR;
    }
    RDB_add_arg(wherep, nexp);
    RDB_add_arg(wherep, vexp);

    cnt = RDB_delete(ops_tbp, wherep, ecp, txp);
    RDB_del_expr(wherep, ecp);
    if (cnt == (RDB_int) RDB_ERROR) {
        return (RDB_int) RDB_ERROR;
    }
    return cnt;
}
