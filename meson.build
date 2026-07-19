project(
  'zutty',
  'cpp',
  version: '0.14',
  meson_version: '>=1.2.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

cpp = meson.get_compiler('cpp')
python = import('python').find_installation('python3')
glslang = find_program('glslangValidator', required: true)

freetype_dep = dependency('freetype2', required: true)
fontconfig_dep = dependency('fontconfig', required: true)
sdl_dep = dependency('sdl3', version: '>=3.2.0', required: true)
vulkan_dep = dependency('vulkan', required: true)
threads_dep = dependency('threads', required: true)

render_spv = custom_target(
  'render-spv',
  input: 'render.comp',
  output: 'render.comp.spv',
  command: [
    glslang,
    '--target-env', 'vulkan1.1',
    '-V', '-S', 'comp',
    '-o', '@OUTPUT@',
    '@INPUT@',
  ],
)

render_spv_header = custom_target(
  'render-spv-header',
  input: render_spv,
  output: 'render_spv.h',
  command: [python, files('embed_spirv.py'), '@INPUT@', '@OUTPUT@'],
)

compile_args = cpp.get_supported_arguments(
  '-fno-omit-frame-pointer',
  '-fsigned-char',
  '-Wsign-compare',
  '-Wno-unused-parameter',
)
compile_args += '-DZUTTY_VERSION="@0@"'.format(meson.project_version())

if get_option('buildtype') in ['debug', 'debugoptimized']
  compile_args += '-DDEBUG'
endif
if get_option('buildtype') == 'debug'
  # Nix enables _FORTIFY_SOURCE globally; -Og keeps it valid while retaining
  # useful debug code generation (and mirrors the former debug build).
  compile_args += cpp.get_supported_arguments('-Og')
endif

zutty_sources = files(
  'charvdev.cpp',
  'font.cpp',
  'fontpack.cpp',
  'frame.cpp',
  'log.cpp',
  'main.cpp',
  'options.cpp',
  'pty.cpp',
  'renderer.cpp',
  'vkpresenter.cpp',
  'vterm.cpp',
)
zutty_sources += render_spv_header

executable(
  'zutty',
  zutty_sources,
  cpp_args: compile_args,
  dependencies: [
    freetype_dep,
    fontconfig_dep,
    sdl_dep,
    vulkan_dep,
    threads_dep,
  ],
  install: true,
  install_dir: get_option('bindir'),
)

install_data(
  'zutty.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'zutty.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
)
