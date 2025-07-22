################################################
# Infra API Libs
################################################
reference_camera_infra_src = [
  'reference_camera_infra.cpp',
  'reference_camera_logger.cpp',
  'pipeline_infra/pipeline_builder.cpp',
  'pipeline_infra/queue.cpp',
  'pipeline_infra/stage.cpp',
  'pipeline_infra/stage_tracing.cpp',
  'pipeline_infra/aggregator_stage.cpp',
  'pipeline_infra/ai_stage.cpp'
]

if perfetto_dep.found()
  reference_camera_infra_src += ['perfetto/perfetto.cpp']
endif

reference_camera_includes = [include_directories('pipeline_infra')] + [include_directories('perfetto')]

reference_camera_infra_lib = shared_library('hailo_reference_camera',
  reference_camera_infra_src,
  cpp_args : hailo_lib_args,
  include_directories: reference_camera_includes,
  dependencies : dependencies_apps + [libhailort_dep, opencv_dep, tracker_dep, tappas_general_dep, image_dep, spdlog_dep, perfetto_dep],
  gnu_symbol_visibility : 'default',
  version: meson.project_version(),
  install: true,
  install_dir: get_option('libdir'),
)

reference_camera_dep = declare_dependency(
  include_directories: reference_camera_includes,
  dependencies : [libhailort_dep, opencv_dep, tracker_dep, spdlog_dep, perfetto_dep],
  link_with : reference_camera_infra_lib)

pkgc.generate(
  name : 'reference_camera_api',
  libraries : reference_camera_infra_lib,
  subdirs : ['hailo/tappas/reference_camera', 'hailo/tappas/reference_camera/perfetto'],
  version : meson.project_version(),
  description : 'Hailo Tappas Reference Camera API',
  requires: ['opencv4', 'hailo_tracker', 'spdlog'],
  extra_cflags : ['-DPERFETTO_NOT_FOUND'],
)

install_subdir('pipeline_infra', strip_directory: true, install_dir: get_option('includedir') + '/hailo/tappas/reference_camera')
install_subdir('perfetto', strip_directory: true, install_dir: get_option('includedir') + '/hailo/tappas/reference_camera/perfetto')