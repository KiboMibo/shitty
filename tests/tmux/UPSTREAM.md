# tmux OSS-Fuzz input corpus

The 4,166 files in `corpus/` reproduce the official OSS-Fuzz tmux seed
corpus recipe at OSS-Fuzz revision
`ea7eeb3ee4de363d97c5067b9b72379423f63da2`. The recipe concatenates and
splits each source into at most 512-byte members, matching
`input-fuzzer.options`:

- `24-bit-color.sh`, `256colors.pl`, and `UTF-8-demo.txt` from tmux revision
  `7abb9af06236eb9def862bb88a82792f6c846bef`;
- Alacritty, esctest, and iTerm2 streams from `tmux-fuzzing-corpus` revision
  `73b1e642654f90279f62bfbf91aa6eb0b3b98646`.

The relevant upstream harness, options, dictionary, and generator inputs are
preserved in `upstream/`. The tmux and source-corpus licenses are preserved
beside this file. The adapter feeds every member whole and across
deterministic parser boundaries, comparing PTY output, frontend actions,
printer output, protocol modes, render state, hyperlink targets, and Zutty's
full rich model state.
