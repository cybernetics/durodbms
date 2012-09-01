#ifndef RDB_RECMAP_H
#define RDB_RECMAP_H

/*
 * $Id$
 *
 * Copyright (C) 2003-2012 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "env.h"
#include <gen/types.h>
#include <db.h>
#include <stdlib.h>

/*
 * Functions for managing record maps. A record map contains
 * records which consist of a key and a data part.
 */

#define RDB_VARIABLE_LEN (-1) /* indicates a variable field length */

enum {
    RDB_UNIQUE = 1,
    RDB_ORDERED = 2,

    RDB_ELEMENT_EXISTS = -200,
    RDB_KEY_VIOLATION = -201,
    RDB_RECORD_CORRUPTED = -202
};

typedef struct {
    int no;
    void *datap;
    size_t len;
    void *(*copyfp)(void *, const void *, size_t);
} RDB_field;

typedef int RDB_field_compare_func(const void *, size_t,
                const void *, size_t, RDB_environment *,
                void *);

typedef struct {
    RDB_field_compare_func *comparep;
    void *arg;
    RDB_bool asc;
} RDB_compare_field;

typedef struct {
    /* internal */
    RDB_environment *envp;
    DB *dbp;
    char *namp;
    char *filenamp;
    int fieldcount;	/* # of fields total */
    int keyfieldcount;	/* # of primary index fields */

    /*
     * The length of each field, if it's fixed-length field,
     * RDB_VARIABLE_LEN if it's a variable-length field
     */
    RDB_int *fieldlens;

    int varkeyfieldcount; /* # of variable-length key fields */
    int vardatafieldcount; /* # of variable-length nonkey fields */ 

    /* For sorted recmaps */
    RDB_compare_field *cmpv;

    /* RDB_TRUE if duplicate keys are allowed */
    RDB_bool dup_keys;
} RDB_recmap;

#define RDB_recmap_name(rmp) ((rmp)->namp)

/*
 * Create a recmap with the name specified by name in the DB environment
 * pointed to by envp in the file specified by filename.
 * If filename is NULL, a transient recmap is created.
 * The recmap will have fieldc fields, the length of field i
 * is given by fieldlenv[i].
 * The first keyfcnt fields constitute the primary index.
 */
int
RDB_create_recmap(const char *name, const char *filename,
        RDB_environment *, int fieldc, const int fieldlenv[], int keyfieldc,
        const RDB_compare_field cmpv[], int flags, DB_TXN *, RDB_recmap **);

/* Open a recmap. For a description of the arguments, see RDB_create_recmap(). */
int
RDB_open_recmap(const char *name, const char *filename,
        RDB_environment *, int fieldc, const int fieldlenv[], int keyfieldc,
        DB_TXN *, RDB_recmap **);

/* Close a recmap. */
int
RDB_close_recmap(RDB_recmap *);

/* Delete a recmap. */
int
RDB_delete_recmap(RDB_recmap *, DB_TXN *);

/*
 * Insert a record into a recmap. The values are given by valv.
 * valv[..].no is ignored.
 */
int
RDB_insert_rec(RDB_recmap *, RDB_field[], DB_TXN *);

/* Update the record whose key values are given by keyv.
 * The new values are given by fieldv.
 * keyv[..].no is ignored. 
 */
int
RDB_update_rec(RDB_recmap *, RDB_field[],
               int, const RDB_field[], DB_TXN *);

/* Delete from a recmap the record whose key values are given by keyv.
 * keyv[..].no is ignored.
 */
int
RDB_delete_rec(RDB_recmap *, RDB_field[], DB_TXN *);

/* Read field values of the record from a recmap using the primary index.
 * Arguments:
 * rm		pointer to the recmap.
 * keyv		array of the key fields specifying the record to read.
 * txid		pointer to a Berkeley DB transaction
 * fieldc	how many fields are to be read
 * resfieldv    the fields to read.
 *              no must be set by the caller,
 *              size and datap are set by RDB_get_fields().
 *              Must not contain key fields.
 */
int
RDB_get_fields(RDB_recmap *, RDB_field[],
           int, DB_TXN *, RDB_field[]);

/* Check if the recmap contains the record whose values are given by valv.
 * Return RDB_OK if yes, RDB_NOT_FOUND if no.
 * valv[..].no is ignored.
 */
int
RDB_contains_rec(RDB_recmap *, RDB_field[], DB_TXN *);

/*
 * Internal functions
 */

size_t
RDB_get_vflen(RDB_byte *, size_t, int, int);

int
RDB_get_field(RDB_recmap *, int, void *, size_t,
        size_t *, int *);

int
RDB_set_field(RDB_recmap *, DBT *, const RDB_field *,
               int);

int
RDB_get_DBT_fields(RDB_recmap *, const DBT *, const DBT *, int,
           RDB_field[]);

int
RDB_fields_to_DBT(RDB_recmap *, int, const RDB_field[],
                   DBT *);

int
RDB_update_DBT_rec(RDB_recmap *, DBT *, DBT *,
        int, const RDB_field[], DB_TXN *);

int
RDB_recmap_est_size(RDB_recmap *, DB_TXN *, unsigned *);

#endif
