/*
 * Built-in operators for type datetime.
 *
 * Copyright (C) 2015 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include "datetimeops.h"
#include "builtintypes.h"
#include "object.h"
#include <gen/strfns.h>

#include <stdio.h>
#include <time.h>

static int datetime_check_month(int m, RDB_exec_context *ecp)
{
    if (m < 1 || m > 12) {
        RDB_raise_type_constraint_violation("datetime: month", ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

static RDB_bool is_leap_year(int y) {
    if (y % 400 == 0)
        return RDB_TRUE;
    return y % 4 == 0 && y % 100 != 0 ? RDB_TRUE : RDB_FALSE;
}

static int datetime_check_day(int y, int m, int d, RDB_exec_context *ecp)
{
    int days;

    switch(m) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
        days = 31;
        break;
    case 4:
    case 6:
    case 9:
    case 11:
        days = 30;
        break;
    case 2:
        /* Ignore Gregorian leap year rule for years before 1924 */
        if (y < 1924) {
            days = y % 4 == 0 ? 29 : 28;
        } else {
            days = is_leap_year(y) ? 29 : 28;
        }
        break;
    }

    if (d < 1 || d > days) {
        RDB_raise_type_constraint_violation("datetime: day", ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

static int datetime_check_hour(int h, RDB_exec_context *ecp)
{
    if (h < 0 || h > 23) {
        RDB_raise_type_constraint_violation("datetime: hour", ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

static int datetime_check_minute(int m, RDB_exec_context *ecp)
{
    if (m < 0 || m > 59) {
        RDB_raise_type_constraint_violation("datetime: minute", ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

static int datetime_check_second(int sec, RDB_exec_context *ecp)
{
    if (sec < 0 || sec > 60) {
        RDB_raise_type_constraint_violation("datetime: minute", ecp);
        return RDB_ERROR;
    }
    return RDB_OK;
}

/** @page datetime-ops Built-in datetime type and operators

<h3>TYPE datetime</h3>

<pre>
TYPE datetime
POSSREP {
    year integer,
    month integer,
    day integer,
    hour integer,
    minute integer,
    second integer
};
</pre>

 * <h3>OPERATOR now</h3>
 *
 * OPERATOR now() RETURNS datetime;
 *
 * Returns the current time as a datetime, according to the current timezone.
 *
 * <h3>OPERATOR now_utc</h3>
 *
 * OPERATOR now_utc() RETURNS datetime;
 *
 * Returns the current time as a UTC datetime.
 *
 * <h3>OPERATOR add_seconds</h3>
 *
 * OPERATOR add_seconds(dt datetime, seconds integer) RETURNS datetime;
 *
 * Adds the number of seconds specified by <var>seconds</var>
 * to the datetime specified by <var>dt</var> using the current time zone
 * and returns the result.
 *
 * <h3>OPERATOR weekday</h3>
 *
 * OPERATOR weekday(dt datetime) RETURNS integer;
 *
 * Returns, for the given datetime, the weekday as the number of days since Sunday.
 *
 * <h3>OPERATOR local_to_utc</h3>
 *
 * OPERATOR local_to_utc(dt datetime) RETURNS datetime;
 *
 * Converts a local datetime to a UTC datetime.
 *
 */

/* Selector of type datetime */
static int
datetime(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    struct tm tm;

    if (datetime_check_month(RDB_obj_int(argv[1]), ecp) != RDB_OK)
        return RDB_ERROR;
    if (datetime_check_day(RDB_obj_int(argv[0]), RDB_obj_int(argv[1]),
            RDB_obj_int(argv[2]), ecp) != RDB_OK)
        return RDB_ERROR;
    if (datetime_check_hour(RDB_obj_int(argv[3]), ecp) != RDB_OK)
        return RDB_ERROR;
    if (datetime_check_minute(RDB_obj_int(argv[4]), ecp) != RDB_OK)
        return RDB_ERROR;
    if (datetime_check_second(RDB_obj_int(argv[5]), ecp) != RDB_OK)
        return RDB_ERROR;

    tm.tm_year = RDB_obj_int(argv[0]) - 1900;
    tm.tm_mon = RDB_obj_int(argv[1]) - 1;
    tm.tm_mday = RDB_obj_int(argv[2]);
    tm.tm_hour = RDB_obj_int(argv[3]);
    tm.tm_min = RDB_obj_int(argv[4]);
    tm.tm_sec = RDB_obj_int(argv[5]);

    RDB_tm_to_obj(retvalp, &tm);
    return RDB_OK;
}

static int
datetime_get_year(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    RDB_int_to_obj(retvalp, (RDB_int) argv[0]->val.time.year);
    return RDB_OK;
}

static int
datetime_get_month(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    RDB_int_to_obj(retvalp, (RDB_int) argv[0]->val.time.month);
    return RDB_OK;
}

static int
datetime_get_day(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    RDB_int_to_obj(retvalp, (RDB_int) argv[0]->val.time.day);
    return RDB_OK;
}

static int
datetime_get_hour(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    RDB_int_to_obj(retvalp, (RDB_int) argv[0]->val.time.hour);
    return RDB_OK;
}

static int
datetime_get_minute(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    RDB_int_to_obj(retvalp, (RDB_int) argv[0]->val.time.minute);
    return RDB_OK;
}

static int
datetime_get_second(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    RDB_int_to_obj(retvalp, (RDB_int) argv[0]->val.time.second);
    return RDB_OK;
}

static int
now_utc_datetime(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    struct tm *tm;
    time_t t = time(NULL);

    tm = gmtime(&t);
    if (tm == NULL) {
        RDB_raise_system("gmtime() failed", ecp);
        return RDB_ERROR;
    }

    RDB_tm_to_obj(retvalp, tm);
    return RDB_OK;
}

static int
now_datetime(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    struct tm *tm;
    time_t t = time(NULL);

    tm = localtime(&t);
    if (tm == NULL) {
        RDB_raise_system("localtime() failed", ecp);
        return RDB_ERROR;
    }

    RDB_tm_to_obj(retvalp, tm);
    return RDB_OK;
}

static int
cast_as_string_datetime(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)

{
    struct tm tm;
    char buf[20];

    RDB_datetime_to_tm(&tm, argv[0]);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    return RDB_string_to_obj(retvalp, buf, ecp);
}

static int
datetime_set_year(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    argv[0]->val.time.year = (int16_t) RDB_obj_int(argv[1]);
    return RDB_OK;
}

static int
datetime_set_month(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (datetime_check_month(RDB_obj_int(argv[1]), ecp) != RDB_OK)
        return RDB_ERROR;

    argv[0]->val.time.month = RDB_obj_int(argv[1]);
    return RDB_OK;
}

static int
datetime_set_day(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (datetime_check_day(argv[0]->val.time.year, argv[0]->val.time.month,
            RDB_obj_int(argv[1]), ecp) != RDB_OK)
        return RDB_ERROR;

    argv[0]->val.time.day = RDB_obj_int(argv[1]);
    return RDB_OK;
}

static int
datetime_set_hour(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (datetime_check_hour(RDB_obj_int(argv[1]), ecp) != RDB_OK)
        return RDB_ERROR;

    argv[0]->val.time.hour = RDB_obj_int(argv[1]);
    return RDB_OK;
}

static int
datetime_set_minute(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (datetime_check_minute(RDB_obj_int(argv[1]), ecp) != RDB_OK)
        return RDB_ERROR;

    argv[0]->val.time.minute = RDB_obj_int(argv[1]);
    return RDB_OK;
}

static int
datetime_set_second(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    if (datetime_check_second(RDB_obj_int(argv[1]), ecp) != RDB_OK)
        return RDB_ERROR;

    argv[0]->val.time.second = RDB_obj_int(argv[1]);
    return RDB_OK;
}

static int
datetime_add_seconds(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    time_t t;
    struct tm tm;
    struct tm *restm;

    RDB_datetime_to_tm(&tm, argv[0]);

    t = mktime(&tm);
    if (t == -1) {
        RDB_raise_invalid_argument("converting datetime to time_t failed", ecp);
        return RDB_ERROR;
    }

    t += RDB_obj_int(argv[1]);

    restm = localtime(&t);
    if (restm == NULL) {
        RDB_raise_system("localtime() failed", ecp);
        return RDB_ERROR;
    }

    RDB_tm_to_obj(retvalp, restm);
    return RDB_OK;
}

static int
datetime_weekday(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    time_t t;
    struct tm tm;
    struct tm *restm;

    RDB_datetime_to_tm(&tm, argv[0]);

    t = mktime(&tm);
    if (t == -1) {
        RDB_raise_invalid_argument("converting datetime to time_t failed", ecp);
        return RDB_ERROR;
    }

    restm = localtime(&t);
    if (restm == NULL) {
        RDB_raise_system("localtime() failed", ecp);
        return RDB_ERROR;
    }

    RDB_int_to_obj(retvalp, restm->tm_wday);
    return RDB_OK;
}

static int
datetime_local_to_utc(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    time_t t;
    struct tm tm;
    struct tm *restm;

    RDB_datetime_to_tm(&tm, argv[0]);

    t = mktime(&tm);
    if (t == -1) {
        RDB_raise_invalid_argument("converting datetime to time_t failed", ecp);
        return RDB_ERROR;
    }

    restm = gmtime(&t);
    if (restm == NULL) {
        RDB_raise_system("gmtime() failed", ecp);
        return RDB_ERROR;
    }

    RDB_tm_to_obj(retvalp, restm);
    return RDB_OK;
}

int
RDB_add_datetime_ro_ops(RDB_op_map *opmap, RDB_exec_context *ecp)
{
    RDB_type *paramtv[6];

    paramtv[0] = &RDB_DATETIME;

    if (RDB_put_ro_op(opmap, "datetime_get_year", 1, paramtv, &RDB_INTEGER,
            &datetime_get_year, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "datetime_get_month", 1, paramtv, &RDB_INTEGER,
            &datetime_get_month, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "datetime_get_day", 1, paramtv, &RDB_INTEGER,
            &datetime_get_day, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "datetime_get_hour", 1, paramtv, &RDB_INTEGER,
            &datetime_get_hour, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "datetime_get_minute", 1, paramtv, &RDB_INTEGER,
            &datetime_get_minute, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "datetime_get_second", 1, paramtv, &RDB_INTEGER,
            &datetime_get_second, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "cast_as_string", 1, paramtv, &RDB_STRING,
            &cast_as_string_datetime, ecp) != RDB_OK)
        return RDB_ERROR;

    paramtv[0] = &RDB_INTEGER;
    paramtv[1] = &RDB_INTEGER;
    paramtv[2] = &RDB_INTEGER;
    paramtv[3] = &RDB_INTEGER;
    paramtv[4] = &RDB_INTEGER;
    paramtv[5] = &RDB_INTEGER;
    if (RDB_put_ro_op(opmap, "datetime", 6, paramtv, &RDB_DATETIME,
            &datetime, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "now_utc", 0, NULL, &RDB_DATETIME,
            &now_utc_datetime, ecp) != RDB_OK)
        return RDB_ERROR;

    if (RDB_put_ro_op(opmap, "now", 0, NULL, &RDB_DATETIME,
            &now_datetime, ecp) != RDB_OK)
        return RDB_ERROR;

    paramtv[0] = &RDB_DATETIME;
    paramtv[1] = &RDB_INTEGER;

    if (RDB_put_ro_op(opmap, "add_seconds", 2, paramtv, &RDB_DATETIME,
            &datetime_add_seconds, ecp) != RDB_OK) {
        return RDB_ERROR;
    }

    if (RDB_put_ro_op(opmap, "weekday", 1, paramtv, &RDB_INTEGER,
            &datetime_weekday, ecp) != RDB_OK) {
        return RDB_ERROR;
    }

    if (RDB_put_ro_op(opmap, "local_to_utc", 1, paramtv, &RDB_DATETIME,
            &datetime_local_to_utc, ecp) != RDB_OK) {
        return RDB_ERROR;
    }

    return RDB_OK;
}

int
RDB_add_datetime_upd_ops(RDB_op_map *opmap, RDB_exec_context *ecp)
{
    RDB_parameter paramv[2];

    paramv[0].typ = &RDB_DATETIME;
    paramv[0].update = RDB_TRUE;
    paramv[1].typ = &RDB_INTEGER;
    paramv[1].update = RDB_FALSE;

    if (RDB_put_upd_op(opmap, "datetime_set_year", 2, paramv,
            &datetime_set_year, ecp) != RDB_OK)
        return RDB_ERROR;
    if (RDB_put_upd_op(opmap, "datetime_set_month", 2, paramv,
            &datetime_set_month, ecp) != RDB_OK)
        return RDB_ERROR;
    if (RDB_put_upd_op(opmap, "datetime_set_day", 2, paramv,
            &datetime_set_day, ecp) != RDB_OK)
        return RDB_ERROR;
    if (RDB_put_upd_op(opmap, "datetime_set_hour", 2, paramv,
            &datetime_set_hour, ecp) != RDB_OK)
        return RDB_ERROR;
    if (RDB_put_upd_op(opmap, "datetime_set_minute", 2, paramv,
            &datetime_set_minute, ecp) != RDB_OK)
        return RDB_ERROR;
    if (RDB_put_upd_op(opmap, "datetime_set_second", 2, paramv,
            &datetime_set_second, ecp) != RDB_OK)
        return RDB_ERROR;

    return RDB_OK;
}
