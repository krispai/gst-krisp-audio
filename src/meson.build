plugin_sources   = ['gstkrisp.cpp']
cpp_feature_args = ['-DGST_USE_UNSTABLE_API']

if build_nc
  plugin_sources   += ['gstkrispnc.cpp']
  cpp_feature_args += ['-DKRISP_HAS_NC']
endif

if build_ar
  plugin_sources   += ['gstkrispaccent.cpp']
  cpp_feature_args += ['-DKRISP_HAS_AR']
endif

shared_library('gstkrisp',
  files(plugin_sources),
  dependencies: [gst_dep, gst_base_dep, gst_audio_dep, krisp_dep],
  cpp_args: cpp_feature_args,
  install: true,
  install_dir: get_option('libdir') / 'gstreamer-1.0',
)
