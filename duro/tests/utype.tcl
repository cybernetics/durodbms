#!/bin/sh
# Execute tclsh from the user's PATH \
exec tclsh "$0"

# $Id$
#
# Test user-defined types
#

load .libs/libdurotcl.so

# Create DB environment
file delete -force tests/dbenv
file mkdir tests/dbenv

set dbenv [duro::env open tests/dbenv]

# Create Database
duro::db create TEST $dbenv

# Create type

set tx [duro::begin $dbenv TEST]

#
# Define type NAME. Only rep is a string without leading or trailing blanks
#
duro::type define NAME {
    {NAME {{NAME STRING}} {NOT ((NAME MATCHES "^ .*") \
            OR (NAME MATCHES ".* $"))}}
} $tx

duro::type implement NAME $tx

duro::table create T1 {
   {NO INTEGER}
   {NAME NAME}
} {{NO}} $tx

duro::insert T1 {NO 1 NAME {NAME "Potter"}} $tx

set tpl {NO 2 NAME {NAME " Johnson"}}
if {![catch {
    duro::insert T1 $tpl $tx
}]} {
    error "Insertion of tuple $tpl should fail, but succeeded"
}

duro::table drop T1 $tx

#
# Define type PNAME. Only rep is (FIRSTNAME,LASTNAME)
#

duro::type define PNAME {
    {PNAME {{FIRSTNAME STRING} {LASTNAME STRING}}}
} $tx

duro::type implement PNAME $tx

#
# Define type INTSET
#

duro::type define INTSET {
    {INTLIST {{INTLIST STRING}}}
} $tx

# Actual rep is relation
duro::type implement INTSET {relation {N INTEGER}} $tx

# Selector
duro::operator create INTLIST -returns INTSET {il STRING} {
    set r {}
    foreach i $il {
        lappend r [list N $i]
    }
    return $r
} $tx

# Getter
duro::operator create INTSET_get_INTLIST -returns STRING {is INTSET} {
    set r {}
    foreach i $is {
        lappend r [lindex $i 1]
    }
    return [lsort -integer $r]
} $tx

# Setter
duro::operator create INTSET_set_INTLIST -updates {is} {is INTSET il STRING} {
    duro::delete $is $tx
    foreach i $il {
        duro::insert $is [list N $i] $tx
    }
} $tx

duro::commit $tx

# Close DB environment
duro::env close $dbenv

# Reopen DB environment
set dbenv [duro::env open tests/dbenv]

set tx [duro::begin $dbenv TEST]

duro::table create T2 {
   {NO INTEGER}
   {NAME PNAME}
} {{NO}} $tx

set tpl {NO 1 NAME {PNAME "Peter" "Potter"}}
duro::insert T2 $tpl $tx

if {![duro::table contains T2 $tpl $tx]} {
    error "T2 should contain $tpl, but does not."
}

array set a [duro::expr {TUPLE FROM (T2 WHERE THE_LASTNAME(NAME) = "Potter")} $tx]

if {($a(NO) != 1) || ($a(NAME) != {PNAME Peter Potter})} {
    error "T2 has wrong value"
}

#
# Test type INTSET
#

duro::table create T3 {
    {IS INTSET}
} {{IS}} $tx

set sil {1 2}
set stpl [list IS [list INTLIST $sil]]
duro::insert T3 $stpl $tx

set tpl [duro::expr {TUPLE FROM T3} $tx]
if {![string equal $tpl $stpl]} {
    error "TUPLE FROM T3 should be $stpl, but is $tpl"
}

set il [duro::expr {THE_INTLIST((TUPLE FROM T3).IS)} $tx]
if {![string equal $il $sil]} {
    error "THE_INTLIST should be $sil, but is $il"
}

#
# Test setter
#

set is {INTLIST {1 3 4}}
duro::call INTSET_set_INTLIST is INTSET {1 2 3} STRING $tx

set sis {INTLIST {1 2 3}}
if {![string equal $is $sis]} {
    error "INTSET value should be $sis, but is $is"
}

duro::commit $tx

# Test setter

# Close DB environment
duro::env close $dbenv
