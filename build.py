import json
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
        deps=[zutty],
        cmd=[
            "python3",
            "tests/mosh/adapter.py",
            corpus,
            "tests/mosh/xfail.txt",
            f"$(B)/tests/mosh/{corpus}.stamp",
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
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
            deps=[zutty],
            cmd=[
                "python3",
                "tests/ghostty/adapter.py",
                "tests/ghostty/xfail.txt",
                f"$(B)/tests/ghostty/{name}.stamp",
                *shard,
            ],
            cwd="$(S)",
            env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
            descr="GHOSTTY",
            color="cyan",
        ))


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
        deps=[zutty],
        cmd=[
            "python3",
            "tests/tmux/adapter.py",
            "tests/tmux/xfail.txt",
            f"$(B)/tests/tmux/{name}.stamp",
            *shard,
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
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
        deps=[zutty],
        cmd=[
            "python3",
            "tests/tmux/adapter.py",
            "tests/tmux/xfail.txt",
            f"$(B)/tests/tmux/input_dictionary_{index}.stamp",
            member,
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
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
        deps=[zutty, wraptest_helper],
        cmd=[
            "python3",
            "tests/wraptest/adapter.py",
            "$(B)/tests/wraptest/wraptest",
            case_id,
            "tests/wraptest/xfail.txt",
            f"$(B)/tests/wraptest/{case_id}.stamp",
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
        descr="WRAPTEST",
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
        deps=[zutty],
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
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
        descr="UCS",
        color="cyan",
    ))

ucs_detect_validation = command(
    name="ucs_detect_catalog",
    inputs=[
        "$(S)/tests/ucs_detect/catalog.py",
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
        deps=[zutty],
        cmd=[
            "python3",
            "tests/vtebench/adapter.py",
            case,
            "tests/vtebench/xfail.txt",
            f"$(B)/tests/vtebench/{case}.stamp",
            "30",
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
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
        deps=[zutty],
        cmd=[
            "python3",
            "tests/libvterm/adapter.py",
            case,
            "tests/libvterm/xfail.txt",
            f"$(B)/tests/libvterm/{name}.stamp",
        ],
        cwd="$(S)",
        env={"ZUTTY_TEST_BINARY": "$(B)/zutty"},
        descr="LIBVTERM",
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
install(*mosh_tests)
install(*ghostty_tests)
install(*tmux_tests)
install(wraptest_helper)
install(*wraptest_tests)
install(*ucs_detect_tests)
install(ucs_detect_validation)
install(*vtebench_tests)
install(*libvterm_tests)
