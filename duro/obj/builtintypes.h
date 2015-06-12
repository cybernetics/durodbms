/*
 * Copyright (C) 2013-2015 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#ifndef BUILTINTYPES_H_
#define BUILTINTYPES_H_

#include "type.h"

#if defined (_WIN32) && !defined (NO_DLL_IMPORT)
#define RDB_EXTERN_VAR __declspec(dllimport)
#else
#define RDB_EXTERN_VAR extern
#endif

/*
 * Built-in scalar data types
 */
RDB_EXTERN_VAR RDB_type RDB_BOOLEAN;
RDB_EXTERN_VAR RDB_type RDB_INTEGER;
RDB_EXTERN_VAR RDB_type RDB_FLOAT;
RDB_EXTERN_VAR RDB_type RDB_STRING;
RDB_EXTERN_VAR RDB_type RDB_BINARY;

RDB_EXTERN_VAR RDB_type RDB_DATETIME;

/*
 * Error types
 */
RDB_EXTERN_VAR RDB_type RDB_NO_RUNNING_TX_ERROR;
RDB_EXTERN_VAR RDB_type RDB_INVALID_ARGUMENT_ERROR;
RDB_EXTERN_VAR RDB_type RDB_TYPE_MISMATCH_ERROR;
RDB_EXTERN_VAR RDB_type RDB_NOT_FOUND_ERROR;
RDB_EXTERN_VAR RDB_type RDB_OPERATOR_NOT_FOUND_ERROR;
RDB_EXTERN_VAR RDB_type RDB_TYPE_NOT_FOUND_ERROR;
RDB_EXTERN_VAR RDB_type RDB_NAME_ERROR;
RDB_EXTERN_VAR RDB_type RDB_ELEMENT_EXISTS_ERROR;
RDB_EXTERN_VAR RDB_type RDB_TYPE_CONSTRAINT_VIOLATION_ERROR;
RDB_EXTERN_VAR RDB_type RDB_KEY_VIOLATION_ERROR;
RDB_EXTERN_VAR RDB_type RDB_PREDICATE_VIOLATION_ERROR;
RDB_EXTERN_VAR RDB_type RDB_IN_USE_ERROR;
RDB_EXTERN_VAR RDB_type RDB_AGGREGATE_UNDEFINED_ERROR;
RDB_EXTERN_VAR RDB_type RDB_VERSION_MISMATCH_ERROR;
RDB_EXTERN_VAR RDB_type RDB_NOT_SUPPORTED_ERROR;

RDB_EXTERN_VAR RDB_type RDB_NO_MEMORY_ERROR;
RDB_EXTERN_VAR RDB_type RDB_LOCK_NOT_GRANTED_ERROR;
RDB_EXTERN_VAR RDB_type RDB_DEADLOCK_ERROR;
RDB_EXTERN_VAR RDB_type RDB_RESOURCE_NOT_FOUND_ERROR;
RDB_EXTERN_VAR RDB_type RDB_INTERNAL_ERROR;
RDB_EXTERN_VAR RDB_type RDB_DATA_CORRUPTED_ERROR;
RDB_EXTERN_VAR RDB_type RDB_RUN_RECOVERY_ERROR;
RDB_EXTERN_VAR RDB_type RDB_SYSTEM_ERROR;

RDB_EXTERN_VAR RDB_type RDB_SYNTAX_ERROR;

RDB_EXTERN_VAR RDB_type RDB_IDENTIFIER;

/* I/O type */

RDB_EXTERN_VAR RDB_type RDB_IOSTREAM_ID;

int
RDB_init_builtin_basic_types(RDB_exec_context *);

int
RDB_add_builtin_pr_types(RDB_exec_context *);

#endif /* BUILTINTYPES_H_ */