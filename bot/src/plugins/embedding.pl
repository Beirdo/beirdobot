#! /usr/bin/perl

package Embed::Persistent;

use strict;
use Symbol qw(delete_package);
#use Devel::Symdump;

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
    local *FH;
    open FH, $filename or die "open '$filename' $!";
    local($/) = undef;
    my $sub = <FH>;
    close FH;

    #wrap the code into a subroutine inside our unique package
    my $eval = qq{package $package; $sub; };
    {
       # hide our variables within this block
       my($filename,$package,$sub);
       eval $eval;
    }
    die $@ if $@;
    
    #take a look if you want
    #print Devel::Symdump->rnew($package)->as_string, $/;
}

sub unload_file {
    my $package = shift;

    delete_package($package);
}
1;

__END__
