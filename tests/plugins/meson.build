plugin_inc = include_directories('../../plugins')
libm_dep = meson.get_compiler('c').find_library('m', required: true)
thread_dep = dependency('threads')

test('spsc_queue',
    executable('test_spsc', 'test_spsc.c',
        include_directories: plugin_inc,
        c_args: ['-std=c11']))

test('yamnet_dsp',
    executable('test_dsp', 'test_dsp.c',
        include_directories: plugin_inc,
        dependencies: [libm_dep],
        c_args: ['-std=c11']))

# Integration test: compile yamnet.c directly, mock TFLite via absent HAVE_TFLITE
test('yamnet_mock',
    executable('test_yamnet_mock', 'test_yamnet_mock.c',
        include_directories: plugin_inc,
        dependencies: [thread_dep, libm_dep],
        c_args: ['-std=c11']))

tflite_dep = dependency('tensorflowlite_c', required: false)
if tflite_dep.found()
    test('yamnet_tflite',
        executable('test_yamnet_tflite', 'test_yamnet_tflite.c',
            include_directories: plugin_inc,
            dependencies: [thread_dep, libm_dep, tflite_dep],
            c_args: ['-std=c11', '-DHAVE_TFLITE']),
        timeout: 120)
endif
