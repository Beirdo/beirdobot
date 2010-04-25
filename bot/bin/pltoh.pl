#! /usr/bin/perl -w
# vim:ts=4:sw=4:ai:et:si:sts=4
#
#   This file is part of the beirdobot package
#   Copyright (C) 2010 Gavin Hurlbut
# 
#   beirdobot is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
# 
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
# 
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Copyright 2010 Gavin Hurlbut
# All rights reserved
#

use strict;

my $header = <<_EOF;
/*
 *  This file is part of the beirdobot package
 *  Copyright (C) 2010 Gavin Hurlbut
 *
 *  beirdobot is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*HEADER---------------------------------------------------
*
* Copyright 2010 Gavin Hurlbut
* All rights reserved
*
*/

_EOF

while( 1 ) {
    my $fh;
    my $out;
    my $filename = shift;
    last if not defined( $filename );
    open $fh, "<$filename"    or next;
    open $out, ">$filename.h" or next;

    my $define = uc $filename;
    $define =~ s/\./_/g;

    print $out $header;

    print $out "#ifndef $define"."_h_\n#define $define"."_h_\n\n";
    print $out "#define $define \\\n";

    while( <$fh> ) {
        chomp;
        my $line = $_;
        $line =~ s/(["\\])/\\$1/g;
        print $out "    \"$line\\n\" \\\n";
    }

    print $out "    \"\"\n";
    print $out "\n#endif\n";

    close $out;
    close $fh;
}
