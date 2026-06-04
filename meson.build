project('am62d-pipeline', 'c', default_options: ['c_std=c11'])

add_project_arguments('-D_GNU_SOURCE', language: 'c')

inc = include_directories('framework/include')

subdir('framework/core')

cc = meson.get_compiler('c')
dl_dep = cc.find_library('dl', required: false)

executable('am62d-pipeline',
  framework_src,
  include_directories: inc,
  dependencies: [dependency('libpipewire-0.3'), dl_dep]
)

plugins = [
  'passthrough',
]

foreach plugin : plugins
  shared_library('am62d_' + plugin,
    'plugins/' + plugin + '.c',
    include_directories: inc,
    c_args: ['-fvisibility=hidden'],
    install: true,
    install_dir: '/usr/lib/am62d/plugins',
  )
endforeach
