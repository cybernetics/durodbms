/*
 * $Id$
 *
 * Copyright (C) 2004-2005 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "rdb.h"
#include "internal.h"
#include <gen/strfns.h>
#include <stdlib.h>
#include <string.h>

static int
alter_op(RDB_expression *exp, const char *name, int argc)
{
    RDB_expression **argv;
    char *newname;
    
    newname = realloc(exp->var.op.name, strlen(name) + 1);
    if (newname == NULL)
        return RDB_NO_MEMORY;
    strcpy(newname, name);
    exp->var.op.name = newname;

    if (argc != exp->var.op.argc) {
        argv = realloc(exp->var.op.argv, sizeof (RDB_expression *) * argc);
        if (argv == NULL)
            return RDB_NO_MEMORY;
        exp->var.op.argc = argc;
        exp->var.op.argv = argv;
    }

    return RDB_OK;
}

static void
del_keys(RDB_table *tbp)
{
    int i;

    if (tbp->keyv != NULL) {
        /* Delete candidate keys */
        for (i = 0; i < tbp->keyc; i++) {
            RDB_free_strvec(tbp->keyv[i].strc, tbp->keyv[i].strv);
        }
        free(tbp->keyv);
        tbp->keyv = NULL;
    }
}

static int
copy_type(RDB_table *dstp, const RDB_table *srcp)
{
    RDB_type *typ = _RDB_dup_nonscalar_type(srcp->typ);
    if (typ == NULL)
        return RDB_NO_MEMORY;

    RDB_drop_type(dstp->typ, NULL);
    dstp->typ = typ;

    return RDB_OK;
}

static int
transform_exp(RDB_expression *);

/* Only for binary operators */
static int
eliminate_child (RDB_expression *exp, const char *name)
{
    RDB_expression *hexp = exp->var.op.argv[0];
    int ret = alter_op(exp, name, 2);
    if (ret != RDB_OK)
        return ret;

    exp->var.op.argv[0] = hexp->var.op.argv[0];
    exp->var.op.argv[1] = hexp->var.op.argv[1];
    free(hexp->var.op.name);
    free(hexp->var.op.argv);
    free(hexp);
    ret = transform_exp(exp->var.op.argv[0]);
    if (ret != RDB_OK)
        return ret;
    return transform_exp(exp->var.op.argv[1]);
}

/* Try to eliminate NOT operator */
static int
eliminate_not(RDB_expression *exp)
{
    int ret;
    RDB_expression *hexp;

    if (exp->var.op.argv[0]->kind != RDB_EX_RO_OP)
        return RDB_OK;

    if (strcmp(exp->var.op.argv[0]->var.op.name, "AND") == 0) {
        hexp = RDB_ro_op("NOT", 1, &exp->var.op.argv[0]->var.op.argv[1]);
        if (hexp == NULL)
            return RDB_NO_MEMORY;
        ret = alter_op(exp, "OR", 2);
        if (ret != RDB_OK)
            return ret;
        exp->var.op.argv[1] = hexp;

        ret = alter_op(exp->var.op.argv[0], "NOT", 1);
        if (ret != RDB_OK)
            return ret;

        ret = eliminate_not(exp->var.op.argv[0]);
        if (ret != RDB_OK)
            return ret;
        return eliminate_not(exp->var.op.argv[1]);
    }
    if (strcmp(exp->var.op.argv[0]->var.op.name, "OR") == 0) {
        hexp = RDB_ro_op("NOT", 1, &exp->var.op.argv[0]->var.op.argv[1]);
        if (hexp == NULL)
            return RDB_NO_MEMORY;
        ret = alter_op(exp, "AND", 2);
        if (ret != RDB_OK)
            return ret;
        exp->var.op.argv[1] = hexp;

        ret = alter_op(exp->var.op.argv[0], "NOT", 1);
        if (ret != RDB_OK)
            return ret;

        ret = eliminate_not(exp->var.op.argv[0]);
        if (ret != RDB_OK)
            return ret;
        return eliminate_not(exp->var.op.argv[1]);
    }
    if (strcmp(exp->var.op.argv[0]->var.op.name, "=") == 0)
        return eliminate_child(exp, "<>");
    if (strcmp(exp->var.op.argv[0]->var.op.name, "<>") == 0)
        return eliminate_child(exp, "=");
    if (strcmp(exp->var.op.argv[0]->var.op.name, "<") == 0)
        return eliminate_child(exp, ">=");
    if (strcmp(exp->var.op.argv[0]->var.op.name, ">") == 0)
        return eliminate_child(exp, "<=");
    if (strcmp(exp->var.op.argv[0]->var.op.name, "<=") == 0)
        return eliminate_child(exp, ">");
    if (strcmp(exp->var.op.argv[0]->var.op.name, ">=") == 0)
        return eliminate_child(exp, "<");
    if (strcmp(exp->var.op.argv[0]->var.op.name, "NOT") == 0) {
        hexp = exp->var.op.argv[0];
        memcpy(exp, hexp->var.op.argv[0], sizeof (RDB_expression));
        free(hexp->var.op.argv[0]->var.op.name);
        free(hexp->var.op.argv[0]->var.op.argv);
        free(hexp->var.op.argv[0]);
        free(hexp->var.op.name);
        free(hexp->var.op.argv);
        free(hexp);
        return transform_exp(exp);
    }

    return transform_exp(exp->var.op.argv[0]);
}

static int
transform_exp(RDB_expression *exp)
{
    int ret;
    int i;

    if (exp->kind != RDB_EX_RO_OP)
        return RDB_OK;

    if (strcmp(exp->var.op.name, "NOT") == 0)
        return eliminate_not(exp);
    for (i = 0; i < exp->var.op.argc; i++) {
        ret = transform_exp(exp->var.op.argv[i]);
        if (ret != RDB_OK)
            return ret;
    }
    return RDB_OK;
}

static int
transform_select(RDB_table *tbp)
{
    int ret;
    RDB_expression *exp;
    RDB_table *chtbp = tbp->var.select.tbp;

    ret = _RDB_transform(chtbp);
    if (ret != RDB_OK)
        return ret;

    do {
        switch (chtbp->kind) {
            case RDB_TB_REAL:
                return transform_exp(tbp->var.select.exp);
            case RDB_TB_SELECT:
            {
                RDB_expression *argv[2];

                /*
                 * Merge SELECT tables
                 */
                argv[0] = tbp->var.select.exp;
                argv[1] = chtbp->var.select.exp;
                exp = RDB_ro_op("AND", 2, argv);
                if (exp == NULL)
                    return RDB_NO_MEMORY;

                tbp->var.select.exp = exp;
                tbp->var.select.tbp = chtbp->var.select.tbp;
                _RDB_free_table(chtbp);
                chtbp = tbp->var.select.tbp;
                break;
            }
            case RDB_TB_MINUS:
            {
                RDB_table *htbp = chtbp->var.minus.tb1p;

                ret = transform_exp(tbp->var.select.exp);
                if (ret != RDB_OK)
                    return ret;
                exp = tbp->var.select.exp;

                ret = _RDB_transform(chtbp->var.minus.tb2p);
                if (ret != RDB_OK)
                    return ret;

                tbp->kind = RDB_TB_MINUS;
                tbp->keyc = chtbp->keyc;
                tbp->keyv = chtbp->keyv;
                tbp->var.minus.tb1p = chtbp;
                tbp->var.minus.tb2p = chtbp->var.minus.tb2p;

                chtbp->kind = RDB_TB_SELECT;
                chtbp->var.select.tbp = htbp;
                chtbp->var.select.exp = exp;
                chtbp->var.select.objpc = 0;

                /* Keys may have changed, so delete them */
                del_keys(chtbp);

                tbp = chtbp;
                chtbp = tbp->var.select.tbp;
                break;
            }
            case RDB_TB_UNION:
            {
                RDB_table *newtbp;
                RDB_expression *ex2p;
                RDB_table *htbp = chtbp->var._union.tb1p;

                ret = transform_exp(tbp->var.select.exp);
                if (ret != RDB_OK)
                    return ret;

                ex2p = RDB_dup_expr(tbp->var.select.exp);
                if (ex2p == NULL)
                    return RDB_NO_MEMORY;

                ret = _RDB_select(chtbp->var._union.tb2p, ex2p, &newtbp);
                if (ret != RDB_OK) {
                    RDB_drop_expr(ex2p);
                    return ret;
                }

                exp = tbp->var.select.exp;

                tbp->kind = RDB_TB_UNION;
                tbp->var._union.tb1p = chtbp;
                tbp->var._union.tb2p = newtbp;
                chtbp->kind = RDB_TB_SELECT;
                chtbp->var.select.tbp = htbp;
                chtbp->var.select.exp = exp;
                chtbp->var.select.objpc = 0;

                ret = transform_select(newtbp);
                if (ret != RDB_OK)
                    return ret;

                /* Keys may have changed, so delete them */
                del_keys(chtbp);

                tbp = chtbp;
                chtbp = tbp->var.select.tbp;
                break;
            }
            case RDB_TB_INTERSECT:
            {
                RDB_table *newtbp;
                RDB_expression *ex2p;
                RDB_table *htbp = chtbp->var._union.tb1p;

                ret = transform_exp(tbp->var.select.exp);
                if (ret != RDB_OK)
                    return ret;

                ex2p = RDB_dup_expr(tbp->var.select.exp);
                if (ex2p == NULL)
                    return RDB_NO_MEMORY;

                ret = _RDB_select(chtbp->var._union.tb2p, ex2p, &newtbp);
                if (ret != RDB_OK) {
                    RDB_drop_expr(ex2p);
                    return ret;
                }

                exp = tbp->var.select.exp;

                tbp->kind = RDB_TB_INTERSECT;
                tbp->var._union.tb1p = chtbp;
                tbp->var._union.tb2p = newtbp;
                chtbp->kind = RDB_TB_SELECT;
                chtbp->var.select.tbp = htbp;
                chtbp->var.select.exp = exp;
                chtbp->var.select.objpc = 0;

                ret = transform_select(newtbp);
                if (ret != RDB_OK)
                    return ret;

                /* Keys may have changed, so delete them */
                del_keys(chtbp);

                tbp = chtbp;
                chtbp = tbp->var.select.tbp;
                break;
            }
            case RDB_TB_EXTEND:
            {
                RDB_table *htbp = chtbp->var.extend.tbp;

                ret = transform_exp(tbp->var.select.exp);
                if (ret != RDB_OK)
                    return ret;
                exp = tbp->var.select.exp;
                ret = _RDB_resolve_extend_expr(&exp, chtbp->var.extend.attrc,
                        chtbp->var.extend.attrv);
                if (ret != RDB_OK)
                    return ret;

                tbp->kind = RDB_TB_EXTEND;
                tbp->var.extend.attrc = chtbp->var.extend.attrc;
                tbp->var.extend.attrv = chtbp->var.extend.attrv;

                chtbp->kind = RDB_TB_SELECT;
                chtbp->var.select.tbp = htbp;
                chtbp->var.select.exp = exp;
                chtbp->var.select.objpc = 0;

                ret = copy_type(chtbp, htbp);
                if (ret != RDB_OK)
                     return ret;

                /* Keys may have changed, so delete them */
                del_keys(chtbp);

                tbp = chtbp;
                chtbp = tbp->var.select.tbp;
                break;
            }
            case RDB_TB_RENAME:
            {
                RDB_table *htbp = chtbp->var.rename.tbp;

                ret = transform_exp(tbp->var.select.exp);
                if (ret != RDB_OK)
                    return ret;
                exp = tbp->var.select.exp;
                ret = _RDB_invrename_expr(exp, chtbp->var.rename.renc,
                        chtbp->var.rename.renv);
                if (ret != RDB_OK)
                    return ret;

                tbp->kind = RDB_TB_RENAME;
                tbp->var.rename.renc = chtbp->var.rename.renc;
                tbp->var.rename.renv = chtbp->var.rename.renv;

                chtbp->kind = RDB_TB_SELECT;
                chtbp->var.select.tbp = htbp;
                chtbp->var.select.exp = exp;
                chtbp->var.select.objpc = 0;

                ret = copy_type(chtbp, htbp);
                if (ret != RDB_OK)
                     return ret;

                /* Keys may have changed, so delete them */
                del_keys(chtbp);

                tbp = chtbp;
                chtbp = tbp->var.select.tbp;
                break;
            }
            case RDB_TB_JOIN:
            case RDB_TB_PROJECT:
            case RDB_TB_SUMMARIZE:
                ret = transform_exp(tbp->var.select.exp);
                if (ret != RDB_OK)
                    return ret;
                return _RDB_transform(chtbp);
            case RDB_TB_WRAP:
            case RDB_TB_UNWRAP:
            case RDB_TB_GROUP:
            case RDB_TB_UNGROUP:
            case RDB_TB_SDIVIDE:
                ret = transform_exp(tbp->var.select.exp);
                if (ret != RDB_OK)
                    return ret;
                return _RDB_transform(chtbp);
        }
    } while (tbp->kind == RDB_TB_SELECT);

    return RDB_OK;
}

static int
transform_project(RDB_table *tbp);

static int
swap_project_union(RDB_table *tbp, RDB_table *chtbp)
{
    int ret;
    int i;
    RDB_table *newtbp;
    RDB_type *newtyp;
    RDB_table *htbp = chtbp->var._union.tb1p;
    int attrc = tbp->typ->var.basetyp->var.tuple.attrc;
    char **attrnamev = malloc(attrc * sizeof(char *));
    if (attrnamev == NULL)
        return RDB_NO_MEMORY;

    for (i = 0; i < attrc; i++) {
        attrnamev[i] = tbp->typ->var.basetyp->var.tuple.attrv[i].name;
    }

    /* Create new project table for child #2 */
    ret = RDB_project(chtbp->var._union.tb2p, attrc, attrnamev, &newtbp);
    if (ret != RDB_OK) {
        free(attrnamev);
        return ret;
    }

    /* Alter parent */
    tbp->kind = RDB_TB_UNION;
    tbp->var._union.tb1p = chtbp;
    tbp->var._union.tb2p = newtbp;

    /* Alter child #1 */
    chtbp->kind = RDB_TB_PROJECT;
    chtbp->var.project.tbp = htbp;
    chtbp->var.project.indexp = NULL;
    ret = RDB_project_relation_type(chtbp->typ, attrc, attrnamev, &newtyp);
    free(attrnamev);
    if (ret != RDB_OK) {
        return ret;
    }
    RDB_drop_type(chtbp->typ, NULL);
    chtbp->typ = newtyp;

    ret = transform_project(newtbp);
    if (ret != RDB_OK)
        return ret;

    /* Infer keys for first child to set keyloss flag */
    return _RDB_infer_keys(chtbp);
}

/* Transforms PROJECT(RENAME) to RENAME(PROJECT) or PROJECT */
static int
swap_project_rename(RDB_table *tbp)
{
    int i, j;
    int ret;
    RDB_type *newtyp;
    RDB_renaming *renv;
    char **attrnamev;
    RDB_table *chtbp = tbp->var.project.tbp;
    RDB_table *htbp = chtbp->var.rename.tbp;

    /*
     * Alter parent
     */

    renv = malloc(sizeof (RDB_renaming) * chtbp->var.rename.renc);
    if (renv == NULL)
        return RDB_NO_MEMORY;

    for (i = 0; i < chtbp->var.rename.renc; i++) {
        renv[i].to = NULL;
        renv[i].from = NULL;
    }

    /* Take renamings whose dest appear in the parent */
    j = 0;
    for (i = 0; i < chtbp->var.rename.renc; i++) {
        if (_RDB_tuple_type_attr(tbp->typ->var.basetyp,
                chtbp->var.rename.renv[i].to) != NULL) {
            renv[j].from = RDB_dup_str(chtbp->var.rename.renv[i].from);
            if (renv[j].from == NULL) {
                for (i = 0; i < chtbp->var.rename.renc; i++) {
                    free(renv[i].from);
                    free(renv[i].to);
                }
                free(renv);
                return RDB_NO_MEMORY;
            }
            renv[j].to = RDB_dup_str(chtbp->var.rename.renv[i].to);
            if (renv[j].to == NULL) {
                for (i = 0; i < chtbp->var.rename.renc; i++) {
                    free(renv[i].from);
                    free(renv[i].to);
                }
                free(renv);
                return RDB_NO_MEMORY;
            }
            j++;
        }
    }

    /* Destroy renamings of child */
    for (i = 0; i < chtbp->var.rename.renc; i++) {
        free(chtbp->var.rename.renv[i].from);
        free(chtbp->var.rename.renv[i].to);
    }
    free(chtbp->var.rename.renv);

    if (j == 0) {
        /* Remove child */
        free(renv);
        tbp->var.project.tbp = chtbp->var.rename.tbp;
        _RDB_free_table(chtbp);
    } else {
        /*
         * Swap parent and child
         */
        int nattrc = tbp->typ->var.basetyp->var.tuple.attrc;

        tbp->kind = RDB_TB_RENAME;
        tbp->var.rename.tbp = chtbp;
        tbp->var.rename.renc = j;
        tbp->var.rename.renv = renv;

        chtbp->kind = RDB_TB_PROJECT;
        chtbp->var.project.tbp = htbp;
        chtbp->var.project.indexp = NULL;

        attrnamev = malloc(nattrc * sizeof(char *));
        if (attrnamev == NULL)
            return RDB_NO_MEMORY;
        for (i = 0; i < nattrc; i++) {
            attrnamev[i] = tbp->typ->var.basetyp->var.tuple.attrv[i].name;
            for (j = 0; j < tbp->var.rename.renc
                    && strcmp(attrnamev[i], tbp->var.rename.renv[j].to) != 0; j++);
            if (j < tbp->var.rename.renc)
                attrnamev[j] = tbp->var.rename.renv[j].from;
        }
        ret = RDB_project_relation_type(htbp->typ, nattrc, attrnamev, &newtyp);
        free(attrnamev);
        if (ret != RDB_OK)
            return ret;
        RDB_drop_type(chtbp->typ, NULL);
        chtbp->typ = newtyp;
    }

    return RDB_OK;
}

/* Transforms PROJECT(EXTEND) to EXTEND(PROJECT) or PROJECT */
static int
swap_project_extend(RDB_table *tbp)
{
    int i, j;
    int ret;
    RDB_type *newtyp;
    RDB_virtual_attr *extv;
    char **attrnamev;
    RDB_table *chtbp = tbp->var.project.tbp;
    RDB_table *htbp = chtbp->var.extend.tbp;

    /*
     * Alter parent
     */
    extv = malloc(sizeof (RDB_virtual_attr) * chtbp->var.extend.attrc);
    if (extv == NULL)
        return RDB_NO_MEMORY;

    for (i = 0; i < chtbp->var.extend.attrc; i++) {
        extv[i].name = NULL;
        extv[i].exp = NULL;
    }

    /* Take attributes who appear in the parent */
    j = 0;
    for (i = 0; i < chtbp->var.extend.attrc; i++) {
        if (_RDB_tuple_type_attr(tbp->typ->var.basetyp,
                chtbp->var.extend.attrv[i].name) != NULL) {
            extv[j].name = RDB_dup_str(chtbp->var.extend.attrv[i].name);
            if (extv[j].name == NULL) {
                for (i = 0; i < chtbp->var.extend.attrc; i++) {
                    free(extv[i].name);
                    if (extv[i].exp != NULL)
                        RDB_drop_expr(extv[i].exp);
                }
                free(extv);
                return RDB_NO_MEMORY;
            }
            extv[j].exp = RDB_dup_expr(chtbp->var.extend.attrv[i].exp);
            if (extv[j].exp == NULL) {
                for (i = 0; i < chtbp->var.extend.attrc; i++) {
                    free(extv[i].name);
                    if (extv[i].exp != NULL)
                        RDB_drop_expr(extv[i].exp);
                }
                free(extv);
                return RDB_NO_MEMORY;
            }
            j++;
        }
    }

    /* Destroy virtual attributes of child */
    for (i = 0; i < chtbp->var.extend.attrc; i++) {
        free(chtbp->var.extend.attrv[i].name);
        RDB_drop_expr(chtbp->var.extend.attrv[i].exp);
    }
    free(chtbp->var.extend.attrv);

    if (j == 0) {
        /* Extend would be empty - remove child */
        free(extv);
        tbp->var.project.tbp = chtbp->var.extend.tbp;
        _RDB_free_table(chtbp);
    } else {
        int nattrc;
        int extc = j;

        /*
         * Swap parent and child
         */
        tbp->kind = RDB_TB_EXTEND;
        tbp->var.extend.tbp = chtbp;
        tbp->var.extend.attrc = extc;
        tbp->var.extend.attrv = extv;

        chtbp->kind = RDB_TB_PROJECT;
        chtbp->var.project.tbp = htbp;
        chtbp->var.project.indexp = NULL;

        attrnamev = malloc(chtbp->typ->var.basetyp->var.tuple.attrc
                * sizeof(char *));
        if (attrnamev == NULL)
            return RDB_NO_MEMORY;

        /*
         * Get those attributes of the grandchild who appear in the parent
         * or are referred to by one of the expressions
         */
        nattrc = 0;
        for (i = 0; i < htbp->typ->var.basetyp->var.tuple.attrc; i++) {
            char *attrname = htbp->typ->var.basetyp->var.tuple.attrv[i].name;

            if (_RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname) != NULL) {
                attrnamev[nattrc++] = attrname;
            } else {
                for(j = 0;
                    j < extc && !_RDB_expr_refers_attr(extv[j].exp, attrname);
                    j++);
                if (j < extc) {
                    attrnamev[nattrc++] = attrname;
                }
            }
        }

        ret = RDB_project_relation_type(htbp->typ, nattrc, attrnamev, &newtyp);
        free(attrnamev);
        if (ret != RDB_OK)
            return ret;
        RDB_drop_type(chtbp->typ, NULL);
        chtbp->typ = newtyp;
    }

    return RDB_OK;
}

/*
 * Transforms PROJECT(SELECT) to SELECT(PROJECT) or PROJECT(SELECT(PROJECT))
 */
static int
swap_project_select(RDB_table *tbp, RDB_table *chtbp)
{
    int i;
    int ret;
    int attrc;
    char **attrv = malloc(sizeof(char *)
            * chtbp->var.select.tbp->typ->var.basetyp->var.tuple.attrc);
    if (attrv == NULL)
        return RDB_NO_MEMORY;

    /*
     * Add attributes from parent
     */
    attrc = tbp->typ->var.basetyp->var.tuple.attrc;
    for (i = 0; i < attrc; i++) {
        attrv[i] = tbp->typ->var.basetyp->var.tuple.attrv[i].name;
    }

    /*
     * Add attributes from child which are not attributes of the parent,
     * but are referred to by the select condition
     */
    for (i = 0; i < chtbp->typ->var.basetyp->var.tuple.attrc; i++) {
        char *attrname = chtbp->typ->var.basetyp->var.tuple.attrv[i].name;

        if (_RDB_tuple_type_attr(tbp->typ->var.basetyp, attrname) == NULL
                && _RDB_expr_refers_attr(chtbp->var.select.exp, attrname)) {
            attrv[attrc++] = attrname;
        }
    }
    if (attrc > tbp->typ->var.basetyp->var.tuple.attrc) {
        RDB_table *ntbp;

        /*
         * Add project
         */
        ret = RDB_project(chtbp->var.select.tbp, attrc, attrv, &ntbp);
        free(attrv);
        if (ret != RDB_OK) {
            return ret;
        }
        chtbp->var.select.tbp = ntbp;
    } else {
        RDB_bool keyloss;

        /*
         * Swap SELECT and project
         */
        free(attrv);        
        keyloss = tbp->var.project.keyloss;

        ret = copy_type(chtbp, tbp);
        if (ret != RDB_OK)
            return ret;

        del_keys(tbp);
        tbp->kind = RDB_TB_SELECT;
        tbp->var.select.exp = chtbp->var.select.exp;
        tbp->var.select.objpc = 0;
        
        del_keys(chtbp);
        chtbp->kind = RDB_TB_PROJECT;
        chtbp->var.project.indexp = NULL;
        chtbp->var.project.keyloss = keyloss;
    }

    return RDB_OK;
}

static int
transform_project(RDB_table *tbp)
{
    int ret;
    RDB_table *chtbp;

    do {
        chtbp = tbp->var.project.tbp;
        ret = _RDB_transform(chtbp);
        if (ret != RDB_OK)
            return ret;

        switch (chtbp->kind) {
            case RDB_TB_REAL:
                return RDB_OK;
            case RDB_TB_PROJECT:
                /* Merge projects by eliminating the child */
                tbp->var.project.tbp = chtbp->var.project.tbp;
                tbp->var.project.keyloss = (RDB_bool) (tbp->var.project.keyloss
                        || chtbp->var.project.keyloss);
                _RDB_free_table(chtbp);
                break;
            case RDB_TB_UNION:
                ret = swap_project_union(tbp, chtbp);
                if (ret != RDB_OK)
                    return ret;
                tbp = chtbp;
                break;
            case RDB_TB_RENAME:
                ret = swap_project_rename(tbp);
                if (ret != RDB_OK)
                    return ret;
                if (tbp->kind == RDB_TB_PROJECT) {
                    /* Rename has been removed */
                    chtbp = tbp->var.project.tbp;
                } else {
                    /* Rename and project have been swapped */
                    tbp = chtbp;
                }
                break;
            case RDB_TB_EXTEND:
                ret = swap_project_extend(tbp);
                if (ret != RDB_OK)
                    return ret;
                if (tbp->kind == RDB_TB_PROJECT) {
                    /* Extend has been removed */
                    chtbp = tbp->var.project.tbp;
                } else {
                    /* Extend and project have been swapped */
                    tbp = chtbp;
                }
                break;
            case RDB_TB_SELECT:
                ret = swap_project_select(tbp, chtbp);
                if (ret != RDB_OK)
                    return ret;
                if (chtbp->kind == RDB_TB_SELECT) {
                    return transform_select(chtbp);
                }
                tbp = chtbp;
                break;
            case RDB_TB_MINUS:
            case RDB_TB_INTERSECT:
            case RDB_TB_JOIN:
            case RDB_TB_SUMMARIZE:
            case RDB_TB_WRAP:
            case RDB_TB_UNWRAP:
            case RDB_TB_SDIVIDE:
            case RDB_TB_GROUP:
            case RDB_TB_UNGROUP:
                return _RDB_transform(chtbp);
        }
    } while (tbp->kind == RDB_TB_PROJECT);
    return RDB_OK;
}

int
_RDB_transform(RDB_table *tbp)
{
    int ret;

    switch (tbp->kind) {
        case RDB_TB_REAL:
            break;
        case RDB_TB_MINUS:
            ret = _RDB_transform(tbp->var.minus.tb1p);
            if (ret != RDB_OK)
                return ret;
            ret = _RDB_transform(tbp->var.minus.tb2p);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_UNION:
            ret = _RDB_transform(tbp->var._union.tb1p);
            if (ret != RDB_OK)
                return ret;
            ret = _RDB_transform(tbp->var._union.tb2p);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_INTERSECT:
            ret = _RDB_transform(tbp->var.intersect.tb1p);
            if (ret != RDB_OK)
                return ret;
            ret = _RDB_transform(tbp->var.intersect.tb2p);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_SELECT:
            ret = transform_select(tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_JOIN:
            ret = _RDB_transform(tbp->var.join.tb1p);
            if (ret != RDB_OK)
                return ret;
            ret = _RDB_transform(tbp->var.join.tb2p);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_EXTEND:
            ret = _RDB_transform(tbp->var.extend.tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_PROJECT:
            ret = transform_project(tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_SUMMARIZE:
            ret = _RDB_transform(tbp->var.summarize.tb1p);
            if (ret != RDB_OK)
                return ret;
            ret = _RDB_transform(tbp->var.summarize.tb2p);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_RENAME:
            ret = _RDB_transform(tbp->var.rename.tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_WRAP:
            ret = _RDB_transform(tbp->var.wrap.tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_UNWRAP:
            ret = _RDB_transform(tbp->var.unwrap.tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_GROUP:
            ret = _RDB_transform(tbp->var.group.tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_UNGROUP:
            ret = _RDB_transform(tbp->var.ungroup.tbp);
            if (ret != RDB_OK)
                return ret;
            break;
        case RDB_TB_SDIVIDE:
            ret = _RDB_transform(tbp->var.sdivide.tb1p);
            if (ret != RDB_OK)
                return ret;
            ret = _RDB_transform(tbp->var.sdivide.tb2p);
            if (ret != RDB_OK)
                return ret;
            ret = _RDB_transform(tbp->var.sdivide.tb3p);
            if (ret != RDB_OK)
                return ret;
            break;
    }

    return RDB_OK;
}
