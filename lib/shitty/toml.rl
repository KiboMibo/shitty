/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/*
 * TOML 1.0 tokenizer. This is a ragel scanner: longest-match tokens with no
 * embedded per-character actions. Grammar-composed variants of this parser
 * (token unions inlined into the array/table productions) make ragel's
 * subset construction explode combinatorially, so the line and container
 * structure is enforced by an explicit pushdown in toml.cpp instead; every
 * token handler receives the [ts, te) slice and does its own decoding.
 *
 * Longest match resolves the classic TOML lexing headaches by itself: the
 * space-separated datetime "1979-05-27 07:32:00" is one token exactly when
 * a time follows, "123abc" lexes as a bare key (longest) and then fails in
 * value position, and "01" never lexes as an integer. Multiline strings
 * cannot swallow a following "...""" because their interior only admits
 * quote runs of length one or two when content follows.
 */

%%{
    machine toml;
    alphtype unsigned char;
    access parser.;
    variable p p;
    variable pe pe;
    variable eof eof;

    utf8Tail = 0x80..0xBF;
    utf8Seq = (
        0xC2..0xDF utf8Tail |
        0xE0 0xA0..0xBF utf8Tail |
        0xE1..0xEC utf8Tail utf8Tail |
        0xED 0x80..0x9F utf8Tail |
        0xEE..0xEF utf8Tail utf8Tail |
        0xF0 0x90..0xBF utf8Tail utf8Tail |
        0xF1..0xF3 utf8Tail utf8Tail utf8Tail |
        0xF4 0x80..0x8F utf8Tail utf8Tail
    );

    wschar = [\t ];
    nl = '\r'? '\n';

    # Characters legal inside basic (double-quoted) strings, minus '"' and
    # backslash; inside literal (single-quoted) strings, minus the quote.
    basicChar = 0x09 | 0x20..0x21 | 0x23..0x5B | 0x5D..0x7E | utf8Seq;
    literalChar = 0x09 | 0x20..0x26 | 0x28..0x7E | utf8Seq;

    escape = '\\' ([\"\\bfnrt] | 'u' xdigit{4} | 'U' xdigit{8});
    escapedNl = '\\' wschar* nl (wschar | nl)*;

    basicString = '"' (basicChar | escape)* '"';
    literalString = '\'' literalChar* '\'';

    # Multiline bodies admit a quote run of one or two only when content
    # follows it, so three consecutive quotes always close the string and
    # the token cannot extend across a delimiter.
    mlBasicUnit = basicChar | nl | escape | escapedNl;
    mlBasicString = '"""' nl? (mlBasicUnit | '"' mlBasicUnit | '""' mlBasicUnit)* ('"' '"'?)? '"""';
    mlLiteralUnit = literalChar | nl;
    mlLiteralString = '\'\'\'' nl? (mlLiteralUnit | '\'' mlLiteralUnit | '\'\'' mlLiteralUnit)* ('\'' '\''?)? '\'\'\'';

    sign = [+\-];
    decUnsigned = '0' | [1-9] ('_'? digit)*;
    hexOctBin = '0x' xdigit ('_'? xdigit)* | '0o' [0-7] ('_'? [0-7])* | '0b' [01] ('_'? [01])*;
    integer = sign? decUnsigned | hexOctBin;
    frac = '.' digit ('_'? digit)*;
    exponent = [eE] sign? digit ('_'? digit)*;
    float = sign? (decUnsigned (frac exponent? | exponent) | 'inf' | 'nan');
    boolean = 'true' | 'false';

    dateMonth = '0' [1-9] | '1' [0-2];
    dateDay = '0' [1-9] | [12] digit | '3' [01];
    timeHour = [01] digit | '2' [0-3];
    timeMinute = [0-5] digit;
    timeSecond = [0-5] digit | '60';
    fullDate = digit{4} '-' dateMonth '-' dateDay;
    partialTime = timeHour ':' timeMinute ':' timeSecond ('.' digit+)?;
    timeOffset = [Zz] | sign timeHour ':' timeMinute;
    moment = fullDate ([Tt ] partialTime timeOffset?)? | partialTime;

    bareKey = (alnum | '_' | '-')+;
    comment = '#' (0x09 | 0x20..0x7E | utf8Seq)*;

    main := |*
        wschar+;
        nl => { if (!parser.onNewline()) { fbreak; } };
        comment;
        '[' => { if (!parser.onPunct(TomlParser::Punct::BracketOpen, parser.ts, parser.te)) { fbreak; } };
        ']' => { if (!parser.onPunct(TomlParser::Punct::BracketClose, parser.ts, parser.te)) { fbreak; } };
        '{' => { if (!parser.onPunct(TomlParser::Punct::BraceOpen, parser.ts, parser.te)) { fbreak; } };
        '}' => { if (!parser.onPunct(TomlParser::Punct::BraceClose, parser.ts, parser.te)) { fbreak; } };
        '=' => { if (!parser.onPunct(TomlParser::Punct::Equals, parser.ts, parser.te)) { fbreak; } };
        ',' => { if (!parser.onPunct(TomlParser::Punct::Comma, parser.ts, parser.te)) { fbreak; } };
        '.' => { if (!parser.onPunct(TomlParser::Punct::Dot, parser.ts, parser.te)) { fbreak; } };
        boolean => { if (!parser.onScalar(parser.ts, parser.te)) { fbreak; } };
        moment => { if (!parser.onScalar(parser.ts, parser.te)) { fbreak; } };
        integer => { if (!parser.onScalar(parser.ts, parser.te)) { fbreak; } };
        float => { if (!parser.onScalar(parser.ts, parser.te)) { fbreak; } };
        bareKey => { if (!parser.onBareWord(parser.ts, parser.te)) { fbreak; } };
        basicString => { if (!parser.onString(parser.ts, parser.te, TomlParser::Quotes::Basic)) { fbreak; } };
        literalString => { if (!parser.onString(parser.ts, parser.te, TomlParser::Quotes::Literal)) { fbreak; } };
        mlBasicString => { if (!parser.onString(parser.ts, parser.te, TomlParser::Quotes::MlBasic)) { fbreak; } };
        mlLiteralString => { if (!parser.onString(parser.ts, parser.te, TomlParser::Quotes::MlLiteral)) { fbreak; } };
    *|;
}%%

#if defined(SHITTY_TOML_DATA)
%% write data;
#elif defined(SHITTY_TOML_INIT)
%% write init;
#elif defined(SHITTY_TOML_EXEC)
%% write exec;
#endif
