/*
 * Copyright (C) 2003, 2004 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

/* $Id$ */

#include "rdb.h"
#include "typeimpl.h"
#include "catalog.h"
#include "internal.h"
#include <gen/strfns.h>
#include <gen/errors.h>
#include <string.h>

/* name of the file in which the tables are physically stored */
#define RDB_DATAFILE "rdata"

RDB_table *
_RDB_new_table(void)
{
    RDB_table *tbp = malloc(sizeof (RDB_table));
    if (tbp == NULL) {
        return NULL;
    }
    tbp->name = NULL;
    tbp->refcount = 0;
    tbp->optimized = RDB_FALSE;
    tbp->keyv = NULL;
    return tbp;
}

/*
 * Creates a stored table, but not the recmap and the indexes
 * and does not insert the table into the catalog.
 * reltyp is consumed on success (must not be freed by caller).
 */
static int
new_stored_table(const char *name, RDB_bool persistent,
                RDB_type *reltyp,
                int keyc, const RDB_string_vec keyv[], RDB_bool usr,
                RDB_table **tbpp)
{
    int ret;
    int i;
    RDB_table *tbp = _RDB_new_table();

    if (tbp == NULL)
        return RDB_NO_MEMORY;
    *tbpp = tbp;
    tbp->is_user = usr;
    tbp->is_persistent = persistent;

    tbp->kind = RDB_TB_REAL;
    RDB_init_hashmap(&tbp->var.real.attrmap, RDB_DFL_MAP_CAPACITY);
    tbp->var.real.indexc = 0;
    tbp->var.real.est_cardinality = 1000;

    if (name != NULL) {
        tbp->name = RDB_dup_str(name);
        if (tbp->name == NULL) {
            ret = RDB_NO_MEMORY;
            goto error;
        }
    }
    tbp->var.real.recmapp = NULL;

    /* copy candidate keys */
    tbp->keyc = keyc;
    tbp->keyv = malloc(sizeof(RDB_attr) * keyc);
    for (i = 0; i < keyc; i++) {
        tbp->keyv[i].strv = NULL;
    }
    for (i = 0; i < keyc; i++) {
        tbp->keyv[i].strc = keyv[i].strc;
        tbp->keyv[i].strv = RDB_dup_strvec(keyv[i].strc, keyv[i].strv);
        if (tbp->keyv[i].strv == NULL)
            goto error;
    }

    tbp->typ = reltyp;

    return RDB_OK;

error:
    /* clean up */
    if (tbp != NULL) {
        free(tbp->name);
        for (i = 0; i < keyc; i++) {
            if (tbp->keyv[i].strv != NULL) {
                RDB_free_strvec(tbp->keyv[i].strc, tbp->keyv[i].strv);
            }
        }
        free(tbp->keyv);
        RDB_destroy_hashmap(&tbp->var.real.attrmap);
        free(tbp);
    }
    return ret;
}

/*
 * Like _RDB_new_stored_table(), but uses attrc and heading instead of reltype.
 */
int
_RDB_new_stored_table(const char *name, RDB_bool persistent,
                int attrc, const RDB_attr heading[],
                int keyc, const RDB_string_vec keyv[], RDB_bool usr,
                RDB_table **tbpp)
{
    RDB_type *reltyp;
    int i;
    int ret = RDB_create_relation_type(attrc, heading, &reltyp);

    if (ret != RDB_OK) {
        return ret;
    }
    for (i = 0; i < attrc; i++) {
        if (heading[i].defaultp != NULL) {
            RDB_type *tuptyp = reltyp->var.basetyp;

            tuptyp->var.tuple.attrv[i].defaultp = malloc(sizeof (RDB_object));
            if (tuptyp->var.tuple.attrv[i].defaultp == NULL)
                return RDB_NO_MEMORY;
            RDB_init_obj(tuptyp->var.tuple.attrv[i].defaultp);
            RDB_copy_obj(tuptyp->var.tuple.attrv[i].defaultp,
                    heading[i].defaultp);
        }
    }

    ret = new_stored_table(name, persistent, reltyp,
            keyc, keyv, usr, tbpp);
    if (ret != RDB_OK)
        RDB_drop_type(reltyp, NULL);
    return ret;
}

static int
drop_anon_table(RDB_table *tbp)
{
    if (RDB_table_name(tbp) == NULL)
        return RDB_drop_table(tbp, NULL);
    return RDB_OK;
}

void
_RDB_free_table(RDB_table *tbp)
{
    int i;

    if (tbp->keyv != NULL) {
        /* Delete candidate keys */
        for (i = 0; i < tbp->keyc; i++) {
            RDB_free_strvec(tbp->keyv[i].strc, tbp->keyv[i].strv);
        }
        free(tbp->keyv);
    }

    RDB_drop_type(tbp->typ, NULL);
    free(tbp->name);
    free(tbp);
}

int
RDB_table_keys(RDB_table *tbp, RDB_string_vec **keyvp)
{
    int ret;

    if (tbp->keyv == NULL) {
        ret = _RDB_infer_keys(tbp);
        if (ret != RDB_OK)
            return ret;
    }

    if (keyvp != NULL)
        *keyvp = tbp->keyv;
        
    return tbp->keyc;
}

static void
destroy_tbindex(_RDB_tbindex *idxp)
{
    int i;

    free(idxp->name);
    for (i = 0; i < idxp->attrc; i++)
        free(idxp->attrv[i].attrname);
    free(idxp->attrv);
}

int
_RDB_drop_table(RDB_table *tbp, RDB_bool rec)
{
    int i;
    int ret;

    switch (tbp->kind) {
        case RDB_TB_REAL:
        {
            RDB_destroy_hashmap(&tbp->var.real.attrmap);
            if (tbp->var.real.indexc > 0) {
                for (i = 0; i < tbp->var.real.indexc; i++) {
                    destroy_tbindex(&tbp->var.real.indexv[i]);
                }
                free(tbp->var.real.indexv);
            }
            break;
        }
        case RDB_TB_SELECT:
            if (tbp->var.select.objpc > 0)
                free(tbp->var.select.objpv);
            RDB_drop_expr(tbp->var.select.exp);
            if (rec) {
                ret = drop_anon_table(tbp->var.select.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_UNION:
            if (rec) {
                ret = drop_anon_table(tbp->var._union.tb1p);
                if (ret != RDB_OK)
                    return ret;
                ret = drop_anon_table(tbp->var._union.tb2p);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_MINUS:
            if (rec) {
                ret = drop_anon_table(tbp->var.minus.tb1p);
                if (ret != RDB_OK)
                    return ret;
                ret = drop_anon_table(tbp->var.minus.tb2p);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_INTERSECT:
            if (rec) {
                ret = drop_anon_table(tbp->var.intersect.tb1p);
                if (ret != RDB_OK)
                    return ret;
                ret = drop_anon_table(tbp->var.intersect.tb2p);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_JOIN:
            if (rec) {
                ret = drop_anon_table(tbp->var.join.tb1p);
                if (ret != RDB_OK)
                    return ret;
                ret = drop_anon_table(tbp->var.join.tb2p);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_EXTEND:
            if (rec) {
                ret = drop_anon_table(tbp->var.extend.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            for (i = 0; i < tbp->var.extend.attrc; i++) {
                free(tbp->var.extend.attrv[i].name);
                RDB_drop_expr(tbp->var.extend.attrv[i].exp);
            }
            break;
        case RDB_TB_PROJECT:
            if (rec) {
                ret = drop_anon_table(tbp->var.project.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_RENAME:
            if (rec) {
                ret = drop_anon_table(tbp->var.rename.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            for (i = 0; i < tbp->var.rename.renc; i++) {
                free(tbp->var.rename.renv[i].from);
                free(tbp->var.rename.renv[i].to);
            }
            break;
        case RDB_TB_SUMMARIZE:
            if (rec) {
                ret = drop_anon_table(tbp->var.summarize.tb1p);
                if (ret != RDB_OK)
                    return ret;
                ret = drop_anon_table(tbp->var.summarize.tb2p);
                if (ret != RDB_OK)
                    return ret;
            }
            for (i = 0; i < tbp->var.summarize.addc; i++) {
                if (tbp->var.summarize.addv[i].op != RDB_COUNT
                        && tbp->var.summarize.addv[i].op != RDB_COUNTD)
                    RDB_drop_expr(tbp->var.summarize.addv[i].exp);
                free(tbp->var.summarize.addv[i].name);
            }
            break;
        case RDB_TB_WRAP:
            if (rec) {
                ret = drop_anon_table(tbp->var.wrap.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            for (i = 0; i < tbp->var.wrap.wrapc; i++) {
                free(tbp->var.wrap.wrapv[i].attrname);
                RDB_free_strvec(tbp->var.wrap.wrapv[i].attrc,
                        tbp->var.wrap.wrapv[i].attrv);
            }
            break;
        case RDB_TB_UNWRAP:
            if (rec) {
                ret = drop_anon_table(tbp->var.unwrap.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            RDB_free_strvec(tbp->var.unwrap.attrc, tbp->var.unwrap.attrv);
            break;
        case RDB_TB_SDIVIDE:
            if (rec) {
                ret = drop_anon_table(tbp->var.sdivide.tb1p);
                if (ret != RDB_OK)
                    return ret;
                ret = drop_anon_table(tbp->var.sdivide.tb2p);
                if (ret != RDB_OK)
                    return ret;
                ret = drop_anon_table(tbp->var.sdivide.tb3p);
                if (ret != RDB_OK)
                    return ret;
            }
            break;
        case RDB_TB_GROUP:
            if (rec) {
                ret = drop_anon_table(tbp->var.group.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            for (i = 0; i < tbp->var.group.attrc; i++) {
                free(tbp->var.group.attrv[i]);
            }
            free(tbp->var.group.gattr);
            break;
        case RDB_TB_UNGROUP:
            if (rec) {
                ret = drop_anon_table(tbp->var.ungroup.tbp);
                if (ret != RDB_OK)
                    return ret;
            }
            free(tbp->var.ungroup.attr);
            break;
    }
    _RDB_free_table(tbp);
    return RDB_OK;
}

static int
compare_field(const void *data1p, size_t len1,
              const void *data2p, size_t len2, void *arg)
{
    RDB_object val1, val2;
    int res;
    RDB_type *typ = (RDB_type *)arg;

    RDB_init_obj(&val1);
    RDB_init_obj(&val2);

    RDB_irep_to_obj(&val1, typ, data1p, len1);
    RDB_irep_to_obj(&val2, typ, data2p, len2);

    res = (*typ->comparep)(&val1, &val2);

    RDB_destroy_obj(&val1);
    RDB_destroy_obj(&val2);

    return res;
}

static int
create_index(RDB_table *tbp, RDB_environment *envp, RDB_transaction *txp,
             _RDB_tbindex *indexp, int flags)
{
    int ret;
    int i;
    RDB_compare_field *cmpv = 0;
    int *fieldv = malloc(sizeof(int *) * indexp->attrc);

    if (fieldv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }

    if (RDB_ORDERED & flags) {
        cmpv = malloc(sizeof (RDB_compare_field) * indexp->attrc);
        if (cmpv == NULL) {
            ret = RDB_NO_MEMORY;
            goto cleanup;
        }
        for (i = 0; i < indexp->attrc; i++) {
            RDB_type *attrtyp = RDB_type_attr_type(tbp->typ,
                    indexp->attrv[i].attrname);

            if (attrtyp->comparep != NULL) {
                cmpv[i].comparep = &compare_field;
                cmpv[i].arg = attrtyp;
            } else {
                cmpv[i].comparep = NULL;
            }
            cmpv[i].asc = indexp->attrv[i].asc;
        }
    }

    /* Get index numbers */
    for (i = 0; i < indexp->attrc; i++) {
        void *np = RDB_hashmap_get(&tbp->var.real.attrmap,
                indexp->attrv[i].attrname, NULL);
        if (np == NULL)
            return RDB_ATTRIBUTE_NOT_FOUND;
        fieldv[i] = *(int *) np;
    }

    /* Create record-layer index */
    ret = RDB_create_index(tbp->var.real.recmapp,
                  tbp->is_persistent ? indexp->name : NULL,
                  tbp->is_persistent ? RDB_DATAFILE : NULL,
                  envp, indexp->attrc, fieldv, cmpv, flags,
                  txp != NULL ? txp->txid : NULL, &indexp->idxp);

cleanup:
    free(fieldv);
    free(cmpv);
    return ret;
}

/*
 * Create secondary indexes
 */
static int
create_key_indexes(RDB_table *tbp, RDB_environment *envp, RDB_transaction *txp)
{
    int i, j;
    int ret;

    tbp->var.real.indexc = tbp->keyc;
    tbp->var.real.indexv = malloc(sizeof (_RDB_tbindex)
            * tbp->var.real.indexc);
    if (tbp->var.real.indexv == NULL)
        return RDB_NO_MEMORY;

    for (i = 0; i < tbp->var.real.indexc; i++) {
        tbp->var.real.indexv[i].attrc = tbp->keyv[i].strc;
        tbp->var.real.indexv[i].attrv = malloc(sizeof(RDB_seq_item)
                * tbp->keyv[i].strc);
        if (tbp->var.real.indexv[i].attrv == NULL)
            return RDB_NO_MEMORY;
        for (j = 0; j < tbp->keyv[i].strc; j++) {
            tbp->var.real.indexv[i].attrv[j].attrname =
                    RDB_dup_str(tbp->keyv[i].strv[j]);
            if (tbp->var.real.indexv[i].attrv[j].attrname == NULL)
                return RDB_NO_MEMORY;
        }

        if (tbp->is_persistent) {
            tbp->var.real.indexv[i].name =
                    malloc(strlen(RDB_recmap_name(tbp->var.real.recmapp)) + 4);
            if (tbp->var.real.indexv[i].name == NULL) {
                return RDB_NO_MEMORY;
            }
            /* build index name */            
            sprintf(tbp->var.real.indexv[i].name, "%s$%d",
                    RDB_recmap_name(tbp->var.real.recmapp), i);
        } else {
            tbp->var.real.indexv[i].name = NULL;
        }

        tbp->var.real.indexv[i].unique = RDB_TRUE;
        tbp->var.real.indexv[i].idxp = NULL;
    }

    /*
     * Create secondary indexes
     */

    /* If it's not a system table, create index in catalog */
    if (tbp->is_user && tbp->is_persistent) {
        for (i = 0; i < tbp->var.real.indexc; i++) {
            ret = _RDB_cat_insert_index(&tbp->var.real.indexv[i], tbp->name,
                    txp);
            if (ret != RDB_OK) {
                if (ret == RDB_KEY_VIOLATION)
                    ret = RDB_ELEMENT_EXISTS;
                return ret;
            }
        }
    }

    /* Create a BDB secondary index for the indexes except the first */
    for (i = 1; i < tbp->var.real.indexc; i++) {
        ret = create_index(tbp, envp, txp, &tbp->var.real.indexv[i], RDB_UNIQUE);
        if (ret != RDB_OK)
            return ret;
    }
    return RDB_OK;
}

/*
 * Return the length (in bytes) of the internal representation
 * of the type pointed to by typ.
 */
static int replen(const RDB_type *typ) {
    switch(typ->kind) {
        case RDB_TP_TUPLE:
        {
            int i;
            size_t len;
            size_t tlen = 0;

            /*
             * Add lengths of attribute types. If one of the attributes is
             * of variable length, the tuple type is of variable length.
             */
            for (i = 0; i < typ->var.tuple.attrc; i++) {
                len = replen(typ->var.tuple.attrv[i].typ);
                if (len == RDB_VARIABLE_LEN)
                    return RDB_VARIABLE_LEN;
                tlen += len;
            }
            return tlen;
        }
        case RDB_TP_RELATION:
        case RDB_TP_ARRAY:
        case RDB_TP_SCALAR:
            return typ->ireplen;
    }
    abort();
}

int
key_fnos(RDB_table *tbp, int **flenvp, const RDB_bool ascv[],
         RDB_compare_field *cmpv)
{
    int ret;
    int i, di;
    int attrc = tbp->typ->var.basetyp->var.tuple.attrc;
    RDB_attr *heading = tbp->typ->var.basetyp->var.tuple.attrv;
    int piattrc = tbp->keyv[0].strc;
    char **piattrv = tbp->keyv[0].strv;

    *flenvp = malloc(sizeof(int) * attrc);
    if (*flenvp == NULL) {
        return RDB_NO_MEMORY;
    }

    di = piattrc;
    for (i = 0; i < attrc; i++) {
        RDB_int fno;

        if (piattrc == attrc) {
            fno = i;
        } else {            
            /* Search attribute in key */
            fno = (RDB_int) RDB_find_str(piattrc, piattrv, heading[i].name);
        }

        /* If it's not found in the key, give it a non-key field number */
        if (fno == -1)
            fno = di++;
        else if (ascv != NULL) {
            /* Set comparison field */
            if (heading[i].typ->comparep != NULL) {
                cmpv[fno].comparep = &compare_field;
                cmpv[fno].arg = heading[i].typ;
            } else {
                cmpv[fno].comparep = NULL;
            }
            cmpv[fno].asc = ascv[fno];
        }

        /* Put the field number into the attrmap */
        ret = RDB_hashmap_put(&tbp->var.real.attrmap,
                tbp->typ->var.basetyp->var.tuple.attrv[i].name,
                &fno, sizeof fno);
        if (ret != RDB_OK) {
            free(*flenvp);
            return ret;
        }

        (*flenvp)[fno] = replen(heading[i].typ);
    }
    return RDB_OK;
}

/*
 * Create the physical representation of a table.
 * (The recmap and the indexes)
 *
 * Arguments:
 * tbp        the table
 * envp       the database environment
 * txp        the transaction under which the operation is performed
 * ascv       the sorting order if it's a sorter, or NULL
 */
int
_RDB_create_table_storage(RDB_table *tbp, RDB_environment *envp,
        const RDB_bool ascv[], RDB_transaction *txp)
{
    int ret;
    int *flenv;
    int flags;
    char *rmname = NULL;
    RDB_compare_field *cmpv = NULL;
    int attrc = tbp->typ->var.basetyp->var.tuple.attrc;
    int piattrc = tbp->keyv[0].strc;

    if (!tbp->is_persistent)
       txp = NULL;

    if (txp != NULL && !RDB_tx_is_running(txp))
        return RDB_INVALID_TRANSACTION;

    /* Allocate comparison vector, if needed */
    if (ascv != NULL) {
        cmpv = malloc(sizeof (RDB_compare_field) * piattrc);
        if (cmpv == NULL) {
            ret = RDB_NO_MEMORY;
            goto error;
        }
    }

    ret = key_fnos(tbp, &flenv, ascv, cmpv);
    if (ret != RDB_OK)
        return ret;

    /*
     * If the table is a persistent user table, insert recmap int SYS_TABLE_RECMAP
     */
    if (tbp->is_persistent && tbp->is_user) {
        ret = _RDB_cat_insert_table_recmap(tbp, tbp->name, txp);
        if (ret == RDB_KEY_VIOLATION) {
            /* Choose a different recmap name */
            int n = 0;
            rmname = malloc(strlen(tbp->name) + 4);
            if (rmname == NULL) {
                ret = RDB_NO_MEMORY;
                goto error;
            }
            do {
                sprintf(rmname, "%s%d", tbp->name, ++n);
                ret = _RDB_cat_insert_table_recmap(tbp, rmname, txp);
            } while (ret == RDB_KEY_VIOLATION && n <= 999);
        }
        if (ret != RDB_OK)
            goto error;
    }

    /*
     * Use a sorted recmap for local tables, so the order of the tuples
     * is always the same if the table is stored as an attribute in a table.
     */
    flags = 0;
    if (ascv != NULL || !tbp->is_persistent)
        flags |= RDB_ORDERED;
    if (ascv == NULL)
        flags |= RDB_UNIQUE;

    ret = RDB_create_recmap(tbp->is_persistent ?
            (rmname == NULL ? tbp->name : rmname) : NULL,
            tbp->is_persistent ? RDB_DATAFILE : NULL,
            envp, attrc, flenv, piattrc, cmpv, flags,
            txp != NULL ? txp->txid : NULL,
            &tbp->var.real.recmapp);
    if (ret != RDB_OK)
        goto error;

    /* Open/create indexes if there is more than one key */
    ret = create_key_indexes(tbp, envp, txp);
    if (ret != RDB_OK)
        goto error;

    free(flenv);
    free(cmpv);
    free(rmname);
    return RDB_OK;

error:
    /* clean up */
    free(flenv);
    free(cmpv);
    free(rmname);
    if (tbp != NULL) {
        RDB_destroy_hashmap(&tbp->var.real.attrmap);
    }
    if (RDB_is_syserr(ret) && txp != NULL)
        RDB_rollback_all(txp);
    return ret;
}

/*
 * Open the physical representation of a table.
 * (The recmap and the indexes)
 *
 * Arguments:
 * tbp        the table
 * envp       the database environment
 * txp        the transaction under which the operation is performed
 */
int
_RDB_open_table_storage(RDB_table *tbp, RDB_environment *envp,
        const char *rmname, RDB_transaction *txp)
{
    int ret;
    int *flenv;
    RDB_compare_field *cmpv = NULL;
    int attrc = tbp->typ->var.basetyp->var.tuple.attrc;
    int piattrc = tbp->keyv[0].strc;

    if (!tbp->is_persistent)
       txp = NULL;

    if (txp != NULL && !RDB_tx_is_running(txp))
        return RDB_INVALID_TRANSACTION;

    ret = key_fnos(tbp, &flenv, NULL, NULL);
    if (ret != RDB_OK)
        return ret;

    ret = RDB_open_recmap(rmname, RDB_DATAFILE, envp,
            attrc, flenv, piattrc, txp != NULL ? txp->txid : NULL,
            &tbp->var.real.recmapp);
    if (ret != RDB_OK)
        goto error;

    /* Flag that the indexes have not been opened */
    tbp->var.real.indexc = -1;

    free(flenv);
    free(cmpv);
    return RDB_OK;

error:
    /* clean up */
    free(flenv);
    free(cmpv);
    if (tbp != NULL) {
        RDB_destroy_hashmap(&tbp->var.real.attrmap);
    }
    if (RDB_is_syserr(ret) && txp != NULL)
        RDB_rollback_all(txp);
    return ret;
}

int
_RDB_open_table_index(RDB_table *tbp, _RDB_tbindex *indexp,
        RDB_environment *envp, RDB_transaction *txp)
{
    int ret;
    int i;
    int *fieldv = malloc(sizeof(int *) * indexp->attrc);

    if (fieldv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }

    /* get index numbers */
    for (i = 0; i < indexp->attrc; i++) {
        fieldv[i] = *(int *) RDB_hashmap_get(&tbp->var.real.attrmap,
                        indexp->attrv[i].attrname, NULL);
    }

    /* open index */
    ret = RDB_open_index(tbp->var.real.recmapp,
                  tbp->is_persistent ? indexp->name : NULL,
                  tbp->is_persistent ? RDB_DATAFILE : NULL,
                  envp, indexp->attrc, fieldv, indexp->unique ? RDB_UNIQUE : 0,
                  txp != NULL ? txp->txid : NULL, &indexp->idxp);

cleanup:
    free(fieldv);
    return ret;
}

int
RDB_create_table_index(const char *name, RDB_table *tbp, int idxcompc,
        const RDB_seq_item idxcompv[], int flags, RDB_transaction *txp)
{
    int i;
    int ret;
    _RDB_tbindex *indexp;
    RDB_transaction tx;

    if (!_RDB_legal_name(name))
        return RDB_INVALID_ARGUMENT;

    tbp->var.real.indexv = realloc(tbp->var.real.indexv,
            (tbp->var.real.indexc + 1) * sizeof (_RDB_tbindex));
    if (tbp->var.real.indexv == NULL) {
        RDB_rollback_all(txp);
        return RDB_NO_MEMORY;
    }

    indexp = &tbp->var.real.indexv[tbp->var.real.indexc++];

    indexp->name = RDB_dup_str(name);
    if (indexp->name == NULL) {
        RDB_rollback_all(txp);
        return RDB_NO_MEMORY;
    }

    indexp->attrc = idxcompc;
    indexp->attrv = malloc(sizeof (RDB_seq_item) * idxcompc);
    if (indexp->attrv == NULL) {
        free(indexp->name);
        RDB_rollback_all(txp);
        return RDB_NO_MEMORY;
    }

    for (i = 0; i < idxcompc; i++) {
        indexp->attrv[i].attrname = RDB_dup_str(idxcompv[i].attrname);
        if (indexp->attrv[i].attrname == NULL) {
            RDB_rollback_all(txp);
            return RDB_NO_MEMORY;
        }
        indexp->attrv[i].asc = idxcompv[i].asc;
    }
    indexp->unique = (RDB_bool) (RDB_UNIQUE & flags);

    ret = RDB_begin_tx(&tx, RDB_tx_db(txp), txp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret))
            RDB_rollback_all(txp);
        return ret;
    }

    if (tbp->is_persistent) {
        /* Insert index into catalog */
        ret = _RDB_cat_insert_index(indexp, tbp->name, &tx);
        if (ret != RDB_OK)
            goto error;
    }

    /* Create index */
    ret = create_index(tbp, RDB_db_env(RDB_tx_db(txp)), &tx, indexp, flags);
    if (ret != RDB_OK) {
        goto error;
    }

    return RDB_commit(&tx);

error:
    if (RDB_is_syserr(ret))
        RDB_rollback_all(&tx);
    else
        RDB_rollback(&tx);
    tbp->var.real.indexv = realloc(tbp->var.real.indexv,
            (--tbp->var.real.indexc) * sizeof (_RDB_tbindex));
    return ret;
}

int
RDB_drop_table_index(const char *name, RDB_transaction *txp)
{
    int ret;
    int i;
    int xi;
    char *tbname;
    RDB_table *tbp;

    if (!_RDB_legal_name(name))
        return RDB_NOT_FOUND;

    ret = _RDB_cat_index_tablename(name, &tbname, txp);
    if (ret != RDB_OK)
        return ret;

    ret = RDB_get_table(tbname, txp, &tbp);
    if (ret != RDB_OK)
        return ret;

    for (i = 0; i < tbp->var.real.indexc
            && strcmp(tbp->var.real.indexv[i].name, name) != 0;
            i++);
    if (i >= tbp->var.real.indexc) {
        /* Index not found, so reread indexes */
        for (i = 0; i < tbp->var.real.indexc; i++)
            destroy_tbindex(&tbp->var.real.indexv[i]);
        ret = _RDB_cat_get_indexes(tbp, txp->dbp->dbrootp, txp);
        if (ret != RDB_OK)
            return ret;

        /* Search again */
        for (i = 0; i < tbp->var.real.indexc
                && strcmp(tbp->var.real.indexv[i].name, name) != 0;
                i++);
        if (i >= tbp->var.real.indexc)
            return RDB_INTERNAL;
    }        
    xi = i;

    /* Destroy index */
    ret = _RDB_del_index(txp, tbp->var.real.indexv[i].idxp);
    if (ret != RDB_OK)
        return ret;

    /* Delete index from catalog */
    ret = _RDB_cat_delete_index(name, txp);
    if (ret != RDB_OK)
        return ret;

    /*
     * Delete index entry
     */

    destroy_tbindex(&tbp->var.real.indexv[xi]);

    tbp->var.real.indexc--;
    for (i = xi; i < tbp->var.real.indexc; i++) {
        tbp->var.real.indexv[i] = tbp->var.real.indexv[i + 1];
    }

    realloc(tbp->var.real.indexv,
            sizeof(_RDB_tbindex) * tbp->var.real.indexc);

    return RDB_OK;
}

RDB_type *
RDB_table_type(const RDB_table *tbp)
{
    return tbp->typ;
}

int
_RDB_move_tuples(RDB_table *dstp, RDB_table *srcp, RDB_transaction *txp)
{
    RDB_qresult *qrp = NULL;
    RDB_object tpl;
    int ret;

    /*
     * Copy all tuples from source table to destination table
     */
    ret = _RDB_table_qresult(srcp, txp, &qrp);
    if (ret != RDB_OK)
        return ret;

    RDB_init_obj(&tpl);

    while ((ret = _RDB_next_tuple(qrp, &tpl, txp)) == RDB_OK) {
        if (dstp->kind == RDB_TB_REAL && !dstp->is_persistent)
            ret = RDB_insert(dstp, &tpl, NULL);
        else
            ret = RDB_insert(dstp, &tpl, txp);
        if (ret != RDB_OK) {
            goto cleanup;
        }
    }
    if (ret == RDB_NOT_FOUND)
        ret = RDB_OK;
cleanup:
    _RDB_drop_qresult(qrp, txp);
    RDB_destroy_obj(&tpl);
    return ret;
}

int
RDB_copy_table(RDB_table *dstp, RDB_table *srcp, RDB_transaction *txp)
{
    RDB_transaction tx;
    int ret;

    if (!RDB_tx_is_running(txp))
        return RDB_INVALID_TRANSACTION;

    /* check if types of the two tables match */
    if (!RDB_type_equals(dstp->typ, srcp->typ))
        return RDB_TYPE_MISMATCH;

    /* start subtransaction */
    ret = RDB_begin_tx(&tx, txp->dbp, txp);
    if (ret != RDB_OK)
        return ret;

    /* Delete all tuples from destination table */
    ret = RDB_delete(dstp, NULL, &tx);
    if (ret != RDB_OK)
        goto error;

    ret = _RDB_move_tuples(dstp, srcp, &tx);
    if (ret != RDB_OK)
        goto error;

    return RDB_commit(&tx);

error:
    RDB_rollback(&tx);
    return ret;
}

int
RDB_all(RDB_table *tbp, const char *attrname, RDB_transaction *txp,
        RDB_bool *resultp)
{
    RDB_type *attrtyp;
    RDB_object arr;
    RDB_object *tplp;
    int ret;
    int i;

    /* attrname may only be NULL if table is unary */
    if (attrname == NULL) {
        if (tbp->typ->var.basetyp->var.tuple.attrc != 1)
            return RDB_INVALID_ARGUMENT;
        attrname = tbp->typ->var.basetyp->var.tuple.attrv[0].name;
    }

    attrtyp = _RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname)->typ;
    if (attrtyp == NULL)
        return RDB_ATTRIBUTE_NOT_FOUND;

    /* initialize result */
    *resultp = RDB_TRUE;

    /*
     * Perform aggregation
     */

    RDB_init_obj(&arr);

    ret = RDB_table_to_array(&arr, tbp, 0, NULL, txp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        RDB_destroy_obj(&arr);
        return ret;
    }

    i = 0;
    while ((ret = RDB_array_get(&arr, (RDB_int) i++, &tplp)) == RDB_OK) {
        if (!RDB_tuple_get_bool(tplp, attrname))
            *resultp = RDB_FALSE;
    }

    if (ret != RDB_NOT_FOUND) {
        RDB_destroy_obj(&arr);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    return RDB_destroy_obj(&arr);
}

int
RDB_any(RDB_table *tbp, const char *attrname, RDB_transaction *txp,
        RDB_bool *resultp)
{
    RDB_type *attrtyp;
    RDB_object arr;
    RDB_object *tplp;
    int ret;
    int i;

    /* attrname may only be NULL if table is unary */
    if (attrname == NULL) {
        if (tbp->typ->var.basetyp->var.tuple.attrc != 1)
            return RDB_INVALID_ARGUMENT;
        attrname = tbp->typ->var.basetyp->var.tuple.attrv[0].name;
    }

    attrtyp = _RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname)->typ;
    if (attrtyp == NULL)
        return RDB_INVALID_ARGUMENT;

    /* initialize result */
    *resultp = RDB_FALSE;

    /*
     * Perform aggregation
     */

    RDB_init_obj(&arr);

    ret = RDB_table_to_array(&arr, tbp, 0, NULL, txp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        RDB_destroy_obj(&arr);
        return ret;
    }

    i = 0;
    while ((ret = RDB_array_get(&arr, (RDB_int) i++, &tplp)) == RDB_OK) {
        if (RDB_tuple_get_bool(tplp, attrname))
            *resultp = RDB_TRUE;
    }

    if (ret != RDB_NOT_FOUND) {
        RDB_destroy_obj(&arr);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    return RDB_destroy_obj(&arr);
}

int
RDB_max(RDB_table *tbp, const char *attrname, RDB_transaction *txp,
        RDB_object *resultp)
{
    RDB_type *attrtyp;
    RDB_object arr;
    RDB_object *tplp;
    int ret;
    int i;

    /* attrname may only be NULL if table is unary */
    if (attrname == NULL) {
        if (tbp->typ->var.basetyp->var.tuple.attrc != 1)
            return RDB_INVALID_ARGUMENT;
        attrname = tbp->typ->var.basetyp->var.tuple.attrv[0].name;
    }

    attrtyp = _RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname)->typ;
    if (attrtyp == NULL)
        return RDB_INVALID_ARGUMENT;

    _RDB_set_obj_type(resultp, attrtyp);

    if (attrtyp == &RDB_INTEGER)
        resultp->var.int_val = RDB_INT_MIN;
    else if (attrtyp == &RDB_RATIONAL)
        resultp->var.rational_val = RDB_RATIONAL_MIN;
    else
        return RDB_TYPE_MISMATCH;

    /*
     * Perform aggregation
     */

    RDB_init_obj(&arr);

    ret = RDB_table_to_array(&arr, tbp, 0, NULL, txp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        RDB_destroy_obj(&arr);
        return ret;
    }

    i = 0;
    while ((ret = RDB_array_get(&arr, (RDB_int) i++, &tplp)) == RDB_OK) {
        if (attrtyp == &RDB_INTEGER) {
            RDB_int val = RDB_tuple_get_int(tplp, attrname);
             
            if (val > resultp->var.int_val)
                 resultp->var.int_val = val;
        } else {
            RDB_rational val = RDB_tuple_get_rational(tplp, attrname);
             
            if (val > resultp->var.rational_val)
                resultp->var.rational_val = val;
        }
    }

    if (ret != RDB_NOT_FOUND) {
        RDB_destroy_obj(&arr);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    return RDB_destroy_obj(&arr);
}

int
RDB_min(RDB_table *tbp, const char *attrname, RDB_transaction *txp,
        RDB_object *resultp)
{
    RDB_type *attrtyp;
    RDB_object arr;
    RDB_object *tplp;
    int ret;
    int i;

    /* attrname may only be NULL if table is unary */
    if (attrname == NULL) {
        if (tbp->typ->var.basetyp->var.tuple.attrc != 1)
            return RDB_INVALID_ARGUMENT;
        attrname = tbp->typ->var.basetyp->var.tuple.attrv[0].name;
    }

    attrtyp = _RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname)->typ;
    if (attrtyp == NULL)
        return RDB_INVALID_ARGUMENT;

    _RDB_set_obj_type(resultp, attrtyp);

    if (attrtyp == &RDB_INTEGER)
        resultp->var.int_val = RDB_INT_MIN;
    else if (attrtyp == &RDB_RATIONAL)
        resultp->var.rational_val = RDB_RATIONAL_MIN;
    else
        return RDB_TYPE_MISMATCH;

    /*
     * Perform aggregation
     */

    RDB_init_obj(&arr);

    ret = RDB_table_to_array(&arr, tbp, 0, NULL, txp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        RDB_destroy_obj(&arr);
        return ret;
    }

    i = 0;
    while ((ret = RDB_array_get(&arr, (RDB_int) i++, &tplp)) == RDB_OK) {
        if (attrtyp == &RDB_INTEGER) {
            RDB_int val = RDB_tuple_get_int(tplp, attrname);
             
            if (val < resultp->var.int_val)
                 resultp->var.int_val = val;
        } else {
            RDB_rational val = RDB_tuple_get_rational(tplp, attrname);
             
            if (val < resultp->var.rational_val)
                resultp->var.rational_val = val;
        }
    }

    if (ret != RDB_NOT_FOUND) {
        RDB_destroy_obj(&arr);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    return RDB_destroy_obj(&arr);
}

int
RDB_sum(RDB_table *tbp, const char *attrname, RDB_transaction *txp,
        RDB_object *resultp)
{
    RDB_type *attrtyp;
    RDB_object arr;
    RDB_object *tplp;
    int ret;
    int i;

    if (attrname == NULL) {
        if (tbp->typ->var.basetyp->var.tuple.attrc != 1)
            return RDB_INVALID_ARGUMENT;
        attrname = tbp->typ->var.basetyp->var.tuple.attrv[0].name;
    }

    attrtyp = _RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname)->typ;
    if (attrtyp == NULL)
        return RDB_INVALID_ARGUMENT;

    _RDB_set_obj_type(resultp, attrtyp);

    /* initialize result */
    if (attrtyp == &RDB_INTEGER)
        resultp->var.int_val = 0;
    else if (attrtyp == &RDB_RATIONAL)
        resultp->var.rational_val = 0.0;
    else
       return RDB_TYPE_MISMATCH;

    /*
     * Perform aggregation
     */

    RDB_init_obj(&arr);

    ret = RDB_table_to_array(&arr, tbp, 0, NULL, txp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        RDB_destroy_obj(&arr);
        return ret;
    }

    i = 0;
    while ((ret = RDB_array_get(&arr, (RDB_int) i++, &tplp)) == RDB_OK) {
        if (attrtyp == &RDB_INTEGER)
            resultp->var.int_val += RDB_tuple_get_int(tplp, attrname);
        else
            resultp->var.rational_val
                            += RDB_tuple_get_rational(tplp, attrname);
    }

    if (ret != RDB_NOT_FOUND) {
        RDB_destroy_obj(&arr);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    return RDB_destroy_obj(&arr);
}

int
RDB_avg(RDB_table *tbp, const char *attrname, RDB_transaction *txp,
        RDB_rational *resultp)
{
    RDB_type *attrtyp;
    RDB_object arr;
    RDB_object *tplp;
    int ret;
    int i;
    unsigned long count;

    /* attrname may only be NULL if table is unary */
    if (attrname == NULL) {
        if (tbp->typ->var.basetyp->var.tuple.attrc != 1)
            return RDB_INVALID_ARGUMENT;
        attrname = tbp->typ->var.basetyp->var.tuple.attrv[0].name;
    }

    attrtyp = _RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname)->typ;
    if (attrtyp == NULL)
        return RDB_INVALID_ARGUMENT;

    if (!RDB_type_is_numeric(attrtyp))
        return RDB_TYPE_MISMATCH;
    count = 0;

    /*
     * Perform aggregation
     */

    RDB_init_obj(&arr);

    ret = RDB_table_to_array(&arr, tbp, 0, NULL, txp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        RDB_destroy_obj(&arr);
        return ret;
    }

    i = 0;
    while ((ret = RDB_array_get(&arr, (RDB_int) i++, &tplp)) == RDB_OK) {
        count++;
        if (attrtyp == &RDB_INTEGER)
            *resultp += RDB_tuple_get_int(tplp, attrname);
        else
            *resultp += RDB_tuple_get_rational(tplp, attrname);
    }
    if (ret != RDB_NOT_FOUND) {
        RDB_destroy_obj(&arr);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    if (count == 0)
        return RDB_AGGREGATE_UNDEFINED;
    *resultp /= count;

    return RDB_destroy_obj(&arr);
}

int
RDB_extract_tuple(RDB_table *tbp, RDB_transaction *txp, RDB_object *tplp)
{
    int ret, ret2;
    RDB_qresult *qrp;
    RDB_object tpl;
    RDB_table *ntbp;

    ret = _RDB_optimize(tbp, 0, NULL, txp, &ntbp);
    if (ret != RDB_OK)
        return ret;

    ret = _RDB_table_qresult(ntbp, txp, &qrp);
    if (ret != RDB_OK) {
        if (ntbp->kind != RDB_TB_REAL)
            RDB_drop_table(ntbp, txp);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    RDB_init_obj(&tpl);

    /* Get tuple */
    ret = _RDB_next_tuple(qrp, tplp, txp);
    if (ret != RDB_OK)
        goto cleanup;

    /* Check if there are more tuples */
    ret = _RDB_next_tuple(qrp, &tpl, txp);
    if (ret != RDB_NOT_FOUND) {
        if (ret == RDB_OK)
            ret = RDB_INVALID_ARGUMENT;
        goto cleanup;
    }

    ret = RDB_OK;

cleanup:
    RDB_destroy_obj(&tpl);

    ret2 = _RDB_drop_qresult(qrp, txp);
    if (ret == RDB_OK)
        ret = ret2;
    if (ntbp->kind != RDB_TB_REAL) {
        ret2 = RDB_drop_table(ntbp, txp);
        if (ret2 != RDB_OK)
            ret = ret2;
    }
    if (RDB_is_syserr(ret) || RDB_is_syserr(ret2)) {
        RDB_rollback_all(txp);
    }
    return ret;
}

int
RDB_table_is_empty(RDB_table *tbp, RDB_transaction *txp, RDB_bool *resultp)
{
    int ret;
    RDB_qresult *qrp;
    RDB_object tpl;

    if (txp != NULL && !RDB_tx_is_running(txp))
        return RDB_INVALID_TRANSACTION;

    ret = _RDB_table_qresult(tbp, txp, &qrp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    RDB_init_obj(&tpl);

    ret = _RDB_next_tuple(qrp, &tpl, txp);
    if (ret == RDB_OK)
        *resultp = RDB_FALSE;
    else if (ret == RDB_NOT_FOUND)
        *resultp = RDB_TRUE;
    else {
         RDB_destroy_obj(&tpl);
        _RDB_drop_qresult(qrp, txp);
        if (RDB_is_syserr(ret))
            RDB_rollback_all(txp);
        return ret;
    }
    RDB_destroy_obj(&tpl);
    return _RDB_drop_qresult(qrp, txp);
}

int
RDB_cardinality(RDB_table *tbp, RDB_transaction *txp)
{
    int ret;
    int count;
    RDB_qresult *qrp;
    RDB_object tpl;
    RDB_table *ntbp;

    if (txp != NULL && !RDB_tx_is_running(txp))
        return RDB_INVALID_TRANSACTION;

    ret = _RDB_optimize(tbp, 0, NULL, txp, &ntbp);
    if (ret != RDB_OK)
        return ret;

    ret = _RDB_table_qresult(ntbp, txp, &qrp);
    if (ret != RDB_OK) {
        if (ntbp->kind != RDB_TB_REAL)
            RDB_drop_table(ntbp, txp);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    /* Duplicates must be removed */
    ret = _RDB_duprem(qrp);
    if (ret != RDB_OK) {
        if (ntbp->kind != RDB_TB_REAL)
            RDB_drop_table(ntbp, txp);
        _RDB_drop_qresult(qrp, txp);
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    RDB_init_obj(&tpl);

    count = 0;
    while ((ret = _RDB_next_tuple(qrp, &tpl, txp)) == RDB_OK) {
        count++;
    }
    RDB_destroy_obj(&tpl);
    if (ret != RDB_NOT_FOUND) {
        _RDB_drop_qresult(qrp, txp);
        goto error;
    }

    ret = _RDB_drop_qresult(qrp, txp);
    if (ret != RDB_OK)
        goto error;

    if (ntbp->kind != RDB_TB_REAL) {
        ret = RDB_drop_table(ntbp, txp);
        if (ret != RDB_OK)
            return ret;
    }

    if (tbp->kind == RDB_TB_REAL)
        tbp->var.real.est_cardinality = count;

    return count;

error:
    if (ntbp->kind != RDB_TB_REAL)
        RDB_drop_table(ntbp, txp);
    if (RDB_is_syserr(ret))
        RDB_rollback_all(txp);
    return ret;
}

int
RDB_subset(RDB_table *tb1p, RDB_table *tb2p, RDB_transaction *txp,
           RDB_bool *resultp)
{
    RDB_qresult *qrp;
    RDB_object tpl;
    int ret;

    if (!RDB_type_equals(tb1p->typ, tb2p->typ))
        return RDB_TYPE_MISMATCH;

    if (!RDB_tx_is_running(txp))
        return RDB_INVALID_TRANSACTION;

    ret = _RDB_table_qresult(tb1p, txp, &qrp);
    if (ret != RDB_OK) {
        if (RDB_is_syserr(ret)) {
            RDB_rollback_all(txp);
        }
        return ret;
    }

    RDB_init_obj(&tpl);

    *resultp = RDB_TRUE;
    while ((ret = _RDB_next_tuple(qrp, &tpl, txp)) == RDB_OK) {
        ret = RDB_table_contains(tb2p, &tpl, txp);
        if (ret == RDB_NOT_FOUND) {
            *resultp = RDB_FALSE;
            break;
        }
        if (ret != RDB_OK) {
            if (RDB_is_syserr(ret)) {
                RDB_rollback_all(txp);
            }
            RDB_destroy_obj(&tpl);
            _RDB_drop_qresult(qrp, txp);
            goto error;
        }
    }

    RDB_destroy_obj(&tpl);
    if (ret != RDB_NOT_FOUND && ret != RDB_OK) {
        _RDB_drop_qresult(qrp, txp);
        goto error;
    }
    ret = _RDB_drop_qresult(qrp, txp);
    if (ret != RDB_OK)
        goto error;
    return RDB_OK;

error:
    if (RDB_is_syserr(ret))
        RDB_rollback_all(txp);
    return ret;
}

int
RDB_table_equals(RDB_table *tb1p, RDB_table *tb2p, RDB_transaction *txp,
        RDB_bool *resp)
{
    int ret;
    RDB_qresult *qrp;
    RDB_object tpl;
    int cnt;

    /* Check if types of the two tables match */
    if (!RDB_type_equals(tb1p->typ, tb2p->typ))
        return RDB_TYPE_MISMATCH;

    /*
     * Check if both tables have same cardinality
     */
    cnt = RDB_cardinality(tb1p, txp);
    if (cnt < 0)
        return cnt;

    ret =  RDB_cardinality(tb2p, txp);
    if (ret < 0)
        return ret;
    if (ret != cnt) {
        *resp = RDB_FALSE;
        return RDB_OK;
    }

    /*
     * Check if all tuples from table #1 are in table #2
     * (The implementation is quite inefficient if table #2
     * is a SUMMARIZE PER or GROUP table)
     */
    ret = _RDB_table_qresult(tb1p, txp, &qrp);
    if (ret != RDB_OK)
        return ret;

    RDB_init_obj(&tpl);
    while ((ret = _RDB_next_tuple(qrp, &tpl, txp)) == RDB_OK) {
        ret = RDB_table_contains(tb2p, &tpl, txp);
        if (ret == RDB_NOT_FOUND) {
            *resp = RDB_FALSE;
            RDB_destroy_obj(&tpl);
            return _RDB_drop_qresult(qrp, txp);
        } else if (ret != RDB_OK) {
            goto error;
        }
    }

    *resp = RDB_TRUE;
    RDB_destroy_obj(&tpl);
    return _RDB_drop_qresult(qrp, txp);

error:
    RDB_destroy_obj(&tpl);
    _RDB_drop_qresult(qrp, txp);
    return ret;
}

/*
 * If there is an index by which the tuples are ordered
 * when read through a qresult.
 */
struct _RDB_tbindex *
_RDB_sortindex (RDB_table *tbp)
{
    switch (tbp->kind) {
        case RDB_TB_REAL:
            return NULL;
        case RDB_TB_SELECT:
            return _RDB_sortindex(tbp->var.select.tbp);
        case RDB_TB_UNION:
            return NULL;
        case RDB_TB_MINUS:
            return _RDB_sortindex(tbp->var.minus.tb1p);
        case RDB_TB_INTERSECT:
            return _RDB_sortindex(tbp->var.intersect.tb1p);
        case RDB_TB_JOIN:
            return _RDB_sortindex(tbp->var.join.tb1p);
        case RDB_TB_EXTEND:
            return _RDB_sortindex(tbp->var.extend.tbp);
        case RDB_TB_PROJECT:
            return tbp->var.project.indexp;
        case RDB_TB_RENAME:
            return NULL; /* !! */
        case RDB_TB_SUMMARIZE:
            return NULL; /* !! */
        case RDB_TB_WRAP:
            return NULL; /* !! */
        case RDB_TB_UNWRAP:
            return NULL; /* !! */
        case RDB_TB_SDIVIDE:
            return _RDB_sortindex(tbp->var.sdivide.tb1p);
        case RDB_TB_GROUP:
            return NULL; /* !! */
        case RDB_TB_UNGROUP:
            return NULL; /* !! */
    }
    abort();
}
