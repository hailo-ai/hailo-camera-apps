################################################
# MEDIA LIBRARY DEPS
################################################
media_library_common_dep = dependency('hailo_media_library_common', method : 'pkg-config')
media_library_frontend_dep = dependency('hailo_media_library_frontend', method : 'pkg-config')
media_library_encoder_dep = dependency('hailo_media_library_encoder', method : 'pkg-config')
media_library_api_dep = dependency('hailo_media_library_api', method : 'pkg-config')
encoder_dep = dependency('hailo_encoder', method : 'pkg-config')
gstmedialibrary_utils_dep = dependency('gstmedialibutils', method : 'pkg-config')
reference_camera_dep = dependency('reference_camera_api', method : 'pkg-config')
fmt_dep = dependency('fmt', method : 'pkg-config')

################################################
# General Include Directory
################################################
hailo_general_inc = include_directories('../../../../core/hailo/general')

################################################
# Plugins Include Directory
################################################
hailo_plugins_inc = include_directories('../../../../core/hailo/plugins')

################################################
# GStreamer Dependencies
################################################
gst_dep = dependency('gstreamer-1.0', method : 'pkg-config')
gst_base_dep = dependency('gstreamer-base-1.0', method : 'pkg-config')
gstvideo_dep = dependency('gstreamer-video-1.0', method : 'pkg-config')
gst_app_dep = dependency('gstreamer-app-1.0', method : 'pkg-config')

# Plugin Dependencies
plugin_deps = [gst_dep, gst_base_dep, gstvideo_dep, gst_app_dep]

################################################
# Common Dependencies for all apps
################################################
opencv_dep = dependency('opencv4', version : '>= 4.0', method : 'pkg-config')
tracker_dep = dependency('hailo_tracker', method : 'pkg-config')
tappas_general_dep = dependency('hailo_tappas_general', method : 'pkg-config')
image_dep = dependency('tappas_image_utils', method : 'pkg-config')

internal_apps_install_dir = apps_install_dir + '/internals'
dependencies_apps = [
  media_library_encoder_dep,
  media_library_common_dep,
  media_library_frontend_dep,
  encoder_dep,
  media_library_api_dep,
  gstmedialibrary_utils_dep,
  reference_camera_dep,
  fmt_dep
]
subdir('encoder_pipelines')
subdir('encoder_pipelines_new_api')
subdir('vision_preproc_applications')
subdir('eis_stress')
subdir('chaos_app')
subdir('stress_app')
subdir('crops_without_ai')
subdir('tests_apps')