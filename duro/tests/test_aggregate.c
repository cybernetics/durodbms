#include <rel/rdb.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int
test_aggregate(RDB_database *dbp, RDB_exec_context *ecp)
{
    RDB_transaction tx;
    RDB_object *tbp;
    RDB_float avg;
    int ret;

    ret = RDB_begin_tx(ecp, &tx, dbp, NULL);
    if (ret != RDB_OK) {
        return ret;
    }

    tbp = RDB_get_table("EMPS1", ecp, &tx);
    if (tbp == NULL) {
        RDB_rollback(ecp, &tx);
        return RDB_ERROR;
    }

    /* Creating aggregation COUNT ( EMPS1 ) */

    assert(RDB_cardinality(tbp, ecp, &tx) == 2);

    /* Creating aggregation AVG ( EMPS1, SALARY ) */

    ret = RDB_avg(tbp, RDB_var_ref("SALARY", ecp), ecp, &tx, &avg);
    if (ret != RDB_OK) {
        RDB_commit(ecp, &tx);
        return RDB_ERROR;
    }

    assert(avg == 4050.0);

    return RDB_commit(ecp, &tx);
}

int
main(int argc, char *argv[])
{
    RDB_environment *dsp;
    RDB_database *dbp;
    int ret;
    RDB_exec_context ec;
    
    RDB_init_exec_context(&ec);

    dsp = RDB_open_env(argc <= 1 ? "dbenv" : argv[1], RDB_RECOVER, &ec);
    if (dsp == NULL) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        return 1;
    }

    dbp = RDB_get_db_from_env("TEST", dsp, &ec, NULL);
    if (dbp == NULL) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        return 1;
    }

    ret = test_aggregate(dbp, &ec);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        return 2;
    }

    ret = RDB_close_env(dsp, &ec);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        return 2;
    }

    return 0;
}
