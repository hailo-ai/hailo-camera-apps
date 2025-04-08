################################################
# AI Example without AI (Only Crops)
################################################

crops_install_dir = internal_apps_install_dir + '/crops_without_ai'

crops_without_ai_app_src = ['main.cpp']

executable('crops_without_ai_example',
  crops_without_ai_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: crops_install_dir,
)

# install json config files
install_subdir('configs', install_dir : crops_install_dir + '/resources')