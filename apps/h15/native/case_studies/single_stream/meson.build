################################################
# Single Stream Case Study
################################################

single_stream_install_dir = case_studies_install_dir + '/single_stream'

single_stream_app_src = ['main.cpp']

executable('single_stream_case_study',
  single_stream_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: single_stream_install_dir,
)

# install json config files
install_subdir('configs', install_dir : single_stream_install_dir + '/resources')