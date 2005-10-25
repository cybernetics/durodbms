/*
 * $Id$
 *
 * Copyright (C) 2003-2005 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "rdb.h"
#include "internal.h"
#include <gen/hashtabit.h>
#include <gen/strfns.h>
#include <string.h>
#include <stdlib.h>

#define RDB_TUPLE_CAPACITY 7

/*
 * A tuple is implemented using a hash table, taking advantage of
 * the fact that removing attributes is not supported.
 */

static unsigned
hash_entry(const void *ep, void *arg)
{
    return RDB_hash_str(((tuple_entry *) ep)->key);
}

static RDB_bool
entry_equals(const void *e1p, const void *e2p, void *arg)
{
     return (RDB_bool)
             strcmp(((tuple_entry *) e1p)->key, ((tuple_entry *) e2p)->key) == 0;
}

static void
init_tuple(RDB_object *tp)
{
    RDB_init_hashtable(&tp->var.tpl_tab, RDB_TUPLE_CAPACITY,
            &hash_entry, &entry_equals);
    tp->kind = RDB_OB_TUPLE;
}

int
provide_entry(RDB_object *tplp, const char *attrname, RDB_object **valpp)
{
    int ret;
    tuple_entry sentry;
    tuple_entry *entryp;

    if (tplp->kind == RDB_OB_INITIAL)
        init_tuple(tplp);

    sentry.key = (char *) attrname;

    /* Check if there is already a value for the key */
    entryp = RDB_hashtable_get(&tplp->var.tpl_tab, &sentry, NULL);
    if (entryp != NULL) {
        /* Return pointer to value */
    } else {
        /* Insert new entry */
        entryp = malloc(sizeof(tuple_entry));
        if (entryp == NULL)
            return RDB_NO_MEMORY;
        entryp->key = RDB_dup_str(attrname);
        if (entryp->key == NULL) {
            free(entryp);
            return RDB_NO_MEMORY;
        }
        ret = RDB_hashtable_put(&tplp->var.tpl_tab, entryp, NULL);
        if (ret != RDB_OK) {
            free(entryp->key);
            free(entryp);
            return ret;
        }
        RDB_init_obj(&entryp->obj);
    }
    *valpp = &entryp->obj;
    return RDB_OK;
}

int
RDB_tuple_set(RDB_object *tplp, const char *attrname, const RDB_object *valp,
        RDB_exec_context *ecp)
{
    RDB_object *dstvalp;
    int ret = provide_entry(tplp, attrname, &dstvalp);
    if (ret != RDB_OK)
        return ret;

    RDB_destroy_obj(dstvalp, ecp);
    RDB_init_obj(dstvalp);
    return RDB_copy_obj(dstvalp, valp, ecp);
}

int
RDB_tuple_set_bool(RDB_object *tplp, const char *attrname, RDB_bool val)
{
    RDB_object *dstvalp;
    int ret = provide_entry(tplp, attrname, &dstvalp);
    if (ret != RDB_OK)
        return ret;

    RDB_bool_to_obj(dstvalp, val);
    return RDB_OK;
} 

int
RDB_tuple_set_int(RDB_object *tplp, const char *attrname, RDB_int val)
{
    RDB_object *dstvalp;
    int ret = provide_entry(tplp, attrname, &dstvalp);
    if (ret != RDB_OK)
        return ret;

    RDB_int_to_obj(dstvalp, val);
    return RDB_OK;
}

int
RDB_tuple_set_rational(RDB_object *tplp, const char *attrname, RDB_rational val)
{
    RDB_object *dstvalp;
    int ret = provide_entry(tplp, attrname, &dstvalp);
    if (ret != RDB_OK)
        return ret;

    RDB_rational_to_obj(dstvalp, val);
    return RDB_OK;
}

int
RDB_tuple_set_string(RDB_object *tplp, const char *attrname, const char *str,
        RDB_exec_context *ecp)
{
    RDB_object *dstvalp;
    int ret = provide_entry(tplp, attrname, &dstvalp);
    if (ret != RDB_OK)
        return ret;

    return RDB_string_to_obj(dstvalp, str, ecp);
}

RDB_object *
RDB_tuple_get(const RDB_object *tplp, const char *attrname)
{
    tuple_entry sentry;
    tuple_entry *entryp;

    if (tplp->kind == RDB_OB_INITIAL)
        return NULL;

    sentry.key = (char *) attrname;
    entryp = RDB_hashtable_get(&tplp->var.tpl_tab, &sentry, NULL);
    if (entryp == NULL)
        return NULL;
    return &entryp->obj;
}

RDB_bool
RDB_tuple_get_bool(const RDB_object *tplp, const char *attrname)
{
    return ((RDB_object *) RDB_tuple_get(tplp, attrname))->var.bool_val;
}

RDB_int
RDB_tuple_get_int(const RDB_object *tplp, const char *attrname)
{
    return ((RDB_object *) RDB_tuple_get(tplp, attrname))->var.int_val;
}

RDB_rational
RDB_tuple_get_rational(const RDB_object *tplp, const char *attrname)
{
    return ((RDB_object *) RDB_tuple_get(tplp, attrname))->var.rational_val;
}

char *
RDB_tuple_get_string(const RDB_object *tplp, const char *attrname)
{
    return ((RDB_object *) RDB_tuple_get(tplp, attrname))->var.bin.datap;
}

RDB_int
RDB_tuple_size(const RDB_object *tplp)
{
    if (tplp->kind == RDB_OB_INITIAL)
        return (RDB_int) 0;
    return (RDB_int) RDB_hashtable_size(&tplp->var.tpl_tab);
}

void
RDB_tuple_attr_names(const RDB_object *tplp, char **namev)
{
    int i = 0;
    tuple_entry *entryp;
    RDB_hashtable_iter hiter;

    RDB_init_hashtable_iter(&hiter, (RDB_hashtable *) &tplp->var.tpl_tab);
    while ((entryp = RDB_hashtable_next(&hiter)) != NULL) {
        namev[i++] = entryp->key;
    }
    RDB_destroy_hashtable_iter(&hiter);
}

int
RDB_project_tuple(const RDB_object *tplp, int attrc, char *attrv[],
                 RDB_exec_context *ecp, RDB_object *restplp)
{
    RDB_object *attrp;
    int i;

    if (tplp->kind != RDB_OB_TUPLE)
        return RDB_INVALID_ARGUMENT;

    RDB_destroy_obj(restplp, ecp);
    RDB_init_obj(restplp);

    for (i = 0; i < attrc; i++) {
        attrp = RDB_tuple_get(tplp, attrv[i]);
        if (attrp == NULL)
            return RDB_ATTRIBUTE_NOT_FOUND;

        RDB_tuple_set(restplp, attrv[i], attrp, ecp);
    }

    return RDB_OK;
}

int
RDB_remove_tuple(const RDB_object *tplp, int attrc, char *attrv[],
                 RDB_exec_context *ecp, RDB_object *restplp)
{
    RDB_hashtable_iter hiter;
    tuple_entry *entryp;
    int i;

    if (tplp->kind != RDB_OB_TUPLE)
        return RDB_INVALID_ARGUMENT;

    RDB_destroy_obj(restplp, ecp);
    RDB_init_obj(restplp);

    RDB_init_hashtable_iter(&hiter, (RDB_hashtable *) &tplp->var.tpl_tab);
    for (;;) {
        /* Get next attribute */
        entryp = (tuple_entry *) RDB_hashtable_next(&hiter);
        if (entryp == NULL)
            break;

        /* Check if attribute is in attribute list */
        for (i = 0; i < attrc && strcmp(entryp->key, attrv[i]) != 0; i++);
        if (i >= attrc) {
            /* Not found, so copy attribute */
            RDB_tuple_set(restplp, entryp->key, &entryp->obj, ecp);
        }
    }
    RDB_destroy_hashtable_iter(&hiter);

    return RDB_OK;
}

int
RDB_join_tuples(const RDB_object *tpl1p, const RDB_object *tpl2p,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *restplp)
{
    RDB_hashtable_iter hiter;
    int ret = RDB_copy_obj(restplp, tpl1p, ecp);

    if (ret != RDB_OK)
        return ret;

    RDB_init_hashtable_iter(&hiter, (RDB_hashtable *) &tpl2p->var.tpl_tab);
    for (;;) {
        /* Get next attribute */
        RDB_object *dstattrp;
        tuple_entry *entryp = (tuple_entry *) RDB_hashtable_next(&hiter);
        if (entryp == NULL)
            break;

        /* Get corresponding attribute from tuple #1 */
        dstattrp = RDB_tuple_get(restplp, entryp->key);
        if (dstattrp != NULL) {
             RDB_bool b;
             RDB_type *typ = RDB_obj_type(dstattrp);
             
             /* Check attribute types for equality */
             if (typ != NULL && !RDB_type_equals(typ,
                     RDB_obj_type(&entryp->obj))) {
                 RDB_destroy_hashtable_iter(&hiter);
                 return RDB_TYPE_MISMATCH;
             }
             
             /* Check attribute values for equality */
             ret = RDB_obj_equals(dstattrp, &entryp->obj, ecp, txp, &b);
             if (ret != RDB_OK) {
                 RDB_destroy_hashtable_iter(&hiter);
                 return ret;
             }
             if (!b) {
                 RDB_destroy_hashtable_iter(&hiter);
                 return RDB_INVALID_ARGUMENT;
             }
        } else {
             ret = RDB_tuple_set(restplp, entryp->key, &entryp->obj, ecp);
             if (ret != RDB_OK)
             {
                 RDB_destroy_hashtable_iter(&hiter);
                 return RDB_INVALID_ARGUMENT;
             }
        }
    }
    RDB_destroy_hashtable_iter(&hiter);
    return RDB_OK;
}

int
RDB_extend_tuple(RDB_object *tplp, int attrc, const RDB_virtual_attr attrv[],
                RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i;
    int res;
    RDB_object val;

    for (i = 0; i < attrc; i++) {
        RDB_init_obj(&val);
        res = RDB_evaluate(attrv[i].exp, tplp, ecp, txp, &val);
        if (res != RDB_OK)
            return res;
        RDB_tuple_set(tplp, attrv[i].name, &val, ecp);
        RDB_destroy_obj(&val, ecp);
    }
    return RDB_OK;
}

int
RDB_rename_tuple(const RDB_object *tplp, int renc, const RDB_renaming renv[],
                 RDB_exec_context *ecp, RDB_object *restup)
{
    RDB_hashtable_iter it;
    tuple_entry *entryp;

    if (tplp->kind != RDB_OB_TUPLE)
        return RDB_INVALID_ARGUMENT;

    /* Copy attributes to tplp */
    RDB_init_hashtable_iter(&it, (RDB_hashtable *)&tplp->var.tpl_tab);
    while ((entryp = RDB_hashtable_next(&it)) != NULL) {
        int ret;
        int ai = _RDB_find_rename_from(renc, renv, entryp->key);

        if (ai >= 0) {
            ret = RDB_tuple_set(restup, renv[ai].to, &entryp->obj, ecp);
        } else {
            ret = RDB_tuple_set(restup, entryp->key, &entryp->obj, ecp);
        }

        if (ret != RDB_OK) {
            RDB_destroy_hashtable_iter(&it);
            return ret;
        }
    }

    RDB_destroy_hashtable_iter(&it);

    return RDB_OK;
}

static int
find_rename_to(int renc, const RDB_renaming renv[], const char *name)
{
    int i;

    for (i = 0; i < renc && strcmp(renv[i].to, name) != 0; i++);
    if (i >= renc)
        return -1; /* not found */
    /* found */
    return i;
}

int
_RDB_invrename_tuple(const RDB_object *tup, int renc, const RDB_renaming renv[],
                 RDB_exec_context *ecp, RDB_object *restup)
{
    RDB_hashtable_iter it;
    tuple_entry *entryp;

    if (tup->kind != RDB_OB_TUPLE)
        return RDB_INVALID_ARGUMENT;

    /* Copy attributes to tup */
    RDB_init_hashtable_iter(&it, (RDB_hashtable *)&tup->var.tpl_tab);
    while ((entryp = RDB_hashtable_next(&it)) != NULL) {
        int ret;
        int ai = find_rename_to(renc, renv, entryp->key);

        if (ai >= 0) {
            ret = RDB_tuple_set(restup, renv[ai].from, &entryp->obj, ecp);
        } else {
            ret = RDB_tuple_set(restup, entryp->key, &entryp->obj, ecp);
        }

        if (ret != RDB_OK) {
            RDB_destroy_hashtable_iter(&it);
            return ret;
        }
    }

    RDB_destroy_hashtable_iter(&it);

    return RDB_OK;
}

/*
 * Invert wrap operation on tuple
 */
int
_RDB_invwrap_tuple(const RDB_object *tplp, int wrapc,
        const RDB_wrapping wrapv[], RDB_exec_context *ecp,
        RDB_object *restplp)
{
    int i;
    int ret;
    char **attrv = malloc(sizeof(char *) * wrapc);

    if (attrv == NULL)
        return RDB_NO_MEMORY;

    /*
     * Create unwrapped tuple
     */

    for (i = 0; i < wrapc; i++)
        attrv[i] = wrapv[i].attrname;

    ret = RDB_unwrap_tuple(tplp, wrapc, attrv, ecp, restplp);
    free(attrv);
    return ret;
}

int
_RDB_invunwrap_tuple(const RDB_object *tplp, int attrc, char *attrv[],
        RDB_type *srctuptyp, RDB_exec_context *ecp, RDB_object *restplp)
{
    int ret;
    int i, j;
    RDB_wrapping *wrapv = malloc(sizeof(RDB_wrapping) * attrc);
    
    if (wrapv == NULL)
        return RDB_NO_MEMORY;

    /*
     * Create wrapped tuple
     */

    for (i = 0; i < attrc; i++) {
        RDB_type *tuptyp = _RDB_tuple_type_attr(srctuptyp, attrv[i])->typ;

        wrapv[i].attrc = tuptyp->var.tuple.attrc;
        wrapv[i].attrv = malloc(sizeof(char *) * tuptyp->var.tuple.attrc);
        if (wrapv[i].attrv == NULL)
            return RDB_NO_MEMORY;
        for (j = 0; j < wrapv[i].attrc; j++)
            wrapv[i].attrv[j] = tuptyp->var.tuple.attrv[j].name;

        wrapv[i].attrname = attrv[i];        
    }

    ret = RDB_wrap_tuple(tplp, attrc, wrapv, ecp, restplp);

    for (i = 0; i < attrc; i++)
        free(wrapv[i].attrv);
    free(wrapv);
    return ret;
}

/* Copy all attributes from one tuple to another. */
int
_RDB_copy_tuple(RDB_object *dstp, const RDB_object *srcp, RDB_exec_context *ecp)
{
    RDB_hashtable_iter it;
    tuple_entry *entryp;

    if (srcp->kind == RDB_OB_INITIAL)
        return RDB_OK;

    if (srcp->kind != RDB_OB_TUPLE)
        return RDB_INVALID_ARGUMENT;

    /* Copy attributes to tup */
    RDB_init_hashtable_iter(&it, (RDB_hashtable *)&srcp->var.tpl_tab);
    while ((entryp = RDB_hashtable_next(&it)) != NULL) {
        int ret = RDB_tuple_set(dstp, entryp->key, &entryp->obj, ecp);

        if (ret != RDB_OK) {
            RDB_destroy_hashtable_iter(&it);
            return ret;
        }
    }

    RDB_destroy_hashtable_iter(&it);
    
    return RDB_OK;
}

int
RDB_wrap_tuple(const RDB_object *tplp, int wrapc, const RDB_wrapping wrapv[],
               RDB_exec_context *ecp, RDB_object *restplp)
{
    int i, j;
    int ret;
    RDB_object tpl;
    RDB_hashtable_iter it;
    tuple_entry *entryp;

    RDB_init_obj(&tpl);

    /* Wrap attributes */
    for (i = 0; i < wrapc; i++) {
        for (j = 0; j < wrapv[i].attrc; j++) {
            RDB_object *attrp = RDB_tuple_get(tplp, wrapv[i].attrv[j]);

            if (attrp == NULL) {
                RDB_destroy_obj(&tpl, ecp);
                return RDB_ATTRIBUTE_NOT_FOUND;
            }

            ret = RDB_tuple_set(&tpl, wrapv[i].attrv[j], attrp, ecp);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl, ecp);
                return ret;
            }
        }
        RDB_tuple_set(restplp, wrapv[i].attrname, &tpl, ecp);
        if (ret != RDB_OK) {
            RDB_destroy_obj(&tpl, ecp);
            return ret;
        }
    }
    RDB_destroy_obj(&tpl, ecp);

    /* Copy attributes which have not been wrapped */
    RDB_init_hashtable_iter(&it, (RDB_hashtable *)&tplp->var.tpl_tab);
    while ((entryp = RDB_hashtable_next(&it)) != NULL) {
        int i;

        for (i = 0; i < wrapc
                && RDB_find_str(wrapv[i].attrc, wrapv[i].attrv, entryp->key) == -1;
                i++);
        if (i == wrapc) {
            /* Attribute not found, copy */
            ret = RDB_tuple_set(restplp, entryp->key, &entryp->obj, ecp);
            if (ret != RDB_OK) {
                RDB_destroy_hashtable_iter(&it);
                return ret;
            }
        }
    }
    RDB_destroy_hashtable_iter(&it);

    return RDB_OK;
}

int
RDB_unwrap_tuple(const RDB_object *tplp, int attrc, char *attrv[],
        RDB_exec_context *ecp, RDB_object *restplp)
{
    int i;
    int ret;
    RDB_hashtable_iter it;
    tuple_entry *entryp;
    
    for (i = 0; i < attrc; i++) {
        RDB_object *wtplp = RDB_tuple_get(tplp, attrv[i]);

        if (wtplp == NULL)
            return RDB_ATTRIBUTE_NOT_FOUND;
        if (wtplp->kind != RDB_OB_TUPLE)
            return RDB_INVALID_ARGUMENT;

        ret = _RDB_copy_tuple(restplp, wtplp, ecp);
        if (ret != RDB_OK)
            return ret;
    }
    
    /* Copy remaining attributes */
    RDB_init_hashtable_iter(&it, (RDB_hashtable *)&tplp->var.tpl_tab);
    while ((entryp = RDB_hashtable_next(&it)) != NULL) {
        /* Copy attribute if it does not appear in attrv */
        if (RDB_find_str(attrc, attrv, entryp->key) == -1) {
            ret = RDB_tuple_set(restplp, entryp->key, &entryp->obj, ecp);
            if (ret != RDB_OK) {
                RDB_destroy_hashtable_iter(&it);
                return ret;
            }
        }
    }
    RDB_destroy_hashtable_iter(&it);

    return RDB_OK;
}

int
_RDB_tuple_equals(const RDB_object *tpl1p, const RDB_object *tpl2p,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_bool *resp)
{
    int ret;
    RDB_hashtable_iter hiter;
    tuple_entry *entryp;
    RDB_bool b;

    RDB_init_hashtable_iter(&hiter, (RDB_hashtable *) &tpl1p->var.tpl_tab);
    while ((entryp = RDB_hashtable_next(&hiter)) != NULL) {
        ret = RDB_obj_equals(&entryp->obj, RDB_tuple_get(tpl2p, entryp->key),
                ecp, txp, &b);
        if (ret != RDB_OK) {
            RDB_destroy_hashtable_iter(&it);
            return ret;
        }
        if (!b) {
            RDB_destroy_hashtable_iter(&it);
            *resp = RDB_FALSE;
            return RDB_OK;
        }
    }
    RDB_destroy_hashtable_iter(&it);
    *resp = RDB_TRUE;
    return RDB_OK;
}

/*
 * Generate type from tuple
 */
RDB_type *
_RDB_tuple_type(const RDB_object *tplp, RDB_exec_context *ecp)
{
    int i;
    tuple_entry *entryp;
    RDB_hashtable_iter hiter;
    RDB_type *typ = malloc(sizeof (RDB_type));
    if (typ == NULL)
        return NULL;

    typ->kind = RDB_TP_TUPLE;
    typ->name = NULL;
    typ->var.tuple.attrc = RDB_tuple_size(tplp);
    if (typ->var.tuple.attrc > 0) {
        typ->var.tuple.attrv = malloc(sizeof(RDB_attr) * typ->var.tuple.attrc);
        if (typ->var.tuple.attrv == NULL)
            goto error;

        for (i = 0; i < typ->var.tuple.attrc; i++)
            typ->var.tuple.attrv[i].name = NULL;

        RDB_init_hashtable_iter(&hiter, (RDB_hashtable *) &tplp->var.tpl_tab);
        i = 0;
        while ((entryp = RDB_hashtable_next(&hiter)) != NULL) {
            if (entryp->obj.kind != RDB_OB_TUPLE) {
                typ->var.tuple.attrv[i].typ = RDB_obj_type(&entryp->obj);
            } else {
                typ->var.tuple.attrv[i].typ = _RDB_tuple_type(&entryp->obj,
                        ecp);
            }
            typ->var.tuple.attrv[i].name = RDB_dup_str(entryp->key);
            if (typ->var.tuple.attrv[i].name == NULL) {
                RDB_destroy_hashtable_iter(&it);
                goto error;
            }
            typ->var.tuple.attrv[i].defaultp = NULL;
            i++;
        }
        RDB_destroy_hashtable_iter(&it);
    }
    return typ;

error:
    if (typ->var.tuple.attrc > 0 && typ->var.tuple.attrv != NULL) {
        for (i = 0; i < typ->var.tuple.attrc; i++) {
            free(typ->var.tuple.attrv[i].name);
            if (typ->var.tuple.attrv[i].typ != NULL
                    && typ->var.tuple.attrv[i].typ->kind == RDB_TP_TUPLE)
                RDB_drop_type(typ->var.tuple.attrv[i].typ, ecp, NULL);
        }
        free(typ->var.tuple.attrv);
    }
    free(typ);
    return NULL;
}
