# Dependencies
libdatachannel_link = compiler.find_library('libdatachannel', required : true, dirs : '/usr/lib')

thread_dep = dependency('threads')
glib_dep = dependency('glib-2.0')

common_src = [
  'main.cpp',
  'resources/encoder.cpp',
  'resources/encoder_control.cpp',
  'resources/osd.cpp',
  'resources/configs_base.cpp',
  'resources/common/repository.cpp',
  'resources/isp.cpp',
  'resources/common/isp/v4l2_ctrl.cpp',
  'resources/privacy_mask.cpp',
  'resources/webpage.cpp',
  'resources/webrtc.cpp',
  'resources/common/event_bus.cpp',
  'pipeline/base_pipeline.cpp',
  'resources/common/isp/common.cpp',
  'common/httplib/httplib_utils.cpp',
  'common/webserver_logger.cpp',
  'common/common.cpp'
]
src = common_src + [
  'resources/configs_medialib.cpp',
  'pipeline/pipeline.cpp',
  'pipeline/pipeline_event_handle.cpp',
  'pipeline/pipeline_endpoints.cpp',
  'pipeline/stages/webrtc_stage.cpp',
  'pipeline/stages/convert_rtp_module.cpp',
]


# Executable for GStreamer API
executable('camera-viewer-server',
  src,
  gnu_symbol_visibility : 'default',
  dependencies : dependencies_apps + [libhailort_dep, thread_dep, glib_dep, spdlog_dep, libdatachannel_link, tappas_general_dep, image_dep, reference_camera_dep],
  cpp_args: hailo_lib_args,
  install: true,
  install_dir: get_option('bindir'),
)