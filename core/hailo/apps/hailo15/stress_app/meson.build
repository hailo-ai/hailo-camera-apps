################################################
# HAILO 15 STRESS APP
################################################

stress_app_src = ['stress_app.cpp',
'../chaos_app/utils/common.hpp', '../chaos_app/utils/vision_config_changes.hpp']
libhailort_dep = [meson.get_compiler('cpp').find_library('hailort', required: false)]
# Opencv Dependencies
opencv_dep = dependency('opencv4', version : '>= 4.0', method : 'pkg-config')
reference_camera_dep = dependency('reference_camera_api', method : 'pkg-config')
executable('stress_app',
  stress_app_src,
  cpp_args : hailo_lib_args,
  include_directories: hailo_general_inc + 
[include_directories('../'), include_directories('../../../plugins/'), include_directories('../chaos_app/utils/')],
  dependencies : dependencies_apps + libhailort_dep + opencv_dep + [tappas_general_dep] + reference_camera_dep,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/stress_app',
)