/*
 * Copyright (C) 2003 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

/* $Id$ */

#include "rdb.h"
#include "internal.h"
#include "catalog.h"
#include <gen/strfns.h>
#include <string.h>
#include <regex.h>

RDB_bool
RDB_expr_is_const(const RDB_expression *exp)
{
    return (RDB_bool) exp->kind == RDB_EX_OBJ;
}

RDB_type *
RDB_expr_type(const RDB_expression *exp, const RDB_type *tuptyp)
{
    switch (exp->kind) {
        case RDB_EX_OBJ:
            return exp->var.obj.typ;
        case RDB_EX_ATTR:
        {
            RDB_attr *attrp = _RDB_tuple_type_attr(
                    tuptyp, exp->var.attr.name);
            return attrp != NULL ? attrp->typ : NULL;
        }
        case RDB_EX_NOT:
        case RDB_EX_EQ:
        case RDB_EX_NEQ:
        case RDB_EX_LT:
        case RDB_EX_GT:
        case RDB_EX_LET:
        case RDB_EX_GET:
        case RDB_EX_AND:
        case RDB_EX_OR:
        case RDB_EX_REGMATCH:
        case RDB_EX_IS_EMPTY:
            return &RDB_BOOLEAN;
        case RDB_EX_ADD:
        case RDB_EX_SUBTRACT:
        case RDB_EX_NEGATE:
        case RDB_EX_MULTIPLY:
        case RDB_EX_DIVIDE:
            return RDB_expr_type(exp->var.op.arg1, tuptyp);
        case RDB_EX_STRLEN:
            return &RDB_INTEGER;
        case RDB_EX_CONCAT:
            return &RDB_STRING;
        case RDB_EX_TUPLE_ATTR:
            return RDB_type_attr_type(RDB_expr_type(exp->var.op.arg1, tuptyp),
                    exp->var.op.name);
        case RDB_EX_GET_COMP:
            return _RDB_get_icomp(RDB_expr_type(exp->var.op.arg1, tuptyp),
                    exp->var.op.name)->typ;
        case RDB_SELECTOR:
            return exp->var.selector.typ;
        case RDB_EX_USER_OP:
            return exp->var.user_op.rtyp;
        case RDB_EX_AGGREGATE:
            switch (exp->var.op.op) {
                case RDB_COUNT:
                    return &RDB_INTEGER;
                case RDB_AVG:
                    return &RDB_RATIONAL;
                default:
                    return _RDB_tuple_type_attr(
                            exp->var.op.arg1->var.obj.var.tbp->typ->var.basetyp,
                            exp->var.op.name)->typ;
            }
    }
    abort();
}

RDB_expression *
RDB_bool_const(RDB_bool v)
{
    RDB_expression *exp = malloc(sizeof (RDB_expression));
    
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_OBJ;
    exp->var.obj.typ = &RDB_BOOLEAN;
    exp->var.obj.kind = RDB_OB_BOOL;
    exp->var.obj.var.bool_val = v;

    return exp;
}

RDB_expression *
RDB_int_const(RDB_int v)
{
    RDB_expression *exp = malloc(sizeof (RDB_expression));
    
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_OBJ;
    exp->var.obj.typ = &RDB_INTEGER;
    exp->var.obj.kind = RDB_OB_INT;
    exp->var.obj.var.int_val = v;

    return exp;
}

RDB_expression *
RDB_rational_const(RDB_rational v)
{
    RDB_expression *exp = malloc(sizeof (RDB_expression));
    
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_OBJ;
    exp->var.obj.typ = &RDB_RATIONAL;
    exp->var.obj.kind = RDB_OB_RATIONAL;
    exp->var.obj.var.rational_val = v;

    return exp;
}

RDB_expression *
RDB_string_const(const char *v)
{
    RDB_expression *exp = malloc(sizeof (RDB_expression));
    
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_OBJ;
    exp->var.obj.typ = &RDB_STRING;
    exp->var.obj.kind = RDB_OB_BIN;
    exp->var.obj.var.bin.datap = RDB_dup_str(v);
    exp->var.obj.var.bin.len = strlen(v)+1;

    return exp;
}

RDB_expression *
RDB_obj_const(const RDB_object *valp)
{
    int ret;
    RDB_expression *exp = malloc(sizeof (RDB_expression));
    
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_OBJ;
    RDB_init_obj(&exp->var.obj);
    ret = RDB_copy_obj(&exp->var.obj, valp);
    if (ret != RDB_OK) {
        free(exp);
        return NULL;
    }
    return exp;
}    

RDB_expression *
RDB_expr_attr(const char *attrname)
{
    RDB_expression *exp = malloc(sizeof (RDB_expression));
    
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_ATTR;
    exp->var.attr.name = RDB_dup_str(attrname);
    if (exp->var.attr.name == NULL) {
        free(exp);
        return NULL;
    }
    
    return exp;
}

RDB_expression *
_RDB_create_unexpr(RDB_expression *arg, enum _RDB_expr_kind kind)
{
    RDB_expression *exp;

    if (arg == NULL)
        return NULL;

    exp = malloc(sizeof (RDB_expression));
    if (exp == NULL)
        return NULL;
        
    exp->kind = kind;
    exp->var.op.arg1 = arg;

    return exp;
}

RDB_expression *
_RDB_create_binexpr(RDB_expression *arg1, RDB_expression *arg2, enum _RDB_expr_kind kind)
{
    RDB_expression *exp;

    if ((arg1 == NULL) || (arg2 == NULL))
        return NULL;

    exp = malloc(sizeof (RDB_expression));
    if (exp == NULL)
        return NULL;
        
    exp->kind = kind;
    exp->var.op.arg1 = arg1;
    exp->var.op.arg2 = arg2;

    return exp;
}

RDB_expression *
RDB_eq(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_EQ);
}

RDB_expression *
RDB_neq(RDB_expression *arg1, RDB_expression *arg2) {
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_NEQ);
}

RDB_expression *
RDB_lt(RDB_expression *arg1, RDB_expression *arg2) {
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_LT);
}

RDB_expression *
RDB_gt(RDB_expression *arg1, RDB_expression *arg2) {
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_GT);
}

RDB_expression *
RDB_let(RDB_expression *arg1, RDB_expression *arg2) {
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_LET);
}

RDB_expression *
RDB_get(RDB_expression *arg1, RDB_expression *arg2) {
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_GET);
}

RDB_expression *
RDB_and(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_AND);
}

RDB_expression *
RDB_or(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_OR);
}    

RDB_expression *
RDB_not(RDB_expression *arg)
{
    RDB_expression *exp;

    if (arg == NULL)
        return NULL;

    exp = malloc(sizeof (RDB_expression));  
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_NOT;
    exp->var.op.arg1 = arg;

    return exp;
}    

RDB_expression *
RDB_add(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_ADD);
}

RDB_expression *
RDB_subtract(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_SUBTRACT);
}

RDB_expression *
RDB_negate(RDB_expression *arg)
{
    return _RDB_create_unexpr(arg, RDB_EX_NEGATE);
}

RDB_expression *
RDB_multiply(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_MULTIPLY);
}

RDB_expression *
RDB_divide(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_DIVIDE);
}

RDB_expression *
RDB_strlen(RDB_expression *arg)
{
    return _RDB_create_unexpr(arg, RDB_EX_STRLEN);
}

RDB_expression *
RDB_regmatch(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_REGMATCH);
}

RDB_expression *
RDB_concat(RDB_expression *arg1, RDB_expression *arg2)
{
    return _RDB_create_binexpr(arg1, arg2, RDB_EX_CONCAT);
}

RDB_expression *
RDB_table_to_expr(RDB_table *tbp)
{
    RDB_expression *exp;

    exp = malloc(sizeof (RDB_expression));  
    if (exp == NULL)
        return NULL;
        
    exp->kind = RDB_EX_OBJ;
    RDB_init_obj(&exp->var.obj);
    RDB_table_to_obj(&exp->var.obj, tbp);
    
    return exp;
}

RDB_expression *
RDB_expr_is_empty(RDB_expression *arg1)
{
    return _RDB_create_unexpr(arg1, RDB_EX_IS_EMPTY);
}

RDB_expression *
RDB_expr_aggregate(RDB_expression *arg, RDB_aggregate_op op,
        const char *attrname)
{
    RDB_expression *exp = _RDB_create_unexpr(arg, RDB_EX_AGGREGATE);

    if (exp == NULL)
        return NULL;
    if (attrname != NULL) {
        exp->var.op.name = RDB_dup_str(attrname);
        if (exp->var.op.name == NULL) {
            free(exp);
            return NULL;
        }
    } else {
        exp->var.op.name = NULL;
    }
    exp->var.op.op = op;

    return exp;
}

RDB_expression *
RDB_expr_sum(RDB_expression *arg, const char *attrname)
{
    return RDB_expr_aggregate(arg, RDB_SUM, attrname);
}

RDB_expression *
RDB_expr_avg(RDB_expression *arg, const char *attrname)
{
    return RDB_expr_aggregate(arg, RDB_AVG, attrname);
}

RDB_expression *
RDB_expr_max(RDB_expression *arg, const char *attrname)
{
    return RDB_expr_aggregate(arg, RDB_MAX, attrname);
}

RDB_expression *
RDB_expr_min(RDB_expression *arg, const char *attrname)
{
    return RDB_expr_aggregate(arg, RDB_MIN, attrname);
}

RDB_expression *
RDB_expr_all(RDB_expression *arg, const char *attrname) {
    return RDB_expr_aggregate(arg, RDB_ALL, attrname);
}

RDB_expression *
RDB_expr_any(RDB_expression *arg, const char *attrname) {
    return RDB_expr_aggregate(arg, RDB_ANY, attrname);
}

RDB_expression *
RDB_expr_cardinality(RDB_expression *arg)
{
    return RDB_expr_aggregate(arg, RDB_COUNT, NULL);
}

RDB_expression *
RDB_tuple_attr(RDB_expression *arg, const char *attrname)
{
    RDB_expression *exp;

    exp = _RDB_create_unexpr(arg, RDB_EX_TUPLE_ATTR);
    if (exp == NULL)
        return NULL;

    exp->var.op.name = RDB_dup_str(attrname);
    if (exp->var.op.name == NULL) {
        RDB_drop_expr(exp);
        return NULL;
    }
    return exp;
}

RDB_expression *
RDB_expr_comp(RDB_expression *arg, const char *compname)
{
    RDB_expression *exp;

    exp = _RDB_create_unexpr(arg, RDB_EX_GET_COMP);
    if (exp == NULL)
        return NULL;

    exp->var.op.name = RDB_dup_str(compname);
    if (exp->var.op.name == NULL) {
        RDB_drop_expr(exp);
        return NULL;
    }
    return exp;
}

RDB_expression *
RDB_selector(RDB_type *typ, const char *repname, RDB_expression *argv[])
{
    RDB_expression *exp;
    RDB_ipossrep *prp = _RDB_get_possrep(typ, repname);
    int i;

    if (prp == NULL)
        return NULL;
    exp = malloc(sizeof (RDB_expression));
    if (exp == NULL)
        return NULL;   

    exp->kind = RDB_SELECTOR;
    exp->var.selector.typ = typ;
    exp->var.selector.argv = NULL;

    exp->var.selector.name = RDB_dup_str(repname);
    if (exp->var.selector.name == NULL)
        goto error;

    exp->var.selector.argv = malloc(prp->compc * sizeof(RDB_expression *));
    if (exp->var.selector.argv == NULL)
        goto error;

    for (i = 0; i < prp->compc; i++)
        exp->var.selector.argv[i] = argv[i];

    return exp;
error:
    free(exp->var.selector.name);
    free(exp->var.selector.argv);
    free(exp);

    return NULL;    
}

int
RDB_user_op(const char *opname, int argc, RDB_expression *argv[],
       RDB_transaction *txp, RDB_expression **expp)
{
    RDB_expression *exp;
    int i;
    int ret;

    exp = malloc(sizeof (RDB_expression));
    if (exp == NULL)
        return RDB_NO_MEMORY;

    exp->kind = RDB_EX_USER_OP;
    
    exp->var.user_op.name = RDB_dup_str(opname);
    if (exp->var.user_op.name == NULL) {
        free(exp);
        return RDB_NO_MEMORY;
    }

    ret = _RDB_get_cat_rtype(opname, txp, &exp->var.user_op.rtyp);
    if (ret != RDB_OK) {
        free(exp);
        return ret;
    }

    exp->var.user_op.argc = argc;
    exp->var.user_op.argv = malloc(argc * sizeof(RDB_expression *));
    if (exp->var.user_op.argv == NULL) {
        free(exp->var.user_op.name);
        free(exp);
        return RDB_NO_MEMORY;
    }

    for (i = 0; i < argc; i++)
        exp->var.user_op.argv[i] = argv[i];

    *expp = exp;
    return RDB_OK;
}

/* Destroy the expression and all subexpressions */
void 
RDB_drop_expr(RDB_expression *exp)
{
    switch (exp->kind) {
        case RDB_EX_EQ:
        case RDB_EX_NEQ:
        case RDB_EX_LT:
        case RDB_EX_GT:
        case RDB_EX_LET:
        case RDB_EX_GET:
        case RDB_EX_AND:
        case RDB_EX_OR:
        case RDB_EX_ADD:
        case RDB_EX_SUBTRACT:
        case RDB_EX_MULTIPLY:
        case RDB_EX_DIVIDE:
        case RDB_EX_REGMATCH:
        case RDB_EX_CONCAT:
            RDB_drop_expr(exp->var.op.arg2);
        case RDB_EX_NOT:
        case RDB_EX_NEGATE:
        case RDB_EX_IS_EMPTY:
        case RDB_EX_STRLEN:
            RDB_drop_expr(exp->var.op.arg1);
            break;
        case RDB_EX_TUPLE_ATTR:
        case RDB_EX_GET_COMP:
            free(exp->var.op.name);
            RDB_drop_expr(exp->var.op.arg1);
            break;
        case RDB_SELECTOR:
        {
            int i;
            int compc = _RDB_get_possrep(exp->var.selector.typ,
                    exp->var.selector.name)->compc;

            for (i = 0; i < compc; i++)
                RDB_drop_expr(exp->var.selector.argv[i]);
            free(exp->var.selector.argv);
            free(exp->var.selector.name);
            break;
        }
        case RDB_EX_USER_OP:
        {
            int i;

            for (i = 0; i < exp->var.user_op.argc; i++)
                RDB_drop_expr(exp->var.user_op.argv[i]);
            free(exp->var.user_op.argv);
            break;
        }
        case RDB_EX_AGGREGATE:
            free(exp->var.op.name);
            break;
        case RDB_EX_OBJ:
            RDB_destroy_obj(&exp->var.obj);
            break;
        case RDB_EX_ATTR:
            free(exp->var.attr.name);
            break;
    }
    free(exp);
}

static int
evaluate_arith(RDB_expression *exp, const RDB_object *tup, RDB_transaction *txp,
            RDB_object *valp, enum _RDB_expr_kind kind)
{
    int ret;
    RDB_object val1, val2;
    RDB_type *typ;

    RDB_init_obj(&val1);
    ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val1);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&val1);
        return ret;
    }

    RDB_init_obj(&val2);
    ret = RDB_evaluate(exp->var.op.arg2, tup, txp, &val2);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return ret;
    }

    typ = RDB_obj_type(&val1);
    if (!RDB_type_equals(typ, RDB_obj_type(&val2))) {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return RDB_TYPE_MISMATCH;
    }

    if (typ == &RDB_INTEGER) {
        RDB_destroy_obj(valp);
        RDB_init_obj(valp);
        _RDB_set_obj_type(valp, &RDB_INTEGER);

        switch (exp->kind) {
            case RDB_EX_ADD:
                valp->var.int_val = val1.var.int_val + val2.var.int_val;
                break;
            case RDB_EX_SUBTRACT:
                valp->var.int_val = val1.var.int_val - val2.var.int_val;
                break;
            case RDB_EX_NEGATE:
                valp->var.int_val = -val1.var.int_val;
                break;
            case RDB_EX_MULTIPLY:
                valp->var.int_val = val1.var.int_val * val2.var.int_val;
                break;
            case RDB_EX_DIVIDE:
                if (val2.var.int_val == 0) {
                    RDB_destroy_obj(&val1);
                    RDB_destroy_obj(&val2);
                    return RDB_INVALID_ARGUMENT;
                }
                valp->var.int_val = val1.var.int_val / val2.var.int_val;
                break;
           default: /* should never happen */
                return RDB_INVALID_ARGUMENT;
        }
    } else if (typ == &RDB_RATIONAL) {
        RDB_destroy_obj(valp);
        RDB_init_obj(valp);
        _RDB_set_obj_type(valp, &RDB_RATIONAL);
 
        switch (exp->kind) {
            case RDB_EX_ADD:
                valp->var.rational_val = val1.var.rational_val
                        + val2.var.rational_val;
                break;
            case RDB_EX_SUBTRACT:
                valp->var.rational_val = val1.var.rational_val
                        - val2.var.rational_val;
                break;
            case RDB_EX_NEGATE:
                valp->var.rational_val = -val1.var.rational_val;
                break;
            case RDB_EX_MULTIPLY:
                valp->var.rational_val = val1.var.rational_val
                        * val2.var.rational_val;
                break;
            case RDB_EX_DIVIDE:
                if (val2.var.rational_val == 0) {
                    RDB_destroy_obj(&val1);
                    RDB_destroy_obj(&val2);
                    return RDB_INVALID_ARGUMENT;
                }
                valp->var.rational_val = val1.var.rational_val
                        / val2.var.rational_val;
                break;
           default: /* should never happen */
                return RDB_INVALID_ARGUMENT;
        }
    } else {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return RDB_INVALID_ARGUMENT;
    }

    RDB_destroy_obj(&val1);
    RDB_destroy_obj(&val2);        
    return RDB_OK;
}

static int
evaluate_eq(RDB_expression *exp, const RDB_object *tup,
                  RDB_transaction *txp, RDB_bool *resp)
{
    int ret;
    RDB_object val1, val2;

    RDB_init_obj(&val1);
    RDB_init_obj(&val2);

    /*
     * Evaluate arguments
     */

    ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val1);
    if (ret != RDB_OK)
        goto cleanup;

    ret = RDB_evaluate(exp->var.op.arg2, tup, txp, &val2);
    if (ret != RDB_OK)
        goto cleanup;

    /*
     * Check types
     */

    if (!RDB_type_equals(RDB_obj_type(&val1), RDB_obj_type(&val2)))
        return RDB_TYPE_MISMATCH;

    /*
     * Compare values
     */

    *resp = RDB_obj_equals(&val1, &val2);

    ret = RDB_OK;

cleanup:
    RDB_destroy_obj(&val1);
    RDB_destroy_obj(&val2);

    return ret;
}

static int evaluate_selector(RDB_expression *exp, const RDB_object *tup,
        RDB_transaction *txp, RDB_object *valp)
{
    int ret;
    int i;
    int compc = _RDB_get_possrep(exp->var.selector.typ, exp->var.selector.name)->compc;
    RDB_object **valpv;
    RDB_object *valv = NULL;

    valpv = malloc(compc * sizeof (RDB_object *));
    if (valpv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }
    valv = malloc(compc * sizeof (RDB_object));
    if (valv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }
    for (i = 0; i < compc; i++) {
        valpv[i] = &valv[i];
        RDB_init_obj(&valv[i]);
        ret = RDB_evaluate(exp->var.selector.argv[i], tup, txp, &valv[i]);
        if (ret != RDB_OK)
            goto cleanup;
    }
    ret = RDB_select_obj(valp, exp->var.selector.typ, exp->var.selector.name, valpv);
cleanup:
    if (valv != NULL) {
        for (i = 0; i < compc; i++) {
            RDB_destroy_obj(&valv[i]);
        }
        free(valv);
    }
    free(valpv);
    return ret;
}

static int evaluate_user_op(RDB_expression *exp, const RDB_object *tup,
        RDB_transaction *txp, RDB_object *valp)
{
    int ret;
    int i;
    RDB_object **valpv;
    RDB_object *valv = NULL;
    int argc = exp->var.user_op.argc;

    valpv = malloc(argc * sizeof (RDB_object *));
    if (valpv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }
    valv = malloc(argc * sizeof (RDB_object));
    if (valv == NULL) {
        ret = RDB_NO_MEMORY;
        goto cleanup;
    }
    for (i = 0; i < argc; i++) {
        valpv[i] = &valv[i];
        RDB_init_obj(&valv[i]);
        ret = RDB_evaluate(exp->var.user_op.argv[i], tup, txp, &valv[i]);
        if (ret != RDB_OK)
            goto cleanup;
    }
    ret = RDB_call_ro_op(exp->var.user_op.name, argc, valpv, valp, txp);
cleanup:
    if (valv != NULL) {
        for (i = 0; i < argc; i++) {
            RDB_destroy_obj(&valv[i]);
        }
        free(valv);
    }
    free(valpv);
    return ret;
}

static int
evaluate_order(RDB_expression *exp, const RDB_object *tup, RDB_transaction *txp,
            RDB_object *valp, enum _RDB_expr_kind kind)
{
    int ret;
    RDB_object val1, val2;
    RDB_type *typ;

    RDB_init_obj(&val1);
    ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val1);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&val1);
        return ret;
    }

    RDB_init_obj(&val2);
    ret = RDB_evaluate(exp->var.op.arg2, tup, txp, &val2);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return ret;
    }

    typ = RDB_obj_type(&val1);
    if (!RDB_type_equals(typ, RDB_obj_type(&val2))) {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return RDB_TYPE_MISMATCH;
    }

    RDB_destroy_obj(valp);
    RDB_init_obj(valp);
    _RDB_set_obj_type(valp, &RDB_BOOLEAN);

    if (typ == &RDB_INTEGER) {
        switch (kind) {
            case RDB_EX_LT:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.int_val < val2.var.int_val);
                break;
            case RDB_EX_GT:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.int_val > val2.var.int_val);
                break;
            case RDB_EX_LET:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.int_val <= val2.var.int_val);
                break;
            case RDB_EX_GET:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.int_val >= val2.var.int_val);
                break;
            default: ;
        }
    } else if (typ == &RDB_RATIONAL) {
        switch (kind) {
            case RDB_EX_LT:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.rational_val < val2.var.rational_val);
                break;
            case RDB_EX_GT:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.rational_val > val2.var.rational_val);
                break;
            case RDB_EX_LET:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.rational_val <= val2.var.rational_val);
                break;
            case RDB_EX_GET:
                valp->var.bool_val = (RDB_bool)
                        (val1.var.rational_val >= val2.var.rational_val);
                break;
            default: /* should never happen */
                return RDB_INVALID_ARGUMENT;
        }
    } else if (typ == &RDB_STRING) {
        switch (kind) {
            case RDB_EX_LT:
                valp->var.bool_val = (RDB_bool)
                        (strcmp(val1.var.bin.datap, val2.var.bin.datap) < 0);
                break;
            case RDB_EX_GT:
                valp->var.bool_val = (RDB_bool)
                        (strcmp(val1.var.bin.datap, val2.var.bin.datap) > 0);
                break;
            case RDB_EX_LET:
                valp->var.bool_val = (RDB_bool)
                        (strcmp(val1.var.bin.datap, val2.var.bin.datap) <= 0);
                break;
            case RDB_EX_GET:
                valp->var.bool_val = (RDB_bool)
                        (strcmp(val1.var.bin.datap, val2.var.bin.datap) >= 0);
                break;
            default: /* should never happen */
                return RDB_INVALID_ARGUMENT;
        }    
    } else {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return RDB_INVALID_ARGUMENT;
    }

    RDB_destroy_obj(&val1);
    RDB_destroy_obj(&val2);        
    return RDB_OK;
}

static int
evaluate_logbin(RDB_expression *exp, const RDB_object *tup, RDB_transaction *txp,
            RDB_object *valp, enum _RDB_expr_kind kind)
{
    int ret;
    RDB_object val1, val2;

    RDB_init_obj(&val1);
    ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val1);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&val1);
        return ret;
    }
    if (RDB_obj_type(&val1) != &RDB_BOOLEAN) {
        RDB_destroy_obj(&val1);
        return ret;
    }

    RDB_init_obj(&val2);
    ret = RDB_evaluate(exp->var.op.arg2, tup, txp, &val2);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return ret;
    }
    if (RDB_obj_type(&val2) != &RDB_BOOLEAN) {
        RDB_destroy_obj(&val1);
        RDB_destroy_obj(&val2);
        return ret;
    }

    RDB_destroy_obj(valp);
    RDB_init_obj(valp);
    _RDB_set_obj_type(valp, &RDB_BOOLEAN);

    switch (kind) {
        case RDB_EX_AND:
            valp->var.bool_val = (RDB_bool)
                    (val1.var.bool_val && val2.var.bool_val);
            break;
        case RDB_EX_OR:
            valp->var.bool_val = (RDB_bool)
                    (val1.var.bool_val || val2.var.bool_val);
            break;
        default: ;
    }

    RDB_destroy_obj(&val1);
    RDB_destroy_obj(&val2);        
    return RDB_OK;
}

static int
aggregate(RDB_table *tbp, RDB_aggregate_op op, const char *attrname,
          RDB_transaction *txp, RDB_object *resultp)
{
    switch(op) {
        case RDB_COUNT:
        {
            int ret = RDB_cardinality(tbp, txp);

            if (ret < 0)
                return ret;
            _RDB_set_obj_type(resultp, &RDB_INTEGER);
            ret = resultp->var.int_val = ret;
            return RDB_OK;
        }
        case RDB_SUM:
            return RDB_sum(tbp, attrname, txp, resultp);
        case RDB_AVG:
            _RDB_set_obj_type(resultp, &RDB_RATIONAL);
            return RDB_avg(tbp, attrname, txp, &resultp->var.rational_val);
        case RDB_MAX:
            return RDB_max(tbp, attrname, txp, resultp);
        case RDB_MIN:
            return RDB_min(tbp, attrname, txp, resultp);
        case RDB_ALL:
            _RDB_set_obj_type(resultp, &RDB_BOOLEAN);
            return RDB_all(tbp, attrname, txp, &resultp->var.bool_val);
        case RDB_ANY:
            _RDB_set_obj_type(resultp, &RDB_BOOLEAN);
            return RDB_any(tbp, attrname, txp, &resultp->var.bool_val);
        default: ;
    }
    abort();
}

int
RDB_evaluate(RDB_expression *exp, const RDB_object *tup, RDB_transaction *txp,
            RDB_object *valp)
{
    int ret;

    switch (exp->kind) {
        case RDB_EX_TUPLE_ATTR:
        {
            int ret;
            RDB_object tpl;
            RDB_object *attrp;

            RDB_init_obj(&tpl);
            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &tpl);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl);
                return ret;
            }
            if (tpl.kind != RDB_OB_TUPLE) {
                RDB_destroy_obj(&tpl);
                return RDB_TYPE_MISMATCH;
            }
                
            attrp = RDB_tuple_get(&tpl, exp->var.op.name);
            if (attrp == NULL) {
                RDB_destroy_obj(&tpl);
                return RDB_INVALID_ARGUMENT;
            }
            ret = RDB_copy_obj(valp, attrp);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&tpl);
                return ret;
            }
            return RDB_destroy_obj(&tpl);
        }
        case RDB_EX_GET_COMP:
        {
            int ret;
            RDB_object obj;

            RDB_init_obj(&obj);
            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &obj);
            if (ret != RDB_OK) {
                 RDB_destroy_obj(&obj);
                 return ret;
            }
            ret = RDB_obj_comp(&obj, exp->var.op.name, valp);
            RDB_destroy_obj(&obj);
            return ret;
        }
        case RDB_SELECTOR:
            return evaluate_selector(exp, tup, txp, valp);
        case RDB_EX_USER_OP:
            return evaluate_user_op(exp, tup, txp, valp);
        case RDB_EX_ATTR:
        {
            if (tup == NULL)
                return RDB_INVALID_ARGUMENT;
        
            RDB_object *srcp = RDB_tuple_get(tup, exp->var.attr.name);

            if (srcp == NULL)
                return RDB_NOT_FOUND;

            return RDB_copy_obj(valp, srcp);
        }
        case RDB_EX_OBJ:
            return RDB_copy_obj(valp, &exp->var.obj);
        case RDB_EX_EQ:
            RDB_destroy_obj(valp);
            _RDB_set_obj_type(valp, &RDB_BOOLEAN);
            return evaluate_eq(exp, tup, txp, &valp->var.bool_val);
        case RDB_EX_NEQ:
        {
            RDB_bool b;
        
            ret = evaluate_eq(exp, tup, txp, &b);
            if (ret != RDB_OK)
               return ret;
            _RDB_set_obj_type(valp, &RDB_BOOLEAN);
            valp->var.bool_val = !b;
            return RDB_OK;
        }
        case RDB_EX_LT:
        case RDB_EX_GT:
        case RDB_EX_LET:
        case RDB_EX_GET:
            return evaluate_order(exp, tup, txp, valp, exp->kind);
        case RDB_EX_AND:
        case RDB_EX_OR:
            return evaluate_logbin(exp, tup, txp, valp, exp->kind);
        case RDB_EX_ADD:
        case RDB_EX_SUBTRACT:
        case RDB_EX_MULTIPLY:
        case RDB_EX_DIVIDE:
            return evaluate_arith(exp, tup, txp, valp, exp->kind);
        case RDB_EX_NEGATE:
        {
            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, valp);
            if (ret != RDB_OK)
                return ret;
            if (RDB_obj_type(valp) == &RDB_INTEGER)
                valp->var.int_val = -valp->var.int_val;
            else if (RDB_obj_type(valp) == &RDB_RATIONAL)
                valp->var.rational_val = -valp->var.rational_val;
            else
                return RDB_TYPE_MISMATCH;

            return RDB_OK;
        }
        case RDB_EX_NOT:
        {
            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, valp);
            if (ret != RDB_OK)
                return ret;
            if (RDB_obj_type(valp) != &RDB_BOOLEAN)
                return RDB_TYPE_MISMATCH;

            valp->var.bool_val = !valp->var.bool_val;
            return RDB_OK;
        }
        case RDB_EX_REGMATCH:
        {
            regex_t reg;
            RDB_object val1, val2;

            RDB_init_obj(&val1);
            RDB_init_obj(&val2);

            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val1);
            if (ret != RDB_OK)
                return ret;
            if (RDB_obj_type(&val1) != &RDB_STRING)
                return RDB_TYPE_MISMATCH;

            ret = RDB_evaluate(exp->var.op.arg2, tup, txp, &val2);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&val1);
                return ret;
            }
            if (RDB_obj_type(&val2) != &RDB_STRING) {
                RDB_destroy_obj(&val1);
                return RDB_TYPE_MISMATCH;
            }

            ret = regcomp(&reg, val2.var.bin.datap, REG_NOSUB);
            if (ret != 0) {
                RDB_destroy_obj(&val1);
                RDB_destroy_obj(&val2);
                return RDB_INVALID_ARGUMENT;
            }
            RDB_destroy_obj(valp);
            RDB_init_obj(valp);
            _RDB_set_obj_type(valp, &RDB_BOOLEAN);
            valp->var.bool_val = (RDB_bool)
                    (regexec(&reg, val1.var.bin.datap, 0, NULL, 0) == 0);
            regfree(&reg);
            RDB_destroy_obj(&val1);
            RDB_destroy_obj(&val2);
            return RDB_OK;
        }
        case RDB_EX_CONCAT:
        {
            int s1len;
            RDB_object val1, val2;

            RDB_init_obj(&val1);
            RDB_init_obj(&val2);

            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val1);
            if (ret != RDB_OK)
                return ret;
            if (RDB_obj_type(&val1) != &RDB_STRING)
                return RDB_TYPE_MISMATCH;

            ret = RDB_evaluate(exp->var.op.arg2, tup, txp, &val2);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&val1);
                return ret;
            }
            if (RDB_obj_type(&val2) != &RDB_STRING) {
                RDB_destroy_obj(&val1);
                return RDB_TYPE_MISMATCH;
            }

            RDB_destroy_obj(valp);
            RDB_init_obj(valp);
            _RDB_set_obj_type(valp, &RDB_STRING);
            s1len = strlen(val1.var.bin.datap);
            valp->var.bin.len = s1len + strlen(val2.var.bin.datap) + 1;
            valp->var.bin.datap = malloc(valp->var.bin.len);
            if (valp->var.bin.datap == NULL) {
                RDB_destroy_obj(&val1);
                RDB_destroy_obj(&val2);
                return RDB_NO_MEMORY;
            }
            strcpy(valp->var.bin.datap, val1.var.bin.datap);
            strcpy(((RDB_byte *)valp->var.bin.datap) + s1len,
                    val2.var.bin.datap);

            RDB_destroy_obj(&val1);
            RDB_destroy_obj(&val2);
            return RDB_OK;
        }
        case RDB_EX_STRLEN:
        {
            RDB_object val;

            RDB_init_obj(&val);

            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&val);
                return ret;
            }
            if (RDB_obj_type(&val) != &RDB_STRING) {
                RDB_destroy_obj(&val);
                return RDB_TYPE_MISMATCH;
            }

            RDB_destroy_obj(valp);
            RDB_init_obj(valp);
            _RDB_set_obj_type(valp, &RDB_INTEGER);
            valp->var.int_val = val.var.bin.len - 1;
            RDB_destroy_obj(&val);

            return RDB_OK;
        }           
        case RDB_EX_AGGREGATE:
        {
            RDB_object val;

            RDB_init_obj(&val);
            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&val);
                return ret;
            }
            if (val.kind != RDB_OB_TABLE) {
                RDB_destroy_obj(&val);
                return RDB_TYPE_MISMATCH;
            }
            ret = aggregate(val.var.tbp, exp->var.op.op,
                    exp->var.op.name, NULL, valp);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&val);
                return ret;
            }
            return RDB_destroy_obj(&val);
        }
        case RDB_EX_IS_EMPTY:
        {
            RDB_object val;

            RDB_init_obj(&val);

            ret = RDB_evaluate(exp->var.op.arg1, tup, txp, &val);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&val);
                return ret;
            }
            if (val.kind != RDB_OB_TABLE) {
                RDB_destroy_obj(&val);
                return RDB_TYPE_MISMATCH;
            }
            RDB_destroy_obj(valp);
            RDB_init_obj(valp);
            _RDB_set_obj_type(valp, &RDB_BOOLEAN);
            ret = RDB_table_is_empty(val.var.tbp, NULL,
                    &valp->var.bool_val);
            if (ret != RDB_OK) {
                RDB_destroy_obj(&val);
                return ret;
            }
            return RDB_destroy_obj(&val);
        }
    }
    /* Should never be reached */
    abort();
}

int
RDB_evaluate_bool(RDB_expression *exp, const RDB_object *tup, RDB_transaction *txp,
                  RDB_bool *resp)
{
    int ret;
    RDB_object val;

    RDB_init_obj(&val);
    ret = RDB_evaluate(exp, tup, txp, &val);
    if (ret != RDB_OK) {
        RDB_destroy_obj(&val);
        return ret;
    }
    if (RDB_obj_type(&val) != &RDB_BOOLEAN) {
        RDB_destroy_obj(&val);
        return RDB_TYPE_MISMATCH;
    }

    *resp = val.var.bool_val;
    RDB_destroy_obj(&val);
    return RDB_OK;
}

RDB_bool
_RDB_expr_refers(RDB_expression *exp, RDB_table *tbp)
{
    switch (exp->kind) {
        case RDB_EX_OBJ:
            if (exp->var.obj.kind == RDB_OB_TABLE)
                return (RDB_bool) (tbp == exp->var.obj.var.tbp);
            return RDB_FALSE;
        case RDB_EX_ATTR:
            return RDB_FALSE;
        case RDB_EX_EQ:
        case RDB_EX_NEQ:
        case RDB_EX_LT:
        case RDB_EX_GT:
        case RDB_EX_LET:
        case RDB_EX_GET:
        case RDB_EX_AND:
        case RDB_EX_OR:
        case RDB_EX_ADD:
        case RDB_EX_SUBTRACT:
        case RDB_EX_MULTIPLY:
        case RDB_EX_DIVIDE:
        case RDB_EX_REGMATCH:
        case RDB_EX_CONCAT:
            return (RDB_bool) (_RDB_expr_refers(exp->var.op.arg1, tbp)
                    || _RDB_expr_refers(exp->var.op.arg2, tbp));
        case RDB_EX_NOT:
        case RDB_EX_NEGATE:
        case RDB_EX_IS_EMPTY:
        case RDB_EX_STRLEN:
        case RDB_EX_TUPLE_ATTR:
        case RDB_EX_GET_COMP:
            return _RDB_expr_refers(exp->var.op.arg1, tbp);
        case RDB_SELECTOR:
        {
            int i;
            int compc = _RDB_get_possrep(exp->var.selector.typ,
                    exp->var.selector.name)->compc;

            for (i = 0; i < compc; i++)
                if (_RDB_expr_refers(exp->var.selector.argv[i], tbp))
                    return RDB_TRUE;
            
            return RDB_FALSE;
        }
        case RDB_EX_USER_OP:
        {
            int i;

            for (i = 0; i < exp->var.user_op.argc; i++)
                if (_RDB_expr_refers(exp->var.user_op.argv[i], tbp))
                    return RDB_TRUE;
            
            return RDB_FALSE;
        }
        case RDB_EX_AGGREGATE:
            return (RDB_bool) (exp->var.op.arg1->var.obj.var.tbp == tbp);
    }
    /* Should never be reached */
    abort();
}

RDB_object *
RDB_expr_obj(RDB_expression *exp)
{
    if (exp->kind != RDB_EX_OBJ)
        return NULL;
    return &exp->var.obj;
}
