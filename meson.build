project('jstok', 'c',
  version : '0.1.0',
  default_options : ['warning_level=3', 'c_std=c99'])

# Header-only dependency
jstok_dep = declare_dependency(include_directories : include_directories('.'))

# Test suite
test_exe = executable('test_jstok', 
  'test_jstok.c',
  dependencies : jstok_dep)

test('jstok_tests', test_exe)

test_async_exe = executable('test_jstok_async', 
  'test_jstok_async.c',
  dependencies : jstok_dep)

test('jstok_async_tests', test_async_exe)

# Fuzzer (requires Clang)
if meson.get_compiler('c').get_id() == 'clang'
  executable('fuzz_jstok',
    'fuzz_jstok.c',
    dependencies : jstok_dep,
    c_args : ['-fsanitize=fuzzer,address,undefined', '-g'],
    link_args : ['-fsanitize=fuzzer,address,undefined']
  )
endif
