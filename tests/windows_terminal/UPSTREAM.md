# Windows Terminal parser streams

`upstream/StateMachineTest.cpp` and `upstream/OutputEngineTest.cpp` are copied
from Windows Terminal revision with CRLF line endings normalized to LF
`a198a6cac075da15d4b6c21eea65f32caa77f0d8` (2026-07-21). The upstream
project is MIT licensed; its license is preserved as `LICENSE.upstream`.

The catalog statically extracts every literal `ProcessString(L"...")` call,
including adjacent C++ string literals, without compiling or executing the
Windows test framework. C1 wide characters are mapped to the raw 8-bit control
bytes accepted by Zutty. Each call site is an independent build target and is
compared whole versus bytewise across parser events and the full observable
terminal state. Variable-built streams and Windows Terminal's semantic screen
assertions remain for a later adapter.
