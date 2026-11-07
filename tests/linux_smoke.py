#!/usr/bin/env python3
"""Exercise the native GTK application on a dedicated X11 display.

Run with: xvfb-run -a dbus-run-session -- python3 tests/linux_smoke.py BINARY
Requires Openbox, xdotool and xprop. No screenshots, physical audio device,
network service, user configuration, or animation-duration assertions are used.
The shared C++ suite covers exhaustive counter and rendering behavior.
"""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from __future__ import annotations

import argparse
import configparser
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


class SmokeFailure(RuntimeError):
    pass


def command(*args: str, env: dict[str, str], timeout: float = 10) -> str:
    result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise SmokeFailure(f"Command failed ({result.returncode}): {args!r}\n{result.stderr}")
    return result.stdout.strip()


def wait_for(description: str, check, process: subprocess.Popen | None = None, timeout: float = 15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise SmokeFailure(f"Application exited before {description}: {process.returncode}")
        result = check()
        if result:
            return result
        time.sleep(0.05)
    raise SmokeFailure(f"Timed out waiting for {description}")


def stop(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--log-dir", type=Path, default=Path("build/smoke-logs"))
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise SmokeFailure(f"Not an executable file: {binary}")
    if not os.environ.get("DISPLAY") or not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        raise SmokeFailure("Run this check inside xvfb-run and dbus-run-session (see --help).")
    for tool in ("openbox", "xdotool", "xprop"):
        if shutil.which(tool) is None:
            raise SmokeFailure(f"Required smoke-test tool is missing: {tool}")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    app = None
    wm = None
    with tempfile.TemporaryDirectory(prefix="tally-linux-smoke-") as temp:
        test_root = Path(temp)
        env = os.environ.copy()
        env.update({
            "XDG_CONFIG_HOME": str(test_root / "config"),
            "XDG_CACHE_HOME": str(test_root / "cache"),
            "XDG_DATA_HOME": str(test_root / "data"),
            "XDG_RUNTIME_DIR": str(test_root / "runtime"),
            "GDK_BACKEND": "x11",
            "NO_AT_BRIDGE": "1",
        })
        for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_RUNTIME_DIR"):
            Path(env[key]).mkdir(mode=0o700)
        # ALSA's null sink exercises the audio path without a hardware dependency.
        alsa = test_root / "asound.conf"
        alsa.write_text("pcm.!default { type null }\n", encoding="utf-8")
        env["ALSA_CONFIG_PATH"] = str(alsa)
        state_dir = Path(env["XDG_CONFIG_HOME"]) / "tally-counter"
        state_dir.mkdir(mode=0o700)
        state = state_dir / "count.txt"
        state.write_text("7\n", encoding="ascii")
        app_log = args.log_dir / "linux-app.log"
        wm_log = args.log_dir / "openbox.log"
        with app_log.open("w", encoding="utf-8") as app_output, wm_log.open("w", encoding="utf-8") as wm_output:
            try:
                wm = subprocess.Popen(["openbox", "--sm-disable"], env=env, stdout=wm_output, stderr=subprocess.STDOUT)

                def wm_ready():
                    result = subprocess.run(
                        ["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"], env=env,
                        capture_output=True, text=True, timeout=3,
                    )
                    return result.returncode == 0 and re.search(r"window id # 0x[1-9a-fA-F]", result.stdout)

                wait_for("the Openbox window manager", wm_ready, wm)

                def launch():
                    nonlocal app
                    app = subprocess.Popen([str(binary)], env=env, stdout=app_output, stderr=subprocess.STDOUT)

                    def find_window():
                        result = subprocess.run(
                            ["xdotool", "search", "--onlyvisible", "--pid", str(app.pid), "--name", "^Tally Counter$"],
                            env=env, capture_output=True, text=True, timeout=3,
                        )
                        windows = result.stdout.strip().splitlines()
                        if result.returncode == 0 and len(windows) == 1:
                            return windows[0]
                        return None

                    window = wait_for("the GTK window", find_window, app)
                    command("xdotool", "windowactivate", "--sync", window, env=env)
                    return window

                def expect_count(value: int):
                    def matches():
                        try:
                            return state.read_text(encoding="ascii").strip() == str(value)
                        except FileNotFoundError:
                            return False
                    wait_for(f"saved count {value}", matches, app)

                def key(name: str):
                    command("xdotool", "key", "--clearmodifiers", name, env=env)

                def close(window: str):
                    command("xdotool", "windowactivate", "--sync", window, env=env)
                    key("alt+F4")
                    try:
                        code = app.wait(timeout=10)
                    except subprocess.TimeoutExpired as exc:
                        raise SmokeFailure("The application did not close through the window manager") from exc
                    if code != 0:
                        raise SmokeFailure(f"The application returned {code} after a normal close")

                window = launch()
                command("xdotool", "keydown", "space", env=env)
                expect_count(8)  # The initial seven must have been loaded from disk.
                command("xdotool", "keydown", "space", env=env)
                command("xdotool", "keydown", "space", env=env)
                command("xdotool", "keyup", "space", env=env)
                key("space")
                expect_count(9)  # Repeated key-downs during one hold count only once.
                key("Tab")
                key("space")
                expect_count(10)  # Space still increments while Reset is selected.
                key("Return")
                expect_count(0)
                key("space")
                expect_count(1)
                second = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=10)
                if second.returncode != 0 or app.poll() is not None:
                    raise SmokeFailure(f"Single-instance activation failed: {second.returncode}\n{second.stderr}")
                expect_count(1)
                close(window)

                geometry = configparser.ConfigParser()
                if not geometry.read(state_dir / "window.ini", encoding="utf-8"):
                    raise SmokeFailure("Normal close did not persist window geometry")
                if geometry.getint("Window", "width") < 400 or geometry.getint("Window", "height") < 220:
                    raise SmokeFailure("Persisted window dimensions are below the supported minimum")
                window = launch()
                key("space")
                expect_count(2)  # A fresh native process must restore the saved one.
                close(window)
                print("GTK smoke passed: launch, restore, repeat suppression, reset-focus input, reset, "
                      "single instance, normal close, count persistence, and window-state persistence.")
            finally:
                stop(app)
                stop(wm)


if __name__ == "__main__":
    try:
        main()
    except (SmokeFailure, OSError, subprocess.SubprocessError, configparser.Error) as error:
        print(f"GTK smoke failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
