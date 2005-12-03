#!/bin/sh
# Execute tclsh from the user's PATH \
exec tclsh "$0"

# $Id$
#
# Test create, insert with UNION, MINUS, INTERSECT, JOIN, RENAME, EXTEND
#

load .libs/libdurotcl.so

source tests/testutil.tcl

# Create DB environment
file delete -force tests/dbenv
file mkdir tests/dbenv

set dbenv [duro::env open tests/dbenv]

# Create Database
duro::db create $dbenv TEST

set tx [duro::begin $dbenv TEST]

# Create real tables
duro::table create T {
   {K INTEGER}
   {S1 STRING}
} {{K}} $tx

duro::table rename T T1 $tx

duro::table create T2 {
   {K INTEGER}
   {S1 STRING}
} {{K}} $tx

duro::table create T3 {
   {K INTEGER}
   {S2 STRING}
} {{K}} $tx

duro::table create T4 {
   {A INTEGER}
   {B INTEGER}
} {{A B}} $tx

# Insert tuples
duro::insert T1 {K 1 S1 Bla} $tx

duro::insert T1 {K 2 S1 Blubb} $tx

duro::insert T2 {K 1 S1 Bla} $tx

duro::insert T2 {K 2 S1 Blipp} $tx

duro::insert T3 {K 1 S2 A} $tx

duro::insert T3 {K 2 S2 B} $tx

duro::insert T4 {A 1 B 1} $tx

duro::insert T4 {A 1 B 2} $tx

duro::insert T4 {A 2 B 1} $tx

duro::insert T4 {A 2 B 3} $tx

# Create virtual tables

duro::table expr -global TU {T1 UNION T2} $tx
duro::table expr -global TM {T1 MINUS T2} $tx
duro::table expr -global TI {T1 INTERSECT T2} $tx
duro::table expr -global VT {T1 JOIN T3} $tx
duro::table rename VT TJ $tx
duro::table expr -global TR {T1 RENAME (K AS KN, S1 AS SN)} $tx
if {![catch {
    duro::table expr -global TX \
            {EXTEND T1 ADD (K*10 AS K0, IF K = 0 THEN "YES" ELSE 1 AS KIS1)} $tx
}]} {
    error "invalid EXTEND should fail, but succeeded"
}
if {![string match "TYPE_MISMATCH_ERROR(*)" [lindex $errorCode 1]]} {
    error "Wrong error: $errorCode"
}

duro::table expr -global TX {EXTEND T1 ADD (K*10 AS K0, IF K = 0 THEN "YES" ELSE
"NO" AS KIS1)} $tx

duro::table expr -global TP {(T1 WHERE K = 1) {K}} $tx
duro::table expr -global TP2 {T1 {S1}} $tx
duro::table expr -global TM2 {(T1 WHERE K = 1) MINUS (T2 WHERE K = 1)} $tx
duro::table expr -global TI2 {(T1 WHERE K = 1) INTERSECT (T2 WHERE K = 1)} $tx
duro::table expr -global TJ2 {(T1 WHERE K = 2) JOIN (T3 WHERE K = 2)} $tx
duro::table expr -global TS {T1 WHERE S1 > "Bla"} $tx
duro::table expr -global TSM {SUMMARIZE T4 PER T4 { A }
        ADD (MIN(B) AS MIN_B, MAX(B) AS MAX_B, COUNT AS CNT)} $tx

duro::table expr -global TS2 {T1 WHERE K = 1} $tx

# Create table again - must fail
if {![catch {
    duro::table expr -global TS2 {T1 WHERE K = 1} $tx
}]} {
    error "creating TS2 a second time should fail, but succeeded"
}

if {![string match "ELEMENT_EXISTS_ERROR(*)" [lindex $errorCode 1]]} {
    error "Wrong error: $errorCode"
}

duro::table expr ts2 {T1 WHERE K = 1} $tx

# Create table again - must fail
if {![catch {
    duro::table expr ts2 {T1 WHERE K = 1} $tx
}]} {
    error "creating ts2 a second time should fail, but succeeded"
}
set code [lindex $errorCode 1]
if {![string match "RDB_ELEMENT_EXISTS_ERROR(*)" $code]} {
    error "Wrong error: $errorCode"
}

if {[duro::expr {TUPLE FROM TP} $tx] != {K 1}} {
    error "Tuple value should be {K 1}, but is not"
}

set tpl [duro::expr {TUPLE FROM TJ2} $tx]
if {![tequal $tpl {K 2 S1 Blubb S2 B}]} {
    error "Invalid value of TUPLE FROM TJ2"
}

set da [duro::array create TSM {A asc} $tx]
checkarray $da {{A 1 MIN_B 1 MAX_B 2 CNT 2} {A 2 MIN_B 1 MAX_B 3 CNT 2}} $tx
duro::array drop $da

if {![catch {duro::table expr -global TX2 {EXTEND T1 ADD (K*10 AS K)} $tx}]} {
    error "invalid EXTEND should fail, but succeeded"
}
if {![string match "INVALID_ARGUMENT_ERROR(*)" [lindex $errorCode 1]]} {
    error "Wrong error: $errorCode"
}

#
# Referring to an attribute which has been removed by project
# must fail
#

if {![catch {duro::update TP2 {K = 1} S1 {"Bli"} $tx}]} {
    error "duro::update should fail, but succeeded"
}

set err [lindex $errorCode 1]
if {![string match "ATTRIBUTE_NOT_FOUND_ERROR(*)" $err]} {
    error "wrong error code: $err"
}
if {![string match "*K*" [duro::expr THE_INFO($err) $tx]]} {
    error "error info should contain attribute type, but does not"
}

if {![catch {duro::update TP2 {S1 = "Bla"} K 3 $tx}]} {
    error "duro::update should fail, but succeeded"
}

set code [lindex $errorCode 1]
if {![string match "ATTRIBUTE_NOT_FOUND_ERROR(*)" $code]} {
    error "wrong error code: $code"
}

if {![catch {duro::update TP2 {K = 1} S1 {"Blb"} $tx}]} {
    error "duro::update should fail, but succeeded"
}

set code [lindex $errorCode 1]
if {![string match "ATTRIBUTE_NOT_FOUND_ERROR(*)" $code]} {
    error "wrong error code: $code"
}

if {![catch {duro::delete TP2 {K = 1} $tx}]} {
    error "duro::delete should fail, but succeeded"
}

set code [lindex $errorCode 1]
if {![string match "ATTRIBUTE_NOT_FOUND_ERROR(*)" $code]} {
    error "wrong error code: $code"
}

duro::commit $tx

# Close DB environment
duro::env close $dbenv

# Reopen DB environment
set dbenv [duro::env open tests/dbenv]

set tx [duro::begin $dbenv TEST]

set da [duro::array create TSM {A asc} $tx]
checkarray $da {{A 1 MIN_B 1 MAX_B 2 CNT 2} {A 2 MIN_B 1 MAX_B 3 CNT 2}} $tx
duro::array drop $da

if {![catch {duro::delete TP {S1 = "Bla"} $tx}]} {
    error "duro::delete on TP should fail, but succeeded"
}

set tpl {K 0 S1 Bold S2 Z}

duro::insert TJ $tpl $tx

if {![duro::table contains TJ $tpl $tx]} {
    error "Insert into TJ was not successful."
}

set da [duro::array create TJ {K asc S1 asc} $tx]
checkarray $da {{K 0 S1 Bold S2 Z} {K 1 S1 Bla S2 A} {K 2 S1 Blubb S2 B}} $tx
duro::array drop $da

set tpl {K 3 S1 Blu}

duro::insert TI $tpl $tx

if {![duro::table contains TI $tpl $tx]} {
    error "Insert into TI was not successful."
}

set tpl {KN 5 SN Ballermann}

duro::insert TR $tpl $tx

if {![duro::table contains TR $tpl $tx]} {
    error "Insert into TR was not successful."
}

set da [duro::array create TR {KN asc} $tx]
checkarray $da {{KN 0 SN Bold} {KN 1 SN Bla} {KN 2 SN Blubb} {KN 3 SN Blu}
        {KN 5 SN Ballermann}} $tx
duro::array drop $da

duro::delete TR {KN = 1} $tx

set da [duro::array create TR {KN asc} $tx]
checkarray $da {{KN 0 SN Bold} {KN 2 SN Blubb} {KN 3 SN Blu}
        {KN 5 SN Ballermann}} $tx
duro::array drop $da

duro::delete TR {KN >= 4} $tx

set da [duro::array create TR {KN asc} $tx]
checkarray $da {{KN 0 SN Bold} {KN 2 SN Blubb} {KN 3 SN Blu}} $tx
duro::array drop $da

duro::delete TR {SN = "Bold"} $tx

set da [duro::array create TR {KN asc} $tx]
checkarray $da {{KN 2 SN Blubb} {KN 3 SN Blu}} $tx
duro::array drop $da

# duro::delete TM {K = 5} $tx

set tpl [duro::expr {TUPLE FROM TM} $tx]
set stpl {K 2 S1 Blubb}
if {![tequal $tpl $stpl]} {
    error "TUPLE FROM TM should be $stpl, but is $tpl"
}

duro::delete TX {K0 = 40} $tx

set tpl [duro::expr {TUPLE FROM (TX WHERE K0 >= 30)} $tx]
set stpl {K 3 S1 Blu K0 30 KIS1 NO}
if {![tequal $tpl $stpl]} {
    error "Tuple should be $stpl, but is $tpl"
}

if {![catch {duro::insert TS {K 4 S1 B} $tx}]} {
    error "Insertion into TS should fail, but succeeded"
}
if {![string match "PREDICATE_VIOLATION_ERROR(*)" [lindex $errorCode 1]]} {
    error "wrong errorCode: $errorCode"
}

duro::insert TS {K 4 S1 Blb} $tx

duro::delete TS {S1 >= "Blu"} $tx
set tpl [duro::expr {TUPLE FROM TS} $tx]
if {![tequal $tpl {K 4 S1 Blb}]} {
    error "Unexpected tuple value: $tpl"
}

duro::table drop TU $tx

duro::table drop TX $tx

# Try to drop TX a second time
if {![catch {duro::table drop TX $tx}]} {
    error "Dropping TX should fail, but succeeded"
}

# Recreate TU
duro::table expr -global TU {T1 UNION T2} $tx

#
# Check if persistent virtual tables can depend on named transient tables
#

duro::table create -local LT {
    {A INTEGER}
} {{A}} $tx

if {![catch {duro::table expr -global TL {LT { A }} $tx}]} {
    error "Creation of TL should fail, but succeeded"
}

duro::table expr LTL {LT { A }} $tx

if {![catch {duro::table drop LT $tx}]} {
    error "Dropping TL should fail, but succeeded"
}

#
# Check empty attribute list
#
duro::table expr -global TP3 {T1 {}} $tx

#
# Check invalid expressions
#
if {![catch {duro::table expr -global ES {T1 WHERE S1 = 2} $tx}]} {
    error "Table creation should fail, but succeded"
}
if {![string match "TYPE_MISMATCH_ERROR(*)" [lindex $::errorCode 1]]} {
    error "Wrong error: $::errorCode"
}

if {![catch {duro::table expr -global ES {T1 WHERE S1 > 2} $tx}]} {
    error "Table creation should fail, but succeded"
}
if {![string match "TYPE_MISMATCH_ERROR(*)" [lindex $::errorCode 1]]} {
    error "Wrong error: $::errorCode"
}

if {![catch {duro::table expr -global ES {T1 WHERE I > 2} $tx}]} {
    error "Table creation should fail, but succeded"
}
if {![string match "ATTRIBUTE_NOT_FOUND_ERROR(*)" [lindex $::errorCode 1]]} {
    error "Wrong error: $::errorCode"
}

duro::table drop LTL $tx
duro::table drop LT $tx

#
# Check RDB_expr_type()
#
duro::table expr t {EXTEND TABLE_DEE ADD ((EXTEND TABLE_DEE ADD (1 AS N)) \
        JOIN (EXTEND TABLE_DEE ADD (2 AS M)) AS R)} $tx

duro::table drop t $tx

duro::table expr t {EXTEND TABLE_DEE ADD ((EXTEND TABLE_DEE ADD (1 AS N)) \
        { N } AS R)} $tx

duro::table drop t $tx

duro::table expr t {EXTEND TABLE_DEE
        ADD (EXTEND TABLE_DEE ADD (1 AS N) WRAP ({ N } AS T) AS R) } $tx

duro::table drop t $tx

duro::table expr t {EXTEND TABLE_DEE
        ADD (EXTEND TABLE_DEE ADD (1 AS N, 2 AS M) { ALL BUT N } AS R)} $tx

set tpl [duro::expr {(TUPLE FROM t).R} $tx]
if {$tpl != "{M 2}"} {
    error "wrong tuple value: $tpl"
}

duro::table drop t $tx

duro::table expr t {EXTEND TABLE_DEE
        ADD (EXTEND TABLE_DEE ADD (1 AS N, 2 AS M)
             GROUP { N, M } AS R AS R)} $tx

duro::table drop t $tx

duro::table expr t {EXTEND TABLE_DEE
        ADD (SUMMARIZE (EXTEND TABLE_DEE ADD (1 AS N))
             PER TABLE_DEE ADD (COUNT AS C) AS R)} $tx

duro::table drop t $tx

duro::table expr t {EXTEND TABLE_DEE
        ADD ((EXTEND TABLE_DEE ADD (1 AS N, 2 AS M)
             GROUP { N, M } AS R) UNGROUP R AS R)} $tx

duro::table drop t $tx

duro::table expr t {EXTEND TABLE_DEE
        ADD ((EXTEND TABLE_DEE ADD (1 AS N, 2 AS M)
             WRAP ({ N, M } AS T)) UNWRAP (T) AS T)} $tx

duro::table drop t $tx

duro::commit $tx

# Close DB environment
duro::env close $dbenv
