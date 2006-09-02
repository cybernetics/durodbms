/*
 * $Id$
 *
 * Copyright (C) 2004-2006 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "rdb.h"
#include "internal.h"
#include <gen/strfns.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int
alter_op(RDB_expression *exp, const char *name, int argc, RDB_exec_context *ecp)
{
    RDB_expression **argv;
    char *newname;
    
    newname = realloc(exp->var.op.name, strlen(name) + 1);
    if (newname == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }
    strcpy(newname, name);
    exp->var.op.name = newname;

    if (argc != exp->var.op.argc) {
        argv = realloc(exp->var.op.argv, sizeof (RDB_expression *) * argc);
        if (argv == NULL) {
            RDB_raise_no_memory(ecp);
            return RDB_ERROR;
        }
        exp->var.op.argc = argc;
        exp->var.op.argv = argv;
    }

    return RDB_OK;
}

/* Only for binary operators */
static int
eliminate_child (RDB_expression *exp, const char *name, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_expression *hexp = exp->var.op.argv[0];
    int ret = alter_op(exp, name, 2, ecp);
    if (ret != RDB_OK)
        return ret;

    exp->var.op.argv[0] = hexp->var.op.argv[0];
    exp->var.op.argv[1] = hexp->var.op.argv[1];
    free(hexp->var.op.name);
    free(hexp->var.op.argv);
    free(hexp);
    ret = _RDB_transform(exp->var.op.argv[0], ecp, txp);
    if (ret != RDB_OK)
        return ret;
    return _RDB_transform(exp->var.op.argv[1], ecp, txp);
}

/* Try to eliminate NOT operator */
static int
eliminate_not(RDB_expression *exp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    int ret;
    RDB_expression *hexp;

    if (exp->var.op.argv[0]->kind != RDB_EX_RO_OP)
        return RDB_OK;

    if (strcmp(exp->var.op.argv[0]->var.op.name, "AND") == 0) {
        hexp = RDB_ro_op("NOT", 1, ecp);
        if (hexp == NULL)
            return RDB_ERROR;
        RDB_add_arg(hexp, exp->var.op.argv[0]->var.op.argv[1]);
        ret = alter_op(exp, "OR", 2, ecp);
        if (ret != RDB_OK)
            return ret;
        exp->var.op.argv[1] = hexp;

        ret = alter_op(exp->var.op.argv[0], "NOT", 1, ecp);
        if (ret != RDB_OK)
            return ret;

        ret = eliminate_not(exp->var.op.argv[0], ecp, txp);
        if (ret != RDB_OK)
            return ret;
        return eliminate_not(exp->var.op.argv[1], ecp, txp);
    }
    if (strcmp(exp->var.op.argv[0]->var.op.name, "OR") == 0) {
        hexp = RDB_ro_op("NOT", 1, ecp);
        if (hexp == NULL)
            return RDB_ERROR;
        RDB_add_arg(hexp, exp->var.op.argv[0]->var.op.argv[1]);
        ret = alter_op(exp, "AND", 2, ecp);
        if (ret != RDB_OK)
            return ret;
        exp->var.op.argv[1] = hexp;

        ret = alter_op(exp->var.op.argv[0], "NOT", 1, ecp);
        if (ret != RDB_OK)
            return ret;

        ret = eliminate_not(exp->var.op.argv[0], ecp, txp);
        if (ret != RDB_OK)
            return ret;
        return eliminate_not(exp->var.op.argv[1], ecp, txp);
    }
    if (strcmp(exp->var.op.argv[0]->var.op.name, "=") == 0)
        return eliminate_child(exp, "<>", ecp, txp);
    if (strcmp(exp->var.op.argv[0]->var.op.name, "<>") == 0)
        return eliminate_child(exp, "=", ecp, txp);
    if (strcmp(exp->var.op.argv[0]->var.op.name, "<") == 0)
        return eliminate_child(exp, ">=", ecp, txp);
    if (strcmp(exp->var.op.argv[0]->var.op.name, ">") == 0)
        return eliminate_child(exp, "<=", ecp, txp);
    if (strcmp(exp->var.op.argv[0]->var.op.name, "<=") == 0)
        return eliminate_child(exp, ">", ecp, txp);
    if (strcmp(exp->var.op.argv[0]->var.op.name, ">=") == 0)
        return eliminate_child(exp, "<", ecp, txp);
    if (strcmp(exp->var.op.argv[0]->var.op.name, "NOT") == 0) {
        hexp = exp->var.op.argv[0];
        memcpy(exp, hexp->var.op.argv[0], sizeof (RDB_expression));
        free(hexp->var.op.argv[0]->var.op.name);
        free(hexp->var.op.argv[0]->var.op.argv);
        free(hexp->var.op.argv[0]);
        free(hexp->var.op.name);
        free(hexp->var.op.argv);
        free(hexp);
        return _RDB_transform(exp, ecp, txp);
    }

    return _RDB_transform(exp->var.op.argv[0], ecp, txp);
}

static RDB_bool
exprs_compl(const RDB_expression *ex1p, const RDB_expression *ex2p,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_bool *resp)
{
    int ret;
    *resp = RDB_FALSE;

    if (strcmp(ex2p->var.op.name, "NOT") == 0) {
        ret = _RDB_expr_equals(ex1p, ex2p->var.op.argv[0], ecp, txp, resp);
        if (ret != RDB_OK)
            return ret;
        if (*resp)
            return RDB_OK;            
    }

    if (strcmp(ex1p->var.op.name, "NOT") == 0) {
        ret = _RDB_expr_equals(ex1p->var.op.argv[0], ex2p, ecp, txp, resp);
        if (ret != RDB_OK)
            return ret;
    }
    return RDB_OK; 
}

static int
transform_union(RDB_expression *exp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (exp->var.op.argv[0]->kind == RDB_EX_RO_OP
            && strcmp(exp->var.op.argv[0]->var.op.name, "PROJECT") == 0
            && exp->var.op.argv[1]->kind == RDB_EX_RO_OP
            && strcmp(exp->var.op.argv[1]->var.op.name, "PROJECT") == 0) {
        RDB_expression *sex1p = exp->var.op.argv[0]->var.op.argv[0];
        RDB_expression *sex2p = exp->var.op.argv[1]->var.op.argv[0];
        if (sex1p->kind == RDB_EX_RO_OP
                && strcmp(sex1p->var.op.name, "WHERE") == 0
                && sex2p->kind == RDB_EX_RO_OP
                && strcmp(sex2p->var.op.name, "WHERE") == 0) {
            RDB_bool merge;
            int i;

            if (exprs_compl(sex1p->var.op.argv[1], sex2p->var.op.argv[1],
                    ecp, txp, &merge) != RDB_OK)
                return RDB_ERROR;
            if (merge) {
                /*
                 * Replace (T WHERE C) { ... } UNION (T WHERE NOT C) { ... }
                 * by T { ... }
                 */

                if (RDB_drop_expr(exp->var.op.argv[1], ecp) != RDB_OK)
                    return RDB_ERROR;

                free(exp->var.op.name);
                exp->var.op.name = exp->var.op.argv[0]->var.op.name;
                exp->var.op.argc = exp->var.op.argv[0]->var.op.argc;
                exp->var.op.argv[0] = exp->var.op.argv[0]->var.op.argv[0];
                for (i = 1; i < exp->var.op.argc; i++)
                    exp->var.op.argv[i] = exp->var.op.argv[0]->var.op.argv[i];

                /* !! free expressions ... */

                if (_RDB_transform(exp, ecp, txp) != RDB_OK)
                    return RDB_ERROR;
            }
        }
    }
    return RDB_OK;
}

static int
transform_where(RDB_expression *exp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    char *hname;
    int hargc;
    RDB_expression **hargv;
    RDB_expression *hexp;

    if (_RDB_transform(exp->var.op.argv[0], ecp, txp) != RDB_OK)
        return RDB_ERROR;

    for(;;) {
        RDB_expression *chexp = exp->var.op.argv[0];
        if (chexp->kind != RDB_EX_RO_OP)
            return RDB_OK;

        if (strcmp(chexp->var.op.name, "WHERE") == 0) {
            /*
             * Merge WHERE expressions
             */
            RDB_expression *condp = RDB_ro_op("AND", 2, ecp);
            if (condp == NULL)
                return RDB_ERROR;
            RDB_add_arg(condp, exp->var.op.argv[1]);
            RDB_add_arg(condp, chexp->var.op.argv[1]);
            
            exp->var.op.argv[0] = chexp->var.op.argv[0];
            exp->var.op.argv[1] = condp;
            
            free(chexp->var.op.name);
            free(chexp->var.op.argv);
            free(chexp);
        } else if (strcmp(chexp->var.op.name, "MINUS") == 0
                || strcmp(chexp->var.op.name, "SEMIMINUS") == 0
                || strcmp(chexp->var.op.name, "SEMIJOIN") == 0) {
            /*
             * WHERE(SEMIMINUS( -> SEMIMINUS(WHERE(
             * Same for MINUS, SEMIJOIN
             */
            if (_RDB_transform(chexp->var.op.argv[1], ecp, txp) != RDB_OK)
                return RDB_ERROR;

            hname = exp->var.op.name;
            exp->var.op.name = chexp->var.op.name;
            chexp->var.op.name = hname;

            hexp = chexp->var.op.argv[1];
            chexp->var.op.argv[1] = exp->var.op.argv[1];
            exp->var.op.argv[1] = hexp;

            exp = chexp;
        } else if (strcmp(chexp->var.op.name, "UNION") == 0) {
            /*
             * WHERE(UNION( -> UNION(WHERE(
             */
            RDB_expression *wexp;
            RDB_expression *condp = RDB_dup_expr(exp->var.op.argv[1], ecp);
            if (condp == NULL)
                return RDB_ERROR;

            wexp = RDB_ro_op("WHERE", 2, ecp);
            if (wexp == NULL) {
                RDB_drop_expr(condp, ecp);
                return RDB_ERROR;
            }
            RDB_add_arg(wexp, exp->var.op.argv[0]->var.op.argv[1]);
            RDB_add_arg(wexp, condp);

            /* Swap operator names */
            hname = chexp->var.op.name;
            chexp->var.op.name = exp->var.op.name;
            exp->var.op.name = hname;

            chexp->var.op.argv[1] = exp->var.op.argv[1];
            exp->var.op.argv[1] = wexp;

            return _RDB_transform(chexp, ecp, txp);
        } else if (strcmp(chexp->var.op.name, "EXTEND") == 0) {
            /*
             * WHERE(EXTEND( -> EXTEND(WHERE(
             */
            if (_RDB_resolve_extend_expr(&exp->var.op.argv[1], chexp, ecp)
                    != RDB_OK)
                return RDB_ERROR;

            hname = chexp->var.op.name;
            chexp->var.op.name = exp->var.op.name;
            exp->var.op.name = hname;
            
            hargv = chexp->var.op.argv;
            hargc = chexp->var.op.argc;
            chexp->var.op.argv = exp->var.op.argv;
            chexp->var.op.argc = exp->var.op.argc;
            exp->var.op.argv = hargv;
            exp->var.op.argc = hargc;

            hexp = chexp->var.op.argv[0];
            chexp->var.op.argv[0] = exp->var.op.argv[0];
            exp->var.op.argv[0] = hexp;
            
            exp = chexp;
        } else if (strcmp(chexp->var.op.name, "RENAME") == 0) {
            /*
             * WHERE(RENAME( -> RENAME(WHERE(
             * Only if the variables in the where condition 
             * all appear in the RENAME table
             */
            if (RDB_expr_type(exp, NULL, ecp, txp) == NULL) {
                RDB_type *errtyp = RDB_obj_type(RDB_get_err(ecp));
                if (errtyp != &RDB_ATTRIBUTE_NOT_FOUND_ERROR)
                    return RDB_ERROR;
                RDB_clear_err(ecp);
                return RDB_OK;
            }
            if (_RDB_invrename_expr(exp->var.op.argv[1], chexp, ecp) != RDB_OK)
                return RDB_ERROR;

            hname = chexp->var.op.name;
            chexp->var.op.name = exp->var.op.name;
            exp->var.op.name = hname;
            
            hargv = chexp->var.op.argv;
            hargc = chexp->var.op.argc;
            chexp->var.op.argv = exp->var.op.argv;
            chexp->var.op.argc = exp->var.op.argc;
            exp->var.op.argv = hargv;
            exp->var.op.argc = hargc;

            hexp = chexp->var.op.argv[0];
            chexp->var.op.argv[0] = exp->var.op.argv[0];
            exp->var.op.argv[0] = hexp;
            
            exp = chexp;
        } else {
            return _RDB_transform(chexp, ecp, txp);
        }
    }
}

static int
swap_project_union(RDB_expression *exp, RDB_expression *chexp,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i;
    RDB_expression *nexp, *nargp;
    RDB_expression **hargv;
    char *hname;

    /*
     * Create new project table for child #2
     */
    nexp = RDB_ro_op("PROJECT", exp->var.op.argc, ecp);
    if (nexp == NULL) {
        return RDB_ERROR;
    }

    RDB_add_arg(nexp, chexp->var.op.argv[1]);

    for (i = 1; i < exp->var.op.argc; i++) {
        nargp = RDB_dup_expr(exp->var.op.argv[i], ecp);
        if (nargp == NULL) {
            RDB_drop_expr(nexp, ecp);
            return RDB_ERROR;
        }
        RDB_add_arg(nexp, nargp);
    }

    /*
     * Swap parent and child
     */

    hname = exp->var.op.name;
    exp->var.op.name = chexp->var.op.name;
    chexp->var.op.name = hname;

    hargv = chexp->var.op.argv;
    chexp->var.op.argc = exp->var.op.argc;
    chexp->var.op.argv = exp->var.op.argv;
    exp->var.op.argc = 2;
    exp->var.op.argv = hargv;

    /*
     * Add new child
     */

    chexp->var.op.argv[0] = exp->var.op.argv[0];
    exp->var.op.argv[0] = chexp;
    exp->var.op.argv[1] = nexp;

    return RDB_OK;
}

static RDB_expression *
proj_attr(const RDB_expression *exp, const char *attrname)
{
    int i;
    
    for (i = 1; i < exp->var.op.argc; i++) {
        if (strcmp(RDB_obj_string(&exp->var.op.argv[i]->var.obj),
                   attrname) == 0)
            return exp->var.op.argv[i];
    }
    return NULL;
}

static int
transform_project(RDB_expression *, RDB_exec_context *, RDB_transaction *);

/* Transforms PROJECT(RENAME) to RENAME(PROJECT) or PROJECT */
static int
swap_project_rename(RDB_expression *texp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    int si, di;
    int i, j;
    int argc;
    RDB_expression **argv;
    char *opname;
    RDB_expression *chtexp = texp->var.op.argv[0];

    /*
     * Alter parent by removing renamings which become obsolete
     * because of the projection
     */

    /* Remove renamings whose dest does not appear in the parent */
    di = 1;
    for (si = 1; si + 1 < chtexp->var.op.argc; si += 2) {
        if (proj_attr(texp, RDB_obj_string(&chtexp->var.op.argv[si + 1]->var.obj))
                == NULL) {
            RDB_drop_expr(chtexp->var.op.argv[si], ecp);
            RDB_drop_expr(chtexp->var.op.argv[si + 1], ecp);
        } else {
            if (di < si) {
                chtexp->var.op.argv[di] = chtexp->var.op.argv[si];
                chtexp->var.op.argv[di + 1] = chtexp->var.op.argv[si + 1];
            }
            di += 2;
        }
    }
    if (di < si) {
        chtexp->var.op.argc = di;
        if (di > 1) {
            chtexp->var.op.argv = realloc(chtexp->var.op.argv,
                    di * sizeof(RDB_expression *));
        } else {
            /*
             * Remove child
             */
            texp->var.op.argv[0] = chtexp->var.op.argv[0];
            free(chtexp->var.op.name);
            for (i = 1; i < chtexp->var.op.argc; i++)
                RDB_drop_expr(chtexp->var.op.argv[i], ecp);
            free(chtexp->var.op.argv);
            free(chtexp);

            return transform_project(texp, ecp, txp);
        }
    }

    /*
     * Swap parent and child
     */

    /* Change project attributes */
    for (i = 1; i < texp->var.op.argc; i++) {
        j = 1;
        while (j + 1 < chtexp->var.op.argc
                && strcmp(RDB_obj_string(&chtexp->var.op.argv[j + 1]->var.obj),
                        RDB_obj_string(&texp->var.op.argv[i]->var.obj)) != 0) {
            j += 2;
        }
        if (j + 1 < chtexp->var.op.argc) {
            if (RDB_string_to_obj(&texp->var.op.argv[i]->var.obj,
                    RDB_obj_string(&chtexp->var.op.argv[j]->var.obj),
                    ecp) != RDB_OK) {
                return RDB_ERROR;
            }
        }
    }

    argc = texp->var.op.argc;
    argv = texp->var.op.argv;
    opname = texp->var.op.name;

    texp->var.op.argc = chtexp->var.op.argc;
    texp->var.op.argv = chtexp->var.op.argv;
    texp->var.op.name = chtexp->var.op.name;
    chtexp->var.op.argc = argc;
    chtexp->var.op.argv = argv;
    chtexp->var.op.name = opname;

    chtexp->var.op.argv[0] = texp->var.op.argv[0];
    texp->var.op.argv[0] = chtexp;

    return transform_project(chtexp, ecp, txp); 
}

/* Transforms PROJECT(EXTEND) to EXTEND(PROJECT) or PROJECT */
static int
swap_project_extend(RDB_expression *exp, RDB_exec_context *ecp)
{
    int i;
    int expc;
    RDB_expression **expv;
    RDB_expression *chexp = exp->var.op.argv[0];

    /*
     * Remove EXTEND-Attributes which are 'projected away'
     */

    expv = malloc(sizeof (RDB_expression *) * chexp->var.op.argc);
    if (expv == NULL) {
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }

    expv[0] = chexp->var.op.argv[0];
    expc = 1;
    for (i = 1; i < chexp->var.op.argc; i += 2) {
        if (proj_attr(exp, RDB_obj_string(&chexp->var.op.argv[i + 1]->var.obj))
                != NULL) {
            expv[expc] = chexp->var.op.argv[i];
            expv[expc + 1] = chexp->var.op.argv[i + 1];
            expc += 2;
        } else {
            RDB_drop_expr(chexp->var.op.argv[i], ecp);
            RDB_drop_expr(chexp->var.op.argv[i + 1], ecp);
        }
    }

    free(chexp->var.op.argv);
    chexp->var.op.argv = expv;
    chexp->var.op.argc = expc;

    /* !! swap parent and child - handle expc == 0 */

    return RDB_OK;
}

/*
 * Transforms PROJECT(SELECT) to SELECT(PROJECT) or PROJECT(SELECT(PROJECT))
 */
static int
swap_project_where(RDB_expression *exp, RDB_expression *chexp,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i;
    int attrc;
    char **attrv;
    RDB_type *chtyp = RDB_expr_type(chexp, NULL, ecp, txp);
    if (chtyp == NULL)
        return RDB_ERROR;

    attrv = malloc(sizeof(char *) * chtyp->var.basetyp->var.tuple.attrc);
    if (attrv == NULL) {
        RDB_drop_type(chtyp, ecp, NULL);
        RDB_raise_no_memory(ecp);
        return RDB_ERROR;
    }

    /*
     * Get attributes from parent
     */
    attrc = exp->var.op.argc - 1;
    for (i = 0; i < attrc; i++) {
        attrv[i] = RDB_obj_string(&exp->var.op.argv[i + 1]->var.obj);
    }

    /*
     * Add attributes from child which are not attributes of the parent,
     * but are referred to by the select condition
     */
    for (i = 0; i < chtyp->var.basetyp->var.tuple.attrc; i++) {
        char *attrname = chtyp->var.basetyp->var.tuple.attrv[i].name;

        if (proj_attr(exp, attrname) == NULL
                && _RDB_expr_refers_attr(chexp, attrname)) {
            attrv[attrc++] = attrname;
        }
    }
    if (attrc > exp->var.op.argc - 1) {
        /*
         * Add project
         */
        RDB_expression *argp;
        RDB_expression *nexp = RDB_ro_op("PROJECT", attrc + 1, ecp);
        if (nexp == NULL)
            return RDB_ERROR;

        RDB_add_arg(nexp, chexp->var.op.argv[0]);
        for (i = 0; i < attrc; i++) {
            argp = RDB_string_to_expr(attrv[i], ecp);
            if (argp == NULL) {
                RDB_drop_expr(nexp, ecp);
                free(attrv);
                return RDB_ERROR;
            }
            RDB_add_arg(nexp, argp);
        }
        free(attrv);
        chexp->var.op.argv[0] = nexp;
    } else {
        char *hname;
        int hargc;
        RDB_expression **hargv;

        free(attrv);

        /*
         * Swap WHERE and project
         */
        hname = exp->var.op.name;
        exp->var.op.name = chexp->var.op.name;
        chexp->var.op.name = hname;

        hargc = exp->var.op.argc;
        hargv = exp->var.op.argv;
        exp->var.op.argc = chexp->var.op.argc;
        exp->var.op.argv = chexp->var.op.argv;
        chexp->var.op.argc = hargc;
        chexp->var.op.argv = hargv;
         
        chexp->var.op.argv[0] = exp->var.op.argv[0];
        exp->var.op.argv[0] = chexp;        
    }

    return RDB_OK;
}

static int
transform_project(RDB_expression *exp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
    RDB_expression *chexp;
    int i;

    do {
        chexp = exp->var.op.argv[0];
        if (_RDB_transform(chexp, ecp, txp) != RDB_OK)
            return RDB_ERROR;

        if (chexp->kind != RDB_EX_RO_OP)
            return RDB_OK;

        if (strcmp(chexp->var.op.name, "PROJECT") == 0) {
            /* Merge projects by eliminating the child */
            exp->var.op.argv[0] = chexp->var.op.argv[0];
            for (i = 1; i < chexp->var.op.argc; i++) {
                if (RDB_drop_expr(chexp->var.op.argv[i], ecp) != RDB_OK)
                    return RDB_ERROR;
            }
            free(chexp->var.op.name);
            free(chexp->var.op.argv);
            free(chexp);
        } else if (strcmp(chexp->var.op.name, "UNION") == 0) {
            if (swap_project_union(exp, chexp, ecp, txp) != RDB_OK)
                return RDB_ERROR;
        } else if (strcmp(chexp->var.op.name, "WHERE") == 0) {
            if (swap_project_where(exp, chexp, ecp, txp) != RDB_OK)
                return RDB_ERROR;
            if (strcmp(chexp->var.op.name, "WHERE") == 0) {
                return transform_where(chexp, ecp, txp);
            }
            exp = chexp;
        } else if (strcmp(chexp->var.op.name, "RENAME") == 0) {
            if (swap_project_rename(exp, ecp, txp) != RDB_OK)
                return RDB_ERROR;
        } else if (strcmp(chexp->var.op.name, "EXTEND") == 0) {
            if (swap_project_extend(exp, ecp) != RDB_OK)
                return RDB_ERROR;
            if (strcmp(exp->var.op.name, "PROJECT") != 0) {
                /* Extend and project have been swapped */
                exp = chexp;
            } else {
                return _RDB_transform(chexp, ecp, txp);
            }
        } else {
            return _RDB_transform(chexp, ecp, txp);
        }
    } while (exp->kind == RDB_EX_RO_OP
            && strcmp(exp->var.op.name, "PROJECT") == 0);
    return RDB_OK;
}

static int
transform_is_empty(RDB_expression *exp, RDB_exec_context *ecp,
        RDB_transaction *txp)
{
   RDB_expression *chexp = exp->var.op.argv[0];
    
   if (_RDB_transform(chexp, ecp, txp) != RDB_OK)
       return RDB_ERROR;

    /* Add projection, if not already present */
    if (chexp->kind != RDB_EX_RO_OP || strcmp(chexp->var.op.name, "PROJECT") != 0
            || chexp->var.op.argc > 1) {
        RDB_expression *pexp = RDB_ro_op("PROJECT", 1, ecp);
        if (pexp == NULL)
            return RDB_ERROR;

        RDB_add_arg(pexp, chexp);

        exp->var.op.argv[0] = pexp;

        return transform_project(pexp, ecp, txp);
    }

    return RDB_OK;
}

int
_RDB_transform(RDB_expression *exp, RDB_exec_context *ecp, RDB_transaction *txp)
{
    int i;

    if (exp->kind != RDB_EX_RO_OP)
        return RDB_OK;

    if (strcmp(exp->var.op.name, "NOT") == 0) {
        return eliminate_not(exp, ecp, txp);
    }
    for (i = 0; i < exp->var.op.argc; i++) {
        if (_RDB_transform(exp->var.op.argv[i], ecp, txp) != RDB_OK)
            return RDB_ERROR;
    }
    if (strcmp(exp->var.op.name, "UNION") == 0) {
        return transform_union(exp, ecp, txp);
    }
    if (strcmp(exp->var.op.name, "PROJECT") == 0) {
        return transform_project(exp, ecp, txp);
    }
/* !!
    if (strcmp(exp->var.op.name, "REMOVE") == 0) {
        return transform_remove(exp, ecp, txp);
    }
*/
    if (strcmp(exp->var.op.name, "WHERE") == 0) {
        return transform_where(exp, ecp, txp);
    }
    if (strcmp(exp->var.op.name, "IS_EMPTY") == 0) {
        return transform_is_empty(exp, ecp, txp);
    }

    return RDB_OK;
}
