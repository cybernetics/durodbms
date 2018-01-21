/*
 * Environment internals
 *
 * Copyright (C) 2018 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#ifndef REC_ENVIMPL_H_
#define REC_ENVIMPL_H_

#include "env.h"
#include "recmap.h"
#include <db.h>

typedef struct RDB_environment {
    /* The Berkeley DB environment */
    DB_ENV *envp;

    /* Functions implementing environment close and recmap functions */
    int (*close_fn)(struct RDB_environment *);
    int (*create_recmap_fn)(const char *, const char *,
            RDB_environment *, int, const int[], int,
            const RDB_compare_field[], int, RDB_rec_transaction *, RDB_recmap **);
    int (*open_recmap_fn)(const char *, const char *,
            RDB_environment *, int, const int[], int,
            RDB_rec_transaction *, RDB_recmap **);

    /* Function which is invoked by RDB_close_env() */
    void (*cleanup_fn)(struct RDB_environment *);

    /*
     * Used by higher layers to store additional data.
     * The relational layer uses this to store a pointer to the dbroot structure.
     */
    void *xdata;

    /* Trace level. 0 means no trace. */
    unsigned trace;
} RDB_environment;

#endif /* REC_ENVIMPL_H_ */
