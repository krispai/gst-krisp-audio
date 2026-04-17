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
TEST_BIN        = MESON_BUILD_DIR / "tests" / "test_pipeline"
UNIT_TEST_BIN   = MESON_BUILD_DIR / "tests" / "test_unit"

# Locate meson via PATH; fall back to bare name so the shell can find it.
_meson_which = shutil.which("meson")
MESON_BIN = Path(_meson_which) if _meson_which else Path("meson")

# On Apple Silicon Homebrew installs pkg-config files under /opt/homebrew.
# Prepend these paths only when running on macOS so the system pkg-config
# can find GStreamer. On Linux/Windows the environment is already set up.
_IS_MACOS = sys.platform == "darwin"
HOMEBREW_PKG = "/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig" if _IS_MACOS else ""

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def run(cmd: list, **kwargs) -> None:
    print(f"\n$ {' '.join(str(c) for c in cmd)}", flush=True)
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        sys.exit(result.returncode)


def meson_setup(sdk_dir: str, nc: bool, ar: bool, wipe: bool) -> None:
    sdk_dir = str(Path(sdk_dir).resolve())
    feature_enabled  = "enabled"
    feature_disabled = "disabled"
    cmd = [
        str(MESON_BIN), "setup", str(MESON_BUILD_DIR),
        f"-Dkrisp_sdk_dir={sdk_dir}",
        f"-Dnc={feature_enabled if nc else feature_disabled}",
        f"-Dar={feature_enabled if ar else feature_disabled}",
    ]
    if wipe:
        cmd.append("--wipe")
    env = os.environ.copy()
    if HOMEBREW_PKG:
        existing = env.get("PKG_CONFIG_PATH", "")
        env["PKG_CONFIG_PATH"] = f"{HOMEBREW_PKG}:{existing}" if existing else HOMEBREW_PKG
    run(cmd, cwd=PROJECT_ROOT, env=env)


def meson_compile() -> None:
    env = os.environ.copy()
    if HOMEBREW_PKG:
        existing = env.get("PKG_CONFIG_PATH", "")
        env["PKG_CONFIG_PATH"] = f"{HOMEBREW_PKG}:{existing}" if existing else HOMEBREW_PKG
    run([str(MESON_BIN), "compile", "-C", str(MESON_BUILD_DIR)],
        cwd=PROJECT_ROOT, env=env)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def cmd_build(args: argparse.Namespace, wipe: bool) -> None:
    meson_setup(args.sdk_dir, nc=args.nc, ar=args.ar, wipe=wipe)
    meson_compile()
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

    env = os.environ | {
        "GST_PLUGIN_PATH":   str(PLUGIN_DIR),
        "KRISP_TEST_INPUT":  str(args.input),
        "KRISP_LICENSE_KEY": args.license_key or "",
    }

    if args.type == "test_nc":
        env["KRISP_NC_MODEL"]  = str(args.model)
        env["KRISP_NC_OUTPUT"] = str(out_dir / "output_nc.wav")
    else:
        env["KRISP_AR_MODEL"]  = str(args.model)
        env["KRISP_AR_OUTPUT"] = str(out_dir / "output_accent.wav")

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
