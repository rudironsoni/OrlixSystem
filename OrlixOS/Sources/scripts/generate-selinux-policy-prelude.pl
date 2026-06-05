#!/usr/bin/env perl
use strict;
use warnings;

my ($classmap_path, $initial_sid_path) = @ARGV;
die "usage: $0 <classmap.h> <initial_sid_to_string.h>\n"
	unless defined $classmap_path && defined $initial_sid_path;

sub read_file {
	my ($path) = @_;
	open my $fh, '<', $path or die "open $path: $!\n";
	local $/;
	return <$fh>;
}

sub parse_string_list {
	my ($text, $macros) = @_;
	my @items;
	for my $token (split /,/, $text) {
		$token =~ s/^\s+|\s+$//g;
		next if $token eq '' || $token eq 'NULL';
		if ($token =~ /^"([^"]+)"$/) {
			push @items, $1;
		} elsif (exists $macros->{$token}) {
			push @items, @{$macros->{$token}};
		} else {
			die "unknown SELinux permission token '$token'\n";
		}
	}
	return @items;
}

my $classmap = read_file($classmap_path);
$classmap =~ s/\\\n/ /g;

my %macros;
while ($classmap =~ /^#define\s+(COMMON_[A-Z0-9_]+)\s+(.+)$/mg) {
	my ($name, $body) = ($1, $2);
	$macros{$name} = [ parse_string_list($body, \%macros) ];
}

my @classes;
while ($classmap =~ /\{\s*"([^"]+)"\s*,\s*\{(.*?)\}\s*\}/sg) {
	my ($class, $body) = ($1, $2);
	my @perms = parse_string_list($body, \%macros);
	next unless @perms;
	push @classes, [ $class, \@perms ];
}

my $initial_sids = read_file($initial_sid_path);
my @sids;
while ($initial_sids =~ /^\s*"([^"]+)"\s*,/mg) {
	push @sids, $1;
}

print "# Generated from upstream Linux SELinux class and initial SID maps.\n";
print "# Do not edit this generated prelude; edit the Linux source or policy body.\n";
for my $entry (@classes) {
	print "class $entry->[0]\n";
}
print "\n";
for my $sid (@sids) {
	print "sid $sid\n";
}
print "\n";
for my $entry (@classes) {
	my ($class, $perms) = @$entry;
	print "class $class { ", join(' ', @$perms), " }\n";
}

print "\n";
print "# ORLIX_SELINUX_ALL_CLASSES ", join(' ', map { $_->[0] } @classes), "\n";
