# wraptest

`wraptest.c`, `README.upstream.md`, and `results.upstream.txt` are copied
verbatim from wraptest revision `5409c25131a24c2cf150d42f3b4de5cb9c771d6b`.
The upstream repository does not contain a license file or a license notice;
this fact is recorded explicitly rather than assigning it an invented
license.

The 25 cases in `cases.json` are the program's own report rows. Their expected
values are the `DEC architectural behaviour` column of the upstream results
table. Every build target runs the unmodified program through Zutty's real
PTY, allowing the program to issue CPR queries and consume Zutty's replies,
then checks one report row.
