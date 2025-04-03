################################################
# HAILO 15 AI EXAMPLE APP
################################################

ai_example_install_dir = apps_install_dir + '/ai_example_app'

# Profiles Install dir
profiles_install_dir = '/etc/imaging/'

ai_example_app_src = ['main.cpp']

executable('ai_example_app',
  ai_example_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, spdlog_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: ai_example_install_dir,
)

################################################
# Profiles tree Installation
################################################
install_subdir('cfg', install_dir : profiles_install_dir)