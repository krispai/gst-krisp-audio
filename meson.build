project('gst-krisp-audio', 'cpp',
  version: '1.0.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
  ]
)

cpp = meson.get_compiler('cpp')

# ---------------------------------------------------------------------------
# GStreamer dependencies (Homebrew or system pkg-config)
# ---------------------------------------------------------------------------
gst_dep       = dependency('gstreamer-1.0',       version: '>=1.20', required: true)
gst_base_dep  = dependency('gstreamer-base-1.0',  version: '>=1.20', required: true)
gst_audio_dep = dependency('gstreamer-audio-1.0', version: '>=1.20', required: true)

# Query GStreamer's own pluginsdir so the plugin is auto-discovered without GST_PLUGIN_PATH.
_gst_plugins_dir_pkg = gst_dep.get_variable(
  pkgconfig: 'pluginsdir',
  default_value: get_option('libdir') / 'gstreamer-1.0',
)
gst_plugins_dir = get_option('gst_plugins_dir') != '' ? get_option('gst_plugins_dir') : _gst_plugins_dir_pkg

# ---------------------------------------------------------------------------
# Krisp Audio SDK — located via -Dkrisp_sdk_dir=PATH
# ---------------------------------------------------------------------------
krisp_sdk_dir = get_option('krisp_sdk_dir')
if krisp_sdk_dir == ''
  error(
    'krisp_sdk_dir is not set.\n' +
    'Download the Krisp Audio SDK and pass its absolute path:\n' +
    '  meson setup build -Dkrisp_sdk_dir=/absolute/path/to/krisp-sdk\n' +
    'Or use sdk_cmd.py build --sdk-dir <path>  (relative paths are accepted there)'
  )
endif

krisp_inc_flag  = '-I' + krisp_sdk_dir / 'include'
krisp_ext_dir   = krisp_sdk_dir / 'lib' / 'static' / 'external'

# Static library name differs by toolchain: MSVC uses krisp-audio-sdk.lib, Unix uses libkrisp-audio-sdk.a
_win = host_machine.system() == 'windows'
_lib_suffix = _win ? '.lib' : '.a'
_lib_prefix = _win ? '' : 'lib'
krisp_static = krisp_sdk_dir / 'lib' / 'static' / (_lib_prefix + 'krisp-audio-sdk' + _lib_suffix)

if not import('fs').exists(krisp_static)
  error('Krisp SDK static library not found: ' + krisp_static)
endif

# Collect all external static libraries bundled with the SDK (cross-platform glob via Python)
_py = find_program('python3', 'python', required: true)
_ext_out = run_command(
  _py, '-c',
  'import glob, sys; [print(x) for x in glob.glob(sys.argv[1])]',
  krisp_ext_dir / ('*' + _lib_suffix),
  check: true
).stdout().strip()
krisp_ext_libs = _ext_out != '' ? _ext_out.split('\n') : []

krisp_macos_frameworks = host_machine.system() == 'darwin' ? [
  '-framework', 'Foundation',          # NSLog (ONNX Runtime) + CF* re-export (libcurl)
  '-framework', 'Accelerate',          # cblas/vDSP in XNNPACK/MLAS
  '-framework', 'SystemConfiguration', # libcurl proxy detection
  '-framework', 'Security',            # libcurl certificate handling
] : []

# libcurl (bundled in the SDK) requires Winsock and Windows certificate APIs on Windows.
krisp_win_libs = _win ? ['ws2_32.lib', 'crypt32.lib'] : []

krisp_dep = declare_dependency(
  compile_args: [krisp_inc_flag, '-DKRISP_AUDIO_STATIC'],
  link_args: [krisp_static] + krisp_ext_libs + krisp_macos_frameworks + krisp_win_libs,
)

# ---------------------------------------------------------------------------
# Feature flags: nc and ar
# ---------------------------------------------------------------------------
nc_opt = get_option('nc')
ar_opt = get_option('ar')

# NC
if nc_opt.enabled()
  if not cpp.has_header('krisp-audio-sdk-nc.hpp', args: [krisp_inc_flag])
    error('nc feature is enabled but krisp-audio-sdk-nc.hpp was not found in ' + krisp_sdk_dir / 'include')
  endif
  build_nc = true
elif nc_opt.auto()
  build_nc = cpp.has_header('krisp-audio-sdk-nc.hpp', args: [krisp_inc_flag])
  if not build_nc
    message('krisp-audio-sdk-nc.hpp not found — krispnc will not be built')
  endif
else
  build_nc = false
endif

# AR
if ar_opt.enabled()
  if not cpp.has_header('krisp-audio-sdk-ar.hpp', args: [krisp_inc_flag])
    error('ar feature is enabled but krisp-audio-sdk-ar.hpp was not found in ' + krisp_sdk_dir / 'include')
  endif
  build_ar = true
elif ar_opt.auto()
  build_ar = cpp.has_header('krisp-audio-sdk-ar.hpp', args: [krisp_inc_flag])
  if not build_ar
    message('krisp-audio-sdk-ar.hpp not found — krispaccent will not be built')
  endif
else
  build_ar = false
endif

if not build_nc and not build_ar
  error('At least one feature (nc or ar) must be enabled')
endif

subdir('src')
subdir('tests')
