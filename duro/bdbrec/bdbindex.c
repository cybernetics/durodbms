/*
 * Copyright (C) 2004-2012 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "bdbindex.h"
#include "bdbrecmap.h"
#include <rec/indeximpl.h>
#include <rec/recmapimpl.h>
#include <rec/envimpl.h>
#include <rec/dbdefs.h>
#include <gen/strfns.h>
#include <obj/excontext.h>

#include <string.h>
#include <errno.h>

#include <db.h>

/*
 * Read field from an index DBT
 */
static int
get_field(RDB_index *ixp, int fi, void *datap, size_t len, size_t *lenp,
              int *vposp)
{
    int i, vpos;
    int offs = 0;
    RDB_byte *databp = (RDB_byte *) datap;
    int fno = ixp->fieldv[fi];

    /*
     * Compute offset and length for key
     */
    if (ixp->rmp->fieldinfos[fno].len != RDB_VARIABLE_LEN) {
        /* Offset is sum of lengths of previous fields */
        for (i = 0; i < fi; i++) {
            if (ixp->rmp->fieldinfos[ixp->fieldv[i]].len != RDB_VARIABLE_LEN) {
                offs += ixp->rmp->fieldinfos[ixp->fieldv[i]].len;
            }
        }

        *lenp = (size_t) ixp->rmp->fieldinfos[fno].len;
    } else {
        /*
         * Offset is sum of lengths of fixed-length fields
         * plus lengths of previous variable-length fields
         */
        int vfcnt = 0;
        for (i = 0; i < ixp->fieldc; i++) {
            if (ixp->rmp->fieldinfos[ixp->fieldv[i]].len == RDB_VARIABLE_LEN)
                vfcnt++;
        }

        vpos = 0;
        for (i = 0; i < fi; i++) {
            if (ixp->rmp->fieldinfos[ixp->fieldv[i]].len != RDB_VARIABLE_LEN) {
                offs += ixp->rmp->fieldinfos[i].len;
            } else {
                offs += RDB_get_vflen(databp, len, vfcnt, vpos++);
            }
        }
        *lenp = RDB_get_vflen(databp, len, vfcnt, vpos);
    }
    if (vposp != NULL)
        *vposp = vpos;
    return offs;
}

static int
compare_key(DB *dbp, const DBT *dbt1p, const DBT *dbt2p, size_t *locp)
{
    int i;
    RDB_index *ixp = dbp->app_private;

    for (i = 0; i < ixp->fieldc; i++) {
        int offs1, offs2;
        size_t len1, len2;
        void *data1p, *data2p;
        int res;

        offs1 = get_field(ixp, i, dbt1p->data, dbt1p->size, &len1, NULL);
        offs2 = get_field(ixp, i, dbt2p->data, dbt2p->size, &len2, NULL);
        data1p = ((RDB_byte *) dbt1p->data) + offs1;
        data2p = ((RDB_byte *) dbt2p->data) + offs2;

        /* Compare fields */
        res = (*ixp->cmpv[i].comparep)(data1p, len1, data2p, len2,
                    ixp->rmp->envp, ixp->cmpv[i].arg);

        /* If the fields are different, we're done */
        if (res != 0) {
            /* If order is descending, revert result */
            if (!ixp->cmpv[i].asc)
                res = -res;
            return res;
        }
    }
    /* All fields equal */
    return 0;
}

static RDB_index *
new_index(RDB_recmap *rmp, const char *name, const char *filename,
        RDB_environment *envp, int fieldc, const int fieldv[],
        const RDB_compare_field cmpv[], int flags, RDB_exec_context *ecp)
{
    int ret;
    int i;
    RDB_index *ixp = RDB_alloc(sizeof (RDB_index), ecp);

    if (ixp == NULL) {
        return NULL;
    }
    ixp->fieldv = NULL;
    ixp->rmp = rmp;

    ixp->namp = ixp->filenamp = NULL;
    if (name != NULL) {  
        ixp->namp = RDB_dup_str(name);
        if (ixp->namp == NULL) {
            RDB_raise_no_memory(ecp);
            goto error;
        }
    }
    if (filename != NULL) {
        ixp->filenamp = RDB_dup_str(filename);
        if (ixp->filenamp == NULL) {
            RDB_raise_no_memory(ecp);
            goto error;
        }
    }

    ixp->fieldc = fieldc;
    ixp->fieldv = malloc(fieldc * sizeof(int));
    if (ixp->fieldv == NULL) {
        RDB_raise_no_memory(ecp);
        goto error;
    }
    for (i = 0; i < fieldc; i++)
        ixp->fieldv[i] = fieldv[i];
    ixp->cmpv = 0;

    ret = db_create(&ixp->dbp, envp != NULL ? envp->env.envp : NULL, 0);
    if (ret != 0) {
        goto error;
    }

    if (cmpv != NULL) {
        RDB_bool all_cmpfn = RDB_TRUE;

        ixp->cmpv = RDB_alloc(sizeof (RDB_compare_field) * fieldc, ecp);
        if (ixp->cmpv == NULL)
            goto error;
        for (i = 0; i < fieldc; i++) {
            ixp->cmpv[i].comparep = cmpv[i].comparep;
            ixp->cmpv[i].arg = cmpv[i].arg;
            ixp->cmpv[i].asc = cmpv[i].asc;

            if (cmpv[i].comparep == NULL)
                all_cmpfn = RDB_FALSE;
        }

        /*
         * If there is a comparison function for all fields,
         * set B-tree comparison function
         */
        if (all_cmpfn) {
            ixp->dbp->app_private = ixp;
            ixp->dbp->set_bt_compare(ixp->dbp, &compare_key);
        }
    }

    if (!(RDB_UNIQUE & flags))
        ixp->dbp->set_flags(ixp->dbp, DB_DUPSORT);

    return ixp;

error:
    free(ixp->namp);
    free(ixp->filenamp);
    free(ixp->fieldv);
    free(ixp);
    return NULL;
}

/* Fill the DBT for the secondary key */
static int
make_skey(DB *dbp, const DBT *pkeyp, const DBT *pdatap, DBT *skeyp)
{
    RDB_index *ixp = dbp->app_private;
    RDB_field *fieldv;
    int ret;
    int i;

    fieldv = malloc (sizeof(RDB_field) * ixp->fieldc);
    if (fieldv == NULL)
        return ENOMEM;

    for (i = 0; i < ixp->fieldc; i++) {
        fieldv[i].no = ixp->fieldv[i];
        fieldv[i].copyfp = &memcpy;
    }
    ret = RDB_get_DBT_fields(ixp->rmp, pkeyp, pdatap, ixp->fieldc, fieldv);
    if (ret != RDB_OK) {
        free(fieldv);
        return ret;
    }

    ret = RDB_fields_to_DBT(ixp->rmp, ixp->fieldc, fieldv, skeyp);
    skeyp->flags = DB_DBT_APPMALLOC;
    free(fieldv);
    return ret;
}

RDB_index *
RDB_create_bdb_index(RDB_recmap *rmp, const char *namp, const char *filenamp,
        RDB_environment *envp, int fieldc, const RDB_field_descriptor fieldv[],
        const RDB_compare_field cmpv[], int flags, RDB_rec_transaction *rtxp,
        RDB_exec_context *ecp)
{
    RDB_index *ixp;
    int ret;
    int i;

    int *fnov = RDB_alloc(fieldc * sizeof(int), ecp);
    if (fnov == NULL)
        goto error;
    for (i = 0; i < fieldc; i++) {
        fnov[i] = fieldv[i].no;
    }
    ixp = new_index(rmp, namp, filenamp, envp, fieldc, fnov, cmpv, flags, ecp);
    RDB_free(fnov);
    if (ixp == NULL)
        return NULL;

    ret = ixp->dbp->open(ixp->dbp, (DB_TXN *) rtxp, filenamp, namp,
            RDB_ORDERED & flags ? DB_BTREE : DB_HASH, DB_CREATE, 0664);
    if (ret != 0) {
        RDB_errcode_to_error(ret, ecp);
        goto error;
    }

    /* Attach index to BDB database (for the callback) */
    ixp->dbp->app_private = ixp;

    /* Associate the index DB with the recmap DB */
    ret = ixp->dbp->associate(rmp->dbp, (DB_TXN *) rtxp, ixp->dbp, make_skey, DB_CREATE);
    if (ret != 0) {
        RDB_errcode_to_error(ret, ecp);
        goto error;
    }

    return ixp;

error:
    RDB_delete_bdb_index(ixp, envp, rtxp, ecp);
    return NULL;
}

RDB_index *
RDB_open_bdb_index(RDB_recmap *rmp, const char *namp, const char *filenamp,
        RDB_environment *envp, int fieldc, const int fieldv[],
        const RDB_compare_field cmpv[], int flags, RDB_rec_transaction *rtxp,
        RDB_exec_context *ecp)
{
    int ret;
    RDB_index *ixp = new_index(rmp, namp, filenamp, envp, fieldc, fieldv, cmpv, flags, ecp);
    if (ixp == NULL)
        return NULL;

    ret = ixp->dbp->open(ixp->dbp, (DB_TXN *) rtxp, filenamp, namp, DB_UNKNOWN, 0, 0664);
    if (ret != 0) {
        RDB_errcode_to_error(ret, ecp);
        goto error;
    }

    /* attach index to BDB database (for the callback) */
    ixp->dbp->app_private = ixp;

    /* associate the index DB with the recmap DB */
    ret = ixp->dbp->associate(rmp->dbp, (DB_TXN *) rtxp, ixp->dbp, make_skey, 0);
    if (ret != 0) {
        RDB_errcode_to_error(ret, ecp);
        goto error;
    }

    return ixp;

error:
    RDB_close_index(ixp, ecp);
    return NULL;
}

int
RDB_close_bdb_index(RDB_index *ixp, RDB_exec_context *ecp)
{
    int ret = ixp->dbp->close(ixp->dbp, 0);
    free(ixp->namp);
    free(ixp->filenamp);
    free(ixp->fieldv);
    free(ixp->cmpv);
    free(ixp);
    if (ret != 0) {
        RDB_errcode_to_error(ret, ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

RDB_bool
RDB_bdb_index_is_ordered(RDB_index *ixp)
{
    DBTYPE t;

    ixp->dbp->get_type(ixp->dbp, &t);
    return (RDB_bool) (t == DB_BTREE);
}

/* Delete an index. */
int
RDB_delete_bdb_index(RDB_index *ixp, RDB_environment *envp, RDB_rec_transaction *rtxp,
        RDB_exec_context *ecp)
{
    int ret = ixp->dbp->close(ixp->dbp, 0);
    if (ret != 0) {
        RDB_errcode_to_error(ret, ecp);
        goto cleanup;
    }

    if (ixp->namp != NULL)
        ret = envp->env.envp->dbremove(envp->env.envp, (DB_TXN *) rtxp,
                ixp->filenamp, ixp->namp, 0);

cleanup:
    free(ixp->namp);
    free(ixp->filenamp);
    free(ixp->fieldv);
    free(ixp->cmpv);
    free(ixp);

    return ret == 0 ? RDB_OK : RDB_ERROR;
}

int
RDB_bdb_index_get_fields(RDB_index *ixp, RDB_field keyv[], int fieldc,
        RDB_rec_transaction *rtxp, RDB_field retfieldv[], RDB_exec_context *ecp)
{
    DBT key, pkey, data;
    int ret;
    int i;

    for (i = 0; i < ixp->fieldc; i++) {
        keyv[i].no = ixp->fieldv[i];
    }

    /* Fill key DBT */
    ret = RDB_fields_to_DBT(ixp->rmp, ixp->fieldc, keyv, &key);
    if (ret != RDB_OK) {
        RDB_errcode_to_error(ret, ecp);
        return RDB_ERROR;
    }

    memset(&pkey, 0, sizeof (DBT));
    memset(&data, 0, sizeof (DBT));

    /* Get primary key and data */
    ret = ixp->dbp->pget(ixp->dbp, (DB_TXN *) rtxp, &key, &pkey, &data, 0);
    if (ret != 0) {
        RDB_errcode_to_error(ret, ecp);
        return RDB_ERROR;
    }

    /* Get field values */
    for (i = 0; i < fieldc; i++) {
        int offs;
        int fno = retfieldv[i].no;

        if (fno < ixp->rmp->keyfieldcount) {
            offs = RDB_get_field(ixp->rmp, fno, pkey.data, pkey.size,
                    &retfieldv[i].len, NULL);
            if (offs < 0)
                return offs;
            retfieldv[i].datap = ((RDB_byte *)pkey.data) + offs;
        } else {
            offs = RDB_get_field(ixp->rmp, fno,
                    data.data, data.size, &retfieldv[i].len, NULL);
            if (offs < 0)
                return offs;
            retfieldv[i].datap = ((RDB_byte *)data.data) + offs;
        }
    }
    free(key.data);
    return RDB_OK;
}

int
RDB_bdb_index_delete_rec(RDB_index *ixp, RDB_field keyv[], RDB_rec_transaction *rtxp,
        RDB_exec_context *ecp)
{
    DBT key;
    int ret;
    int i;

    for (i = 0; i < ixp->fieldc; i++) {
        keyv[i].no = ixp->fieldv[i];
    }

    /* Fill key DBT */
    ret = RDB_fields_to_DBT(ixp->rmp, ixp->fieldc, keyv, &key);
    if (ret != RDB_OK) {
        RDB_errcode_to_error(ret, ecp);
        return RDB_ERROR;
    }

    /* Delete record */
    ret = ixp->dbp->del(ixp->dbp, (DB_TXN *) rtxp, &key, 0);
    if (ret != RDB_OK) {
        RDB_errcode_to_error(ret, ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}