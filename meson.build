project(
  'neovim3d',
  'cpp',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=0.63.0',
  default_options: ['cpp_std=c++20', 'warning_level=2', 'buildtype=debugoptimized'],
)

add_project_arguments(
  '-DNEOVIM3D_VERSION="@0@"'.format(meson.project_version()),
  language: 'cpp',
)

threads_dep = dependency('threads')
glfw_dep = dependency('glfw3', version: '>=3.3')
epoxy_dep = dependency('epoxy')
freetype_dep = dependency('freetype2')
fontconfig_dep = dependency('fontconfig')
lua_dep = dependency('lua-5.4', 'lua5.4', 'lua54', 'lua', version: '>=5.4')

earcut_inc = include_directories('third_party/earcut')

# Everything except main() lives in a static library so tests can link it.
core_lib = static_library(
  'neovim3d-core',
  'src/msgpack.cpp',
  'src/nvim.cpp',
  'src/ui_state.cpp',
  'src/font.cpp',
  'src/camera.cpp',
  'src/scene.cpp',
  'src/input.cpp',
  'src/lua_api.cpp',
  'src/cinema.cpp',
  'src/renderer.cpp',
  'src/app.cpp',
  'src/png.cpp',
  include_directories: earcut_inc,
  dependencies: [threads_dep, glfw_dep, epoxy_dep, freetype_dep, fontconfig_dep, lua_dep],
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: include_directories('src'),
  dependencies: [threads_dep, glfw_dep, epoxy_dep, freetype_dep, fontconfig_dep, lua_dep],
)

executable('neovim3d', 'src/main.cpp', dependencies: core_dep, install: true)

if get_option('tests')
  subdir('tests')
endif
