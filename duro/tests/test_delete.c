/* $Id$ */

#include <rel/rdb.h>
#include <stdlib.h>
#include <stdio.h>

static int
print_table(RDB_table *tbp, RDB_transaction *txp)
{
    int ret;
    RDB_object *tplp;
    RDB_object array;
    RDB_int i;

    RDB_init_obj(&array);

    printf("converting table to array\n");
    ret = RDB_table_to_array(&array, tbp, 0, NULL, txp);
    if (ret != RDB_OK) {
        goto error;
    }
    
    for (i = 0; (ret = RDB_array_get(&array, i, &tplp)) == RDB_OK; i++) {
        printf("EMPNO: %d\n", (int) RDB_tuple_get_int(tplp, "EMPNO"));
        printf("NAME: %s\n", RDB_tuple_get_string(tplp, "NAME"));
        printf("SALARY: %f\n", (double) RDB_tuple_get_rational(tplp, "SALARY"));
    }
    if (ret != RDB_NOT_FOUND) {
        goto error;
    }

    RDB_destroy_obj(&array);
    
    return RDB_OK;
error:
    RDB_destroy_obj(&array);
    
    return ret;
}

int
test_delete(RDB_database *dbp)
{
    int ret;
    RDB_transaction tx;
    RDB_table *tbp;
    RDB_expression *exprp;

    printf("Starting transaction\n");
    ret = RDB_begin_tx(&tx, dbp, NULL);
    if (ret != RDB_OK) {
        return ret;
    }

    RDB_get_table("EMPS1", &tx, &tbp);

    printf("Deleting #1 from EMPS1\n");
    exprp = RDB_eq(RDB_expr_attr("EMPNO"),
            RDB_int_const(1));
    ret = RDB_delete(tbp, exprp, &tx);
    if (ret != RDB_OK) {
        RDB_rollback(&tx);
        return ret;
    }

    RDB_drop_expr(exprp);

    ret = print_table(tbp, &tx);
    if (ret != RDB_OK) {
        RDB_rollback(&tx);
        return ret;
    }

    printf("Deleting all tuples from EMPS1\n");
    ret = RDB_delete(tbp, NULL, &tx);
    if (ret != RDB_OK) {
        RDB_rollback(&tx);
        return ret;
    }

    ret = print_table(tbp, &tx);
    if (ret != RDB_OK) {
        RDB_rollback(&tx);
        return ret;
    }

    printf("End of transaction\n");
    return RDB_commit(&tx);
}

int
main(void)
{
    RDB_environment *dsp;
    RDB_database *dbp;
    int ret;
    
    printf("Opening environment\n");
    ret = RDB_open_env("dbenv", &dsp);
    if (ret != 0) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 1;
    }
    ret = RDB_get_db_from_env("TEST", dsp, &dbp);
    if (ret != 0) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 1;
    }

    ret = test_delete(dbp);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 2;
    }

    printf ("Closing environment\n");
    ret = RDB_close_env(dsp);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_strerror(ret));
        return 2;
    }

    return 0;
}
