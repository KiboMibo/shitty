import build


build.includes += ["$(B)"]
build.cppflags += ['-DZUTTY_VERSION="0.14"']
build.cxxflags += [
    "-std=c++17",
    "-Og" if "-DDEBUG" in build.cppflags else "-O2",
    "-fno-omit-frame-pointer",
    "-fsigned-char",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wsign-compare",
    "-Wno-unused-parameter",
]


freetype = pkg_config("freetype2")
fontconfig = pkg_config("fontconfig")
glfw = pkg_config("glfw3 >= 3.4")
vulkan = pkg_config("vulkan")
threads = dependency(ldflags=["-pthread"])


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
    deps=[freetype, fontconfig, glfw, vulkan, threads],
)


install(zutty)
