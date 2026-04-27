#!/usr/bin/env python3
"""
sdk_cmd.py — Build and test helper for gst-krisp-audio

Commands:
  build         Configure + compile (pass --sdk-dir to locate the Krisp SDK)
  clean_build   Same as build but wipes the Meson build directory first
  unit_test     Run unit tests (no model or license key required)
  test          Run a single pipeline test (NC or AR)

Examples:
  python sdk_cmd.py build --sdk-dir /path/to/krisp-sdk
  python sdk_cmd.py build --sdk-dir /path/to/krisp-sdk --nc --no-ar
  python sdk_cmd.py clean_build --sdk-dir /path/to/krisp-sdk
  python sdk_cmd.py unit_test
  python sdk_cmd.py test --type test_nc \\
      --model /path/to/nc_model.kef \\
      --input /path/to/audio.wav \\
      --license-key YOUR_LICENSE_KEY
  python sdk_cmd.py test --type test_ar \\
      --model /path/to/ar_model.kef \\
      --input /path/to/audio.wav \\
      --license-key YOUR_LICENSE_KEY
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Fixed paths
# ---------------------------------------------------------------------------
PROJECT_ROOT    = Path(__file__).parent.resolve()
BUILD_DIR       = PROJECT_ROOT / "build"
MESON_BUILD_DIR = BUILD_DIR / "meson"
PLUGIN_DIR      = MESON_BUILD_DIR / "src"
_EXE            = ".exe" if sys.platform == "win32" else ""
TEST_BIN        = MESON_BUILD_DIR / "tests" / f"test_pipeline{_EXE}"
UNIT_TEST_BIN   = MESON_BUILD_DIR / "tests" / f"test_unit{_EXE}"

# Locate meson. Search order:
#   1. MESON env var (explicit override, e.g. MESON=C:\venv\Scripts\meson.exe)
#   2. shutil.which PATH lookup
#   3. Active virtualenv / conda prefix Scripts directory
#   4. Manual PATH scan (Windows Store Python doesn't inherit the full PATH)
#   5. Sibling to the Python running this script
#   6. Python module fallback
def _find_meson() -> list:
    meson_env = os.environ.get("MESON")
    if meson_env:
        return [meson_env]

    found = shutil.which("meson")
    if found:
        return [found]

    # Check active virtual environment or conda environment
    for env_var in ("VIRTUAL_ENV", "CONDA_PREFIX"):
        root = os.environ.get(env_var)
        if root:
            for name in ("meson.exe", "meson"):
                candidate = Path(root) / "Scripts" / name
                if candidate.exists():
                    return [str(candidate)]

    if sys.platform == "win32":
        # shutil.which is unreliable in Windows Store Python and venv Scripts dirs
        # are often only in the interactive shell PATH, not inherited by subprocesses.
        # Scan sibling directories of the project root for a venv that has meson.exe —
        # this covers layouts like C:\projects\venv_conan2\ next to C:\projects\gst-krisp-audio\.
        try:
            for sibling in sorted(PROJECT_ROOT.parent.iterdir()):
                candidate = sibling / "Scripts" / "meson.exe"
                if candidate.exists():
                    return [str(candidate)]
        except OSError:
            pass
        # Manual scan of os.environ PATH as a last resort
        for p in os.environ.get("PATH", "").split(os.pathsep):
            for name in ("meson.exe", "meson.EXE"):
                candidate = Path(p) / name
                if candidate.exists():
                    return [str(candidate)]

    for name in ("meson.exe", "meson"):
        sibling = Path(sys.executable).parent / name
        if sibling.exists():
            return [str(sibling)]

    import warnings
    warnings.warn("meson not found in PATH or any known location; falling back to 'python -m meson'")
    return [sys.executable, "-m", "meson"]

MESON_CMD = _find_meson()

# On Apple Silicon Homebrew installs pkg-config files under /opt/homebrew.
_IS_MACOS   = sys.platform == "darwin"
_IS_WINDOWS = sys.platform == "win32"
HOMEBREW_PKG = "/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig" if _IS_MACOS else ""

# GStreamer Windows installer puts pkg-config.exe and .pc files under its root.
# The installer also sets GSTREAMER_1_0_ROOT_MSVC_X86_64 / _MINGW_X86_64 env vars.
_GST_WIN_SUBDIRS = ("msvc_x86_64", "mingw_x86_64")
_GST_WIN_ROOTS   = [Path(r"C:\gstreamer\1.0"), Path(r"C:\Program Files\gstreamer\1.0")]

def _find_gst_win_paths(gst_dir: str = None):
    """Return (pkg_config_exe_str, pkg_config_path_str) or (None, None)."""
    candidates = []
    if gst_dir:
        candidates.append(Path(gst_dir))
    for var in ("GSTREAMER_1_0_ROOT_MSVC_X86_64", "GSTREAMER_1_0_ROOT_MINGW_X86_64"):
        root = os.environ.get(var)
        if root:
            candidates.append(Path(root))
    for base in _GST_WIN_ROOTS:
        for sub in _GST_WIN_SUBDIRS:
            candidates.append(base / sub)
    for root in candidates:
        exe     = root / "bin" / "pkg-config.exe"
        pc_path = root / "lib" / "pkgconfig"
        if exe.exists() and pc_path.is_dir():
            return str(exe), str(pc_path)
    return None, None


def _make_env(gst_dir: str = None) -> dict:
    env = os.environ.copy()
    if HOMEBREW_PKG:
        existing = env.get("PKG_CONFIG_PATH", "")
        env["PKG_CONFIG_PATH"] = f"{HOMEBREW_PKG}:{existing}" if existing else HOMEBREW_PKG
    elif _IS_WINDOWS:
        pkg_config_exe, pkg_config_path = _find_gst_win_paths(gst_dir)
        if pkg_config_exe:
            env.setdefault("PKG_CONFIG", pkg_config_exe)
            existing = env.get("PKG_CONFIG_PATH", "")
            env["PKG_CONFIG_PATH"] = f"{pkg_config_path};{existing}" if existing else pkg_config_path
        else:
            print(
                "WARNING: GStreamer pkg-config not found. "
                "Install GStreamer from gstreamer.freedesktop.org or pass --gst-dir PATH.",
                file=sys.stderr,
            )
    return env

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def run(cmd: list, **kwargs) -> None:
    print(f"\n$ {' '.join(str(c) for c in cmd)}", flush=True)
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        sys.exit(result.returncode)


def meson_setup(sdk_dir: str, nc: bool, ar: bool, wipe: bool, gst_dir: str = None) -> None:
    sdk_dir = str(Path(sdk_dir).resolve())
    cmd = [
        *MESON_CMD, "setup", str(MESON_BUILD_DIR),
        f"-Dkrisp_sdk_dir={sdk_dir}",
        f"-Dnc={'enabled' if nc else 'disabled'}",
        f"-Dar={'enabled' if ar else 'disabled'}",
    ]
    if _IS_WINDOWS:
        # --vsenv activates the Visual Studio environment so cl.exe is used even when
        # MinGW is also on PATH.
        # b_vscrt=mt matches the Krisp SDK which is compiled with /MT (static release CRT).
        cmd += ["--vsenv", "-Db_vscrt=mt"]
    if wipe:
        cmd.append("--wipe")
    run(cmd, cwd=PROJECT_ROOT, env=_make_env(gst_dir))


def meson_compile(gst_dir: str = None) -> None:
    run([*MESON_CMD, "compile", "-C", str(MESON_BUILD_DIR)],
        cwd=PROJECT_ROOT, env=_make_env(gst_dir))


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_build(args: argparse.Namespace, wipe: bool) -> None:
    gst_dir = getattr(args, "gst_dir", None)
    meson_setup(args.sdk_dir, nc=args.nc, ar=args.ar, wipe=wipe, gst_dir=gst_dir)
    meson_compile(gst_dir)
    print("\nBuild complete.")


def cmd_unit_test(_args: argparse.Namespace) -> None:
    if not UNIT_TEST_BIN.exists():
        print(f"ERROR: unit test binary not found at {UNIT_TEST_BIN}\nRun 'build' first.",
              file=sys.stderr)
        sys.exit(1)
    run([str(UNIT_TEST_BIN)], cwd=PROJECT_ROOT)


def cmd_test(args: argparse.Namespace) -> None:
    if not TEST_BIN.exists():
        print(f"ERROR: test binary not found at {TEST_BIN}\nRun 'build' first.",
              file=sys.stderr)
        sys.exit(1)

    if not Path(args.model).exists():
        print(f"ERROR: model file not found: {args.model}", file=sys.stderr)
        sys.exit(1)

    if not Path(args.input).exists():
        print(f"ERROR: input WAV not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    # GStreamer's pipeline parser (GLib GScanner) interprets backslashes as escape
    # sequences, so paths embedded in pipeline descriptions must use forward slashes.
    def _gst_path(p) -> str:
        return Path(p).resolve().as_posix()

    env = os.environ.copy()
    env.update({
        "GST_PLUGIN_PATH":   str(PLUGIN_DIR),
        "KRISP_TEST_INPUT":  _gst_path(args.input),
        "KRISP_LICENSE_KEY": args.license_key or "",
    })

    # On Windows the GStreamer runtime DLLs must be on PATH so that both
    # test_pipeline.exe and gstkrisp.dll can load them.
    if _IS_WINDOWS:
        pkg_config_exe, _ = _find_gst_win_paths(getattr(args, "gst_dir", None))
        if pkg_config_exe:
            gst_bin = str(Path(pkg_config_exe).parent)
            existing_path = env.get("PATH", "")
            env["PATH"] = f"{gst_bin};{existing_path}" if existing_path else gst_bin

    if args.type == "test_nc":
        env["KRISP_NC_MODEL"]  = _gst_path(args.model)
        env["KRISP_NC_OUTPUT"] = _gst_path(out_dir / "output_nc.wav")
    else:
        env["KRISP_AR_MODEL"]  = _gst_path(args.model)
        env["KRISP_AR_OUTPUT"] = _gst_path(out_dir / "output_accent.wav")

    run([str(TEST_BIN)], env=env, cwd=PROJECT_ROOT)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--sdk-dir", required=True, metavar="PATH",
        help="Path to the Krisp Audio SDK directory (contains include/ and lib/)")
    parser.add_argument(
        "--nc", dest="nc", action="store_true", default=True,
        help="Enable the krispnc element (default: on)")
    parser.add_argument(
        "--no-nc", dest="nc", action="store_false",
        help="Disable the krispnc element")
    parser.add_argument(
        "--ar", dest="ar", action="store_true", default=True,
        help="Enable the krispaccent element (default: on)")
    parser.add_argument(
        "--no-ar", dest="ar", action="store_false",
        help="Disable the krispaccent element")
    parser.add_argument(
        "--gst-dir", default=None, metavar="PATH",
        help="(Windows) Root of the GStreamer installation, e.g. "
             r"C:\gstreamer\1.0\msvc_x86_64. Auto-detected when omitted."
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="sdk_cmd.py",
        description="Build and test helper for gst-krisp-audio",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_build = sub.add_parser("build", help="Configure + compile")
    build_args(p_build)

    p_clean = sub.add_parser("clean_build",
                              help="Wipe Meson build dir then build from scratch")
    build_args(p_clean)

    sub.add_parser("unit_test", help="Run unit tests (no model or license key required)")

    p_test = sub.add_parser("test", help="Run a pipeline test")
    p_test.add_argument(
        "--type", choices=["test_nc", "test_ar"], required=True,
        help="Which element to test")
    p_test.add_argument(
        "--model", required=True, metavar="PATH",
        help="Path to the .kef model file")
    p_test.add_argument(
        "--input", required=True, metavar="PATH",
        help="Path to the input WAV file")
    p_test.add_argument(
        "--out", default=".", metavar="DIR",
        help="Directory to write the output WAV into (default: current directory)")
    p_test.add_argument(
        "--license-key", default="", metavar="KEY",
        help="Krisp SDK license key (passed as KRISP_LICENSE_KEY to the test binary)")

    args = parser.parse_args()

    if args.command == "build":
        cmd_build(args, wipe=False)
    elif args.command == "clean_build":
        cmd_build(args, wipe=True)
    elif args.command == "unit_test":
        cmd_unit_test(args)
    elif args.command == "test":
        cmd_test(args)


if __name__ == "__main__":
    main()
