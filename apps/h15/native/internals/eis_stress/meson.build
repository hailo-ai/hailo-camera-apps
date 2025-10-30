################################################
# EIS Stress app
################################################
eis_stress_src = ['eis_stress.cpp', '../chaos_app/utils/common.hpp', '../chaos_app/utils/vision_config_changes.hpp', './common_struct.hpp']
libhailort_dep = [meson.get_compiler('cpp').find_library('hailort', required: false)]

executable('eis_stress',
  eis_stress_src,
  cpp_args : hailo_lib_args,
  include_directories: [hailo_general_inc, include_directories('../'), include_directories('../../../../../core/hailo/plugins/')],
  dependencies : dependencies_apps + libhailort_dep + [opencv_dep, tappas_general_dep, reference_camera_dep] + plugin_deps,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/tests/',
)
