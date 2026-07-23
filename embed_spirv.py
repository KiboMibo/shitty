#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


import struct
import sys
from pathlib import Path


def main() -> None:
    source = Path(sys.argv[1]).read_bytes()
    if len(source) % 4 != 0:
        raise ValueError("SPIR-V size is not a multiple of four bytes")

    words = struct.unpack(f"<{len(source) // 4}I", source)
    rows = []
    for offset in range(0, len(words), 8):
        chunk = words[offset:offset + 8]
        values = ", ".join(f"0x{word:08x}u" for word in chunk)
        rows.append(f"    {values},")

    output = "\n".join([
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <std/sys/types.h>",
        "",
        "inline constexpr u32 renderShaderSpv[] = {",
        *rows,
        "};",
        "inline constexpr size_t renderShaderSpvSize = sizeof(renderShaderSpv);",
        "",
    ])
    Path(sys.argv[2]).write_text(output, encoding="utf-8")


if __name__ == "__main__":
    main()
