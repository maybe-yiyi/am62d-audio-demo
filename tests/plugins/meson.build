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

test('downmix',
    executable('test_downmix', 'test_downmix.c',
        include_directories: plugin_inc,
        c_args: ['-std=c11', '-DAM62D_MAX_CHANNELS=' + max_channels.to_string()]))

if webrtc_dep.found()
    test('webrtc_channels',
        executable('test_webrtc_channels', 'test_webrtc_channels.cpp',
            include_directories: plugin_inc,
            dependencies: [webrtc_dep],
            cpp_args: ['-std=c++17', '-DAM62D_MAX_CHANNELS=' + max_channels.to_string()]))
endif

if tflite_dep.found()
    fs = import('fs')
    _model = get_option('yamnet_model')
    _bundle = _model != '' ? fs.parent(_model) : ''

    test('yamnet_tflite',
        executable('test_yamnet_tflite', 'test_yamnet_tflite.c',
            include_directories: plugin_inc,
            dependencies: [thread_dep, libm_dep, tflite_dep],
            c_args: ['-std=c11', '-DYAMNET_BUNDLE_PATH="' + _bundle + '"']),
        timeout: 120)
endif
