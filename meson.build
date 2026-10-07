project(
  'pointerless',
  'c',
  meson_version: '>=1.1',
  default_options: ['c_std=c11'],
  license: 'GPL-3.0',
  version: '0.4.1',
)

add_languages('cpp', native: false)

version_suffix = ''

if get_option('buildtype') == 'debug'
  add_project_arguments('-DDEBUG', language: ['c', 'cpp'])
  version_suffix = '-debug'
endif

add_project_arguments(
  '-DVERSION="@0@@1@"'.format(meson.project_version(), version_suffix),
  # The NCNN model files are installed with the package; the detector loads
  # them from this compile-time location at runtime.
  '-DGUIDETECT_MODEL_DIR="@0@"'.format(get_option('prefix') / get_option('datadir') / 'pointerless'),
  language: ['c', 'cpp'],
)

# C only: libstdc++ already defines _GNU_SOURCE for C++, and redefining it
# on the command line triggers a redefinition warning in guidetect.cpp.
add_project_arguments('-D_GNU_SOURCE=200809L', language: 'c')

cc = meson.get_compiler('c')

wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
cairo = dependency('cairo')
math = cc.find_library('m')

subdir('protocol')

dependencies = [
  wayland_client,
  xkbcommon,
  cairo,
  math,
]

# NCNN is provided by the system (the Nix flake devShell ships the nixpkgs
# ncnn package); the build never bundles a vendored copy.
ncnn_dep = dependency('ncnn')

# The system NCNN installs its public headers under `<prefix>/include/ncnn/`
# while pkg-config only adds `<prefix>/include`, so the flat includes in the
# sources (`<layer.h>`, `<mat.h>`, ...) need the `ncnn/` subdirectory added.
ncnn_incdir = ncnn_dep.get_variable(pkgconfig: 'includedir')
dependencies += [
  ncnn_dep,
  declare_dependency(include_directories: include_directories(ncnn_incdir / 'ncnn')),
]

sources = [
  'src/main.c',
  'src/surface_buffer.c',
  'src/floating.c',
  'src/guidetect.cpp',
  'src/screencopy.c',
  'src/utils.c',
  'src/utils_cairo.c',
  'src/utils_wayland.c',
  'src/config.c',
  'src/label.c',
  protos_src,
]

executable(
  'pointerless',
  sources,
  dependencies: dependencies,
  install: true,
)

label_test_exec = executable(
  'test_label',
  [
    'src/test_label.c',
    'src/label.c',
    'src/utils.c',
  ],
)

test('test_label', label_test_exec)

install_data(
  'share/pointerless.desktop',
  rename: 'pointerless.desktop',
  install_dir: 'share/applications',
)

# Bundle the NCNN model files with the package (the datadir `pointerless`
# directory matches the compile-time `GUIDETECT_MODEL_DIR`).
install_data(
  'share/pointerless/model.ncnn.param',
  install_dir: get_option('datadir') / 'pointerless',
)

install_data(
  'share/pointerless/model.ncnn.bin',
  install_dir: get_option('datadir') / 'pointerless',
)
