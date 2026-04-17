# ---------------------------------------------------------------------------
# Unit tests — pure C++, no model files or license key required
# ---------------------------------------------------------------------------
unit_test_exe = executable('test_unit',
  'test_unit.cpp',
  include_directories: include_directories('../src'),
  dependencies: [krisp_dep],
)
test('krisp-unit', unit_test_exe, timeout: 30)

# ---------------------------------------------------------------------------
# Integration test — requires model files and a real audio input
# ---------------------------------------------------------------------------
test_exe = executable('test_pipeline',
  'test_pipeline.cpp',
  dependencies: [gst_dep, gst_audio_dep],
)

# Run via: meson test
# Or use sdk_cmd.py test --type test_nc/test_ar --model ... --input ...
# Required env vars (set before running meson test):
#   KRISP_TEST_INPUT   — path to a mono WAV file
#   KRISP_NC_MODEL     — path to a .kef NC model file  (if build_nc)
#   KRISP_AR_MODEL     — path to a .kef AR model file  (if build_ar)
test('krisp-pipeline',
  test_exe,
  env: {
    'GST_PLUGIN_PATH': meson.project_build_root() / 'src',
  },
  timeout: 120,
)
