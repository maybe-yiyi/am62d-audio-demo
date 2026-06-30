project('am62d-pipeline', ['c', 'cpp'],
  default_options: ['c_std=c11', 'cpp_std=c++17'])

add_project_arguments('-D_GNU_SOURCE', language: 'c')

subdir('framework/core')
subdir('third_party/cjson')

cc = meson.get_compiler('c')
dl_dep = cc.find_library('dl', required: false)
lilv_dep = dependency('lilv-0')

executable('am62d-pipeline',
  framework_src,
  dependencies: [dependency('libpipewire-0.3'), dl_dep, cjson_dep, lilv_dep]
)

webrtc_dep = dependency('webrtc-audio-processing-2', required: false)
tflite_dep = dependency('tensorflowlite_c', required: false)

# RPATH recorded at configure time so libam62d_yamnet.so finds libtensorflowlite_c.so
# without requiring LD_LIBRARY_PATH at runtime.
tflite_libdir = ''
if tflite_dep.found()
  tflite_libdir = tflite_dep.get_variable(pkgconfig: 'libdir', default_value: '')
endif

plugin_configs = {
  'passthrough': { 'sources': ['plugins/passthrough.c'], 'deps': [] },
  'webrtc': { 'sources': ['plugins/webrtc.cpp'], 'deps': [webrtc_dep] },
  'yamnet': {
    'sources': ['plugins/yamnet.c'],
    'deps': [tflite_dep],
    'c_args': ['-DHAVE_TFLITE'],
  },
}

plugins = get_option('plugins')

foreach plugin : plugins
  cfg = plugin_configs[plugin]

  skip = false
  foreach dep : cfg['deps']
    if not dep.found()
      warning('Skipping @0@: missing dependency'.format(plugin))
      skip = true
      break
    endif
  endforeach
  if skip
    continue
  endif

  _rpath = (plugin == 'yamnet') ? tflite_libdir : ''
  shared_module('am62d_' + plugin,
    cfg['sources'],
    dependencies: cfg['deps'],
    c_args: ['-fvisibility=hidden'] + cfg.get('c_args', []),
    cpp_args: ['-fvisibility=hidden'],
    build_rpath: _rpath,
    install_rpath: _rpath,
    install: true,
    install_dir: '/usr/lib/am62d/plugins',
  )
endforeach

install_data('plugins/manifest.ttl', 'plugins/passthrough.ttl', 'plugins/webrtc.ttl',
  install_dir: '/usr/lib/am62d/plugins')

install_subdir('configs/', install_dir: '/usr/lib/am62d/')

subdir('tests')
