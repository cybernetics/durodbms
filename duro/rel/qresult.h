#ifndef QRESULT_H
#define QRESULT_H

/*
 * Copyright (C) 2006, 2012-2014 Rene Hartmann.
 * See the file COPYING for redistribution information.
 * 
 * Internal functions for iterating over query results.
 */

#include "rdb.h"
#include <rec/cursor.h>

struct RDB_tbindex;

typedef struct RDB_qresult {
    /* May be NULL */
    RDB_expression *exp;
    RDB_bool nested;
    union {
        /* !nested */
        struct {
            /* May be a descendant of *exp, NULL for sorter or SQL query */
            RDB_object *tbp;

            /* NULL if a unique index is used */
            RDB_cursor *curp;
        } stored;
        /* nested */
        struct {
            struct RDB_qresult *qrp;

            /* Only for some operators, may be NULL */
            struct RDB_qresult *qr2p;

            /* only used for join and ungroup */
            RDB_object tpl;
            RDB_bool tpl_valid;
        } children;
        /* Used when iterating over operator arguments */
        RDB_expression *next_exp;
    } val;
    RDB_bool endreached;

    /*
     * Temporary 'materialized' result table, needed for SUMMARIZE PER,
     * sorting etc.
     * Destroyed when the qresult is destroyed.
     */
    RDB_object *matp;

    /*
     * Otimized expression created by RDB_table_iterator().
     */
    RDB_expression *opt_exp;
} RDB_qresult;

/*
 * Iterator over the tuples of a RDB_object. Used internally.
 * Using it from an application is possible, but violates RM proscription 7.
 */
RDB_qresult *
RDB_table_qresult(RDB_object *, RDB_exec_context *, RDB_transaction *);

RDB_qresult *
RDB_expr_qresult(RDB_expression *, RDB_exec_context *, RDB_transaction *);

RDB_qresult *
RDB_index_qresult(RDB_object *, struct RDB_tbindex *,
        RDB_exec_context *, RDB_transaction *);

int
RDB_del_qresult(RDB_qresult *, RDB_exec_context *, RDB_transaction *);

int
RDB_seek_index_qresult(RDB_qresult *, struct RDB_tbindex *,
        const RDB_object *, RDB_exec_context *, RDB_transaction *);

int
RDB_sorter(RDB_expression *, RDB_qresult **qrespp, RDB_exec_context *,
        RDB_transaction *, int seqitc, const RDB_seq_item seqitv[]);

int
RDB_duprem(RDB_qresult *, RDB_exec_context *, RDB_transaction *);

int
RDB_get_by_cursor(RDB_object *, RDB_cursor *, RDB_type *, RDB_object *,
        RDB_exec_context *, RDB_transaction *);

int
RDB_get_by_uindex(RDB_object *tbp, RDB_object *objpv[],
        struct RDB_tbindex *indexp, RDB_type *, RDB_exec_context *,
        RDB_transaction *, RDB_object *tplp);

int
RDB_reset_qresult(RDB_qresult *, RDB_exec_context *, RDB_transaction *);

int
RDB_sdivide_preserves(RDB_expression *, const RDB_object *tplp, RDB_qresult *qr3p,
        RDB_exec_context *, RDB_transaction *, RDB_bool *);

#endif /*QRESULT_H*/
