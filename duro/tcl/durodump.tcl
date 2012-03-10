#!/bin/sh
# Execute tclsh from the user's PATH \
exec tclsh "$0" ${1+"$@"}

#
# $Id$
#
# Durodump - tool to dump a Duro database environment to a file.
# Copyright (C) 2004-2012 Rene Hartmann.
# See the file COPYING for redistribution information.
#

set duro_version 0.15

package require -exact duro $duro_version

proc dump_rtable {out t tx cr} {
    if {$cr} {
        set alist [duro::table attrs $t $tx]
        puts -nonewline $out "duro::table create $t \{$alist\}"
        puts $out " \{[duro::table keys $t $tx]\} \$tx"
    }

    set a [duro::array create $t $tx]
    duro::array foreach tpl $a {
        puts $out "duro::insert $t \{$tpl\} \$tx" 
    } $tx
    duro::array drop $a
}

proc dump_vtable {out t tx} {
    puts $out "duro::table expr -global $t \{[duro::table def $t $tx]\} \$tx"
}

if {$argc == 0 || $argc > 2} {
    puts "usage: durodump.tcl environment \[output_file\]"
    exit 1
}

if {$argc == 2} {
    set outfile [lindex $argv 1]
} else {
    set outfile restore.tcl
}

set out [open $outfile w 0755]

puts $out "#!/bin/sh"
puts $out "# Execute tclsh from the user's PATH \\"
puts $out {exec tclsh "$0" ${1+"$@"}}
puts $out "# Created by Durodump / Duro $duro_version"
puts $out "package require duro"

puts $out "if {\$argc > 1} {"
puts $out "    puts \"usage: $outfile environment \\\[output_file\\\]\""
puts $out "    exit 1"
puts $out "}"

set dbenvpath [lindex $argv 0]
puts $out "if {\$argc == 1 } {"
puts $out "    set dbenv \[duro::env open \[lindex \$argv 0\]\]"
puts $out "} else {"
puts $out "    set dbenv \[duro::env open \{$dbenvpath\}\]"
puts $out "}"

set dbenv [duro::env open $dbenvpath]
set dbs [duro::env dbs $dbenv]
set tables {}
set types_ops_dumped 0

foreach db $dbs {
    if {$db != {SYS_DB}} { 
	    puts $out "duro::db create \$dbenv $db"
	
	    set tx [duro::begin $dbenv $db]
	
	    puts $out "set tx \[duro::begin \$dbenv $db\]"
	    if {!$types_ops_dumped} {
	        foreach t {sys_types sys_possrepcomps sys_ro_ops sys_upd_ops} {
	            dump_rtable $out $t $tx 0
	        }
	        set types_ops_dumped 1
	    }
	    foreach t [duro::tables -real $tx] {
	        if {[lsearch -exact $tables $t] == -1} {
	            dump_rtable $out $t $tx 1
	            lappend tables $t
	        }
	    }
	    foreach t [duro::tables -virtual $tx] {
	        if {[lsearch -exact $tables $t] == -1} {
	            dump_vtable $out $t $tx
	            lappend tables $t
	        }
	    }
	    puts $out {duro::commit $tx}
	    duro::commit $tx
    }
}

duro::env close $dbenv

puts $out "duro::env close \$dbenv"

close $out
