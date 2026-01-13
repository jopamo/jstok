project('jstok', 'c',
  version : '0.1.0',
  default_options : ['warning_level=3', 'c_std=c99'])

pkg = import('pkgconfig')

install_headers('jstok.h')

# Header-only dependency
jstok_dep = declare_dependency(include_directories : include_directories('.'))

pkg.generate(
  name : 'jstok',
  description : 'Self-contained, header-only C JSON tokenizer',
  version : meson.project_version(),
  libraries : [],
  subdirs : '.',
)

configs = [
  ['default', []],
  ['strict', ['-DJSTOK_STRICT']],
  ['links', ['-DJSTOK_PARENT_LINKS']],
  ['strict_links', ['-DJSTOK_STRICT', '-DJSTOK_PARENT_LINKS']],
]

foreach c : configs
  name = c[0]
  args = c[1]
  
  # Core tests
  t_core = executable('test_jstok_' + name, 
    'test_jstok.c',
    c_args : args,
    dependencies : jstok_dep)
  test('core_' + name, t_core)

  # Async tests
  t_async = executable('test_jstok_async_' + name, 
    'test_jstok_async.c',
    c_args : args,
    dependencies : jstok_dep)
  test('async_' + name, t_async)
endforeach

# JSTOK_NO_HELPERS test
t_no_helpers = executable('test_jstok_no_helpers', 
  'test_jstok.c',
  c_args : ['-DJSTOK_NO_HELPERS'],
  dependencies : jstok_dep)
test('core_no_helpers', t_no_helpers)

test_sse_exe = executable('test_jstok_sse_next', 
  'test_jstok_sse_next.c',
  dependencies : jstok_dep)

test('jstok_sse_tests', test_sse_exe)

# JSTOK_STATIC test (multiple TUs with implementation)
test_static_exe = executable('test_static',
  ['test_static_1.c', 'test_static_2.c'],
  dependencies : jstok_dep)
test('jstok_static', test_static_exe)

# Fuzzer (requires Clang)
if meson.get_compiler('c').get_id() == 'clang'
  executable('fuzz_jstok',
    'fuzz_jstok.c',
    dependencies : jstok_dep,
    c_args : ['-fsanitize=fuzzer,address,undefined', '-g'],
    link_args : ['-fsanitize=fuzzer,address,undefined']
  )
endif
