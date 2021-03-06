#ifndef RDB_STRFNS_H
#define RDB_STRFNS_H

/*
 * Copyright (C) 2003-2005, 2012, 2013 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

/*
 * Create a copy of string str on the heap, including the
 * terminating null byte.
 * Returns the copy or NULL if allocating the memory for the
 * copy failed.
 */
char *
RDB_dup_str(const char *);

/*
 * Duplicate a vector of strings.
 */
char **
RDB_dup_strvec(int cnt, char **srcv);

/*
 * Free a vector of strings.
 */
void
RDB_free_strvec(int cnt, char **strv);

/*
 * Return the index of string name in attrv, or -1
 * if not found.
 */
int
RDB_find_str(int strc, char *strv[], const char *str);

/*
 * Hash function for strings
 */
unsigned
RDB_hash_str(const char *);

#endif
