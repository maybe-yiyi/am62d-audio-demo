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
tflite_dep = dependency('tensorflow2-lite', required: false)

_yamnet_model = get_option('yamnet_model')
if _yamnet_model != ''
  install_data(_yamnet_model,
    rename: 'yamnet.tflite',
    install_dir: '/usr/lib/am62d/plugins')
endif

max_channels = get_option('max_channels')
channel_args = ['-DAM62D_MAX_CHANNELS=' + max_channels.to_string()]

plugin_configs = {
  'passthrough': { 'sources': ['plugins/passthrough.c'], 'deps': [] },
  'webrtc': { 'sources': ['plugins/webrtc.cpp'], 'deps': [webrtc_dep], 'cpp_args': channel_args },
  'downmix': { 'sources': ['plugins/downmix.c'], 'deps': [], 'c_args': channel_args },
  'yamnet': {
    'sources': ['plugins/yamnet.c'],
    'deps': [tflite_dep],
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

  shared_module('am62d_' + plugin,
    cfg['sources'],
    dependencies: cfg['deps'],
    c_args: ['-fvisibility=hidden'] + cfg.get('c_args', []),
    cpp_args: ['-fvisibility=hidden'] + cfg.get('cpp_args', []),
    install: true,
    install_dir: '/usr/lib/am62d/plugins',
  )
endforeach

install_data('plugins/manifest.ttl', 'plugins/passthrough.ttl',
  'plugins/webrtc.ttl', 'plugins/downmix.ttl', 'plugins/yamnet.ttl',
  install_dir: '/usr/lib/am62d/plugins')

install_subdir('configs/', install_dir: '/usr/lib/am62d/')

subdir('tests')
