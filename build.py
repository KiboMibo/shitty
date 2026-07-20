import os

import build


std_build = os.path.join("third_party", "libstd", "build.py")

build.includes += ["$(B)"]
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


if os.path.isfile(os.path.join(os.path.dirname(__file__), std_build)):
    libstd = import_build(std_build, "libstd.a", extra_cflags=["-Wno-error"])
else:
    libstd = dependency(ldflags=["-lstd"])


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


main_source = "$(S)/main.cpp"
libzutty_sources = [
    source for source in build.glob("$(S)/*.cpp")
    if source != main_source
]


libzutty = library(
    srcs=libzutty_sources,
    deps=[freetype, fontconfig, glfw, vulkan, threads, libstd, brotli_common,
          utf8proc],
    output="$(B)/libzutty.a",
)


zutty = program(
    srcs=[main_source],
    deps=[libzutty],
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


install(libzutty)
install(zutty)
install(test_suite)
install(parser_fuzz)
install(vttest_profile)
