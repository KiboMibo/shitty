package Term::ReadKey;

use strict;
use warnings;
use Exporter 'import';

our @EXPORT = qw(ReadKey ReadLine ReadMode GetTerminalSize);

sub ReadMode {
    my ($mode) = @_;
    if ($mode eq 'raw' or $mode eq 'ultra-raw' or $mode eq 'cbreak'
            or $mode eq '1') {
        system('stty', 'raw', '-echo');
    } else {
        system('stty', 'sane');
    }
}

sub ReadKey {
    my ($timeout) = @_;
    if ($timeout > 0) {
        my $readable = '';
        vec($readable, fileno(STDIN), 1) = 1;
        return undef unless select($readable, undef, undef, $timeout);
    }
    my $character = '';
    return undef unless sysread(STDIN, $character, 1);
    return $character;
}

sub ReadLine {
    return scalar <STDIN>;
}

sub GetTerminalSize {
    my ($handle) = @_;
    $handle = *STDIN unless defined $handle;
    my $size = pack('S4', 0, 0, 0, 0);
    return (80, 25, 0, 0) unless ioctl($handle, 0x5413, $size);
    my ($rows, $columns, $pixel_width, $pixel_height) = unpack('S4', $size);
    return ($columns, $rows, $pixel_width, $pixel_height);
}

1;
