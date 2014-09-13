/*
 * test_interp.c
 *
 *  Created on: 17.08.2013
 *      Author: Rene Hartmann
 */

/* $Id$ */

#include <rel/rdb.h>
#include <dli/iinterp.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int
main(void)
{
    RDB_environment *dsp;
    RDB_database *dbp;
    RDB_exec_context ec;
    int ret;
    Duro_interp interp;

    ret = RDB_open_env("dbenv", &dsp, RDB_RECOVER);
    if (ret != 0) {
        fprintf(stderr, "Error: %s\n", db_strerror(ret));
        return 1;
    }

    RDB_init_exec_context(&ec);
    dbp = RDB_get_db_from_env("TEST", dsp, &ec);
    if (dbp == NULL) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        RDB_destroy_exec_context(&ec);
        return 1;
    }

    assert(Duro_init_interp(&interp, &ec, RDB_db_env(dbp),
            RDB_db_name(dbp)) == RDB_OK);

    if (Duro_dt_execute_str("put_line ('Test');"
            "begin tx;"
            "put_line (cast_as_string(count(depts)));"
            "commit;",
            &interp, &ec) != RDB_OK) {
        Duro_print_error(RDB_get_err(&ec));
        abort();
    }

    RDB_destroy_exec_context(&ec);
    Duro_destroy_interp(&interp);

    return 0;
}
