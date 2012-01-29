/*
 * $Id$
 *
 * Copyright (C) 2003-2012 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "catalog.h"
#include "typeimpl.h"
#include "insert.h"
#include "internal.h"
#include "stable.h"
#include "serialize.h"

#include <gen/strfns.h>
#include <string.h>

enum {
    MAJOR_VERSION = 0,
    MINOR_VERSION = 15
};

/*
 * Definitions of the catalog tables.
 */

static RDB_attr table_attr_attrv[] = {
        { "ATTRNAME", &RDB_STRING, NULL, 0 },
        { "TABLENAME", &RDB_STRING, NULL, 0 },
        { "TYPE", &RDB_BINARY, NULL, 0 },
        { "I_FNO", &RDB_INTEGER, NULL, 0 } };
static char *table_attr_keyattrv[] = { "ATTRNAME", "TABLENAME" };
static RDB_string_vec table_attr_keyv[] = { { 2, table_attr_keyattrv } };

static RDB_attr table_attr_defvals_attrv[] = {
            { "ATTRNAME", &RDB_STRING, NULL, 0 },
            { "TABLENAME", &RDB_STRING, NULL, 0 },
            { "DEFAULT_VALUE", &RDB_BINARY, NULL, 0 } };
static char *table_attr_defvals_keyattrv[] = { "ATTRNAME", "TABLENAME" };
static RDB_string_vec table_attr_defvals_keyv[] = { { 2, table_attr_defvals_keyattrv } };

static RDB_attr rtables_attrv[] = {
    { "TABLENAME", &RDB_STRING, NULL, 0 },
    { "IS_USER", &RDB_BOOLEAN, NULL, 0 },
};
static char *rtables_keyattrv[] = { "TABLENAME" };
static RDB_string_vec rtables_keyv[] = { { 1, rtables_keyattrv } };

static RDB_attr vtables_attrv[] = {
    { "TABLENAME", &RDB_STRING, NULL, 0 },
    { "IS_USER", &RDB_BOOLEAN, NULL, 0 },
    { "I_DEF", &RDB_BINARY, NULL, 0 }
};
static char *vtables_keyattrv[] = { "TABLENAME" };
static RDB_string_vec vtables_keyv[] = { { 1, vtables_keyattrv } };

static RDB_attr table_recmap_attrv[] = {
    { "TABLENAME", &RDB_STRING, NULL, 0 },
    { "RECMAP", &RDB_STRING, NULL, 0 },
};
static char *table_recmap_keyattrv1[] = { "TABLENAME" };
static char *table_recmap_keyattrv2[] = { "RECMAP" };
static RDB_string_vec table_recmap_keyv[] = {
    { 1, table_recmap_keyattrv1 }, { 1, table_recmap_keyattrv2 } };

static RDB_attr dbtables_attrv[] = {
    { "TABLENAME", &RDB_STRING },
    { "DBNAME", &RDB_STRING }
};
static char *dbtables_keyattrv[] = { "TABLENAME", "DBNAME" };
static RDB_string_vec dbtables_keyv[] = { { 2, dbtables_keyattrv } };

static RDB_attr keys_attrv[] = {
    { "TABLENAME", &RDB_STRING, NULL, 0 },
    { "KEYNO", &RDB_INTEGER, NULL, 0 },
    { "ATTRS", NULL, NULL, 0 } /* type is set to a relation type later */
};
static char *keys_keyattrv[] = { "TABLENAME", "KEYNO" };
static RDB_string_vec keys_keyv[] = { { 2, keys_keyattrv } };

static RDB_attr types_attrv[] = {
    { "TYPENAME", &RDB_STRING, NULL, 0 },
    { "I_AREP_LEN", &RDB_INTEGER, NULL, 0 },
    { "I_AREP_TYPE", &RDB_BINARY, NULL, 0 },
    { "I_SYSIMPL", &RDB_BOOLEAN, NULL, 0},
    { "I_CONSTRAINT", &RDB_BINARY, NULL, 0 }
};
static char *types_keyattrv[] = { "TYPENAME" };
static RDB_string_vec types_keyv[] = { { 1, types_keyattrv } };

static RDB_attr possrepcomps_attrv[] = {
    { "TYPENAME", &RDB_STRING, NULL, 0 },
    { "POSSREPNAME", &RDB_STRING, NULL, 0 },
    { "COMPNO", &RDB_INTEGER, NULL, 0 },
    { "COMPNAME", &RDB_STRING, NULL, 0 },
    { "COMPTYPE", &RDB_BINARY, NULL, 0 }
};
static char *possrepcomps_keyattrv1[] = { "TYPENAME", "POSSREPNAME", "COMPNO" };
static char *possrepcomps_keyattrv2[] = { "TYPENAME", "POSSREPNAME", "COMPNAME" };
static RDB_string_vec possrepcomps_keyv[] = {
    { 3, possrepcomps_keyattrv1 },
    { 3, possrepcomps_keyattrv2 }
};

static RDB_attr ro_ops_attrv[] = {
    { "NAME", &RDB_STRING, NULL, 0 },
    { "ARGTYPES", NULL, NULL, 0 }, /* type is set to array of BINARY later */
    { "LIB", &RDB_STRING, NULL, 0 },
    { "SYMBOL", &RDB_STRING, NULL, 0 },
    { "IARG", &RDB_BINARY, NULL, 0 },
    { "RTYPE", &RDB_BINARY, NULL, 0 }
};

static char *ro_ops_keyattrv[] = { "NAME", "ARGTYPES" };
static RDB_string_vec ro_ops_keyv[] = { { 2, ro_ops_keyattrv } };

static RDB_attr upd_ops_attrv[] = {
    { "NAME", &RDB_STRING, NULL, 0 },
    { "ARGTYPES", NULL, NULL, 0 }, /* type is set to array of BINARY later */
    { "LIB", &RDB_STRING, NULL, 0 },
    { "SYMBOL", &RDB_STRING, NULL, 0 },
    { "IARG", &RDB_BINARY, NULL, 0 },
    { "UPDV", NULL, NULL, 0 } /* type is set to array of BOOLEAN later */
};

static char *upd_ops_keyattrv[] = { "NAME", "ARGTYPES" };
static RDB_string_vec upd_ops_keyv[] = { { 2, upd_ops_keyattrv } };

static RDB_attr indexes_attrv[] = {
    { "NAME", &RDB_STRING, NULL, 0 },
    { "TABLENAME", &RDB_STRING, NULL, 0 },
    { "ATTRS", NULL, NULL, 0 }, /* type is set to an array type later */
    { "UNIQUE", &RDB_BOOLEAN, 0 },
    { "ORDERED", &RDB_BOOLEAN, 0 }
};

static char *indexes_keyattrv[] = { "NAME" };
static RDB_string_vec indexes_keyv[] = { { 1, indexes_keyattrv } };

static RDB_attr indexes_attrs_attrv[] = {
    { "NAME", &RDB_STRING, NULL, 0 },
    { "ASC", &RDB_BOOLEAN, NULL, 0 }
};

static RDB_attr constraints_attrv[] = {
    { "CONSTRAINTNAME", &RDB_STRING, NULL, 0 },
    { "I_EXPR", &RDB_BINARY, NULL, 0 }
};
static char *constraints_keyattrv[] = { "CONSTRAINTNAME" };
static RDB_string_vec constraints_keyv[] = { { 1, constraints_keyattrv } };

static RDB_attr version_info_attrv[] = {
    { "MAJOR_VERSION", &RDB_INTEGER, NULL, 0 },
    { "MINOR_VERSION", &RDB_INTEGER, NULL, 0 },
    { "MICRO_VERSION", &RDB_INTEGER, NULL, 0 }
};
static RDB_string_vec version_info_keyv[] = { { 0, NULL } };

static int
dbtables_insert(RDB_object *tbp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_object tpl;
    int ret;

    /* Insert (database, table) pair into SYS_DBTABLES */
    RDB_init_obj(&tpl);

    if (RDB_tuple_set_string(&tpl, "TABLENAME", RDB_table_name(tbp), ecp)
            != RDB_OK)
    {
        RDB_destroy_obj(&tpl, ecp);
        return RDB_ERROR;
    }
    if (RDB_tuple_set_string(&tpl, "DBNAME", txp->dbp->name, ecp) != RDB_OK)
    {
        RDB_destroy_obj(&tpl, ecp);
        return RDB_ERROR;
    }
    ret = RDB_insert(txp->dbp->dbrootp->dbtables_tbp, &tpl, ecp, txp);
    RDB_destroy_obj(&tpl, ecp);
    
    return ret;
}

static int
insert_key(RDB_string_vec *keyp, int i, const char *tablename,
        RDB_dbroot *dbrootp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    int j;
    RDB_object tpl;
    RDB_object *keystbp;
    RDB_attr keysattr;

    RDB_init_obj(&tpl);
    if (RDB_tuple_set_int(&tpl, "KEYNO", i, ecp) != RDB_OK)
        goto error;

    if (RDB_tuple_set_string(&tpl, "TABLENAME", tablename, ecp) != RDB_OK)
        goto error;

    if (RDB_tuple_set(&tpl, "ATTRS", NULL, ecp) != RDB_OK)
        goto error;

    keystbp = RDB_tuple_get(&tpl, "ATTRS");

    keysattr.name = "KEY";
    keysattr.typ = &RDB_STRING;
    keysattr.defaultp = NULL;
    keysattr.options = 0;

    if (RDB_init_table(keystbp, NULL, 1, &keysattr, 0, NULL, ecp)
            != RDB_OK) {
        goto error;
    }

    for (j = 0; j < keyp->strc; j++) {
        RDB_object ktpl;

        RDB_init_obj(&ktpl);
        if (RDB_tuple_set_string(&ktpl, "KEY", keyp->strv[j], ecp) != RDB_OK) {
            RDB_destroy_obj(&ktpl, ecp);
            goto error;
        }

        ret = RDB_insert(keystbp, &ktpl, ecp, txp);
        RDB_destroy_obj(&ktpl, ecp);
        if (ret != RDB_OK) {
            goto error;
        }
    }

    if (RDB_insert(dbrootp->keys_tbp, &tpl, ecp, txp) != RDB_OK)
        goto error;

    RDB_destroy_obj(&tpl, ecp);

    return RDB_OK;

error:
    RDB_destroy_obj(&tpl, ecp);

    return RDB_ERROR;
}

/* Insert the table pointed to by tbp into the catalog. */
static int
insert_rtable(RDB_object *tbp, RDB_dbroot *dbrootp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_object tpl;
    RDB_type *tuptyp = tbp->typ->var.basetyp;
    int ret;
    int i;

    /* insert entry into table SYS_RTABLES */
    RDB_init_obj(&tpl);
    ret = RDB_tuple_set_string(&tpl, "TABLENAME", RDB_table_name(tbp), ecp);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&tpl, ecp);
        return RDB_ERROR;
    }
    ret = RDB_tuple_set_bool(&tpl, "IS_USER", tbp->var.tb.is_user, ecp);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&tpl, ecp);
        return RDB_ERROR;
    }
    ret = RDB_insert(dbrootp->rtables_tbp, &tpl, ecp, txp);
    RDB_destroy_obj(&tpl, ecp);
    if (ret != RDB_OK) {
        return RDB_ERROR;
    }

    /* insert entries into table SYS_TABLEATTRS */
    RDB_init_obj(&tpl);
    ret = RDB_tuple_set_string(&tpl, "TABLENAME", RDB_table_name(tbp), ecp);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&tpl, ecp);
        return RDB_ERROR;
    }

    for (i = 0; i < tuptyp->var.tuple.attrc; i++) {
        RDB_object typedata;
        char *attrname = tuptyp->var.tuple.attrv[i].name;

        RDB_init_obj(&typedata);
        ret = _RDB_type_to_binobj(&typedata, tuptyp->var.tuple.attrv[i].typ,
                ecp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&typedata, ecp);
            RDB_destroy_obj(&tpl, ecp);
            return RDB_ERROR;
        }
        ret = RDB_tuple_set(&tpl, "TYPE", &typedata, ecp);
        RDB_destroy_obj(&typedata, ecp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&tpl, ecp);
            return RDB_ERROR;
        }

        ret = RDB_tuple_set_string(&tpl, "ATTRNAME", attrname, ecp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&tpl, ecp);
            return ret;
        }
        ret = RDB_tuple_set_int(&tpl, "I_FNO", i, ecp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&tpl, ecp);
            return ret;
        }
        ret = RDB_insert(dbrootp->table_attr_tbp, &tpl, ecp, txp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&tpl, ecp);
            return ret;
        }
    }
    RDB_destroy_obj(&tpl, ecp);

    /* insert entries into table SYS_TABLEATTR_DEFVALS */
    RDB_init_obj(&tpl);
    ret = RDB_tuple_set_string(&tpl, "TABLENAME", RDB_table_name(tbp), ecp);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&tpl, ecp);
        return RDB_ERROR;
    }

    for (i = 0; i < tuptyp->var.tuple.attrc; i++) {
        if (tuptyp->var.tuple.attrv[i].defaultp != NULL) {
            char *attrname = tuptyp->var.tuple.attrv[i].name;
            RDB_object binval;
            void *datap;
            size_t len;

            if (!RDB_type_equals(tuptyp->var.tuple.attrv[i].defaultp->typ,
                    tuptyp->var.tuple.attrv[i].typ)) {
                RDB_raise_type_mismatch(
                        "Type of default value does not match attribute type",
                        ecp);
                return RDB_ERROR;
            }
            
            ret = RDB_tuple_set_string(&tpl, "ATTRNAME", attrname, ecp);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl, ecp);
                return RDB_ERROR;
            }

            RDB_init_obj(&binval);
            datap = RDB_obj_irep(tuptyp->var.tuple.attrv[i].defaultp, &len);
            ret = RDB_binary_set(&binval, 0, datap, len, ecp);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl, ecp);
                RDB_destroy_obj(&binval, ecp);
                return RDB_ERROR;
            }

            ret = RDB_tuple_set(&tpl, "DEFAULT_VALUE", &binval, ecp);
            RDB_destroy_obj(&binval, ecp);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl, ecp);
                return RDB_ERROR;
            }

            ret = RDB_insert(dbrootp->table_attr_defvals_tbp, &tpl, ecp, txp);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl, ecp);
                return RDB_ERROR;
            }
        }
    }
    RDB_destroy_obj(&tpl, ecp);

    /*
     * Insert keys into SYS_KEYS
     */
    for (i = 0; i < tbp->var.tb.keyc; i++) {
        if (insert_key(&tbp->var.tb.keyv[i], i, RDB_table_name(tbp), dbrootp,
                ecp, txp) != RDB_OK)
            return RDB_ERROR;
    }
    return RDB_OK;
}

static int
insert_vtable(RDB_object *tbp, RDB_dbroot *dbrootp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;
    RDB_object defval;

    RDB_init_obj(&tpl);
    RDB_init_obj(&defval);

    /* Insert entry into SYS_VTABLES */

    ret = RDB_tuple_set_string(&tpl, "TABLENAME", RDB_table_name(tbp), ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_tuple_set_bool(&tpl, "IS_USER", RDB_TRUE, ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = _RDB_expr_to_binobj(&defval, tbp->var.tb.exp, ecp);
    if (ret != RDB_OK)
        goto cleanup;
    ret = RDB_tuple_set(&tpl, "I_DEF", &defval, ecp);
    if (ret != RDB_OK)
        goto cleanup;

#ifdef DEBUG
    fprintf(stderr, "Writing virtual table definition:\n");
    _RDB_dump(defval.var.bin.datap, defval.var.bin.len, stderr);
#endif

    ret = RDB_insert(dbrootp->vtables_tbp, &tpl, ecp, txp);
    if (ret != RDB_OK) {
        if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_KEY_VIOLATION_ERROR) {
            RDB_clear_err(ecp);
            RDB_raise_element_exists("table already exists", ecp);
        }
        goto cleanup;
    }

    ret = RDB_OK;

cleanup:
    RDB_destroy_obj(&tpl, ecp);
    RDB_destroy_obj(&defval, ecp);

    return ret;
}

int
_RDB_cat_insert_index(const char *name, int attrc, const RDB_seq_item attrv[],
        RDB_bool unique, RDB_bool ordered, const char *tbname,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    int i;
    RDB_object tpl;
    RDB_object attrsarr;
    RDB_object attrtpl;

    RDB_init_obj(&tpl);
    RDB_init_obj(&attrsarr);
    RDB_init_obj(&attrtpl);

    ret = RDB_tuple_set_string(&tpl, "NAME", name, ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_tuple_set_string(&tpl, "TABLENAME", tbname, ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_set_array_length(&attrsarr, (RDB_int) attrc, ecp);
    if (ret != RDB_OK)
        goto cleanup;
    for (i = 0; i < attrc; i++) {
        ret = RDB_tuple_set_string(&attrtpl, "NAME", attrv[i].attrname, ecp);
        if (ret != RDB_OK)
            goto cleanup;
        ret = RDB_tuple_set_bool(&attrtpl, "ASC", attrv[i].asc, ecp);
        if (ret != RDB_OK)
            goto cleanup;
        ret = RDB_array_set(&attrsarr, (RDB_int) i, &attrtpl, ecp);
        if (ret != RDB_OK)
            goto cleanup;
    }
    ret = RDB_tuple_set(&tpl, "ATTRS", &attrsarr, ecp);
    if (ret != RDB_OK)
        goto cleanup;   

    ret = RDB_tuple_set_bool(&tpl, "UNIQUE", unique, ecp);
    if (ret != RDB_OK)
        goto cleanup;   

    ret = RDB_tuple_set_bool(&tpl, "ORDERED", ordered, ecp);
    if (ret != RDB_OK)
        goto cleanup;   

    ret = RDB_insert(txp->dbp->dbrootp->indexes_tbp, &tpl, ecp, txp);

cleanup:
    RDB_destroy_obj(&tpl, ecp);
    RDB_destroy_obj(&attrsarr, ecp);
    RDB_destroy_obj(&attrtpl, ecp);
    return ret;
}

int
_RDB_cat_insert_table_recmap(RDB_object *tbp, const char *rmname,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;

    RDB_init_obj(&tpl);

    ret = RDB_tuple_set_string(&tpl, "TABLENAME", RDB_table_name(tbp), ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_tuple_set_string(&tpl, "RECMAP", rmname, ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_insert(txp->dbp->dbrootp->table_recmap_tbp, &tpl, ecp, txp);

cleanup:
    RDB_destroy_obj(&tpl, ecp);
    return ret;
}

int
_RDB_cat_index_tablename(const char *name, char **tbnamep,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;
    RDB_object *vtbp;
    RDB_expression *argp;
    RDB_expression *exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        return RDB_ERROR;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->indexes_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("NAME", ecp), RDB_string_to_expr(name, ecp),
            ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);

    vtbp = RDB_expr_to_vtable(exp, ecp, txp);
    if (vtbp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }

    RDB_init_obj(&tpl);
    ret = RDB_extract_tuple(vtbp, ecp, txp, &tpl);
    RDB_drop_table(vtbp, ecp, txp);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&tpl, ecp);
        return RDB_ERROR;
    }

    *tbnamep = RDB_dup_str(RDB_tuple_get_string(&tpl, "TABLENAME"));
    RDB_destroy_obj(&tpl, ecp);
    if (*tbnamep == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

int
_RDB_cat_delete_index(const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int ret;
    RDB_expression *wherep = RDB_eq(RDB_var_ref("NAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (wherep == NULL) {
        return RDB_ERROR;
    }

    ret = RDB_delete(txp->dbp->dbrootp->indexes_tbp, wherep, ecp, txp);
    RDB_drop_expr(wherep, ecp);
    if (ret == RDB_ERROR)
        return RDB_ERROR;
    return RDB_OK;
}

int
_RDB_cat_insert(RDB_object *tbp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;

    /*
     * Create table in the catalog.
     */
    if (tbp->var.tb.exp == NULL) {
        ret = insert_rtable(tbp, txp->dbp->dbrootp, ecp, txp);
        /* If the table already exists in the catalog, proceed */
        if (ret != RDB_OK) {
            if (RDB_obj_type(RDB_get_err(ecp)) != &RDB_ELEMENT_EXISTS_ERROR) {
                return RDB_ERROR;
            }
            RDB_clear_err(ecp);
        }

        /*
         * If it's a system table, insert indexes into the catalog.
         * For user tables, the indexes are inserted when the recmap is created.
         */
        if (ret == RDB_OK) {
            if (!tbp->var.tb.is_user) {
                int i;

                for (i = 0; i < tbp->var.tb.stp->indexc; i++) {
                    ret = _RDB_cat_insert_index(tbp->var.tb.stp->indexv[i].name,
                            tbp->var.tb.stp->indexv[i].attrc,
                            tbp->var.tb.stp->indexv[i].attrv,                            
                            tbp->var.tb.stp->indexv[i].unique,
                            tbp->var.tb.stp->indexv[i].ordered,
                            RDB_table_name(tbp), ecp, txp);
                    if (ret != RDB_OK)
                        return ret;
                }
            }
        }
    } else {
        ret = insert_vtable(tbp, txp->dbp->dbrootp, ecp, txp);
        /* If the table already exists in the catalog, proceed */
        if (ret != RDB_OK) {
            if (RDB_obj_type(RDB_get_err(ecp)) != &RDB_ELEMENT_EXISTS_ERROR) {
                return RDB_ERROR;
            }
            RDB_clear_err(ecp);
        }
    }

    /*
     * Add the table into the database by inserting the table/DB pair
     * into SYS_DBTABLES.
     */
    return dbtables_insert(tbp, ecp, txp);
}

/* Delete a real table from the catalog */
static int
delete_rtable(RDB_object *tbp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_expression *exprp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
            RDB_string_to_expr(RDB_table_name(tbp), ecp), ecp);
    if (exprp == NULL) {
        return RDB_ERROR;
    }
    ret = RDB_delete(txp->dbp->dbrootp->rtables_tbp, exprp, ecp, txp);
    if (ret == RDB_ERROR)
        goto cleanup;

    ret = RDB_delete(txp->dbp->dbrootp->table_recmap_tbp, exprp, ecp, txp);
    if (ret == RDB_ERROR)
        goto cleanup;

    ret = RDB_delete(txp->dbp->dbrootp->table_attr_tbp, exprp, ecp, txp);
    if (ret == RDB_ERROR)
        goto cleanup;

    ret = RDB_delete(txp->dbp->dbrootp->table_attr_defvals_tbp, exprp, ecp,
            txp);
    if (ret == RDB_ERROR)
        goto cleanup;

    ret = RDB_delete(txp->dbp->dbrootp->keys_tbp, exprp, ecp, txp);
    if (ret == RDB_ERROR)
        goto cleanup;

    ret = RDB_delete(txp->dbp->dbrootp->indexes_tbp, exprp, ecp, txp);
    if (ret == RDB_ERROR)
        goto cleanup;

    ret = RDB_delete(txp->dbp->dbrootp->dbtables_tbp, exprp, ecp, txp);

cleanup:
    RDB_drop_expr(exprp, ecp);
    return ret == RDB_ERROR ? RDB_ERROR : RDB_OK;
}

/* Delete a virtual table from the catalog */
static int
delete_vtable(RDB_object *tbp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_expression *exprp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
                   RDB_string_to_expr(RDB_table_name(tbp), ecp), ecp);
    if (exprp == NULL) {
        return RDB_ERROR;
    }
    ret = RDB_delete(txp->dbp->dbrootp->vtables_tbp, exprp, ecp, txp);
    if (ret == RDB_ERROR)
        goto cleanup;

    ret = RDB_delete(txp->dbp->dbrootp->dbtables_tbp, exprp, ecp, txp);

cleanup:
    RDB_drop_expr(exprp, ecp);

    return ret == RDB_ERROR ? RDB_ERROR : RDB_OK;
}

int
_RDB_cat_delete(RDB_object *tbp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (tbp->var.tb.exp == NULL)
        return delete_rtable(tbp, ecp, txp);
    else
        return delete_vtable(tbp, ecp, txp);
}

/*
 * Read table indexes from catalog
 */
int
_RDB_cat_get_indexes(const char *tablename, RDB_dbroot *dbrootp,
        RDB_exec_context *ecp, RDB_transaction *txp, struct _RDB_tbindex **indexvp)
{
    int ret;
    int i;
    int j;
    int indexc;
    RDB_object *vtbp;
    RDB_object arr;
    RDB_expression *argp;
    RDB_expression *exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        return RDB_ERROR;
    }
    argp = RDB_table_ref(dbrootp->indexes_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
            RDB_string_to_expr(tablename, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);

    vtbp = RDB_expr_to_vtable(exp, ecp, txp);
    if (vtbp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }

    RDB_init_obj(&arr);
    ret = RDB_table_to_array(&arr, vtbp, 0, NULL, 0, ecp, txp);
    if (ret != RDB_OK)
        goto cleanup;

    indexc = RDB_array_length(&arr, ecp);
    if (indexc > 0) {
        (*indexvp) = RDB_alloc(sizeof(_RDB_tbindex) * indexc, ecp);
        if (*indexvp == NULL) {
            ret = RDB_ERROR;
            goto cleanup;
        }
        for (i = 0; i < indexc; i++) {
            RDB_object *tplp;
            RDB_object *attrarrp;
            char *idxname;
            _RDB_tbindex *indexp = &(*indexvp)[i];

            tplp = RDB_array_get(&arr, (RDB_int) i, ecp);
            if (tplp == NULL) {
                ret = RDB_ERROR;
                goto cleanup;
            }

            idxname = RDB_tuple_get_string(tplp, "NAME");
            if (idxname[0] != '\0') {
                indexp->name = RDB_dup_str(idxname);
                if (indexp->name == NULL) {
                    RDB_raise_no_memory(ecp);
                    ret = RDB_ERROR;
                    goto cleanup;
                }
            }

            attrarrp = RDB_tuple_get(tplp, "ATTRS");
            indexp->attrc = RDB_array_length(attrarrp, ecp);
            if (indexp->attrc < 0) {
                ret = indexp->attrc;
                goto cleanup;
            }
            
            indexp->attrv = RDB_alloc(sizeof (RDB_seq_item) * indexp->attrc, ecp);
            if (indexp->attrv == NULL) {
                ret = RDB_ERROR;
                goto cleanup;
            }

            for (j = 0; j < indexp->attrc; j++) {
                RDB_object *attrtplp;                

                attrtplp = RDB_array_get(attrarrp, (RDB_int) j, ecp);
                if (attrtplp == NULL) {
                    ret = RDB_ERROR;
                    goto cleanup;
                }

                indexp->attrv[j].attrname = RDB_dup_str(RDB_tuple_get_string(
                        attrtplp, "NAME"));
                if (indexp->attrv[j].attrname == NULL) {
                    RDB_raise_no_memory(ecp);
                    ret = RDB_ERROR;
                    goto cleanup;
                }
                indexp->attrv[j].asc = RDB_tuple_get_bool(attrtplp, "ASC");
            }

            indexp->unique = RDB_tuple_get_bool(tplp, "UNIQUE");
            indexp->ordered = RDB_tuple_get_bool(tplp, "ORDERED");
        }
    }
    ret = indexc;

cleanup:
    RDB_destroy_obj(&arr, ecp);
    RDB_drop_table(vtbp, ecp, NULL);
    return ret;
}

static int
provide_systable(const char *name, int attrc, RDB_attr heading[],
           int keyc, RDB_string_vec keyv[], RDB_bool create,
           RDB_exec_context *ecp, RDB_transaction *txp, RDB_environment *envp,
           RDB_object **tbpp)
{
    int ret;
    RDB_type *tbtyp = RDB_create_relation_type(attrc, heading, ecp);
    if (tbtyp == NULL) {
        return RDB_ERROR;
    }

    *tbpp = _RDB_new_rtable(name, RDB_TRUE, tbtyp, keyc, keyv, RDB_FALSE, ecp);
    if (*tbpp == NULL) {
        RDB_drop_type(tbtyp, ecp, NULL);
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }

    if (create) {
        ret = _RDB_create_stored_table(*tbpp, txp != NULL ? txp->envp : NULL,
                NULL, ecp, txp);
    } else {
        ret = _RDB_open_stored_table(*tbpp, txp != NULL ? txp->envp : NULL,
                name, -1, NULL, ecp, txp);
    }
    if (ret != RDB_OK) {
        _RDB_free_obj(*tbpp, ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

static int
insert_version_info(RDB_dbroot *dbrootp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;

    RDB_init_obj(&tpl);
    ret = RDB_tuple_set_int(&tpl, "MAJOR_VERSION", MAJOR_VERSION, ecp);
    if (ret != RDB_OK)
        goto cleanup;
    ret = RDB_tuple_set_int(&tpl, "MINOR_VERSION", MINOR_VERSION, ecp);
    if (ret != RDB_OK)
        goto cleanup;
    ret = RDB_tuple_set_int(&tpl, "MICRO_VERSION", 0, ecp);
    if (ret != RDB_OK)
        goto cleanup;

    /* bypass checks */
    ret = _RDB_insert_real(dbrootp->version_info_tbp, &tpl, ecp, txp);

cleanup:
    RDB_destroy_obj(&tpl, ecp);
    return ret;
}

static int
check_version_info(RDB_dbroot *dbrootp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;

    RDB_init_obj(&tpl);
    ret = RDB_extract_tuple(dbrootp->version_info_tbp, ecp, txp, &tpl);
    if (ret != RDB_OK) {
        if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_NOT_FOUND_ERROR) {
            RDB_clear_err(ecp);
            RDB_raise_version_mismatch(ecp);
            ret = RDB_ERROR;
        }
        goto cleanup;
    }

    if (RDB_tuple_get_int(&tpl, "MAJOR_VERSION") != MAJOR_VERSION
            || RDB_tuple_get_int(&tpl, "MINOR_VERSION") != MINOR_VERSION) {
        RDB_raise_version_mismatch(ecp);
        ret = RDB_ERROR;
    } else {
        ret = RDB_OK;
    }

cleanup:
    RDB_destroy_obj(&tpl, ecp);
    return ret;
}

static int
open_indexes(RDB_object *tbp, RDB_dbroot *dbrootp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int i;
    int ret;
    _RDB_tbindex *indexv;
    int indexc = _RDB_cat_get_indexes(RDB_table_name(tbp), dbrootp, ecp, txp, &indexv);
    if (indexc < 0)
        return indexc;

    tbp->var.tb.stp->indexc = indexc;
    tbp->var.tb.stp->indexv = indexv;

    /* Open secondary indexes */
    for (i = 0; i < indexc; i++) {
        char *p = strchr(indexv[i].name, '$');
        if (p == NULL || strcmp (p, "$0") != 0) {
            ret = _RDB_open_table_index(tbp, &indexv[i], dbrootp->envp, ecp,
                    txp);
            if (ret != RDB_OK)
                return RDB_ERROR;
        } else {
            indexv[i].idxp = NULL;
        }
    }

    return RDB_OK;
}

/*
 * Create or open system tables.
 * If the tables are created, associate them with the database pointed to by
 * txp->dbp.
 */
int
_RDB_open_systables(RDB_dbroot *dbrootp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int ret;
    RDB_attr keysattr;
    RDB_type *typ;
    RDB_bool create = RDB_FALSE;

    /* Create or open catalog tables */

    for(;;) {
        ret = provide_systable("SYS_TABLEATTRS", 4, table_attr_attrv,
                1, table_attr_keyv, create, ecp, txp, dbrootp->envp,
                &dbrootp->table_attr_tbp);
        if (!create && ret != RDB_OK
                && RDB_obj_type(RDB_get_err(ecp)) == &RDB_NOT_FOUND_ERROR) {
            /* Table not found, so tables must be created */
            create = RDB_TRUE;
            RDB_clear_err(ecp);
        } else {
            break;
        }
    }       
    if (ret != RDB_OK) {
        return ret;
    }
        
    ret = provide_systable("SYS_TABLEATTR_DEFVALS",
            3, table_attr_defvals_attrv, 1, table_attr_defvals_keyv,
            create, ecp, txp, dbrootp->envp, &dbrootp->table_attr_defvals_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = provide_systable("SYS_RTABLES", 2, rtables_attrv,
            1, rtables_keyv, create, ecp,
            txp, dbrootp->envp, &dbrootp->rtables_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = provide_systable("SYS_VTABLES", 3, vtables_attrv,
            1, vtables_keyv, create, ecp, txp, dbrootp->envp,
            &dbrootp->vtables_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = provide_systable("SYS_TABLE_RECMAP", 2, table_recmap_attrv,
            2, table_recmap_keyv, create, ecp,
            txp, dbrootp->envp, &dbrootp->table_recmap_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = provide_systable("SYS_DBTABLES", 2, dbtables_attrv, 1, dbtables_keyv,
            create, ecp, txp, dbrootp->envp, &dbrootp->dbtables_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    keysattr.name = "KEY";
    keysattr.typ = &RDB_STRING;
    keys_attrv[2].typ = RDB_create_relation_type(1, &keysattr, ecp);
    if (keys_attrv[2].typ == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }

    ret = provide_systable("SYS_KEYS", 3, keys_attrv, 1, keys_keyv,
            create, ecp, txp, dbrootp->envp, &dbrootp->keys_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = provide_systable("SYS_TYPES", 5, types_attrv, 1, types_keyv,
            create, ecp, txp, dbrootp->envp, &dbrootp->types_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = provide_systable("SYS_POSSREPCOMPS", 5, possrepcomps_attrv,
            2, possrepcomps_keyv, create, ecp, txp, dbrootp->envp,
            &dbrootp->possrepcomps_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ro_ops_attrv[1].typ = RDB_create_array_type(&RDB_BINARY, ecp);
    if (ro_ops_attrv[1].typ == NULL) {
        return RDB_ERROR;
    }

    ret = provide_systable("SYS_RO_OPS", 6, ro_ops_attrv,
            1, ro_ops_keyv, create, ecp, txp, dbrootp->envp,
            &dbrootp->ro_ops_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    upd_ops_attrv[1].typ = RDB_create_array_type(&RDB_BINARY, ecp);
    if (upd_ops_attrv[1].typ == NULL) {
        return RDB_ERROR;
    }
    upd_ops_attrv[5].typ = RDB_create_array_type(&RDB_BOOLEAN, ecp);
    if (upd_ops_attrv[5].typ == NULL) {
        return RDB_ERROR;
    }

    ret = provide_systable("SYS_UPD_OPS", 6, upd_ops_attrv,
            1, upd_ops_keyv, create, ecp, txp, dbrootp->envp,
            &dbrootp->upd_ops_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = provide_systable("SYS_VERSION_INFO", 3, version_info_attrv,
            1, version_info_keyv, create, ecp, txp, dbrootp->envp,
            &dbrootp->version_info_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    if (create) {
        /* Insert version info into catalog */
        ret = insert_version_info(dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;
    } else {
        /* Check if catalog version matches software version */
        ret = check_version_info(dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;
    }

    ret = provide_systable("SYS_CONSTRAINTS", 2, constraints_attrv,
            1, constraints_keyv, create, ecp, txp, dbrootp->envp,
            &dbrootp->constraints_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    typ = RDB_create_tuple_type(2, indexes_attrs_attrv, ecp);
    if (typ == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }
    indexes_attrv[2].typ = RDB_create_array_type(typ, ecp);
    if (indexes_attrv[2].typ == NULL) {
        return RDB_ERROR;
    }

    ret = provide_systable("SYS_INDEXES", 5, indexes_attrv,
            1, indexes_keyv, create, ecp, txp, dbrootp->envp,
            &dbrootp->indexes_tbp);
    if (ret != RDB_OK) {
        return ret;
    }

    if (!create && (dbrootp->rtables_tbp->var.tb.stp->indexc == -1)) {
        /*
         * Read indexes from the catalog 
         */

        /* Open indexes of SYS_INDEXES first, because they are used often */
        ret = open_indexes(dbrootp->indexes_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->rtables_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->table_attr_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->table_attr_defvals_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->vtables_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->table_recmap_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->dbtables_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->keys_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->types_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->possrepcomps_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->ro_ops_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->upd_ops_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->constraints_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;

        ret = open_indexes(dbrootp->version_info_tbp, dbrootp, ecp, txp);
        if (ret != RDB_OK)
            return ret;
    }
    return RDB_OK;
}

int
_RDB_cat_create_db(RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;

    ret = _RDB_cat_insert(txp->dbp->dbrootp->table_attr_tbp, ecp, txp);
    if (ret != RDB_OK) {
        if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_ELEMENT_EXISTS_ERROR) {
            RDB_clear_err(ecp);
            /*
             * The catalog table already exists, but not in this database,
             * so associate it with this database too
             */
            ret = dbtables_insert(txp->dbp->dbrootp->table_attr_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->table_attr_defvals_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->rtables_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->vtables_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->table_recmap_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->dbtables_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->keys_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->types_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->possrepcomps_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->ro_ops_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->upd_ops_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;
            ret = dbtables_insert(txp->dbp->dbrootp->indexes_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;

            ret = dbtables_insert(txp->dbp->dbrootp->constraints_tbp, ecp, txp);
            if (ret != RDB_OK) 
                return ret;

            ret = dbtables_insert(txp->dbp->dbrootp->version_info_tbp, ecp, txp);
        }
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->table_attr_defvals_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->rtables_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->vtables_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->table_recmap_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->dbtables_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->keys_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->types_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->possrepcomps_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->ro_ops_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->upd_ops_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->indexes_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->constraints_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    ret = _RDB_cat_insert(txp->dbp->dbrootp->version_info_tbp, ecp, txp);
    if (ret != RDB_OK) {
        return ret;
    }

    return ret;
}

static int
get_key(RDB_object *tbp, RDB_string_vec *keyp, RDB_exec_context *ecp)
{
    int ret;
    int i;
    RDB_object attrarr;
    RDB_object *tplp;

    RDB_init_obj(&attrarr);
    ret = RDB_table_to_array(&attrarr, tbp, 0, NULL, 0, ecp, NULL);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&attrarr, ecp);
        return ret;
    }
    keyp->strc = RDB_array_length(&attrarr, ecp);
    if (keyp->strc < 0) {
        RDB_destroy_obj(&attrarr, ecp);
        ret = keyp->strc;
        return ret;
    }
    keyp->strv = RDB_alloc(sizeof (char *) * keyp->strc, ecp);
    if (keyp->strv == NULL) {
        RDB_raise_no_memory(ecp);
        goto error;
    }

    for (i = 0; i < keyp->strc; i++)
        keyp->strv[i] = NULL;

    for (i = 0; i < keyp->strc; i++) {
        tplp = RDB_array_get(&attrarr, i, ecp);
        if (tplp == NULL) {
            RDB_destroy_obj(&attrarr, ecp);
            goto error;
        }
        keyp->strv[i] = RDB_dup_str(RDB_tuple_get_string(tplp, "KEY"));
        if (keyp->strv[i] == NULL) {
            RDB_raise_no_memory(ecp);
            goto error;
        }
    }

    RDB_destroy_obj(&attrarr, ecp);
    return RDB_OK;

error:
    if (keyp->strv != NULL) {
        for (i = 0; i < keyp->strc; i++)
            RDB_free(keyp->strv[i]);
        RDB_free(keyp->strv);
    }
    RDB_destroy_obj(&attrarr, ecp);
    return RDB_ERROR;
}

static int
get_keys(const char *name, RDB_exec_context *ecp, RDB_transaction *txp,
         int *keycp, RDB_string_vec **keyvp)
{
    RDB_expression *exp, *argp;
    RDB_object *vtbp;
    RDB_object arr;
    RDB_object *tplp;
    int ret;
    int i;

    *keyvp = NULL;

    exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        return RDB_ERROR;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->keys_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_string_to_expr(name, ecp),
            RDB_var_ref("TABLENAME", ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);

    vtbp = RDB_expr_to_vtable(exp, ecp, txp);
    if (vtbp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }

    RDB_init_obj(&arr);
    
    ret = RDB_table_to_array(&arr, vtbp, 0, NULL, 0, ecp, txp);
    if (ret != RDB_OK)
        goto error;

    ret = RDB_array_length(&arr, ecp);
    if (ret < 0)
        goto error;
    *keycp = ret;

    *keyvp = RDB_alloc(sizeof(RDB_string_vec) * *keycp, ecp);
    if (*keyvp == NULL) {
        goto error;
    }
    for (i = 0; i < *keycp; i++)
        (*keyvp)[i].strv = NULL;

    for (i = 0; i < *keycp; i++) {
        RDB_int kno;
        tplp = RDB_array_get(&arr, i, ecp);
        if (tplp == NULL) {
            goto error;
        }
        kno = RDB_tuple_get_int(tplp, "KEYNO");

        ret = get_key(RDB_tuple_get(tplp, "ATTRS"), &(*keyvp)[kno], ecp);
        if (ret != RDB_OK) {
            goto error;
        }
    }
    ret = RDB_destroy_obj(&arr, ecp);
    RDB_drop_table(vtbp, ecp, txp);

    return ret;

error:
    RDB_destroy_obj(&arr, ecp);
    RDB_drop_table(vtbp, ecp, txp);
    if (*keyvp != NULL) {
        int i, j;
    
        for (i = 0; i < *keycp; i++) {
            if ((*keyvp)[i].strv != NULL) {
                for (j = 0; j < (*keyvp)[i].strc; j++)
                    RDB_free((*keyvp)[i].strv[j]);
            }
        }
        RDB_free(*keyvp);
    }
    return RDB_ERROR;
}

RDB_object *
_RDB_cat_get_rtable(const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_expression *exp, *argp;
    RDB_object *tbp = NULL;
    RDB_object *tmptb1p = NULL;
    RDB_object *tmptb2p = NULL;
    RDB_object *tmptb3p = NULL;
    RDB_object *tmptb4p = NULL;
    RDB_object arr;
    RDB_object tpl;
    RDB_object *tplp;
    RDB_bool usr;
    int ret;
    RDB_int i;
    int attrc;
    RDB_attr *attrv = NULL;
    int defvalc;
    int keyc;
    RDB_string_vec *keyv;
    RDB_type *tbtyp;
    int indexc;
    _RDB_tbindex *indexv;
    char *recmapname = NULL;

    /* Read real table data from the catalog */

    RDB_init_obj(&arr);
    RDB_init_obj(&tpl);

    /* !! Should check if table is from txp->dbp ... */

    exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        goto error;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->rtables_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }

    /* Set transformed flags to avoid infinite recursion */

    argp->transformed = RDB_TRUE;
    RDB_add_arg(exp, argp);

    exp->transformed = RDB_TRUE;

    tmptb1p = RDB_expr_to_vtable(exp, ecp, txp);
    if (tmptb1p == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }

    ret = RDB_extract_tuple(tmptb1p, ecp, txp, &tpl);
    if (ret != RDB_OK) {
        goto error;
    }

    usr = RDB_tuple_get_bool(&tpl, "IS_USER");

    /*
     * Read attribute names and types
     */

    exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        goto error;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->table_attr_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    RDB_add_arg(exp, argp);

    tmptb2p = RDB_expr_to_vtable(exp, ecp, txp);
    if (tmptb2p == NULL)
        goto error;
    ret = RDB_table_to_array(&arr, tmptb2p, 0, NULL, 0, ecp, txp);
    if (ret != RDB_OK) {
        goto error;
    }

    attrc = RDB_array_length(&arr, ecp);
    if (attrc > 0) {
        attrv = RDB_alloc(sizeof(RDB_attr) * attrc, ecp);
        if (attrv == NULL) {
            goto error;
        }
    }
    for (i = 0; i < attrc; i++)
        attrv[i].name = NULL;

    for (i = 0; i < attrc; i++) {
        RDB_object *typedatap;
        RDB_int fno;

        tplp = RDB_array_get(&arr, i, ecp);
        if (tplp == NULL)
            goto error;
        fno = RDB_tuple_get_int(tplp, "I_FNO");
        attrv[fno].name = RDB_dup_str(RDB_tuple_get_string(tplp, "ATTRNAME"));
        if (attrv[fno].name == NULL) {
            RDB_raise_no_memory(ecp);
            goto error;
        }
        typedatap = RDB_tuple_get(tplp, "TYPE");

        attrv[fno].typ = _RDB_binobj_to_type(typedatap, ecp, txp);
        if (attrv[fno].typ == NULL)
            goto error;
        
        attrv[fno].defaultp = NULL;
        attrv[fno].options = 0;
    }

    /*
     * Read default values
     */

    exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        goto error;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->table_attr_defvals_tbp,
            ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    RDB_add_arg(exp, argp);

    tmptb3p = RDB_expr_to_vtable(exp, ecp, txp);
    if (tmptb3p == NULL)
        goto error;
    ret = RDB_table_to_array(&arr, tmptb3p, 0, NULL, 0, ecp, txp);
    if (ret != RDB_OK) {
        goto error;
    }

    defvalc = RDB_array_length(&arr, ecp);

    for (i = 0; i < defvalc; i++) {
        char *name;
        RDB_object *binvalp;

        tplp = RDB_array_get(&arr, i, ecp);
        if (tplp == NULL)
            goto error;
        name = RDB_tuple_get_string(tplp, "ATTRNAME");
        binvalp = RDB_tuple_get(tplp, "DEFAULT_VALUE");
        
        /* Find attrv entry and set default value */
        for (i = 0; i < attrc && (strcmp(attrv[i].name, name) != 0); i++);
        if (i >= attrc) {
            /* Not found */
            RDB_raise_internal(
                    "attribute not found while setting default value", ecp);
            goto error;
        }
        attrv[i].defaultp = RDB_alloc(sizeof (RDB_object), ecp);
        if (attrv[i].defaultp == NULL) {
            goto error;
        }
        RDB_init_obj(attrv[i].defaultp);
        ret = RDB_irep_to_obj(attrv[i].defaultp, attrv[i].typ,
                binvalp->var.bin.datap, binvalp->var.bin.len, ecp);
        if (ret != RDB_OK)
            goto error;            
    }

    ret = get_keys(name, ecp, txp, &keyc, &keyv);
    if (ret != RDB_OK)
        goto error;

    if (usr) {
        /*
         * Read recmap name from catalog, if it's user table.
         * For system tables, the recmap name is the table name.
         */
        exp = RDB_ro_op("WHERE", ecp);
        if (exp == NULL) {
            goto error;
        }
        argp = RDB_table_ref(txp->dbp->dbrootp->table_recmap_tbp, ecp);
        if (argp == NULL) {
            RDB_drop_expr(exp, ecp);
            goto error;
        }
        RDB_add_arg(exp, argp);
        argp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
                RDB_string_to_expr(name, ecp), ecp);
        if (argp == NULL) {
            RDB_drop_expr(exp, ecp);
            goto error;
        }
        RDB_add_arg(exp, argp);

        tmptb4p = RDB_expr_to_vtable(exp, ecp, txp);
        if (tmptb4p == NULL) {
            RDB_drop_expr(exp, ecp);
            goto error;
        }
        ret = RDB_extract_tuple(tmptb4p, ecp, txp, &tpl);
        if (ret != RDB_OK) {
            if (RDB_obj_type(RDB_get_err(ecp)) != &RDB_NOT_FOUND_ERROR) {
                goto error;
            }
            RDB_clear_err(ecp);
        }
        if (ret == RDB_OK) {
            recmapname = RDB_tuple_get_string(&tpl, "RECMAP");
        }
    }

    tbtyp = RDB_create_relation_type(attrc, attrv, ecp);
    if (tbtyp == NULL) {
        RDB_drop_type(tbtyp, ecp, NULL);
        goto error;
    }
    if (_RDB_set_defvals(tbtyp, attrc, attrv, ecp) != RDB_OK) {
        RDB_drop_type(tbtyp, ecp, NULL);
        return NULL;
    }

    tbp = _RDB_new_rtable(name, RDB_TRUE, tbtyp, keyc, keyv, usr, ecp);
    _RDB_free_keys(keyc, keyv);
    if (tbp == NULL) {
        RDB_drop_type(tbtyp, ecp, NULL);
        goto error;
    }

    indexc = _RDB_cat_get_indexes(name, txp->dbp->dbrootp, ecp, txp, &indexv);
    if (indexc < 0) {
        ret = indexc;
        goto error;
    }

    if (!usr) {
        recmapname = RDB_table_name(tbp);
    }
    if (recmapname != NULL) {
        ret = _RDB_open_stored_table(tbp, txp->envp, recmapname,
                indexc, indexv, ecp, txp);
        if (ret != RDB_OK) {
            goto error;
        }
    }
    for (i = 0; i < attrc; i++) {
        RDB_free(attrv[i].name);
        if (attrv[i].defaultp != NULL) {
            RDB_destroy_obj(attrv[i].defaultp, ecp);
            RDB_free(attrv[i].defaultp);
        }
    }
    if (attrc > 0)
        RDB_free(attrv);

    if (_RDB_assoc_table_db(tbp, txp->dbp, ecp) != RDB_OK)
        goto error;

    ret = RDB_destroy_obj(&arr, ecp);

    RDB_drop_table(tmptb1p, ecp, txp);
    RDB_drop_table(tmptb2p, ecp, txp);
    RDB_drop_table(tmptb3p, ecp, txp);
    if (tmptb4p != NULL)
        RDB_drop_table(tmptb4p, ecp, txp);

    RDB_destroy_obj(&tpl, ecp);

    return tbp;

error:
    if (attrv != NULL) {
        for (i = 0; i < attrc; i++)
            RDB_free(attrv[i].name);
        RDB_free(attrv);
    }

    RDB_destroy_obj(&arr, ecp);

    if (tmptb1p != NULL)
        RDB_drop_table(tmptb1p, ecp, txp);
    if (tmptb2p != NULL)
        RDB_drop_table(tmptb2p, ecp, txp);
    if (tmptb3p != NULL)
        RDB_drop_table(tmptb3p, ecp, txp);
    if (tmptb4p != NULL)
        RDB_drop_table(tmptb4p, ecp, txp);

    RDB_destroy_obj(&tpl, ecp);

    if (tbp != NULL)
        _RDB_free_obj(tbp, ecp);

    return NULL;
} /* _RDB_cat_get_rtable */

RDB_object *
_RDB_cat_get_vtable(const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_object *tbp;
    RDB_expression *exp, *argp;
    RDB_object *tmptbp = NULL;
    RDB_object tpl;
    RDB_object arr;
    RDB_object *valp;
    RDB_bool usr;
    int ret;

    /*
     * Read virtual table data from the catalog
     */

    RDB_init_obj(&arr);
    RDB_init_obj(&tpl);

    exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        goto error;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->vtables_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    /* Set transformed flags to avoid infinite recursion */

    argp->transformed = RDB_TRUE;
    RDB_add_arg(exp, argp);

    argp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    argp->transformed = RDB_TRUE;
    RDB_add_arg(exp, argp);

    exp->transformed = RDB_TRUE;
    tmptbp = RDB_expr_to_vtable(exp, ecp, txp);
    if (tmptbp == NULL) {
        RDB_drop_expr(exp, ecp);
        goto error;
    }
    
    ret = RDB_extract_tuple(tmptbp, ecp, txp, &tpl);
    RDB_drop_table(tmptbp, ecp, txp);
    if (ret != RDB_OK) {
        goto error;
    }

    usr = RDB_tuple_get_bool(&tpl, "IS_USER");

    valp = RDB_tuple_get(&tpl, "I_DEF");

    tbp = _RDB_binobj_to_vtable(valp, ecp, txp);
    if (tbp == NULL)
        goto error;

    RDB_destroy_obj(&tpl, ecp);
    ret = RDB_destroy_obj(&arr, ecp);
    if (ret != RDB_OK)
        goto error;

    tbp->var.tb.is_persistent = RDB_TRUE;
    tbp->var.tb.is_user = usr;

    tbp->var.tb.name = RDB_dup_str(name);
    if (tbp->var.tb.name == NULL) {
        RDB_raise_no_memory(ecp);
        return NULL;
    }

    if (_RDB_assoc_table_db(tbp, txp->dbp, ecp) != RDB_OK)
        goto error;

    if (RDB_FALSE /* !! */) {
        fprintf(stderr,
                "Definition of virtual table %s read from the catalog\n",
                name);
    }
    return tbp;

error:
    RDB_destroy_obj(&tpl, ecp);
    RDB_destroy_obj(&arr, ecp);
    
    return NULL;
} /* _RDB_cat_get_vtable */

int
_RDB_cat_rename_table(RDB_object *tbp, const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int ret;
    RDB_attr_update upd;
    RDB_expression *condp = RDB_eq(RDB_var_ref("TABLENAME", ecp),
            RDB_string_to_expr(RDB_table_name(tbp), ecp), ecp);
    if (condp == NULL) {
        return RDB_ERROR;
    }

    upd.name = "TABLENAME";
    upd.exp = RDB_string_to_expr(name, ecp);
    if (upd.exp == NULL) {
        ret = RDB_ERROR;
        goto cleanup;
    }

    if (tbp->var.tb.exp == NULL) {
        ret = RDB_update(txp->dbp->dbrootp->rtables_tbp, condp, 1, &upd, ecp, txp);
        if (ret == RDB_ERROR)
            goto cleanup;
        ret = RDB_update(txp->dbp->dbrootp->table_attr_tbp, condp, 1, &upd, ecp, txp);
        if (ret == RDB_ERROR)
            goto cleanup;
        ret = RDB_update(txp->dbp->dbrootp->table_attr_defvals_tbp, condp,
                1, &upd, ecp, txp);
        if (ret == RDB_ERROR)
            goto cleanup;
        ret = RDB_update(txp->dbp->dbrootp->keys_tbp, condp, 1, &upd, ecp, txp);
        if (ret == RDB_ERROR)
            goto cleanup;
        ret = RDB_update(txp->dbp->dbrootp->indexes_tbp, condp, 1, &upd, ecp, txp);
        if (ret == RDB_ERROR)
            goto cleanup;
    } else {
        ret = RDB_update(txp->dbp->dbrootp->vtables_tbp, condp, 1, &upd, ecp, txp);
        if (ret == RDB_ERROR)
            goto cleanup;
    }
    ret = RDB_update(txp->dbp->dbrootp->table_recmap_tbp, condp, 1, &upd, ecp, txp);
    if (ret == RDB_ERROR)
        goto cleanup;
    ret = RDB_update(txp->dbp->dbrootp->dbtables_tbp, condp, 1, &upd, ecp, txp);

cleanup:
    RDB_drop_expr(condp, ecp);
    if (upd.exp != NULL)
        RDB_drop_expr(upd.exp, ecp);
    return ret == RDB_ERROR ? RDB_ERROR : RDB_OK;
}

static int
types_query(const char *name, RDB_exec_context *ecp, RDB_transaction *txp,
        RDB_object **tbpp)
{
    RDB_expression *exp, *argp;

    exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        return RDB_ERROR;
    }

    argp = RDB_table_ref(txp->dbp->dbrootp->types_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("TYPENAME", ecp),
                    RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);

    *tbpp = RDB_expr_to_vtable(exp, ecp, txp);
    if (*tbpp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

int
_RDB_possreps_query(const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp, RDB_object **tbpp)
{
    RDB_expression *exp, *argp;

    exp = RDB_ro_op("PROJECT", ecp);
    if (exp == NULL)
    	return RDB_ERROR;
    argp = RDB_table_ref(txp->dbp->dbrootp->possrepcomps_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_string_to_expr("TYPENAME", ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_string_to_expr("POSSREPNAME", ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);

    argp = exp;
    exp = RDB_ro_op("WHERE", ecp);
    if (exp == NULL) {
        RDB_drop_expr(argp, ecp);        
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("TYPENAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return RDB_ERROR;
    }
    RDB_add_arg(exp, argp);

    *tbpp = RDB_expr_to_vtable(exp, ecp, txp);
    if (*tbpp == NULL) {
        return RDB_ERROR;
    }
    return RDB_OK;
}

static RDB_object *
possrepcomps_query(const char *name, const char *possrepname,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_object *tbp;
    RDB_expression *argp, *wexp;
    RDB_expression *exp = RDB_ro_op("AND", ecp);
    if (exp == NULL) {
        return NULL;
    }
    argp = RDB_eq(RDB_var_ref("TYPENAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    RDB_add_arg(exp, argp);
    argp = RDB_eq(RDB_var_ref("POSSREPNAME", ecp),
            RDB_string_to_expr(possrepname, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);        
        return NULL;
    }
    RDB_add_arg(exp, argp);

    wexp = RDB_ro_op("WHERE", ecp);
    if (wexp == NULL) {
        RDB_drop_expr(exp, ecp);        
        return NULL;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->possrepcomps_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(wexp, ecp);
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    RDB_add_arg(wexp, argp);
    RDB_add_arg(wexp, exp);

    tbp = RDB_expr_to_vtable(wexp, ecp, txp);
    if (tbp == NULL) {
        RDB_drop_expr(wexp, ecp);
        return NULL;
    }
    return tbp;
}

static int
get_possrepcomps(const char *typename, RDB_possrep *rep,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    int i;
    RDB_object comps;
    RDB_object *tplp;
    RDB_object *tmptbp = NULL;

    RDB_init_obj(&comps);

    tmptbp = possrepcomps_query(typename, rep->name, ecp, txp);
    if (tmptbp == NULL) {
        goto error;
    }
    ret = RDB_table_to_array(&comps, tmptbp, 0, NULL, 0, ecp, txp);
    if (ret != RDB_OK) {
        goto error;
    }
    ret = RDB_array_length(&comps, ecp);
    if (ret < 0) {
        goto error;
    }
    rep->compc = ret;
    if (ret > 0) {
        rep->compv = RDB_alloc(ret * sizeof (RDB_attr), ecp);
        if (rep->compv == NULL) {
            goto error;
        }
    } else {
        rep->compv = NULL;
    }

    /*
     * Read component data from array and store it in
     * rep->compv.
     */
    for (i = 0; i < rep->compc; i++) {
        RDB_int idx;

        tplp = RDB_array_get(&comps, (RDB_int) i, ecp);
        if (tplp == NULL)
            goto error;
        idx = RDB_tuple_get_int(tplp, "COMPNO");
        rep->compv[idx].name = RDB_dup_str(
                RDB_tuple_get_string(tplp, "COMPNAME"));
        if (rep->compv[idx].name == NULL) {
            RDB_raise_no_memory(ecp);
            goto error;
        }

        rep->compv[idx].typ = _RDB_binobj_to_type(
                RDB_tuple_get(tplp, "COMPTYPE"), ecp, txp);
        if (rep->compv[idx].typ == NULL)
            goto error;
    }
    RDB_drop_table(tmptbp, ecp, txp);
    RDB_destroy_obj(&comps, ecp);

    return RDB_OK;

error:
    if (tmptbp != NULL)
        RDB_drop_table(tmptbp, ecp, txp);
    RDB_destroy_obj(&comps, ecp);

    return RDB_ERROR;
}

int
_RDB_cat_get_type(const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp, RDB_type **typp)
{
    RDB_object *tmptb1p = NULL;
    RDB_object *tmptb2p = NULL;
    RDB_object tpl;
    RDB_object *tplp;
    RDB_object possreps;
    RDB_object *cvalp;
    RDB_type *typ = NULL;
    RDB_object *typedatap;
    int ret, tret;
    int i;
    RDB_type *typv[2];
    RDB_operator *cmpop;

    RDB_init_obj(&tpl);
    RDB_init_obj(&possreps);

    /*
     * Get type info from SYS_TYPES
     */

    ret = types_query(name, ecp, txp, &tmptb1p);
    if (ret != RDB_OK)
        goto error;

    ret = RDB_extract_tuple(tmptb1p, ecp, txp, &tpl);
    if (ret != RDB_OK)
        goto error;

    typ = RDB_alloc(sizeof (RDB_type), ecp);
    if (typ == NULL) {
        RDB_raise_no_memory(ecp);
        goto error;
    }
    typ->kind = RDB_TP_SCALAR;
    typ->compare_op = NULL;

    typedatap = RDB_tuple_get(&tpl, "I_AREP_TYPE");
    if (RDB_binary_length(typedatap) != 0) {   
        typ->var.scalar.arep = _RDB_binobj_to_type(typedatap, ecp, txp);
        if (typ->var.scalar.arep == NULL)
            goto error;
    } else {
        typ->var.scalar.arep = NULL;
    }

    typ->name = RDB_dup_str(name);
    if (typ->name == NULL) {
        RDB_raise_no_memory(ecp);
        goto error;
    }

    cvalp = RDB_tuple_get(&tpl, "I_CONSTRAINT");
    if (RDB_binary_length(cvalp) > 0) {
        typ->var.scalar.constraintp = _RDB_binobj_to_expr(cvalp, ecp, txp);
        if (typ->var.scalar.constraintp == NULL)
            goto error;
    } else {
        typ->var.scalar.constraintp = NULL;
    }

    typ->ireplen = RDB_tuple_get_int(&tpl, "I_AREP_LEN");
    typ->var.scalar.sysimpl = RDB_tuple_get_bool(&tpl, "I_SYSIMPL");
    typ->var.scalar.repc = 0;
    typ->var.scalar.builtin = RDB_FALSE;

    /*
     * Get possrep info from SYS_POSSREPS
     */

    ret = _RDB_possreps_query(name, ecp, txp, &tmptb2p);
    if (ret != RDB_OK)
        goto error;
    ret = RDB_table_to_array(&possreps, tmptb2p, 0, NULL, 0, ecp, txp);
    if (ret != RDB_OK) {
        goto error;
    }
    ret = RDB_array_length(&possreps, ecp);
    if (ret < 0) {
        goto error;
    }
    typ->var.scalar.repc = ret;
    if (ret > 0) {
        typ->var.scalar.repv = RDB_alloc(ret * sizeof (RDB_possrep), ecp);
        if (typ->var.scalar.repv == NULL) {
            goto error;
        }
    }
    for (i = 0; i < typ->var.scalar.repc; i++)
        typ->var.scalar.repv[i].compv = NULL;

    /*
     * Read possrep data from array and store it in typ->var.scalar.repv
     */
    for (i = 0; i < typ->var.scalar.repc; i++) {
        tplp = RDB_array_get(&possreps, (RDB_int) i, ecp);
        if (tplp == NULL)
            goto error;
        typ->var.scalar.repv[i].name = RDB_dup_str(
                RDB_tuple_get_string(tplp, "POSSREPNAME"));
        if (typ->var.scalar.repv[i].name == NULL) {
            RDB_raise_no_memory(ecp);
            goto error;
        }

        ret = get_possrepcomps(name, &typ->var.scalar.repv[i], ecp, txp);
        if (ret != RDB_OK)
            goto error;
    }

    /* Search for comparison function */
    typv[0] = typ;
    typv[1] = typ;
    cmpop = _RDB_get_ro_op("CMP", 2, typv, ecp, txp);
    if (cmpop != NULL) {
        typ->compare_op = cmpop;
    } else {
        RDB_object *errp = RDB_get_err(ecp);
        if (errp != NULL
                && RDB_obj_type(errp) != &RDB_OPERATOR_NOT_FOUND_ERROR
                && RDB_obj_type(errp) != &RDB_TYPE_MISMATCH_ERROR) {
            goto error;
        }
        RDB_clear_err(ecp);
    }

    *typp = typ;

    ret = RDB_drop_table(tmptb1p, ecp, txp);
    tret = RDB_drop_table(tmptb2p, ecp, txp);
    if (ret == RDB_OK)
        ret = tret;

    RDB_destroy_obj(&tpl, ecp);
    tret = RDB_destroy_obj(&possreps, ecp);
    if (ret == RDB_OK)
        ret = tret;

    return ret;

error:
    if (tmptb1p != NULL)
        RDB_drop_table(tmptb1p, ecp, txp);
    if (tmptb2p != NULL)
        RDB_drop_table(tmptb2p, ecp, txp);
    RDB_destroy_obj(&tpl, ecp);
    RDB_destroy_obj(&possreps, ecp);
    if (typ != NULL) {
        if (typ->var.scalar.repc != 0) {
            for (i = 0; i < typ->var.scalar.repc; i++)
                RDB_free(typ->var.scalar.repv[i].compv);
            RDB_free(typ->var.scalar.repv);
        }
        RDB_free(typ->name);
        RDB_free(typ);
    }
    return RDB_ERROR;
}

int
_RDB_make_typesobj(int argc, RDB_type *argtv[], RDB_exec_context *ecp,
        RDB_object *objp)
{
    int i;
    int ret;
    RDB_object typeobj;

    RDB_set_array_length(objp, argc, ecp);
    RDB_init_obj(&typeobj);
    for (i = 0; i < argc; i++) {
        ret = _RDB_type_to_binobj(&typeobj, argtv[i], ecp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&typeobj, ecp);
            return ret;
        }
        ret = RDB_array_set(objp, (RDB_int) i, &typeobj, ecp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&typeobj, ecp);
            return ret;
        }
    }
    return RDB_destroy_obj(&typeobj, ecp);
}

/* Read read-only operator from database */
RDB_operator *
_RDB_cat_get_ro_op(const char *name, int argc, RDB_type *argtv[],
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_expression *exp, *wexp, *argp;
    RDB_object *vtbp;
    RDB_object tpl;
    RDB_object typesobj;
    int ret;
    RDB_type *rtyp;
    char *libname, *symname;
    RDB_operator *op = NULL;

    RDB_init_obj(&typesobj);
    ret = _RDB_make_typesobj(argc, argtv, ecp, &typesobj);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&typesobj, ecp);
        return NULL;
    }

    exp = RDB_ro_op("AND", ecp);
    if (exp == NULL) {
        RDB_destroy_obj(&typesobj, ecp);
        return NULL;
    }
    argp = RDB_eq(RDB_var_ref("NAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        RDB_destroy_obj(&typesobj, ecp);
        return NULL;
    }
    RDB_add_arg(exp, argp);
    RDB_obj_set_typeinfo(&typesobj, ro_ops_attrv[1].typ); /* Set array type */
    argp = RDB_eq(RDB_var_ref("ARGTYPES", ecp),
            RDB_obj_to_expr(&typesobj, ecp), ecp);
    RDB_destroy_obj(&typesobj, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    RDB_add_arg(exp, argp);

    wexp = RDB_ro_op("WHERE", ecp);
    if (wexp == NULL) {
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->ro_ops_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(wexp, ecp);
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    RDB_add_arg(wexp, argp);
    RDB_add_arg(wexp, exp);

    vtbp = RDB_expr_to_vtable(wexp, ecp, txp);
    if (vtbp == NULL) {
        RDB_drop_expr(wexp, ecp);
        return NULL;
    }
    RDB_init_obj(&tpl);
    ret = RDB_extract_tuple(vtbp, ecp, txp, &tpl);
    RDB_drop_table(vtbp, ecp, txp);
    if (ret != RDB_OK)
        goto error;

    rtyp = _RDB_binobj_to_type(RDB_tuple_get(&tpl, "RTYPE"), ecp, txp);
    if (rtyp == NULL) {
        goto error;
    }

    op = _RDB_new_operator(name, argc, argtv, rtyp, ecp);
    if (op == NULL)
        goto error;
    op->opfn.ro_fp = NULL;

    RDB_init_obj(&op->iarg);

    ret = RDB_copy_obj(&op->iarg, RDB_tuple_get(&tpl, "IARG"), ecp);
    if (ret != RDB_OK)
        goto error;

    libname = RDB_tuple_get_string(&tpl, "LIB");
    if (libname[0] != '\0') {
        op->modhdl = lt_dlopenext(libname);
        if (op->modhdl == NULL) {
            RDB_raise_resource_not_found(libname, ecp);
            goto error;
        }
    } else {
        op->modhdl = NULL;
    }
    symname = RDB_tuple_get_string(&tpl, "SYMBOL");
    if (strcmp(symname, "_RDB_sys_select") == 0) {
        op->opfn.ro_fp = &_RDB_sys_select;
    } else {
        op->opfn.ro_fp = (RDB_ro_op_func *) lt_dlsym(op->modhdl, symname);
        if (op->opfn.ro_fp == NULL) {
            RDB_raise_resource_not_found(symname, ecp);
            goto error;
        }
    }

    RDB_destroy_obj(&tpl, ecp);

    return op;

error:
    if (op != NULL) {
        RDB_free_op_data(op, ecp);
    }

    RDB_destroy_obj(&tpl, ecp);
    return NULL;
}

/* Read update operator from database */
RDB_operator *
_RDB_cat_get_upd_op(const char *name, int argc, RDB_type *argtv[],
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    RDB_expression *exp, *wexp, *argp;
    RDB_object *vtbp;
    RDB_object tpl;
    RDB_object typesobj;
    int i;
    int ret;
    char *libname, *symname;
    RDB_operator *op = NULL;
    RDB_object *updvobjp, *updobjp;

    RDB_init_obj(&typesobj);
    ret = _RDB_make_typesobj(argc, argtv, ecp, &typesobj);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&typesobj, ecp);
        return NULL;
    }
        
    exp = RDB_ro_op("AND", ecp);
    if (exp == NULL) {
        RDB_destroy_obj(&typesobj, ecp);
        return NULL;
    }
    argp = RDB_eq(RDB_var_ref("NAME", ecp),
            RDB_string_to_expr(name, ecp), ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        RDB_destroy_obj(&typesobj, ecp);
        return NULL;
    }
    RDB_add_arg(exp, argp);
    RDB_obj_set_typeinfo(&typesobj, upd_ops_attrv[1].typ); /* Set array type */
    argp = RDB_eq(RDB_var_ref("ARGTYPES", ecp),
                   RDB_obj_to_expr(&typesobj, ecp), ecp);
    RDB_destroy_obj(&typesobj, ecp);
    if (argp == NULL) {
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    RDB_add_arg(exp, argp);

    wexp = RDB_ro_op("WHERE", ecp);
    if (wexp == NULL) {
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    argp = RDB_table_ref(txp->dbp->dbrootp->upd_ops_tbp, ecp);
    if (argp == NULL) {
        RDB_drop_expr(wexp, ecp);
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    RDB_add_arg(wexp, argp);
    RDB_add_arg(wexp, exp);

    vtbp = RDB_expr_to_vtable(wexp, ecp, txp);
    if (vtbp == NULL) {
        RDB_drop_expr(exp, ecp);
        return NULL;
    }
    RDB_init_obj(&tpl);
    ret = RDB_extract_tuple(vtbp, ecp, txp, &tpl);
    RDB_drop_table(vtbp, ecp, txp);
    if (ret != RDB_OK)
        goto error;

    /* Allocate RDB_operator */
    op = _RDB_new_operator(name, argc, argtv, NULL, ecp);
    if (op == NULL) {
        goto error;
    }
    op->opfn.ro_fp = NULL;

    RDB_init_obj(&op->iarg);

    ret = RDB_copy_obj(&op->iarg, RDB_tuple_get(&tpl, "IARG"), ecp);
    if (ret != RDB_OK)
        goto error;

    updvobjp = RDB_tuple_get(&tpl, "UPDV");
    for (i = 0; i < argc; i++) {
        updobjp = RDB_array_get(updvobjp, (RDB_int) i, ecp);
        if (updobjp == NULL)
            goto error;
        op->paramv[i].update = RDB_obj_bool(updobjp);
    }
        
    libname = RDB_tuple_get_string(&tpl, "LIB");
    op->modhdl = lt_dlopenext(libname);
    if (op->modhdl == NULL) {
        RDB_raise_resource_not_found(libname, ecp);
        goto error;
    }
    symname = RDB_tuple_get_string(&tpl, "SYMBOL");
    op->opfn.upd_fp = (RDB_upd_op_func *) lt_dlsym(op->modhdl, symname);
    if (op->opfn.upd_fp == NULL) {
        RDB_raise_resource_not_found(libname, ecp);
        goto error;
    }

    RDB_destroy_obj(&tpl, ecp);

    return op;

error:
    if (op != NULL) {
        if (op->name != NULL)
            RDB_free(op->name);
        RDB_destroy_obj(&op->iarg, ecp);
        if (op->paramv != NULL) {
            for (i = 0; i < op->paramc; i++) {
                if (op->paramv[i].typ != NULL
                       && !RDB_type_is_scalar(op->paramv[i].typ)) {
                    RDB_drop_type(op->paramv[i].typ, ecp, NULL);
                }
            }
        }
        RDB_free(op);
    }

    RDB_destroy_obj(&tpl, ecp);
    return NULL;
}

int
_RDB_cat_create_constraint(const char *name, RDB_expression *exp,
                      RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_object tpl;
    RDB_object exprval;

    RDB_init_obj(&tpl);
    RDB_init_obj(&exprval);

    /*
     * Insert data into SYS_CONSTRAINTS
     */

    ret = RDB_tuple_set_string(&tpl, "CONSTRAINTNAME", name, ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = _RDB_expr_to_binobj(&exprval, exp, ecp);
    if (ret != RDB_OK)
        goto cleanup;
    ret = RDB_tuple_set(&tpl, "I_EXPR", &exprval, ecp);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_insert(RDB_tx_db(txp)->dbrootp->constraints_tbp, &tpl, ecp, txp);
    if (ret != RDB_OK) {
        if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_KEY_VIOLATION_ERROR) {
            RDB_clear_err(ecp);
            RDB_raise_element_exists("constraint already exists", ecp);
        }
        ret = RDB_ERROR;
        goto cleanup;
    }

    ret = RDB_OK;

cleanup:
    RDB_destroy_obj(&tpl, ecp);
    RDB_destroy_obj(&exprval, ecp);

    return ret;
}

int
RDB_major_version(void)
{
    return MAJOR_VERSION;
}

int
RDB_minor_version(void)
{
    return MINOR_VERSION;
}
