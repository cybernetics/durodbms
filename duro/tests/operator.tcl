#!/bin/sh
# Execute tclsh from the user's PATH \
exec tclsh "$0"

# $Id$
#
# Test creating, invoking, deleting operators
#

load .libs/libdurotcl.so

source tests/testutil.tcl

# Create DB environment
file delete -force tests/dbenv
file mkdir tests/dbenv
set dbenv [duro::env open tests/dbenv]

# Create Database
duro::db create TEST $dbenv

# Start transaction
set tx [duro::begin $dbenv TEST]

# Create overloaded update operator

duro::operator create strmul -updates {a} {a STRING b STRING} {
    upvar $a la

    append la $b
} $tx

duro::operator create strmul -updates {a} {a STRING b INTEGER} {
    upvar $a la

    set h $la
    for {set i 1} {$i < $b} {incr i} {
        append la $h
    }
} $tx

# Create read-only operator
duro::operator create concat -returns STRING {a STRING b STRING} {
    return $a$b
} $tx

# Create read-only operator with relation argument and return value
duro::operator create relop1 -returns {relation {A STRING}} \
        {r {relation {A STRING}}} {
    lappend r {A b}
    return $r
} $tx

# Create read-only operator with tuple argument
duro::operator create tswap -returns {tuple {A STRING} {B STRING}} \
	{r {tuple {A STRING} {B STRING}}} {
    array set a $r
    return [list A $a(B) B $a(A)]
} $tx

# Create update operator with relation argument
duro::operator create relop2 -updates {r1} {r1 {relation {A STRING}} \
        r2 {relation {A STRING}}} {
    foreach i $r2 {
        duro::insert $r1 $i $tx
    }
} $tx

if {![tequal [duro::expr {tswap (TUPLE {A "a", B "b"})} $tx] {A b B a}]} {
    error "unexpected value of tswap()"
}

duro::table create -local t {
   {A STRING}
} {{A}} $tx

duro::insert t {A a} $tx

duro::call relop2 t {relation {A STRING}} {{A b}} {relation {A STRING}} $tx

set arr [duro::array create t {A asc} $tx]
checkarray $arr {{A a} {A b}} $tx
duro::array drop $arr

duro::commit $tx

# Close DB environment
duro::env close $dbenv

# Reopen DB environment
set dbenv [duro::env open tests/dbenv]

set tx [duro::begin $dbenv TEST]

# Invoke update operator
set v foo
duro::call strmul v STRING bar STRING $tx

if {![string equal $v foobar]} {
    error [format "result is %s, should be %s" $v foobar]
}

set i X
set v foo
duro::call strmul v STRING 3 INTEGER $tx

if {![string equal $v foofoofoo]} {
    error [format "result is %s, should be %s" $v foofoofoo]
}

if {$i != "X"} {
    error "global variable was modified by operator"
}

# Invoke read-only operator
set v [duro::expr {concat("X", "Y")} $tx]

if {![string equal $v XY]} {
   error "result is %s, should be %s" $v XY
}

# Destroy operators
duro::operator drop strmul $tx
duro::operator drop concat $tx

# try to invoke deleted operator

if {![catch {
    duro::call strmul v STRING bar STRING $tx
}]} {
    error "Operator invocation should fail, but succeded"
}

set r [duro::expr {relop1(RELATION {TUPLE {A "a"}})} $tx]
set sr {{A a} {A b}}
if {[lsort $r] != $sr} {
    error "relation should be $sr, but is $r"
}

duro::table create -local t {
   {A STRING}
} {{A}} $tx

duro::call relop2 t {relation {A STRING}} {{A b} {A c}} {relation {A STRING}} \
        $tx

set arr [duro::array create t {A asc} $tx]
checkarray $arr {{A b} {A c}} $tx
duro::array drop $arr

duro::table drop t $tx

duro::commit $tx
