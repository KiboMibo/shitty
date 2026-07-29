#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


MUTABLE = 1
EXTENDED = 2


@dataclass(frozen=True)
class Variant:
    name: str
    present: str
    color_space: str
    view: str
    declaration: str
    store: str
    flags: int = 0


FLOAT_DECLARATIONS = {
    "rgba8": "layout (set = 0, binding = 0, rgba8) uniform writeonly image2D outputImage;",
    "rgb10_a2": "layout (set = 0, binding = 0, rgb10_a2) uniform writeonly image2D outputImage;",
    "rgba16": "layout (set = 0, binding = 0, rgba16) uniform writeonly image2D outputImage;",
    "rgba16f": "layout (set = 0, binding = 0, rgba16f) uniform writeonly image2D outputImage;",
}


def float_store(expression: str) -> str:
    return f"   imageStore (outputImage, position, vec4 ({expression}, 1.0));"


SRGB_ENCODE = """mix (
      color * 12.92,
      1.055 * pow (color, vec3 (1.0 / 2.4)) - 0.055,
      greaterThan (color, vec3 (0.0031308)))"""

SRGB_DECODE = """mix (
      color / 12.92,
      pow ((color + 0.055) / 1.055, vec3 (2.4)),
      greaterThan (color, vec3 (0.04045)))"""


def packed16_store(
    scales: str, red: str, green: str, blue: str, alpha: str
) -> str:
    return "\n".join([
        "   uvec4 value = uvec4 (round (clamp (vec4 (color, 1.0), 0.0, 1.0) *",
        f"                               vec4 ({scales})));",
        f"   uint packed = ({red}) | ({green}) | ({blue}) | ({alpha});",
        "   imageStore (outputImage, position, uvec4 (packed, 0u, 0u, 0u));",
    ])


R16_DECLARATION = (
    "layout (set = 0, binding = 0, r16ui) "
    "uniform writeonly uimage2D outputImage;"
)


VARIANTS = (
    Variant(
        "rgba8_unorm",
        "VK_FORMAT_R8G8B8A8_UNORM",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R8G8B8A8_UNORM",
        FLOAT_DECLARATIONS["rgba8"],
        float_store("color"),
    ),
    Variant(
        "bgra8_unorm",
        "VK_FORMAT_B8G8R8A8_UNORM",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R8G8B8A8_UNORM",
        FLOAT_DECLARATIONS["rgba8"],
        float_store("color.bgr"),
        MUTABLE,
    ),
    Variant(
        "a8b8g8r8_unorm",
        "VK_FORMAT_A8B8G8R8_UNORM_PACK32",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R8G8B8A8_UNORM",
        FLOAT_DECLARATIONS["rgba8"],
        float_store("color"),
        MUTABLE,
    ),
    Variant(
        "rgba8_srgb",
        "VK_FORMAT_R8G8B8A8_SRGB",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R8G8B8A8_UNORM",
        FLOAT_DECLARATIONS["rgba8"],
        float_store(SRGB_ENCODE),
        MUTABLE,
    ),
    Variant(
        "bgra8_srgb",
        "VK_FORMAT_B8G8R8A8_SRGB",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R8G8B8A8_UNORM",
        FLOAT_DECLARATIONS["rgba8"],
        float_store(f"({SRGB_ENCODE}).bgr"),
        MUTABLE,
    ),
    Variant(
        "a8b8g8r8_srgb",
        "VK_FORMAT_A8B8G8R8_SRGB_PACK32",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R8G8B8A8_UNORM",
        FLOAT_DECLARATIONS["rgba8"],
        float_store(SRGB_ENCODE),
        MUTABLE,
    ),
    Variant(
        "a2b10g10r10_unorm",
        "VK_FORMAT_A2B10G10R10_UNORM_PACK32",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_A2B10G10R10_UNORM_PACK32",
        FLOAT_DECLARATIONS["rgb10_a2"],
        float_store("color"),
        EXTENDED,
    ),
    Variant(
        "a2r10g10b10_unorm",
        "VK_FORMAT_A2R10G10B10_UNORM_PACK32",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_A2B10G10R10_UNORM_PACK32",
        FLOAT_DECLARATIONS["rgb10_a2"],
        float_store("color.bgr"),
        MUTABLE | EXTENDED,
    ),
    Variant(
        "rgba16_unorm",
        "VK_FORMAT_R16G16B16A16_UNORM",
        "VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT",
        "VK_FORMAT_R16G16B16A16_UNORM",
        FLOAT_DECLARATIONS["rgba16"],
        float_store("color"),
        EXTENDED,
    ),
    Variant(
        "rgba16_sfloat_linear",
        "VK_FORMAT_R16G16B16A16_SFLOAT",
        "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT",
        "VK_FORMAT_R16G16B16A16_SFLOAT",
        FLOAT_DECLARATIONS["rgba16f"],
        float_store(SRGB_DECODE),
        EXTENDED,
    ),
    Variant(
        "r5g6b5_unorm",
        "VK_FORMAT_R5G6B5_UNORM_PACK16",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R16_UINT",
        R16_DECLARATION,
        packed16_store(
            "31.0, 63.0, 31.0, 0.0",
            "(value.r & 31u) << 11",
            "(value.g & 63u) << 5",
            "value.b & 31u",
            "0u",
        ),
        MUTABLE | EXTENDED,
    ),
    Variant(
        "b5g6r5_unorm",
        "VK_FORMAT_B5G6R5_UNORM_PACK16",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R16_UINT",
        R16_DECLARATION,
        packed16_store(
            "31.0, 63.0, 31.0, 0.0",
            "value.r & 31u",
            "(value.g & 63u) << 5",
            "(value.b & 31u) << 11",
            "0u",
        ),
        MUTABLE | EXTENDED,
    ),
    Variant(
        "r4g4b4a4_unorm",
        "VK_FORMAT_R4G4B4A4_UNORM_PACK16",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R16_UINT",
        R16_DECLARATION,
        packed16_store(
            "15.0, 15.0, 15.0, 15.0",
            "(value.r & 15u) << 12",
            "(value.g & 15u) << 8",
            "(value.b & 15u) << 4",
            "value.a & 15u",
        ),
        MUTABLE | EXTENDED,
    ),
    Variant(
        "b4g4r4a4_unorm",
        "VK_FORMAT_B4G4R4A4_UNORM_PACK16",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R16_UINT",
        R16_DECLARATION,
        packed16_store(
            "15.0, 15.0, 15.0, 15.0",
            "(value.r & 15u) << 4",
            "(value.g & 15u) << 8",
            "(value.b & 15u) << 12",
            "value.a & 15u",
        ),
        MUTABLE | EXTENDED,
    ),
    Variant(
        "r5g5b5a1_unorm",
        "VK_FORMAT_R5G5B5A1_UNORM_PACK16",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R16_UINT",
        R16_DECLARATION,
        packed16_store(
            "31.0, 31.0, 31.0, 1.0",
            "(value.r & 31u) << 11",
            "(value.g & 31u) << 6",
            "(value.b & 31u) << 1",
            "value.a",
        ),
        MUTABLE | EXTENDED,
    ),
    Variant(
        "b5g5r5a1_unorm",
        "VK_FORMAT_B5G5R5A1_UNORM_PACK16",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R16_UINT",
        R16_DECLARATION,
        packed16_store(
            "31.0, 31.0, 31.0, 1.0",
            "(value.r & 31u) << 1",
            "(value.g & 31u) << 6",
            "(value.b & 31u) << 11",
            "value.a",
        ),
        MUTABLE | EXTENDED,
    ),
    Variant(
        "a1r5g5b5_unorm",
        "VK_FORMAT_A1R5G5B5_UNORM_PACK16",
        "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR",
        "VK_FORMAT_R16_UINT",
        R16_DECLARATION,
        packed16_store(
            "31.0, 31.0, 31.0, 1.0",
            "(value.r & 31u) << 10",
            "(value.g & 31u) << 5",
            "value.b & 31u",
            "value.a << 15",
        ),
        MUTABLE | EXTENDED,
    ),
)


def words(path: Path) -> tuple[int, ...]:
    source = path.read_bytes()
    if len(source) % 4:
        raise ValueError(f"{path}: SPIR-V size is not a multiple of four bytes")
    return struct.unpack(f"<{len(source) // 4}I", source)


def format_words(name: str, values: tuple[int, ...]) -> list[str]:
    result = [f"inline constexpr u32 renderShader_{name}[] = {{"]
    for offset in range(0, len(values), 8):
        row = ", ".join(f"0x{value:08x}u" for value in values[offset:offset + 8])
        result.append(f"    {row},")
    result += ["};", ""]
    return result


def find_variant(name: str) -> Variant:
    for variant in VARIANTS:
        if variant.name == name:
            return variant
    raise ValueError(f"unknown render shader variant: {name}")


def compile_variant(
    source_path: Path,
    name: str,
    output_path: Path,
    compiler: str,
) -> None:
    variant = find_variant(name)
    template = source_path.read_text(encoding="utf-8")
    source = template.replace(
        "@OUTPUT_DECLARATION@", variant.declaration
    ).replace("@OUTPUT_STORE@", variant.store)

    with tempfile.TemporaryDirectory(prefix="shitty-render-") as directory:
        temporary = Path(directory)
        shader_path = temporary / f"{variant.name}.comp"
        spirv_path = temporary / f"{variant.name}.spv"
        shader_path.write_text(source, encoding="utf-8")
        subprocess.run(
            [
                compiler,
                "--quiet",
                "--target-env", "vulkan1.1",
                "-V", "-S", "comp",
                "-o", str(spirv_path),
                str(shader_path),
            ],
            check=True,
        )
        output_path.write_text(
            "\n".join(format_words(variant.name, words(spirv_path))),
            encoding="utf-8",
        )


def compile_metal(
    source_path: Path,
    output_path: Path,
    glsl_compiler: str,
    spirv_cross: str,
) -> None:
    variant = find_variant("rgba8_unorm")
    template = source_path.read_text(encoding="utf-8")
    source = template.replace(
        "@OUTPUT_DECLARATION@", variant.declaration
    ).replace("@OUTPUT_STORE@", variant.store)

    with tempfile.TemporaryDirectory(prefix="shitty-metal-") as directory:
        temporary = Path(directory)
        shader_path = temporary / "render.comp"
        spirv_path = temporary / "render.spv"
        metal_path = temporary / "render.metal"
        shader_path.write_text(source, encoding="utf-8")
        subprocess.run(
            [
                glsl_compiler,
                "--quiet",
                "--target-env", "vulkan1.1",
                "-V", "-S", "comp",
                "-o", str(spirv_path),
                str(shader_path),
            ],
            check=True,
        )
        subprocess.run(
            [
                spirv_cross,
                str(spirv_path),
                "--msl",
                "--msl-version", "24000",
                "--output", str(metal_path),
            ],
            check=True,
        )
        metal = metal_path.read_text(encoding="utf-8")
        delimiter = "ST_METAL"
        if f"){delimiter}" in metal:
            raise ValueError("Metal shader contains its raw string delimiter")
        output_path.write_text(
            "\n".join([
                "#pragma once",
                "",
                f'inline constexpr char renderMetalSource[] = R"{delimiter}(',
                metal.rstrip(),
                f'){delimiter}";',
                "",
            ]),
            encoding="utf-8",
        )


def combine(output_path: Path, inputs: list[Path]) -> None:
    if len(inputs) != len(VARIANTS):
        raise ValueError(
            f"expected {len(VARIANTS)} shader fragments, got {len(inputs)}"
        )
    output = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <std/sys/types.h>",
        "",
        "struct GeneratedRenderShader {",
        "    const char* name;",
        "    VkFormat presentFormat;",
        "    VkColorSpaceKHR colorSpace;",
        "    VkFormat storageViewFormat;",
        "    const u32* code;",
        "    size_t codeSize;",
        "    u32 flags;",
        "};",
        "",
        "inline constexpr u32 renderShaderMutableFormat = 1u;",
        "inline constexpr u32 renderShaderExtendedStorage = 2u;",
        "",
    ]
    for variant, input_path in zip(VARIANTS, inputs):
        if variant.name not in input_path.name:
            raise ValueError(
                f"expected fragment for {variant.name}, got {input_path}"
            )
        output += input_path.read_text(encoding="utf-8").rstrip().splitlines()
        output.append("")
    output += [
        "inline constexpr GeneratedRenderShader generatedRenderShaders[] = {",
    ]
    for variant in VARIANTS:
        output.append(
            "    {"
            f'"{variant.name}", {variant.present}, {variant.color_space}, '
            f"{variant.view}, renderShader_{variant.name}, "
            f"sizeof(renderShader_{variant.name}), {variant.flags}u"
            "},"
        )
    output += [
        "};",
        "",
        "inline constexpr const GeneratedRenderShader& fallbackRenderShader =",
        "    generatedRenderShaders[0];",
        "",
    ]
    output_path.write_text("\n".join(output), encoding="utf-8")


def main() -> None:
    if len(sys.argv) >= 2 and sys.argv[1] == "compile":
        if len(sys.argv) not in (5, 6):
            raise ValueError(
                "usage: generate_render_shaders.py compile "
                "SOURCE VARIANT OUTPUT [COMPILER]"
            )
        compile_variant(
            Path(sys.argv[2]),
            sys.argv[3],
            Path(sys.argv[4]),
            sys.argv[5] if len(sys.argv) == 6 else "glslangValidator",
        )
        return
    if len(sys.argv) >= 2 and sys.argv[1] == "metal":
        if len(sys.argv) not in (4, 6):
            raise ValueError(
                "usage: generate_render_shaders.py metal "
                "SOURCE OUTPUT [GLSL_COMPILER SPIRV_CROSS]"
            )
        compile_metal(
            Path(sys.argv[2]),
            Path(sys.argv[3]),
            sys.argv[4] if len(sys.argv) == 6 else "glslangValidator",
            sys.argv[5] if len(sys.argv) == 6 else "spirv-cross",
        )
        return
    if len(sys.argv) >= 2 and sys.argv[1] == "combine":
        if len(sys.argv) < 4:
            raise ValueError(
                "usage: generate_render_shaders.py combine OUTPUT INPUT..."
            )
        combine(Path(sys.argv[2]), [Path(path) for path in sys.argv[3:]])
        return
    raise ValueError(
        "usage: generate_render_shaders.py {compile|metal|combine} ..."
    )


if __name__ == "__main__":
    main()
