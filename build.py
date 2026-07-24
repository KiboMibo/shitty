import json
import os
from datetime import date
from pathlib import Path

import build


std_build = os.path.join("third_party", "libstd", "build.py")
shitty_version = date.today().strftime("%Y.%m.%d")

build.includes += ["$(B)"]
build.cppflags += [f'-DSHITTY_VERSION="{shitty_version}"']
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
all_libshitty_sources = [
    source for source in build.glob("$(S)/*.cpp")
    if source not in (main_source, fuzz_source)
]
libshitty_sources = all_libshitty_sources
libshitty_deps = [
    freetype, fontconfig, glfw, vulkan, threads, libstd, brotli_common,
    utf8proc,
]


libshitty = library(
    srcs=libshitty_sources,
    deps=libshitty_deps,
    output="$(B)/libshitty_prod.a",
)


st = program(
    srcs=[main_source],
    deps=[libshitty],
)


# The control protocol is compiled into both binaries. SHITTY_FOR_TESTS only
# opens its application entry point and exposes Vterm::testApi().
libshitty_test = library(
    name="libshitty_test",
    srcs=all_libshitty_sources,
    cppflags=["-DSHITTY_FOR_TESTS=1"],
    deps=libshitty_deps,
    output="$(B)/libshitty_test.a",
)


st_test = program(
    name="st_test",
    output="$(B)/st_test",
    srcs=[main_source],
    cppflags=["-DSHITTY_FOR_TESTS=1"],
    deps=[libshitty_test],
)


main_fuzz = program(
    srcs=[fuzz_source],
    deps=[libshitty],
)


test_suite = command(
    inputs=[
        *build.glob("$(S)/tests/*.py"),
        "$(S)/application.cpp",
        "$(S)/shitty.desktop",
    ],
    outputs=["$(B)/tests.stamp"],
    deps=[st_test, st],
    cmd=[
        ["python3", "-m", "unittest", "discover", "-s", "tests", "-v"],
        [
            "python3", "-c",
            "from pathlib import Path; Path(r'$(B)/tests.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    env={
        "SHITTY_TEST_BINARY": "$(B)/st_test",
        "SHITTY_TEST_VERSION": shitty_version,
        "SHITTY_PRODUCTION_BINARY": "$(B)/st",
    },
    descr="TEST",
    color="cyan",
)


parser_fuzz = command(
    inputs=["$(S)/tests/fuzz_parser.py", "$(S)/tests/harness.py"],
    outputs=["$(B)/parser-fuzz.stamp"],
    deps=[st_test],
    cmd=[
        ["python3", "tests/fuzz_parser.py"],
        [
            "python3", "-c",
            "from pathlib import Path; Path(r'$(B)/parser-fuzz.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
    descr="FUZZ",
    color="yellow",
)


vttest_profile = command(
    inputs=["$(S)/tests/vttest.py", "$(S)/tests/harness.py"],
    outputs=["$(B)/vttest.stamp"],
    deps=[st_test],
    cmd=[
        ["python3", "tests/vttest.py"],
        [
            "python3", "-c",
            "from pathlib import Path; Path(r'$(B)/vttest.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
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
        deps=[st_test],
        cmd=[
            "python3",
            "tests/xtermjs/adapter.py",
            case,
            "tests/xtermjs/xfail.txt",
            f"$(B)/tests/xtermjs/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
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
        deps=[st_test],
        cmd=[
            "python3",
            "tests/alacritty/adapter.py",
            case,
            "tests/alacritty/xfail.txt",
            f"$(B)/tests/alacritty/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
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
        deps=[st_test, contour_vttest],
        cmd=[
            "python3",
            "tests/contour/adapter.py",
            "$(B)/tests/contour/vttest",
            case,
            "tests/contour/xfail.txt",
            f"$(B)/tests/contour/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="CONTOUR",
        color="cyan",
    ))


mosh_tests = []
for corpus in ("terminal_corpus", "terminal_parser_corpus"):
    mosh_tests.append(command(
        name="mosh_" + corpus,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/mosh/adapter.py",
            "$(S)/tests/mosh/xfail.txt",
            *build.glob(f"$(S)/tests/mosh/{corpus}/*"),
        ],
        outputs=[f"$(B)/tests/mosh/{corpus}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/mosh/adapter.py",
            corpus,
            "tests/mosh/xfail.txt",
            f"$(B)/tests/mosh/{corpus}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="MOSH",
        color="cyan",
    ))


ghostty_root = Path(__file__).parent / "tests" / "ghostty"
ghostty_members = []
for corpus in ("osc-cmin", "parser-cmin", "stream-cmin"):
    ghostty_members.extend(
        f"{corpus}/{path.name}"
        for path in sorted((ghostty_root / corpus).iterdir())
    )
ghostty_xfails = {
    line.strip()
    for line in (ghostty_root / "xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
unknown_ghostty_xfails = ghostty_xfails - set(ghostty_members)
if unknown_ghostty_xfails:
    raise RuntimeError(
        "unknown Ghostty XFAIL members: "
        + ", ".join(sorted(unknown_ghostty_xfails))
    )

ghostty_tests = []
ghostty_shard_size = 128
for corpus in ("osc-cmin", "parser-cmin", "stream-cmin"):
    members = [
        member for member in ghostty_members
        if member.startswith(corpus + "/")
    ]
    for shard_index, start in enumerate(range(0, len(members), ghostty_shard_size)):
        shard = members[start : start + ghostty_shard_size]
        name = corpus.replace("-", "_") + f"_{shard_index:03d}"
        ghostty_tests.append(command(
            name="ghostty_" + name,
            inputs=[
                "$(S)/tests/harness.py",
                "$(S)/tests/fuzz_parser.py",
                "$(S)/tests/ghostty/adapter.py",
                "$(S)/tests/ghostty/xfail.txt",
                *("$(S)/tests/ghostty/" + member for member in shard),
            ],
            outputs=[f"$(B)/tests/ghostty/{name}.stamp"],
            deps=[st_test],
            cmd=[
                "python3",
                "tests/ghostty/adapter.py",
                "tests/ghostty/xfail.txt",
                f"$(B)/tests/ghostty/{name}.stamp",
                *shard,
            ],
            cwd="$(S)",
            env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
            descr="GHOSTTY",
            color="cyan",
        ))


ghostty_semantic_cases = (
    ghostty_root / "semantic_file_names.txt"
).read_text().split()
ghostty_semantic_tests = []
for case in ghostty_semantic_cases:
    ghostty_semantic_tests.append(command(
        name="ghostty_model_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/ghostty/semantic_adapter.py",
            "$(S)/tests/ghostty/semantic_catalog.py",
            "$(S)/tests/ghostty/semantic_file_names.txt",
            "$(S)/tests/ghostty/semantic_xfail.txt",
            "$(S)/tests/ghostty/upstream/stream_terminal_tests.zig",
        ],
        outputs=[f"$(B)/tests/ghostty/model/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/ghostty/semantic_adapter.py",
            case,
            "tests/ghostty/semantic_xfail.txt",
            f"$(B)/tests/ghostty/model/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="GHOSTTY",
        color="cyan",
    ))


ghostty_semantic_validation = command(
    name="ghostty_model_catalog",
    inputs=[
        "$(S)/tests/ghostty/semantic_catalog.py",
        "$(S)/tests/ghostty/semantic_file_names.txt",
        "$(S)/tests/ghostty/semantic_validate.py",
        "$(S)/tests/ghostty/semantic_xfail.txt",
        "$(S)/tests/ghostty/upstream/stream_terminal_tests.zig",
    ],
    outputs=["$(B)/tests/ghostty/model/catalog.stamp"],
    cmd=[
        ["python3", "tests/ghostty/semantic_validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/ghostty/model/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="GHOSTTY",
    color="cyan",
)


kitty_root = Path(__file__).parent / "tests" / "kitty"
kitty_cases = (kitty_root / "file_names.txt").read_text().split()
kitty_tests = []
for case in kitty_cases:
    kitty_tests.append(command(
        name="kitty_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/kitty/adapter.py",
            "$(S)/tests/kitty/catalog.py",
            "$(S)/tests/kitty/file_names.txt",
            "$(S)/tests/kitty/xfail.txt",
            "$(S)/tests/kitty/upstream/parser.py",
        ],
        outputs=[f"$(B)/tests/kitty/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/kitty/adapter.py",
            case,
            "tests/kitty/xfail.txt",
            f"$(B)/tests/kitty/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="KITTY",
        color="cyan",
    ))


kitty_validation = command(
    name="kitty_catalog",
    inputs=[
        "$(S)/tests/kitty/catalog.py",
        "$(S)/tests/kitty/file_names.txt",
        "$(S)/tests/kitty/validate.py",
        "$(S)/tests/kitty/xfail.txt",
        "$(S)/tests/kitty/upstream/parser.py",
    ],
    outputs=["$(B)/tests/kitty/catalog.stamp"],
    cmd=[
        ["python3", "tests/kitty/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/kitty/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="KITTY",
    color="cyan",
)


kitty_screen_cases = (
    kitty_root / "screen_file_names.txt"
).read_text().split()
kitty_screen_tests = []
for case in kitty_screen_cases:
    kitty_screen_tests.append(command(
        name="kitty_screen_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/kitty/screen_adapter.py",
            "$(S)/tests/kitty/screen_catalog.py",
            "$(S)/tests/kitty/screen_file_names.txt",
            "$(S)/tests/kitty/screen_xfail.txt",
            "$(S)/tests/kitty/upstream/parser.py",
        ],
        outputs=[f"$(B)/tests/kitty/screen/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/kitty/screen_adapter.py",
            case,
            "tests/kitty/screen_xfail.txt",
            f"$(B)/tests/kitty/screen/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="KIT-SCREEN",
        color="cyan",
    ))


kitty_screen_validation = command(
    name="kitty_screen_catalog",
    inputs=[
        "$(S)/tests/kitty/screen_catalog.py",
        "$(S)/tests/kitty/screen_file_names.txt",
        "$(S)/tests/kitty/screen_validate.py",
        "$(S)/tests/kitty/screen_xfail.txt",
        "$(S)/tests/kitty/upstream/parser.py",
    ],
    outputs=["$(B)/tests/kitty/screen/catalog.stamp"],
    cmd=[
        ["python3", "tests/kitty/screen_validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/kitty/screen/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="KIT-SCREEN",
    color="cyan",
)


vte_root = Path(__file__).parent / "tests" / "vte"
vte_cases = (vte_root / "file_names.txt").read_text().split()
vte_tests = []
for case in vte_cases:
    vte_tests.append(command(
        name="vte_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/vte/adapter.py",
            "$(S)/tests/vte/catalog.py",
            "$(S)/tests/vte/file_names.txt",
            "$(S)/tests/vte/xfail.txt",
            "$(S)/tests/vte/upstream/parser-test.cc",
        ],
        outputs=[f"$(B)/tests/vte/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/vte/adapter.py",
            case,
            "tests/vte/xfail.txt",
            f"$(B)/tests/vte/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="VTE",
        color="cyan",
    ))


vte_validation = command(
    name="vte_catalog",
    inputs=[
        "$(S)/tests/vte/catalog.py",
        "$(S)/tests/vte/file_names.txt",
        "$(S)/tests/vte/validate.py",
        "$(S)/tests/vte/xfail.txt",
        "$(S)/tests/vte/upstream/parser-test.cc",
    ],
    outputs=["$(B)/tests/vte/catalog.stamp"],
    cmd=[
        ["python3", "tests/vte/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/vte/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="VTE",
    color="cyan",
)


vte_width_cases = (vte_root / "width_file_names.txt").read_text().split()
vte_width_tests = []
for case in vte_width_cases:
    vte_width_tests.append(command(
        name="vte_width_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/vte/width_adapter.py",
            "$(S)/tests/vte/width_catalog.py",
            "$(S)/tests/vte/width_file_names.txt",
            "$(S)/tests/vte/width_xfail.txt",
            "$(S)/tests/vte/upstream/unicode-width-test.cc",
        ],
        outputs=[f"$(B)/tests/vte/width/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/vte/width_adapter.py",
            case,
            "tests/vte/width_xfail.txt",
            f"$(B)/tests/vte/width/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="VTE-WIDTH",
        color="cyan",
    ))


vte_width_validation = command(
    name="vte_width_catalog",
    inputs=[
        "$(S)/tests/vte/width_catalog.py",
        "$(S)/tests/vte/width_file_names.txt",
        "$(S)/tests/vte/width_validate.py",
        "$(S)/tests/vte/width_xfail.txt",
        "$(S)/tests/vte/upstream/unicode-width-test.cc",
    ],
    outputs=["$(B)/tests/vte/width/catalog.stamp"],
    cmd=[
        ["python3", "tests/vte/width_validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/vte/width/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="VTE-WIDTH",
    color="cyan",
)


windows_terminal_root = Path(__file__).parent / "tests" / "windows_terminal"
windows_terminal_cases = (windows_terminal_root / "file_names.txt").read_text().split()
windows_terminal_tests = []
for case in windows_terminal_cases:
    windows_terminal_tests.append(command(
        name="windows_terminal_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/windows_terminal/adapter.py",
            "$(S)/tests/windows_terminal/catalog.py",
            "$(S)/tests/windows_terminal/file_names.txt",
            "$(S)/tests/windows_terminal/xfail.txt",
            "$(S)/tests/windows_terminal/upstream/StateMachineTest.cpp",
            "$(S)/tests/windows_terminal/upstream/OutputEngineTest.cpp",
        ],
        outputs=[f"$(B)/tests/windows_terminal/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/windows_terminal/adapter.py",
            case,
            "tests/windows_terminal/xfail.txt",
            f"$(B)/tests/windows_terminal/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="WINTERM",
        color="cyan",
    ))


windows_terminal_validation = command(
    name="windows_terminal_catalog",
    inputs=[
        "$(S)/tests/windows_terminal/catalog.py",
        "$(S)/tests/windows_terminal/file_names.txt",
        "$(S)/tests/windows_terminal/validate.py",
        "$(S)/tests/windows_terminal/xfail.txt",
        "$(S)/tests/windows_terminal/upstream/StateMachineTest.cpp",
        "$(S)/tests/windows_terminal/upstream/OutputEngineTest.cpp",
    ],
    outputs=["$(B)/tests/windows_terminal/catalog.stamp"],
    cmd=[
        ["python3", "tests/windows_terminal/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/windows_terminal/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="WINTERM",
    color="cyan",
)


wezterm_root = Path(__file__).parent / "tests" / "wezterm"
wezterm_cases = (wezterm_root / "file_names.txt").read_text().split()
wezterm_tests = []
for case in wezterm_cases:
    wezterm_tests.append(command(
        name="wezterm_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/wezterm/adapter.py",
            "$(S)/tests/wezterm/catalog.py",
            "$(S)/tests/wezterm/file_names.txt",
            "$(S)/tests/wezterm/xfail.txt",
            *build.glob("$(S)/tests/wezterm/upstream/*.rs"),
        ],
        outputs=[f"$(B)/tests/wezterm/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/wezterm/adapter.py",
            case,
            "tests/wezterm/xfail.txt",
            f"$(B)/tests/wezterm/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="WEZTERM",
        color="cyan",
    ))


wezterm_validation = command(
    name="wezterm_catalog",
    inputs=[
        "$(S)/tests/wezterm/catalog.py",
        "$(S)/tests/wezterm/file_names.txt",
        "$(S)/tests/wezterm/validate.py",
        "$(S)/tests/wezterm/xfail.txt",
        *build.glob("$(S)/tests/wezterm/upstream/*.rs"),
    ],
    outputs=["$(B)/tests/wezterm/catalog.stamp"],
    cmd=[
        ["python3", "tests/wezterm/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/wezterm/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="WEZTERM",
    color="cyan",
)


wezterm_screen_cases = (
    wezterm_root / "screen_file_names.txt"
).read_text().split()
wezterm_screen_tests = []
for case in wezterm_screen_cases:
    wezterm_screen_tests.append(command(
        name="wezterm_screen_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/wezterm/catalog.py",
            "$(S)/tests/wezterm/screen_adapter.py",
            "$(S)/tests/wezterm/screen_catalog.py",
            "$(S)/tests/wezterm/screen_file_names.txt",
            "$(S)/tests/wezterm/screen_xfail.txt",
            *build.glob("$(S)/tests/wezterm/upstream/*.rs"),
        ],
        outputs=[f"$(B)/tests/wezterm/screen/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/wezterm/screen_adapter.py",
            case,
            "tests/wezterm/screen_xfail.txt",
            f"$(B)/tests/wezterm/screen/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="WEZ-SCREEN",
        color="cyan",
    ))


wezterm_screen_validation = command(
    name="wezterm_screen_catalog",
    inputs=[
        "$(S)/tests/wezterm/catalog.py",
        "$(S)/tests/wezterm/screen_catalog.py",
        "$(S)/tests/wezterm/screen_file_names.txt",
        "$(S)/tests/wezterm/screen_validate.py",
        "$(S)/tests/wezterm/screen_xfail.txt",
        *build.glob("$(S)/tests/wezterm/upstream/*.rs"),
    ],
    outputs=["$(B)/tests/wezterm/screen/catalog.stamp"],
    cmd=[
        ["python3", "tests/wezterm/screen_validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/wezterm/screen/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="WEZ-SCREEN",
    color="cyan",
)


konsole_root = Path(__file__).parent / "tests" / "konsole"
konsole_cases = (konsole_root / "file_names.txt").read_text().split()
konsole_tests = []
for case in konsole_cases:
    konsole_tests.append(command(
        name="konsole_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/konsole/adapter.py",
            "$(S)/tests/konsole/catalog.py",
            "$(S)/tests/konsole/file_names.txt",
            "$(S)/tests/konsole/xfail.txt",
            "$(S)/tests/konsole/upstream/Vt102EmulationTest.cpp",
        ],
        outputs=[f"$(B)/tests/konsole/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/konsole/adapter.py",
            case,
            "tests/konsole/xfail.txt",
            f"$(B)/tests/konsole/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="KONSOLE",
        color="cyan",
    ))


konsole_validation = command(
    name="konsole_catalog",
    inputs=[
        "$(S)/tests/konsole/catalog.py",
        "$(S)/tests/konsole/file_names.txt",
        "$(S)/tests/konsole/validate.py",
        "$(S)/tests/konsole/xfail.txt",
        "$(S)/tests/konsole/upstream/Vt102EmulationTest.cpp",
    ],
    outputs=["$(B)/tests/konsole/catalog.stamp"],
    cmd=[
        ["python3", "tests/konsole/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/konsole/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="KONSOLE",
    color="cyan",
)


tmux_root = Path(__file__).parent / "tests" / "tmux"
tmux_corpus_members = [
    "corpus/" + path.name
    for path in sorted((tmux_root / "corpus").iterdir())
]
tmux_dictionary_members = [
    f"dictionary/{index:03d}"
    for index, line in enumerate(
        (tmux_root / "upstream" / "input-fuzzer.dict").read_text().splitlines()
    )
    if line.strip() and not line.startswith("#")
]
tmux_members = tmux_corpus_members + tmux_dictionary_members
tmux_xfails = {
    line.strip()
    for line in (tmux_root / "xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
unknown_tmux_xfails = tmux_xfails - set(tmux_members)
if unknown_tmux_xfails:
    raise RuntimeError(
        "unknown tmux XFAIL members: "
        + ", ".join(sorted(unknown_tmux_xfails))
    )

tmux_tests = []
tmux_shard_size = 128
for shard_index, start in enumerate(
    range(0, len(tmux_corpus_members), tmux_shard_size)
):
    shard = tmux_corpus_members[start : start + tmux_shard_size]
    name = f"input_corpus_{shard_index:03d}"
    tmux_tests.append(command(
        name="tmux_" + name,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/tmux/adapter.py",
            "$(S)/tests/tmux/xfail.txt",
            *("$(S)/tests/tmux/" + member for member in shard),
        ],
        outputs=[f"$(B)/tests/tmux/{name}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/tmux/adapter.py",
            "tests/tmux/xfail.txt",
            f"$(B)/tests/tmux/{name}.stamp",
            *shard,
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="TMUX",
        color="cyan",
    ))

for member in tmux_dictionary_members:
    index = member.split("/", 1)[1]
    tmux_tests.append(command(
        name="tmux_input_dictionary_" + index,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/fuzz_parser.py",
            "$(S)/tests/tmux/adapter.py",
            "$(S)/tests/tmux/xfail.txt",
            "$(S)/tests/tmux/upstream/input-fuzzer.dict",
        ],
        outputs=[f"$(B)/tests/tmux/input_dictionary_{index}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/tmux/adapter.py",
            "tests/tmux/xfail.txt",
            f"$(B)/tests/tmux/input_dictionary_{index}.stamp",
            member,
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="TMUX",
        color="cyan",
    ))


wraptest_helper = program(
    name="wraptest_helper",
    srcs=["$(S)/tests/wraptest/wraptest.c"],
    cflags=["-Wno-error"],
    output="$(B)/tests/wraptest/wraptest",
)

wraptest_root = Path(__file__).parent / "tests" / "wraptest"
wraptest_cases = json.loads((wraptest_root / "cases.json").read_text())
wraptest_tests = []
for case_id, _, _ in wraptest_cases:
    wraptest_tests.append(command(
        name="wraptest_" + case_id,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/wraptest/adapter.py",
            "$(S)/tests/wraptest/cases.json",
            "$(S)/tests/wraptest/xfail.txt",
        ],
        outputs=[f"$(B)/tests/wraptest/{case_id}.stamp"],
        deps=[st_test, wraptest_helper],
        cmd=[
            "python3",
            "tests/wraptest/adapter.py",
            "$(B)/tests/wraptest/wraptest",
            case_id,
            "tests/wraptest/xfail.txt",
            f"$(B)/tests/wraptest/{case_id}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="WRAPTEST",
        color="cyan",
    ))


tack_root = Path(__file__).parent / "tests" / "tack"
tack_cases = (tack_root / "file_names.txt").read_text().split()
tack_xfails = {
    line.strip()
    for line in (tack_root / "xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
unknown_tack_xfails = tack_xfails - set(tack_cases)
if unknown_tack_xfails:
    raise RuntimeError(
        "unknown tack XFAIL capabilities: "
        + ", ".join(sorted(unknown_tack_xfails))
    )
tack_upstream_inputs = build.glob("$(S)/tests/tack/upstream/*")
tack_program = command(
    name="tack_program",
    inputs=[
        "$(S)/tests/tack/build_tack.sh",
        *tack_upstream_inputs,
    ],
    outputs=["$(B)/tests/tack/tack"],
    cmd=[
        "sh",
        "tests/tack/build_tack.sh",
        "tests/tack/upstream",
        "$(B)/tests/tack/tack",
    ],
    cwd="$(S)",
    descr="TACK-CC",
    color="magenta",
)
tack_validation = command(
    name="tack_catalog",
    inputs=[
        "$(S)/tests/tack/file_names.txt",
        "$(S)/tests/tack/validate.py",
        *tack_upstream_inputs,
    ],
    outputs=["$(B)/tests/tack/catalog.stamp"],
    cmd=[
        ["python3", "tests/tack/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/tack/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="TACK",
    color="cyan",
)
tack_tests = []
for capability in tack_cases:
    tack_tests.append(command(
        name="tack_" + capability,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/tack/adapter.py",
            "$(S)/tests/tack/file_names.txt",
            "$(S)/tests/tack/xfail.txt",
        ],
        outputs=[f"$(B)/tests/tack/{capability}.stamp"],
        deps=[st_test, tack_program],
        cmd=[
            "python3",
            "tests/tack/adapter.py",
            "$(B)/tests/tack/tack",
            capability,
            "tests/tack/xfail.txt",
            f"$(B)/tests/tack/{capability}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="TACK",
        color="cyan",
    ))


ucs_detect_root = Path(__file__).parent / "tests" / "ucs_detect"
ucs_detect_tests = []
ucs_detect_table_inputs = [
    "$(S)/" + path.relative_to(Path(__file__).parent).as_posix()
    for path in sorted(ucs_detect_root.glob("table_*.py"))
]
ucs_detect_shards = [
    (category, int(start), int(end))
    for category, start, end in (
        line.split() for line in
        (ucs_detect_root / "shards.txt").read_text().splitlines()
    )
]
ucs_detect_category_indices = {}
for category, start, end in ucs_detect_shards:
    shard_index = ucs_detect_category_indices.get(category, 0)
    ucs_detect_category_indices[category] = shard_index + 1
    name = f"{category}_{shard_index:03d}"
    ucs_detect_tests.append(command(
        name="ucs_detect_" + name,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/ucs_detect/adapter.py",
            "$(S)/tests/ucs_detect/catalog.py",
            "$(S)/tests/ucs_detect/shards.txt",
            "$(S)/tests/ucs_detect/xfail.txt",
            *ucs_detect_table_inputs,
        ],
        outputs=[f"$(B)/tests/ucs_detect/{name}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/ucs_detect/adapter.py",
            category,
            str(start),
            str(end),
            "tests/ucs_detect/xfail.txt",
            f"$(B)/tests/ucs_detect/{name}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="UCS",
        color="cyan",
    ))

ucs_detect_validation = command(
    name="ucs_detect_catalog",
    inputs=[
        "$(S)/tests/ucs_detect/catalog.py",
        "$(S)/tests/ucs_detect/probe_cases.py",
        "$(S)/tests/ucs_detect/probe_names.txt",
        "$(S)/tests/ucs_detect/probe_xfail.txt",
        "$(S)/tests/ucs_detect/validate.py",
        "$(S)/tests/ucs_detect/shards.txt",
        "$(S)/tests/ucs_detect/xfail.txt",
        *ucs_detect_table_inputs,
    ],
    outputs=["$(B)/tests/ucs_detect/catalog.stamp"],
    cmd=[
        ["python3", "tests/ucs_detect/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/ucs_detect/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="UCS",
    color="cyan",
)


ucs_detect_probe_cases = (ucs_detect_root / "probe_names.txt").read_text().split()
ucs_detect_probe_tests = []
for case in ucs_detect_probe_cases:
    ucs_detect_probe_tests.append(command(
        name="ucs_detect_probe_" + case.lower(),
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/ucs_detect/probe_adapter.py",
            "$(S)/tests/ucs_detect/probe_cases.py",
            "$(S)/tests/ucs_detect/probe_names.txt",
            "$(S)/tests/ucs_detect/probe_xfail.txt",
            "$(S)/tests/ucs_detect/upstream/terminal.py",
            "$(S)/tests/ucs_detect/upstream/table_xtgettcap.py",
            "$(S)/tests/ucs_detect/upstream/data/shitty.yaml",
        ],
        outputs=[f"$(B)/tests/ucs_detect/probes/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/ucs_detect/probe_adapter.py",
            case,
            "tests/ucs_detect/probe_xfail.txt",
            f"$(B)/tests/ucs_detect/probes/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="UCS-PROBE",
        color="cyan",
    ))


vtebench_root = Path(__file__).parent / "tests" / "vtebench"
vtebench_cases = (vtebench_root / "file_names.txt").read_text().split()
vtebench_tests = []
for case in vtebench_cases:
    vtebench_case_inputs = [
        "$(S)/" + path.relative_to(Path(__file__).parent).as_posix()
        for path in sorted((vtebench_root / "benchmarks" / case).iterdir())
        if path.is_file()
    ]
    vtebench_tests.append(command(
        name="vtebench_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/vtebench/adapter.py",
            "$(S)/tests/vtebench/file_names.txt",
            "$(S)/tests/vtebench/xfail.txt",
            *vtebench_case_inputs,
        ],
        outputs=[f"$(B)/tests/vtebench/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/vtebench/adapter.py",
            case,
            "tests/vtebench/xfail.txt",
            f"$(B)/tests/vtebench/{case}.stamp",
            "30",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="VTEBENCH",
        color="yellow",
    ))


libvterm_root = Path(__file__).parent / "tests" / "libvterm"
libvterm_cases = (libvterm_root / "file_names.txt").read_text().split()
libvterm_xfails = {
    line.strip()
    for line in (libvterm_root / "xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
unknown_libvterm_xfails = libvterm_xfails - set(libvterm_cases)
if unknown_libvterm_xfails:
    raise RuntimeError(
        "unknown libvterm XFAIL fixtures: "
        + ", ".join(sorted(unknown_libvterm_xfails))
    )
libvterm_tests = []
for case in libvterm_cases:
    name = case.removesuffix(".test").replace("-", "_")
    libvterm_tests.append(command(
        name="libvterm_" + name,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/libvterm/adapter.py",
            "$(S)/tests/libvterm/file_names.txt",
            "$(S)/tests/libvterm/xfail.txt",
            f"$(S)/tests/libvterm/upstream/{case}",
        ],
        outputs=[f"$(B)/tests/libvterm/{name}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/libvterm/adapter.py",
            case,
            "tests/libvterm/xfail.txt",
            f"$(B)/tests/libvterm/{name}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="LIBVTERM",
        color="cyan",
    ))


xterm_vttests_root = Path(__file__).parent / "tests" / "xterm_vttests"
xterm_vttests_cases = (xterm_vttests_root / "file_names.txt").read_text().split()
xterm_vttests_xfails = {
    line.strip()
    for line in (xterm_vttests_root / "xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
unknown_xterm_vttests_xfails = xterm_vttests_xfails - set(xterm_vttests_cases)
if unknown_xterm_vttests_xfails:
    raise RuntimeError(
        "unknown xterm vttests XFAIL scripts: "
        + ", ".join(sorted(unknown_xterm_vttests_xfails))
    )
xterm_vttests_tests = []
for case in xterm_vttests_cases:
    name = case.removesuffix(".sh").replace("-", "_")
    xterm_vttests_tests.append(command(
        name="xterm_vttests_" + name,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/xterm_vttests/adapter.py",
            "$(S)/tests/xterm_vttests/file_names.txt",
            "$(S)/tests/xterm_vttests/xfail.txt",
            *build.glob("$(S)/tests/xterm_vttests/bin/*"),
            *build.glob("$(S)/tests/xterm_vttests/lib/**/*.pm"),
            f"$(S)/tests/xterm_vttests/upstream/{case}",
        ],
        outputs=[f"$(B)/tests/xterm_vttests/{name}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/xterm_vttests/adapter.py",
            case,
            "tests/xterm_vttests/xfail.txt",
            f"$(B)/tests/xterm_vttests/{name}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="XTERM-VT",
        color="cyan",
    ))


esctest_root = Path(__file__).parent / "tests" / "esctest"
esctest_cases = (esctest_root / "file_names.txt").read_text().split()
esctest_xfails = {
    line.strip()
    for line in (esctest_root / "xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
unknown_esctest_xfails = esctest_xfails - set(esctest_cases)
if unknown_esctest_xfails:
    raise RuntimeError(
        "unknown esctest XFAIL cases: "
        + ", ".join(sorted(unknown_esctest_xfails))
    )
esctest_ported_inputs = [
    "$(S)/" + path.relative_to(Path(__file__).parent).as_posix()
    for path in sorted((esctest_root / "ported").rglob("*.py"))
]
esctest_tests = []
for case in esctest_cases:
    name = case.replace(".", "_")
    esctest_tests.append(command(
        name="esctest_" + name,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/esctest/adapter.py",
            "$(S)/tests/esctest/file_names.txt",
            "$(S)/tests/esctest/xfail.txt",
            *esctest_ported_inputs,
        ],
        outputs=[f"$(B)/tests/esctest/{name}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/esctest/adapter.py",
            case,
            "tests/esctest/xfail.txt",
            f"$(B)/tests/esctest/{name}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="ESCTEST",
        color="cyan",
    ))


termless_root = Path(__file__).parent / "tests" / "termless"
termless_cases = json.loads((termless_root / "cases.json").read_text())
termless_ids = {case_id for case_id, _, _ in termless_cases}
termless_xfails = {
    line.strip()
    for line in (termless_root / "xfail.txt").read_text().splitlines()
    if line.strip() and not line.startswith("#")
}
unknown_termless_xfails = termless_xfails - termless_ids
if unknown_termless_xfails:
    raise RuntimeError(
        "unknown Termless XFAIL cases: "
        + ", ".join(sorted(unknown_termless_xfails))
    )
termless_upstream_inputs = [
    "$(S)/" + path.relative_to(Path(__file__).parent).as_posix()
    for path in sorted((termless_root / "upstream").rglob("*"))
    if path.is_file()
]
termless_validation = command(
    name="termless_catalog",
    inputs=[
        "$(S)/tests/termless/cases.json",
        "$(S)/tests/termless/validate.py",
        "$(S)/tests/termless/xfail.txt",
        *termless_upstream_inputs,
    ],
    outputs=["$(B)/tests/termless/catalog.stamp"],
    cmd=[
        ["python3", "tests/termless/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/termless/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="TERMLESS",
    color="cyan",
)
termless_tests = []
for case_id, _, _ in termless_cases:
    termless_tests.append(command(
        name="termless_" + case_id,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/termless/adapter.py",
            "$(S)/tests/termless/backend.py",
            "$(S)/tests/termless/cases.py",
            "$(S)/tests/termless/cases.json",
            "$(S)/tests/termless/xfail.txt",
            *termless_upstream_inputs,
        ],
        outputs=[f"$(B)/tests/termless/{case_id}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/termless/adapter.py",
            case_id,
            "tests/termless/xfail.txt",
            f"$(B)/tests/termless/{case_id}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="TERMLESS",
        color="cyan",
    ))


realworld_root = Path(__file__).parent / "tests" / "realworld"
realworld_cases = (realworld_root / "file_names.txt").read_text().split()
realworld_validation = command(
    name="realworld_catalog",
    inputs=[
        "$(S)/tests/realworld/validate.py",
        "$(S)/tests/realworld/corpus.py",
        "$(S)/tests/realworld/cases.json",
        "$(S)/tests/realworld/file_names.txt",
        *build.glob("$(S)/tests/realworld/input/*.input.zst"),
        *build.glob("$(S)/tests/realworld/screen/*.screen.json"),
    ],
    outputs=["$(B)/tests/realworld/catalog.stamp"],
    cmd=[
        ["python3", "tests/realworld/validate.py"],
        [
            "python3", "-c",
            "from pathlib import Path; "
            "Path(r'$(B)/tests/realworld/catalog.stamp').touch()",
        ],
    ],
    cwd="$(S)",
    descr="REALWORLD",
    color="cyan",
)
realworld_tests = []
for case in realworld_cases:
    realworld_tests.append(command(
        name="realworld_" + case,
        inputs=[
            "$(S)/tests/harness.py",
            "$(S)/tests/realworld/adapter.py",
            "$(S)/tests/realworld/corpus.py",
            "$(S)/tests/realworld/cases.json",
            "$(S)/tests/realworld/file_names.txt",
            f"$(S)/tests/realworld/input/{case}.input.zst",
            f"$(S)/tests/realworld/screen/{case}.screen.json",
        ],
        outputs=[f"$(B)/tests/realworld/{case}.stamp"],
        deps=[st_test],
        cmd=[
            "python3",
            "tests/realworld/adapter.py",
            case,
            f"$(B)/tests/realworld/{case}.stamp",
        ],
        cwd="$(S)",
        env={"SHITTY_TEST_BINARY": "$(B)/st_test"},
        descr="REALWORLD",
        color="cyan",
    ))


install(libshitty)
install(st)
install(test_suite)
install(parser_fuzz)
install(vttest_profile)
install(*xtermjs_tests)
install(*alacritty_tests)
install(contour_vttest)
install(*contour_tests)
install(*mosh_tests)
install(*ghostty_tests)
install(*ghostty_semantic_tests)
install(ghostty_semantic_validation)
install(*kitty_tests)
install(kitty_validation)
install(*kitty_screen_tests)
install(kitty_screen_validation)
install(*vte_tests)
install(vte_validation)
install(*vte_width_tests)
install(vte_width_validation)
install(*windows_terminal_tests)
install(windows_terminal_validation)
install(*wezterm_tests)
install(wezterm_validation)
install(*wezterm_screen_tests)
install(wezterm_screen_validation)
install(*konsole_tests)
install(konsole_validation)
install(*tmux_tests)
install(wraptest_helper)
install(*wraptest_tests)
install(tack_program)
install(tack_validation)
install(*tack_tests)
install(*ucs_detect_tests)
install(ucs_detect_validation)
install(*ucs_detect_probe_tests)
install(*vtebench_tests)
install(*libvterm_tests)
install(*xterm_vttests_tests)
install(*esctest_tests)
install(termless_validation)
install(*termless_tests)
install(realworld_validation)
install(*realworld_tests)
