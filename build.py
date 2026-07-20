import os
import platform

import build


std_root = os.path.realpath(os.path.join(os.path.dirname(__file__), "../std"))

build.includes += ["$(B)", std_root]
build.cppflags += ['-DZUTTY_VERSION="0.14"']
build.cxxflags += [
    "-std=c++23",
    "-Og" if "-DDEBUG" in build.cppflags else "-O2",
]


freetype = pkg_config("freetype2")
fontconfig = pkg_config("fontconfig")
glfw = pkg_config("glfw3 >= 3.4")
vulkan = pkg_config("vulkan")
brotli_common = pkg_config("libbrotlicommon", required=False)
utf8proc = pkg_config("libutf8proc")
threads = dependency(ldflags=["-pthread"])


std_sources = [
    source for source in build.glob(f"{std_root}/std/*/*.cpp")
    if not source.endswith("_ut.cpp")
]
std_cxxflags = ["-std=c++26"]
if platform.machine() == "x86_64":
    std_cxxflags.append("-mcx16")
libstd = library(
    srcs=std_sources,
    cxxflags=std_cxxflags,
    output="$(B)/libstd.a",
)


render_spv = command(
    inputs=["$(S)/render.comp", "$(S)/embed_spirv.py"],
    outputs=["$(B)/render_spv.h", "$(B)/render.comp.spv"],
    cmd=[
        [
            "glslangValidator",
            "--target-env", "vulkan1.1",
            "-V", "-S", "comp",
            "-o", "$(B)/render.comp.spv",
            "$(S)/render.comp",
        ],
        [
            "python3",
            "$(S)/embed_spirv.py",
            "$(B)/render.comp.spv",
            "$(B)/render_spv.h",
        ],
    ],
    descr="SH",
    color="magenta",
)


zutty = program(
    srcs=build.glob("$(S)/*.cpp"),
    deps=[freetype, fontconfig, glfw, vulkan, threads, libstd, brotli_common,
          utf8proc],
)


test_suite = command(
    inputs=build.glob("$(S)/tests/*.py"),
    outputs=["$(B)/tests.stamp"],
    deps=[zutty],
    cmd=[
        ["python3", "-m", "unittest", "discover", "-s", "tests", "-v"],
        [
            "python3", "-c",
            "from pathlib import Path; Path(r'$(B)/tests.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
    descr="TEST",
    color="cyan",
)


parser_fuzz = command(
    inputs=["$(S)/tests/fuzz_parser.py", "$(S)/tests/harness.py"],
    outputs=["$(B)/parser-fuzz.stamp"],
    deps=[zutty],
    cmd=[
        ["python3", "tests/fuzz_parser.py"],
        [
            "python3", "-c",
            "from pathlib import Path; Path(r'$(B)/parser-fuzz.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
    descr="FUZZ",
    color="yellow",
)


vttest_profile = command(
    inputs=["$(S)/tests/vttest.py", "$(S)/tests/harness.py"],
    outputs=["$(B)/vttest.stamp"],
    deps=[zutty],
    cmd=[
        ["python3", "tests/vttest.py"],
        [
            "python3", "-c",
            "from pathlib import Path; Path(r'$(B)/vttest.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
    descr="VTTEST",
    color="blue",
)


install(zutty)
install(test_suite)
install(parser_fuzz)
install(vttest_profile)
