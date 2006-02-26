/* $Id$ */

#include <rel/rdb.h>
#include <stdlib.h>
#include <stdio.h>

RDB_type *plusargtv[] = {
    &RDB_INTEGER,
    &RDB_INTEGER
};

RDB_type *addargtv[] = {
    &RDB_INTEGER,
    &RDB_INTEGER
};

int
test_callop(RDB_database *dbp, RDB_exec_context *ecp)
{
    RDB_transaction tx;
    int ret;
    RDB_object arg1, arg2;
    RDB_object retval;
    RDB_object *argv[2];

    printf("Starting transaction\n");
    ret = RDB_begin_tx(ecp, &tx, dbp, NULL);
    if (ret != RDB_OK) {
        return ret;
    }

    RDB_init_obj(&arg1);
    RDB_init_obj(&arg2);
    RDB_init_obj(&retval);

    RDB_int_to_obj(&arg1, 2);
    RDB_int_to_obj(&arg2, 2);
    argv[0] = &arg1;
    argv[1] = &arg2;

    printf("Calling PLUS\n");
    ret = RDB_call_ro_op("PLUS", 2, argv, ecp, &tx, &retval);
    if (ret != RDB_OK) {
        RDB_rollback(ecp, &tx);
        goto error;
    }

    printf("Result value is %d\n", RDB_obj_int(&retval));

    printf("Calling ADD\n");
    ret = RDB_call_update_op("ADD", 2, argv, ecp, &tx);
    if (ret != RDB_OK) {
        RDB_rollback(ecp, &tx);
        goto error;
    }

    printf("Value of arg #1 is %d\n", RDB_obj_int(&arg1));

    return RDB_commit(ecp, &tx);

error:
    RDB_destroy_obj(&arg1, ecp);
    RDB_destroy_obj(&arg2, ecp);
    RDB_destroy_obj(&retval, ecp);

    RDB_rollback(ecp, &tx);
    return RDB_ERROR;
}

int
test_useop(RDB_database *dbp, RDB_exec_context *ecp)
{
    RDB_transaction tx;
    RDB_table *tbp, *vtbp;
    RDB_virtual_attr extend;
    RDB_expression *expv[2];
    int ret;

    printf("Starting transaction\n");
    ret = RDB_begin_tx(ecp, &tx, dbp, NULL);
    if (ret != RDB_OK) {
        return ret;
    }

    tbp = RDB_get_table("DEPTS", ecp, &tx);
    if (tbp == NULL) {
        goto error;
    }

    expv[0] = RDB_expr_var("DEPTNO", ecp);
    expv[1] = RDB_int_to_expr(100, ecp);

    extend.name = "XDEPTNO";
    extend.exp = RDB_ro_op("PLUS", 2, expv, ecp);
    if (extend.exp == NULL) {
        goto error;
    }

    vtbp = RDB_extend(tbp, 1, &extend, ecp, &tx);
    if (vtbp == NULL) {
        goto error;
    }

    printf("Making vtable persistent\n");

    ret = RDB_set_table_name(vtbp, "DEPTSX", ecp, &tx);
    if (ret != RDB_OK) {
        goto error;
    }

    ret = RDB_add_table(vtbp, ecp, &tx);
    if (ret != RDB_OK) {
        goto error;
    }

    return RDB_commit(ecp, &tx);

error:
    RDB_rollback(ecp, &tx);
    return RDB_ERROR;
}

int
main(void)
{
    RDB_environment *envp;
    RDB_database *dbp;
    int ret;
    RDB_exec_context ec;
    
    printf("Opening environment\n");
    ret = RDB_open_env("dbenv", &envp);
    if (ret != 0) {
        fprintf(stderr, "Error: %s\n", db_strerror(ret));
        return 1;
    }

    RDB_bdb_env(envp)->set_errfile(RDB_bdb_env(envp), stderr);
    RDB_init_exec_context(&ec);
    dbp = RDB_get_db_from_env("TEST", envp, &ec);
    if (dbp == NULL) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        RDB_destroy_exec_context(&ec);
        return 1;
    }

    ret = test_callop(dbp, &ec);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        RDB_destroy_exec_context(&ec);
        return 2;
    }

    ret = test_useop(dbp, &ec);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", RDB_type_name(RDB_obj_type(RDB_get_err(&ec))));
        RDB_destroy_exec_context(&ec);
        return 2;
    }
    RDB_destroy_exec_context(&ec);

    printf ("Closing environment\n");
    ret = RDB_close_env(envp);
    if (ret != RDB_OK) {
        fprintf(stderr, "Error: %s\n", db_strerror(ret));
        return 2;
    }

    return 0;
}
