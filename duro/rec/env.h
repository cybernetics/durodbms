#ifndef RDB_ENV_H
#define RDB_ENV_H

/*
 * Copyright (C) 2003 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

/* $Id$ */

#include <db.h>
#include <stdio.h>

typedef void (RDB_errfn)(const char *msg, void *arg);

typedef struct RDB_environment {
    DB_ENV *envp;
    void (*closefn)(struct RDB_environment *);
    FILE *errfilep;
    RDB_errfn *errfn;
    void *errfn_arg;
    void *user_data;
} RDB_environment;

#define RDB_internal_env(renvp) (renvp->envp)

#define RDB_env_private(renvp) (renvp->user_data)

/*
 * Open a database environment.
 *
 * Arguments:
 * path		pathname of the direcory where the data is stored.
 * envpp	location where the pointer to the environment is stored.
 */
int
RDB_open_env(const char *path, RDB_environment **envpp);

/*
 * close a database environment.
 *
 * Arguments:
 * envp		the pointer to the environment.
 */
int
RDB_close_env(RDB_environment *envp);

void
RDB_set_env_closefn(RDB_environment *, void (*)(struct RDB_environment *));

void
RDB_set_errfile(RDB_environment *, FILE *errfile);

FILE *
RDB_get_errfile(RDB_environment *);

void
RDB_set_errfn(RDB_environment *, RDB_errfn *, void *arg);

RDB_errfn *
RDB_get_errfn(RDB_environment *, void **argp);

void
RDB_errmsg(RDB_environment *envp, const char *format, ...);

#endif
