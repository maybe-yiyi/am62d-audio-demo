plugin_inc = include_directories('../../plugins')
libm_dep = meson.get_compiler('c').find_library('m', required: true)
thread_dep = dependency('threads')

test('spsc_queue',
    executable('test_spsc', 'test_spsc.c',
        include_directories: plugin_inc,
        c_args: ['-std=c11']))
