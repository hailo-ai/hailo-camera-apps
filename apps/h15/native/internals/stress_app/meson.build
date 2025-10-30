################################################
# HAILO 15 STRESS APP
################################################

stress_app_src = ['stress_app.cpp',
'../chaos_app/utils/common.hpp', '../chaos_app/utils/vision_config_changes.hpp']
libhailort_dep = [meson.get_compiler('cpp').find_library('hailort', required: false)]

executable('stress_app',
  stress_app_src,
  cpp_args : hailo_lib_args + ['-DENABLE_PERFETTO'],
  include_directories: [hailo_general_inc, include_directories('../'), include_directories('../../../../../core/hailo/plugins/'), include_directories('../chaos_app/utils/')],
  dependencies : dependencies_apps + libhailort_dep + [opencv_dep, tappas_general_dep, reference_camera_dep] + plugin_deps,
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/stress_app',
)