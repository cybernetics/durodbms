#!/bin/sh

# $Id$

# This file is part of Duro, a relational database library.
# Copyright (C) 2003 Ren� Hartmann
# 
# Duro is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# Duro is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Duro; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

failure () {
    echo Test failed.
    exit 3
}

# Invoke a test application and compare output
calltest () {
    echo $1
    if ./$1 > /tmp/duro$$; then true; else echo $?; failure; fi
    if diff /tmp/duro$$ $1_output; then true; else failure; fi
}

LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PWD/.libs
export LD_LIBRARY_PATH

cd `dirname $0`

calltest tupletest

rm -rf dbenv
mkdir dbenv

if ./prepare; then true; else failure; fi

echo lstables
if ../util/lstables -e dbenv -d TEST | sort > /tmp/duro$$; then true
else echo $?; failure; fi
if diff /tmp/duro$$ lstables_output; then true
else failure; fi

calltest test_select
calltest test_minus
calltest test_union
calltest test_intersect
calltest test_extend
calltest test_join
calltest test_ra
calltest test_create_view
calltest test_print_view
calltest test_keys
calltest test_project
calltest test_summarize
calltest test_rename
calltest test_regexp
calltest test_aggregate
calltest test_update
calltest test_srupdate
calltest test_insert
calltest test_delete
calltest test_null
calltest test_binary
calltest test_deftype
calltest test_utypetable
calltest test_defpointtype
calltest test_pointtable
calltest test_2db
calltest test_defop
calltest test_callop
calltest test_print_opview
calltest test_dropop

rm /tmp/duro$$