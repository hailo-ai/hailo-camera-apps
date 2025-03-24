# Project Declaration

project('tappas-verification', 'c', 'cpp',
        version : '1.6.0',
        default_options : [ 'buildtype=release',
                            'c_std=c11', 'cpp_std=c++17']
       )

# Compiler Arguments
compiler = meson.get_compiler('cpp')
compiler_version = compiler.version().split('.')[0].to_int()
if compiler_version >= 9
  message('GCC >= 9.0.0 detected, applying extra arguments.')
  add_global_arguments('-Wpessimizing-move', language : 'cpp')
  add_global_arguments('-Wredundant-move', language : 'cpp')
  add_global_arguments('-fconcepts', language : 'cpp')
endif

# Ignore unused variables
add_global_arguments('-Wno-unused-variable', language : 'cpp')

# Pkgconfig
pkgc = import('pkgconfig')


spdlog_dep = dependency('spdlog', version: '>= 1.9.2')
dependency_catch2 = dependency('catch2', version: '>=2.13', required: true)
dependency_nlohmann_json = dependency('nlohmann_json', version: '>=3.0', required: true)
dependency_libcurl = dependency('libcurl', version: '>=7.0', required: true)


tests_path = '/home/root/tests/tappas_tests'

executable('test_webserver', 
    ['main.cpp','webserver_test.cpp', 'tests_utils.cpp'],
    dependencies: [
        dependency_nlohmann_json,
        dependency_libcurl,
        dependency_catch2,
        spdlog_dep,
    ],
    install: true,
    install_dir: tests_path,
)

config_files = run_command('find', '.', '-type', 'f', '-name', '*.json').stdout().split()
install_data(config_files, install_dir: tests_path + '/payloads')