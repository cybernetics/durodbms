#include "errors.h"
#include <db.h>

/* $Id$ */

const char *
RDB_strerror(int err)
{
    if (err < -30800 || err > 0) {
        /* It's a Berkeley DB or system error */
        return db_strerror(err);
    }
    switch (err) {
        case RDB_OK:
            return "OK";

        case RDB_NO_SPACE:
            return "out of secondary storage space";
        case RDB_NO_MEMORY:
            return "out of memory";
        case RDB_SYSTEM_ERROR:
            return "system error";
        case RDB_DEADLOCK:
            return "deadlock occured";
        case RDB_INTERNAL:
            return "internal error";

        case RDB_ILLEGAL_ARG:
            return "illegal argument";
        case RDB_NOT_FOUND:
            return "not found";
        case RDB_INVALID_TRANSACTION:
            return "invalid transaction";
        case RDB_ELEMENT_EXISTS:
            return "element already exists in set";
        case RDB_TYPE_MISMATCH:
            return "type mismatch";
        case RDB_KEY_VIOLATION:
            return "key constraint violation";
        case RDB_PREDICATE_VIOLATION:
            return "table predicate violation";
        case RDB_AGGREGATE_UNDEFINED:
            return "result of aggregate operation is undefined";

        case RDB_NOT_SUPPORTED:
            return "operation or option not supported";
    }
    return "unknown error code";
}
