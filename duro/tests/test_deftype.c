/* $Id$ */

#include <rel/rdb.h>
#include <rel/typeimpl.h>
#include <stdlib.h>
#include <stdio.h>

int
test_type(RDB_database *dbp)
{
    RDB_transaction tx;
    RDB_possrep pr;
    RDB_attr comp;
    int ret;

    printf("Starting transaction\n");
    ret = RDB_begin_tx(&tx, dbp, NULL);
    if (ret != RDB_OK) {
        return ret;
    }

    printf("Defining type\n");
    comp.name = NULL;
    comp.typ = &RDB_INTEGER;
    pr.name = NULL;
    pr.compc = 1;
    pr.compv = &comp;
    ret = RDB_ro_op_2("<", RDB_expr_attr("TINYINT"),
            RDB_int_to_expr(100), &tx, &pr.constraintp);
    if (ret != RDB_OK) {
        return ret;
    }
    ret = RDB_define_type("TINYINT", 1, &pr, &tx);
    if (ret != RDB_OK) {
        RDB_rollback(&tx);
        return ret;
    }

    printf("Implementing type\n");
    ret = RDB_implement_type("TINYINT", NULL, -1, &tx);
    if (ret != RDB_OK) {
        RDB_rollback(&tx);
        return ret;
    }

    ret = RDB_commit(&tx);
    if (ret != RDB_OK) {
        return ret;
    }
    return RDB_OK;
}

int
main(void)
{
    RDB_environment *envp;
    RDB_database *dbp;
    int ret;
    
    printf("Opening environment\n");
    ret = RDB_open_env("dbenv", &envp);
    if (ret != 0) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 1;
    }

    RDB_set_errfile(envp, stderr);

    ret = RDB_get_db_from_env("TEST", envp, &dbp);
    if (ret != 0) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 1;
    }

    ret = test_type(dbp);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 2;
    }

    printf ("Closing environment\n");
    ret = RDB_close_env(envp);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 2;
    }

    return 0;
}
