#! /usr/bin/perl

package Embed::Persistent;

use strict;
our %Cache;
use Symbol qw(delete_package);

sub valid_package_name {
    my($string) = @_;
    $string =~ s/^.*\///;
    $string =~ s/\.pl$//i;
    $string =~ s/([^A-Za-z0-9\/])/sprintf("_%2x",unpack("C",$1))/eg;
    # second pass only for words starting with a digit
    $string =~ s|/(\d)|sprintf("/_%2x",unpack("C",$1))|eg;

    return "BeirdoBot::" . $string;
}

sub eval_file {
    my($filename) = @_;
    my $package = valid_package_name($filename);
    my $mtime = -M $filename;
    my $delete = 0;
    if(defined $Cache{$package}{mtime}
       &&
       $Cache{$package}{mtime} <= $mtime)
    {
       # we have compiled this subroutine already,
       # it has not been updated on disk, nothing left to do
       print STDERR "already compiled $package->handler\n";
    }
    else {
       local *FH;
       open FH, $filename or die "open '$filename' $!";
       local($/) = undef;
       my $sub = <FH>;
       close FH;

       #wrap the code into a subroutine inside our unique package
       my $eval = qq{package $package; sub handler { $sub; }};
       {
	   # hide our variables within this block
	   my($filename,$mtime,$package,$sub);
	   eval $eval;
       }
       die $@ if $@;

       #cache it unless we're cleaning out each time
       $Cache{$package}{mtime} = $mtime unless $delete;
    }

    eval {$package->handler;};
    die $@ if $@;

    delete_package($package) if $delete;

    #take a look if you want
    #print Devel::Symdump->rnew($package)->as_string, $/;
}

sub unload_file {
    my $package = shift;

    delete_package($package);
}
1;

__END__
