#ifndef STMTSER_H
#define STMTSER_H

/* 
 * $Id$
 *
 * Copyright (C) 2007 Ren� Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "parse.h"   

int
Duro_op_to_binobj(RDB_object *, const RDB_parse_statement *, RDB_exec_context *);

RDB_parse_statement *
Duro_bin_to_stmts(const void *p, size_t, int argc, RDB_exec_context *,
        RDB_transaction *, char **argnamev);

#endif /*STMTSER_H*/