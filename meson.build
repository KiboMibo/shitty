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

freetype_dep = dependency('freetype2', required: true)
sdl_dep = dependency('sdl3', version: '>=3.2.0', required: true)
vulkan_dep = dependency('vulkan', required: true)
threads_dep = dependency('threads', required: true)

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
  'charvdev.cc',
  'font.cc',
  'fontpack.cc',
  'frame.cc',
  'log.cc',
  'main.cc',
  'options.cc',
  'pty.cc',
  'renderer.cc',
  'vkpresenter.cc',
  'vterm.cc',
)

executable(
  'zutty',
  zutty_sources,
  cpp_args: compile_args,
  dependencies: [
    freetype_dep,
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
