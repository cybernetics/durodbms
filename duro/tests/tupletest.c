/* $Id$ */

#include <rel/rdb.h>
#include <rel/internal.h>
#include <stdio.h>

int main(void) {
    const void *datap;
    int i;
    int res;
    RDB_object tpl;

    _RDB_init_builtin_types();

    RDB_init_obj(&tpl);
    RDB_destroy_obj(&tpl);

    RDB_init_obj(&tpl);
    res = RDB_tuple_set_string(&tpl, "A", "Aaa");
    if (res != RDB_OK) {
        fprintf(stderr, "Error %s\n", RDB_strerror(res));
        RDB_destroy_obj(&tpl);
        return 2;
    }
    res = RDB_tuple_set_int(&tpl, "B", (RDB_int)4711);
    if (res != RDB_OK) {
        fprintf(stderr, "Error %s\n", RDB_strerror(res));
        RDB_destroy_obj(&tpl);
        return 2;
    }
    
    datap = RDB_tuple_get_string(&tpl, "A");
    printf("%s -> %s\n", "A", (char *)datap);

    i = RDB_tuple_get_int(&tpl, "B");
    printf("%s -> %d\n", "B", i);
    
    RDB_destroy_obj(&tpl);
    
    return 0;
}
