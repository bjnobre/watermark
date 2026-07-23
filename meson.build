project(
  'whatermak',
  'c',
  version: '0.1.0',
  license: 'GPL-3.0-only',
  default_options: ['c_std=c11', 'warning_level=3'],
)

gtk = dependency('gtk+-3.0', version: '>=3.24')
layer_shell = dependency('gtk-layer-shell-0', version: '>=0.7')

whatermak = executable(
  'whatermak',
  'src/main.c',
  dependencies: [gtk, layer_shell],
  install: true,
)

test('version', whatermak, args: ['--version'])

install_man('whatermak.1')
