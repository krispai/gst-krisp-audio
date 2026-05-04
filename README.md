# gst-krisp-audio

GStreamer plugin wrapping the Krisp Audio SDK for real-time AI audio processing. Exposes two elements:

| Element | Description |
|---|---|
| `krispnc` | Noise Cancellation & Voice Isolation (NC/VI) |
| `krispaccent` | Accent Reduction (AR) |

Both elements are in-place audio filters that accept mono PCM (`S16LE` or `F32LE`) at any rate supported by the SDK (8, 16, 24, 32, 44.1, 48, 88.2, 96 kHz).

**Supported platforms:** macOS (arm64, x86_64) · Linux (x86_64, arm64) · Windows (x86_64, MSVC)

---

## Requirements

### Build tools

| Tool | macOS | Linux | Windows |
|---|---|---|---|
| C++17 compiler | Xcode CLT (`xcode-select --install`) | GCC ≥ 9 or Clang ≥ 9 | MSVC 2019+ (Build Tools for Visual Studio) |
| Meson ≥ 0.60 | `brew install meson` | `pip install meson` or `apt install meson` | `pip install meson` |
| Ninja | `brew install ninja` | `pip install ninja` or `apt install ninja-build` | `pip install ninja` |
| pkg-config | `brew install pkg-config` | `apt install pkg-config` | bundled with GStreamer installer |
| Python ≥ 3.8 | pre-installed | pre-installed | [python.org](https://www.python.org/downloads/) |

### GStreamer ≥ 1.20

**macOS (Homebrew)**
```sh
brew install gstreamer
```

The Homebrew `gstreamer` formula bundles the core, base libraries, and all required plugins.

**Linux — Debian/Ubuntu**

For building:
```sh
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
```

Additionally, to run the tests (`wavparse`, `audioconvert`, `wavenc`):
```sh
sudo apt install gstreamer1.0-plugins-base gstreamer1.0-plugins-good
```

**Linux — Fedora/RHEL**

For building:
```sh
sudo dnf install gstreamer1-devel gstreamer1-plugins-base-devel
```

Additionally, to run the tests:
```sh
sudo dnf install gstreamer1-plugins-base gstreamer1-plugins-good
```

**Windows**

Download and run both the **runtime** and **development** installers from
[gstreamer.freedesktop.org/download](https://gstreamer.freedesktop.org/download/) (MSVC variant, x86_64).
Install to the default path (`C:\gstreamer\`). The development installer includes pkg-config and all required plugins.

### Krisp Audio SDK

Obtain the server SDK package for your platform. It must contain:

```
<sdk-dir>/
├── include/
│   ├── krisp-audio-api-definitions.hpp
│   ├── krisp-audio-sdk.hpp
│   ├── krisp-audio-sdk-nc.hpp   ← required for krispnc
│   └── krisp-audio-sdk-ar.hpp   ← required for krispaccent
└── lib/
    └── static/
        ├── libkrisp-audio-sdk.a       (macOS / Linux)
        │   libkrisp-audio-sdk.lib     (Windows)
        └── external/                  ← bundled third-party libs (libcurl, OpenSSL, …)
```

### Model files

Each element requires a `.kef` model file at runtime:

- NC/VI model — passed via the `model` property of `krispnc`
- AR model — passed via the `model` property of `krispaccent`

### License key

A valid Krisp SDK license key is required. Pass it via:

- the `license-key` property on each element, **or**
- the `KRISP_LICENSE_KEY` environment variable (read by the test runner)

---

## Repository layout

```
gst-krisp-audio/
├── .clang-format               # C++ formatting rules
├── meson.build                 # Top-level build definition
├── meson_options.txt           # Build options (krisp_sdk_dir, nc, ar)
├── sdk_cmd.py                  # Build/test helper script
├── src/
│   ├── krisp_session.hpp       # SDK session wrapper + GlobalInit lifecycle
│   ├── gstkrisp_common.hpp/.cpp# Shared GObject property helpers
│   ├── gstkrisp.cpp            # Plugin entry point (GST_PLUGIN_DEFINE)
│   ├── gstkrispnc.hpp/.cpp     # krispnc element
│   └── gstkrispaccent.hpp/.cpp # krispaccent element
└── tests/
    ├── test_unit.cpp           # Unit tests (no model or license key required)
    └── test_pipeline.cpp       # Integration test (EOS-based pipeline test)
```

---

## Build

### Using `sdk_cmd.py` (recommended)

```sh
# First build (configure + compile)
python sdk_cmd.py build --sdk-dir /path/to/krisp-sdk

# Rebuild from scratch (wipes the Meson build directory first)
python sdk_cmd.py clean_build --sdk-dir /path/to/krisp-sdk

# Build only NC/VI (skip accent reduction)
python sdk_cmd.py build --sdk-dir /path/to/krisp-sdk --no-ar

# Build only AR
python sdk_cmd.py build --sdk-dir /path/to/krisp-sdk --no-nc
```

On Windows, `sdk_cmd.py` automatically activates the Visual Studio environment (via `--vsenv`) and locates GStreamer's pkg-config. If GStreamer is installed outside the default paths (`C:\gstreamer\` or `C:\Program Files\gstreamer\`), pass its root explicitly:

```cmd
python sdk_cmd.py build --sdk-dir C:\path\to\krisp-sdk --gst-dir C:\custom\gstreamer\1.0\msvc_x86_64
```

The built plugin is written to `build/meson/src/`:

| Platform | File |
|---|---|
| macOS | `libgstkrisp.dylib` |
| Linux | `libgstkrisp.so` |
| Windows | `gstkrisp.dll` |

### Using Meson directly

**macOS**
```sh
PKG_CONFIG_PATH=/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig \
meson setup build/meson \
  -Dkrisp_sdk_dir=/path/to/krisp-sdk \
  -Dnc=enabled \
  -Dar=enabled
meson compile -C build/meson
```

**Linux**
```sh
meson setup build/meson \
  -Dkrisp_sdk_dir=/path/to/krisp-sdk \
  -Dnc=enabled \
  -Dar=enabled
meson compile -C build/meson
```

**Windows**
```cmd
REM Only needed when invoking meson directly; sdk_cmd.py sets this automatically
set PKG_CONFIG_PATH=C:\gstreamer\1.0\msvc_x86_64\lib\pkgconfig
meson setup build\meson ^
  -Dkrisp_sdk_dir=C:\path\to\krisp-sdk ^
  -Dnc=enabled ^
  -Dar=enabled ^
  --vsenv ^
  -Db_vscrt=mt
meson compile -C build\meson
```

`--vsenv` activates the Visual Studio x64 environment automatically (no need to open an x64 Native Tools Command Prompt first). `-Db_vscrt=mt` matches the static release CRT used by the Krisp SDK.

### Meson options

| Option | Type | Default | Description |
|---|---|---|---|
| `krisp_sdk_dir` | string | *(required)* | Absolute path to the Krisp SDK directory |
| `nc` | feature | `enabled` | Build the `krispnc` element |
| `ar` | feature | `auto` | Build `krispaccent` (auto = enabled if SDK header found) |
| `gst_plugins_dir` | string | *(from pkg-config)* | Override the GStreamer plugin install directory |

---

## Install

```sh
meson install -C build/meson
```

This installs the plugin into the directory GStreamer itself reports as its plugin
search path (`pluginsdir` from its pkg-config file). GStreamer will discover it
automatically — no `GST_PLUGIN_PATH` needed.

| Platform | Typical install path |
|---|---|
| macOS (Homebrew) | `/opt/homebrew/lib/gstreamer-1.0/` |
| Linux (Debian/Ubuntu apt) | `/usr/lib/x86_64-linux-gnu/gstreamer-1.0/` |
| Linux (Fedora/RHEL dnf) | `/usr/lib64/gstreamer-1.0/` |
| Windows | `C:\gstreamer\1.0\msvc_x86_64\lib\gstreamer-1.0\` or `C:\Program Files\gstreamer\1.0\msvc_x86_64\lib\gstreamer-1.0\` |

The exact path is queried at configure time from `pkg-config --variable=pluginsdir gstreamer-1.0`
so it always matches the GStreamer installation that was used to build the plugin.

To install to a custom location regardless of what pkg-config reports:
```sh
meson setup build/meson -Dkrisp_sdk_dir=... -Dgst_plugins_dir=/custom/path/gstreamer-1.0
meson install -C build/meson
```

---

## Test

### Using `sdk_cmd.py` (recommended)

```sh
# Unit tests — no model file or license key required
python sdk_cmd.py unit_test

# Test noise cancellation / voice isolation
python sdk_cmd.py test \
  --type test_nc \
  --model /path/to/nc_vi_model.kef \
  --input /path/to/audio.wav \
  --license-key YOUR_LICENSE_KEY

# Test accent reduction
python sdk_cmd.py test \
  --type test_ar \
  --model /path/to/ar_model.kef \
  --input /path/to/audio.wav \
  --license-key YOUR_LICENSE_KEY

# Write output to a specific directory
python sdk_cmd.py test --type test_nc ... --out /tmp/krisp-out
```

Output files: `output_nc.wav` / `output_accent.wav` in the `--out` directory (default: current directory).

### Using `meson test`

Set the required environment variables, then run:

```sh
export KRISP_LICENSE_KEY=your_license_key
export KRISP_TEST_INPUT=/path/to/audio.wav
export KRISP_NC_MODEL=/path/to/nc_vi_model.kef
export KRISP_AR_MODEL=/path/to/ar_model.kef   # optional

meson test -C build/meson --verbose
```

On Windows (Command Prompt):
```cmd
set KRISP_LICENSE_KEY=your_license_key
set KRISP_TEST_INPUT=C:\path\to\audio.wav
set KRISP_NC_MODEL=C:\path\to\nc_vi_model.kef
meson test -C build\meson --verbose
```

The test binary is `build/meson/tests/test_pipeline` (`.exe` on Windows). It exits with `0` on success.

---

## Usage

If the plugin is not installed system-wide, point `GST_PLUGIN_PATH` at the build output directory:

```sh
# macOS / Linux
export GST_PLUGIN_PATH=/path/to/gst-krisp-audio/build/meson/src

# Windows
set GST_PLUGIN_PATH=C:\path\to\gst-krisp-audio\build\meson\src
```

### Noise Cancellation & Voice Isolation

```sh
gst-launch-1.0 \
  filesrc location=input.wav \
  ! wavparse \
  ! audioconvert \
  ! audioresample \
  ! audio/x-raw,format=F32LE,channels=1,rate=16000 \
  ! krispnc model=/path/to/nc_vi_model.kef license-key=YOUR_KEY \
  ! audioconvert \
  ! wavenc \
  ! filesink location=output_nc.wav
```

### Accent Reduction

```sh
gst-launch-1.0 \
  filesrc location=input.wav \
  ! wavparse \
  ! audioconvert \
  ! audioresample \
  ! audio/x-raw,format=F32LE,channels=1,rate=16000 \
  ! krispaccent model=/path/to/ar_model.kef license-key=YOUR_KEY \
  ! audioconvert \
  ! wavenc \
  ! filesink location=output_ar.wav
```

### Inspect element properties

```sh
gst-inspect-1.0 krispnc
gst-inspect-1.0 krispaccent
```

### Handling licensing warnings

The Krisp SDK validates the license key asynchronously after the pipeline starts. If validation fails, the element posts a `GST_MESSAGE_WARNING` to the bus and continues processing — the SDK passes audio through after its grace period so the pipeline is not interrupted.

To handle these warnings in application code, include `GST_MESSAGE_WARNING` in your bus watch:

```c
GstBus *bus = gst_element_get_bus(pipeline);

while (TRUE) {
    GstMessage *msg = gst_bus_timed_pop_filtered(
        bus,
        GST_CLOCK_TIME_NONE,
        (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            gst_message_unref(msg);
            goto done;

        case GST_MESSAGE_WARNING: {
            GError *err = NULL;
            gchar  *dbg = NULL;
            gst_message_parse_warning(msg, &err, &dbg);
            g_printerr("Warning [%s]: %s\n", GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), err->message);
            g_error_free(err);
            g_free(dbg);
            gst_message_unref(msg);
            break; /* non-fatal — keep running */
        }

        default: { /* GST_MESSAGE_ERROR */
            GError *err = NULL;
            gchar  *dbg = NULL;
            gst_message_parse_error(msg, &err, &dbg);
            g_printerr("Error [%s]: %s\n", GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), err->message);
            g_error_free(err);
            g_free(dbg);
            gst_message_unref(msg);
            goto done;
        }
    }
}
done:
gst_object_unref(bus);
```

> **Terminating on a licensing error** — If your application requires a valid license and should not
> process audio at all when one is absent, change the `GST_MESSAGE_WARNING` arm to treat the warning
> as fatal: replace `break` with `goto done`. The pipeline will stop immediately on the first
> licensing warning instead of continuing through the grace period.

### Accessing SDK log messages

All Krisp SDK internal log messages are routed to a GStreamer debug category named `krisp-sdk`. Enable it with the `GST_DEBUG` environment variable:

```sh
# Level 5 = TRACE (most verbose), 4 = DEBUG, 3 = INFO, 2 = WARNING, 1 = ERROR
GST_DEBUG=krisp-sdk:5 gst-launch-1.0 ...
```

---

## Element properties

Both `krispnc` and `krispaccent` share the same set of properties:

| Property | Type | Default | Description |
|---|---|---|---|
| `model` | string | *(required)* | Path to the `.kef` model file |
| `license-key` | string | `""` | Krisp SDK license key |
| `noise-suppression-level` | float `[0.0, 100.0]` | `100.0` | Suppression intensity (0 = off, 100 = full) |
| `frame-duration` | int `{10,15,20,30,32}` | `10` | Internal processing frame size in ms |

### Supported audio formats

| Parameter | Accepted values |
|---|---|
| Format | `S16LE`, `F32LE` |
| Channels | `1` (mono only) |
| Rate (Hz) | `8000`, `16000`, `24000`, `32000`, `44100`, `48000`, `88200`, `96000` |

---

## Platform notes

### macOS

The `meson.build` links against four system frameworks required by the SDK's bundled libraries.
These are applied automatically on macOS:

| Framework | Required by |
|---|---|
| `Foundation` | `NSLog` in ONNX Runtime's `apple_log_sink.mm`; also re-exports `CoreFoundation` (CF* symbols used by libcurl) |
| `Accelerate` | cblas/vDSP in XNNPACK / MLAS |
| `SystemConfiguration` | libcurl proxy detection |
| `Security` | libcurl certificate handling |

### Linux

The SDK's bundled `external/` static libraries cover all libcurl dependencies, so no extra system
libraries are needed beyond GStreamer. The `-framework` flags in `meson.build` are guarded by a
`host_machine.system() == 'darwin'` check and are not applied on Linux.

### Windows

- **`sdk_cmd.py` handles the MSVC environment automatically** via Meson's `--vsenv` flag — no need
  to open an x64 Native Tools Command Prompt first. Visual Studio 2019 or 2022 (or Build Tools)
  must be installed.
- **`sdk_cmd.py` auto-detects GStreamer's pkg-config** by checking `GSTREAMER_1_0_ROOT_MSVC_X86_64`
  env var, then `C:\gstreamer\1.0\msvc_x86_64\` and `C:\Program Files\gstreamer\1.0\msvc_x86_64\`.
  For non-standard locations pass `--gst-dir`.
- When invoking Meson directly, pass `--vsenv -Db_vscrt=mt` and set `PKG_CONFIG_PATH` as shown in
  the build section above. `-Db_vscrt=mt` is required to match the `/MT` CRT used by the Krisp SDK.
- The plugin is a `.dll`; `GST_PLUGIN_PATH` must point to the directory containing it.

---

## Architecture notes

- **Process-wide SDK lifecycle** — `KrispGst::GlobalInit` is ref-counted. The first element to reach the `READY→PAUSED` transition calls `globalInit`; the last one to be finalized calls `globalDestroy`. Multiple elements in the same process share a single SDK instance.
- **Async licensing** — The server SDK validates the license key on its own internal thread after `globalInit` returns. Any licensing error is stored and surfaced as a `GST_ELEMENT_WARNING` on the first processed audio buffer. The pipeline keeps running — the SDK passes audio through after its grace period. Applications should watch for `GST_MESSAGE_WARNING` on the bus to detect and react to licensing failures.
- **Carry-buffer FIFO** — The SDK requires fixed-size frames. Incoming GStreamer buffers of arbitrary size are accumulated in an input carry buffer; complete frames are processed; output is drained back into the GStreamer buffer. This ensures no samples are dropped regardless of upstream buffer sizes.
