project('am62d-pipeline', ['c', 'cpp'],
  default_options: ['c_std=c11', 'cpp_std=c++17'])

add_project_arguments('-D_GNU_SOURCE', language: 'c')

inc = include_directories('framework/include')

subdir('framework/core')
subdir('third_party/cjson')

cc = meson.get_compiler('c')
dl_dep = cc.find_library('dl', required: false)
lilv_dep = dependency('lilv-0')

executable('am62d-pipeline',
  framework_src,
  include_directories: inc,
  dependencies: [dependency('libpipewire-0.3'), dl_dep, cjson_dep, lilv_dep]
)

webrtc_dep = dependency('webrtc-audio-processing-2', required: false)
plugin_configs = {
  'passthrough': { 'sources': ['plugins/passthrough.c'], 'deps': [] },
  'webrtc': { 'sources': ['plugins/webrtc.cpp'], 'deps': [webrtc_dep] },
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
    include_directories: inc,
    dependencies: cfg['deps'],
    c_args: ['-fvisibility=hidden'],
    cpp_args: ['-fvisibility=hidden'],
    install: true,
    install_dir: '/usr/lib/am62d/plugins',
  )
endforeach

install_data('plugins/manifest.ttl', 'plugins/passthrough.ttl', 'plugins/webrtc.ttl',
  install_dir: '/usr/lib/am62d/plugins')

install_subdir('configs/', install_dir: '/usr/lib/am62d/')
