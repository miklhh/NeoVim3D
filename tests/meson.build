unit_tests = executable(
  'unit-tests',
  'test_main.cpp',
  'test_msgpack.cpp',
  'test_ui_state.cpp',
  'test_input.cpp',
  'test_geometry.cpp',
  'test_lua.cpp',
  dependencies: core_dep,
)
test('unit', unit_tests)

nvim_test = executable('nvim-integration-test', 'test_nvim.cpp', dependencies: core_dep)
test('nvim-integration', nvim_test, timeout: 30)
