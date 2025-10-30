################################################
# HAILO 15 TEST APPS
################################################

test_apps_install_dir = apps_install_dir + '/test_apps'
# Get the current source directory
current_dir = meson.current_source_dir()

# Print the current source directory
message('Current source directory: @0@'.format(current_dir))

test_app_src = ['main.cpp', 'scenarios/generate_pipeline.hpp', 'scenarios/recovery_scenarios.hpp',
                'scenarios/robustness_scenarios.hpp', 'scenarios/parse_and_update_json.hpp',
                '../chaos_app/utils/common.hpp']

json_dep = meson.get_compiler('cpp').find_library('libnlohmann_json_schema_validator', required: true, dirs: '/usr/lib/')

executable('test_app',
  test_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, spdlog_dep,
                json_dep] + plugin_deps,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/test_apps'
)

# install json config files
install_subdir('configs', install_dir : apps_install_dir + '/test_apps/resources')
