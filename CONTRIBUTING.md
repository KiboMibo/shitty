# Contributing to Shitty

## AI-assisted contributions

The architecture of this project is designed by a human. Every change is
reviewed by a human, and performance work is also profiled and evaluated by a
human. The implementation itself, however, is currently written entirely by
large language models.

The project author believes that, with capable human direction, modern LLMs
write code faster than people and introduce fewer bugs. AI-assisted
contributions are therefore preferred, but they must have a human actively in
the loop. That human must define the problem, direct the design, inspect the
result, understand the tests and profiles, and take responsibility for the
contribution.

Unsupervised, unreviewed AI output is AI slop. It is not useful to this project
and will not be accepted.

The preferred models are Fable and Sol at their highest capability settings.
Opus is also acceptable.

The project prefers community contributions in roughly this order:

1. Excellent issue reports, preferably developed with a capable AI. They
   should contain a precise problem statement and reproduction. Ideally they
   also include a failing test that will pass once the feature or fix is
   implemented.
2. Pull requests produced by an AI with an actively involved human in the
   loop.
3. Conventional human-written code.

## License grant

By committing code to this repository, you agree that your contribution
is dual-licensed, at every recipient's option, under both:

- the GNU General Public License, version 3 or, at the recipient's
  option, any later version (`LICENSE.GPL3`); and
- the MIT License (`LICENSE.MIT`).

The same agreement applies when you submit a contribution by a pull
request, patch, email, or any other means for inclusion in this
repository.  The act of submitting the contribution constitutes your
agreement to this license grant.  It permits the contribution to be
used and distributed under either license, including as part of a
future MIT-only release, without further permission or compensation.

By submitting a contribution, you represent that you have the legal
right and authority to grant these licenses.  Do not submit code that
you cannot license on these terms.

This grant applies only to rights you can grant in your original
contribution.  It does not change the license of imported or
third-party material.

## Licensing direction

The project intends to complete its transition from GPLv3 to the MIT
License after the GPL-only Imported Baseline identified in `LICENSE`
has been removed, rewritten, or separately relicensed.

## Source-file notices

New first-party source files must contain only this license notice; do
not add a GPL notice to a new file:

```text
/*
 * Copyright (C) YYYY Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */
```

Use the corresponding comment syntax for scripts and other source
formats.  A shebang or format-required declaration may remain before
the notice.

When modifying a file derived from the Imported Baseline, add the MIT
notice at the very top but retain every existing GPL and copyright
notice below it.  The MIT notice covers the Shitty contribution layer;
it does not relicense the imported code.

Preserve all third-party copyright and license notices.  Do not add the
Shitty notice to unmodified vendored or third-party sources.
