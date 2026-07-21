import os
from pathlib import Path

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


if '-lstd' in build.ldflags:
    libstd = dependency(ldflags=["-lstd"])
elif os.path.isfile(os.path.join(os.path.dirname(__file__), std_build)):
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
fuzz_source = "$(S)/main_fuzz.cpp"
libzutty_sources = [
    source for source in build.glob("$(S)/*.cpp")
    if source not in (main_source, fuzz_source)
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


main_fuzz = program(
    srcs=[fuzz_source],
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


xtermjs_root = Path(__file__).parent / "tests" / "xtermjs"
xtermjs_cases = (xtermjs_root / "file_names.txt").read_text().split()
xtermjs_tests = []
for case in xtermjs_cases:
    xtermjs_tests.append(command(
        name="xtermjs_" + case.replace("-", "_"),
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/xtermjs/adapter.py",
            "$(S)/tests/xtermjs/file_names.txt",
            "$(S)/tests/xtermjs/xfail.txt",
            f"$(S)/tests/xtermjs/{case}.in",
            f"$(S)/tests/xtermjs/{case}.text",
        ],
        outputs=[f"$(B)/tests/xtermjs/{case}.stamp"],
        deps=[zutty],
        cmd=[
            "python3",
            "tests/xtermjs/adapter.py",
            case,
            "tests/xtermjs/xfail.txt",
            f"$(B)/tests/xtermjs/{case}.stamp",
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
        descr="XTERMJS",
        color="cyan",
    ))


alacritty_root = Path(__file__).parent / "tests" / "alacritty"
alacritty_cases = (alacritty_root / "file_names.txt").read_text().split()
alacritty_tests = []
for case in alacritty_cases:
    alacritty_tests.append(command(
        name="alacritty_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/alacritty/adapter.py",
            "$(S)/tests/alacritty/file_names.txt",
            "$(S)/tests/alacritty/xfail.txt",
            f"$(S)/tests/alacritty/{case}/alacritty.recording",
            f"$(S)/tests/alacritty/{case}/config.json",
            f"$(S)/tests/alacritty/{case}/grid.json",
            f"$(S)/tests/alacritty/{case}/size.json",
        ],
        outputs=[f"$(B)/tests/alacritty/{case}.stamp"],
        deps=[zutty],
        cmd=[
            "python3",
            "tests/alacritty/adapter.py",
            case,
            "tests/alacritty/xfail.txt",
            f"$(B)/tests/alacritty/{case}.stamp",
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
        descr="ALACRITTY",
        color="cyan",
    ))


contour_vttest_sources = [
    source for source in build.glob("$(S)/tests/contour/vttest/*.c")
    if not source.endswith("/vms_io.c")
]
contour_vttest = program(
    name="contour_vttest_helper",
    srcs=contour_vttest_sources,
    cflags=["-Wno-error"],
    cppflags=[
        "-DHAVE_CONFIG_H",
        "-I$(S)/tests/contour/vttest",
    ],
    output="$(B)/tests/contour/vttest",
)


contour_root = Path(__file__).parent / "tests" / "contour"
contour_cases = (contour_root / "file_names.txt").read_text().split()
contour_tests = []
for case in contour_cases:
    golden_inputs = [
        "$(S)/" + path.relative_to(Path(__file__).parent).as_posix()
        for path in sorted((contour_root / "golden").glob(f"{case}.step*.dump"))
    ]
    contour_tests.append(command(
        name="contour_" + case.replace(".", "_"),
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/contour/adapter.py",
            "$(S)/tests/contour/file_names.txt",
            "$(S)/tests/contour/scenarios.json",
            "$(S)/tests/contour/xfail.txt",
            *golden_inputs,
        ],
        outputs=[f"$(B)/tests/contour/{case}.stamp"],
        deps=[zutty, contour_vttest],
        cmd=[
            "python3",
            "tests/contour/adapter.py",
            "$(B)/tests/contour/vttest",
            case,
            "tests/contour/xfail.txt",
            f"$(B)/tests/contour/{case}.stamp",
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
        descr="CONTOUR",
        color="cyan",
    ))


install(libzutty)
install(zutty)
install(test_suite)
install(parser_fuzz)
install(vttest_profile)
install(*xtermjs_tests)
install(*alacritty_tests)
install(contour_vttest)
install(*contour_tests)
