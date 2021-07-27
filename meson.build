project('tracy')

compiler = meson.get_compiler('c')

link_deps = []
if target_machine.system() == 'windows'
    link_deps += [
        compiler.find_library('dbghelp'),
    ]
endif

tracy_dep = declare_dependency(
  include_directories : include_directories('.'),
  dependencies: link_deps)

meson.override_dependency('tracy', tracy_dep)
