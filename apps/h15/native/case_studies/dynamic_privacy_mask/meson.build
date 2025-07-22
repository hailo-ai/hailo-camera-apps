################################################
# Dynamic Privacy Mask Case Study
################################################

dynamic_privacy_mask_install_dir = case_studies_install_dir + '/dynamic_privacy_mask'

dynamic_privacy_mask_app_src = ['main.cpp']

executable('dynamic_privacy_mask_case_study',
  dynamic_privacy_mask_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: dynamic_privacy_mask_install_dir,
)

# install json config files
install_subdir('configs', install_dir : dynamic_privacy_mask_install_dir + '/resources')