package Text::CharWidth;

use strict;
use warnings;
use Exporter 'import';
use Unicode::UCD qw(charprop);

our @EXPORT_OK = qw(mbswidth);

sub mbswidth {
    my ($text) = @_;
    my $width = 0;
    for my $character (split(//, $text)) {
        my $codepoint = ord($character);
        my $category = charprop($codepoint, 'General_Category') // '';
        return -1 if $category eq 'Control' or $category eq 'Surrogate';
        next if $category =~ /_Mark$/ or $category eq 'Format';
        my $east_asian = charprop($codepoint, 'East_Asian_Width') // '';
        $width += (
            $east_asian eq 'Wide' or $east_asian eq 'Fullwidth'
        ) ? 2 : 1;
    }
    return $width;
}

1;
