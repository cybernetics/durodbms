#!/bin/sh
# Execute tclsh from the user's PATH \
exec tclsh "$0"

# $Id$
#
# Test create, insert with UNION, INTERSECT, JOIN, RENAME
#

load .libs/libdurotcl.so

# Compare tuples
proc tequal {t1 t2} {
    return [string equal [lsort $t1] [lsort $t2]]
}

# Create DB environment
file delete -force tests/dbenv
file mkdir tests/dbenv

set dbenv [duro::env create tests/dbenv]

# Create Database
duro::db create TEST $dbenv

set tx [duro::begin $dbenv TEST]

# Create real tables
duro::table create T1 {
   {K INTEGER}
   {S1 STRING}
} {{K}} $tx

duro::table create T2 {
   {K INTEGER}
   {S1 STRING}
} {{K}} $tx

duro::table create T3 {
   {K INTEGER}
   {S2 STRING}
} {{K}} $tx

# Insert tuples
set r [duro::insert T1 {K 1 S1 Bla} $tx]
if {$r != 0} {
    puts "Insert returned $r, should be 0"
    exit 1
}

duro::insert T1 {K 2 S1 Blubb} $tx

duro::insert T2 {K 1 S1 Bla} $tx

duro::insert T2 {K 2 S1 Blipp} $tx

duro::insert T3 {K 1 S2 A} $tx

duro::insert T3 {K 2 S2 B} $tx

# Create virtual tables

duro::table expr -global TU {T1 UNION T2} $tx
duro::table expr -global TM {T1 MINUS T2} $tx
duro::table expr -global TI {T1 INTERSECT T2} $tx
duro::table expr -global TJ {TU JOIN T3} $tx
duro::table expr -global TR {T1 RENAME (K AS KN, S1 AS SN)} $tx

duro::commit $tx

# Close DB environment
duro::env close $dbenv

# Reopen DB environment
set dbenv [duro::env create tests/dbenv]

set tx [duro::begin $dbenv TEST]

set tpl {K 0 S1 Bold S2 Z}

duro::insert TJ $tpl $tx

if {![duro::table contains TJ $tpl $tx]} {
    puts "Insert into TJ was not successful."
    exit 1
}

set tpl {K 3 S1 Blu}

duro::insert TI $tpl $tx

if {![duro::table contains TI $tpl $tx]} {
    puts "Insert into TI was not successful."
    exit 1
}

set tpl {K 4 S1 Buchara}

duro::insert TU $tpl $tx

if {![duro::table contains TU $tpl $tx]} {
    puts "Insert into TU was not successful."
    exit 1
}

set tpl {KN 5 SN Ballermann}

duro::insert TR $tpl $tx

if {![duro::table contains TR $tpl $tx]} {
    puts "Insert into TR was not successful."
    exit 1
}

duro::commit $tx

# Close DB environment
duro::env close $dbenv