# One-off probes

Small standalone programs written to settle a question that the test suite
could not answer, kept because the answer is only as good as the way it was
measured.

- `vkident.cpp` — the four expressions wave 10 changed in `render_vk.cpp`,
  cut out and run: the Vulkan path is byte-identical to its pre-wave values.
  The file itself does not compile in this tree (no Vulkan headers), so this
  is the only way its edit was checked at all.
- `framesib.m` — what the window's frame view holds, and where a backdrop can
  sit without moving `contentView`.
- `default_is_identity.cpp` — the blend from `render_blend.h` over all
  16,777,216 (fg, bg, coverage) triples: at 100 it matches the pre-wave
  formula byte for byte; at 50 it deliberately does not.
- `layer_opaque.mm` — what `CAMetalLayer.opaque` defaults to. The whole
  "reloading 100 → 50 must change nothing" argument rests on it being YES.
- `bad.toml` — a config with a bare word, kept to show that the parser drops
  the rest of the file after it.

Build them by hand; nothing here is part of the build.
