project('am62d-pipeline', ['c', 'cpp'],
  default_options: ['c_std=c11', 'cpp_std=c++17'])

add_project_arguments('-D_GNU_SOURCE', language: 'c')

inc = include_directories('framework/include')

cc = meson.get_compiler('c')
dl_dep = cc.find_library('dl', required: false)

pthread_dep = dependency('threads')

spa_dep = dependency('libspa-0.2')
spa_plugin_dir = get_option('spa_plugin_dir')
if spa_plugin_dir == ''
  spa_plugin_dir = spa_dep.get_variable(pkgconfig: 'plugindir')
endif
add_project_arguments('-DSPA_PLUGIN_DIR="' + spa_plugin_dir + '"', language: 'c')
add_project_arguments('-DSPA_PLUGIN_DIR="' + spa_plugin_dir + '"', language: 'cpp')

subdir('framework/core')
subdir('third_party/cjson')

executable('am62d-pipeline',
  framework_src,
  include_directories: inc,
  dependencies: [dl_dep, cjson_dep, spa_dep, pthread_dep]
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

install_subdir('configs/', install_dir: '/usr/lib/am62d/')
