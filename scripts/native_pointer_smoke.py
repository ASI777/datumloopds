#!/usr/bin/env python3
"""Exercise the native viewport and Move workflow with X11 pointer events."""

from __future__ import annotations

import ctypes
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time


ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = Path(os.environ.get(
    "DATUMLOOPDS_EXECUTABLE",
    ROOT / "build/native/apps/datumloop-desktop/DatumLoopDS",
))


class XTestPointer:
    def __init__(self) -> None:
        self.x11 = ctypes.CDLL("libX11.so.6")
        self.xtst = ctypes.CDLL("libXtst.so.6")
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XDefaultScreen.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultScreen.restype = ctypes.c_int
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.x11.XCloseDisplay.restype = ctypes.c_int
        self.x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XKeysymToKeycode.restype = ctypes.c_ubyte
        self.xtst.XTestFakeMotionEvent.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong
        ]
        self.xtst.XTestFakeMotionEvent.restype = ctypes.c_int
        self.xtst.XTestFakeButtonEvent.argtypes = [
            ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong
        ]
        self.xtst.XTestFakeButtonEvent.restype = ctypes.c_int
        self.xtst.XTestFakeKeyEvent.argtypes = [
            ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong
        ]
        self.xtst.XTestFakeKeyEvent.restype = ctypes.c_int

        self.display = self.x11.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("Could not open the X11 display from DISPLAY")
        self.screen = self.x11.XDefaultScreen(self.display)

    def close(self) -> None:
        if self.display:
            self.x11.XCloseDisplay(self.display)
            self.display = None

    def click(self, x: int, y: int) -> None:
        self.xtst.XTestFakeMotionEvent(self.display, self.screen, x, y, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.08)
        self.xtst.XTestFakeButtonEvent(self.display, 1, 1, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.08)
        self.xtst.XTestFakeButtonEvent(self.display, 1, 0, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.12)

    def drag(self, x: int, y: int, dx: int, dy: int, button: int) -> None:
        self.xtst.XTestFakeMotionEvent(self.display, self.screen, x, y, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.08)
        self.xtst.XTestFakeButtonEvent(self.display, button, 1, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.08)
        self.xtst.XTestFakeMotionEvent(self.display, self.screen, x + dx, y + dy, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.12)
        self.xtst.XTestFakeButtonEvent(self.display, button, 0, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.16)

    def wheel_up(self, x: int, y: int) -> None:
        self.xtst.XTestFakeMotionEvent(self.display, self.screen, x, y, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.08)
        self.xtst.XTestFakeButtonEvent(self.display, 4, 1, 0)
        self.x11.XFlush(self.display)
        self.xtst.XTestFakeButtonEvent(self.display, 4, 0, 0)
        self.x11.XFlush(self.display)
        time.sleep(0.16)

    def key(self, keysym: int, down: bool) -> None:
        keycode = self.x11.XKeysymToKeycode(self.display, keysym)
        if not keycode:
            raise RuntimeError(f"X11 has no keycode for keysym {keysym:#x}")
        self.xtst.XTestFakeKeyEvent(self.display, keycode, int(down), 0)
        self.x11.XFlush(self.display)

    def replace_text(self, text: str) -> None:
        control_l = 0xFFE3
        self.key(control_l, True)
        self.key(ord("a"), True)
        self.key(ord("a"), False)
        self.key(control_l, False)
        for character in text:
            self.key(ord(character), True)
            self.key(ord(character), False)
        self.x11.XFlush(self.display)
        time.sleep(0.15)


def read_output(stream, lines: queue.Queue[str]) -> None:
    for line in stream:
        print(line, end="", flush=True)
        lines.put(line.rstrip("\n"))


def collect_targets(lines: queue.Queue[str], process: subprocess.Popen[str]) -> dict[str, tuple[int, int]]:
    targets: dict[str, tuple[int, int]] = {}
    needed = {
        "cadViewport",
        "selectionModeFaceButton",
        "moveAxisZButton",
        "moveDistanceField",
        "cancelMoveButton",
        "confirmMoveButton",
        "undoMoveButton",
        "redoMoveButton",
    }
    deadline = time.monotonic() + 5.0
    while needed - targets.keys() and time.monotonic() < deadline:
        if process.poll() is not None and lines.empty():
            break
        try:
            line = lines.get(timeout=0.1)
        except queue.Empty:
            continue
        fields = line.split()
        if len(fields) == 4 and fields[0] == "INTERACTION_TARGET":
            targets[fields[1]] = (int(fields[2]), int(fields[3]))
    missing = needed - targets.keys()
    if missing:
        raise RuntimeError("Native app did not report pointer targets: " + ", ".join(sorted(missing)))
    return targets


def main() -> int:
    if not EXECUTABLE.is_file():
        print(f"Native executable not found: {EXECUTABLE}", file=sys.stderr)
        return 2

    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "xcb"
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    process = subprocess.Popen(
        [str(EXECUTABLE), "--interaction-smoke"],
        cwd=ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    assert process.stdout is not None
    lines: queue.Queue[str] = queue.Queue()
    reader = threading.Thread(target=read_output, args=(process.stdout, lines), daemon=True)
    reader.start()
    pointer = None
    try:
        time.sleep(0.8)
        targets = collect_targets(lines, process)
        pointer = XTestPointer()

        # Select the exact solid through the real viewport MouseArea/pick path.
        pointer.click(*targets["cadViewport"])
        pointer.drag(*targets["cadViewport"], 28, 9, 2)
        pointer.wheel_up(*targets["cadViewport"])
        pointer.click(*targets["selectionModeFaceButton"])
        pointer.click(*targets["cadViewport"])
        # Start a constrained move with the visible viewport axis control.
        pointer.click(*targets["moveAxisZButton"])
        pointer.click(*targets["moveDistanceField"])
        pointer.replace_text("4.5")
        # Cancel leaves the exact document position untouched.
        pointer.click(*targets["cancelMoveButton"])

        # Repeat and commit, then exercise the visible Undo and Redo controls.
        pointer.click(*targets["moveAxisZButton"])
        pointer.click(*targets["moveDistanceField"])
        pointer.replace_text("4.5")
        pointer.click(*targets["confirmMoveButton"])
        pointer.click(*targets["undoMoveButton"])
        pointer.click(*targets["redoMoveButton"])

        try:
            return_code = process.wait(timeout=9.0)
        except subprocess.TimeoutExpired:
            raise RuntimeError("Native pointer-smoke app did not finish its state check")
        reader.join(timeout=1.0)
        if return_code != 0:
            return return_code
        print("XTest pointer smoke passed: object/face pick, camera navigation, and Move cancel/confirm/undo/redo.")
        return 0
    except Exception as error:
        print(f"Native pointer smoke failed: {error}", file=sys.stderr)
        process.terminate()
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()
        return 1
    finally:
        if pointer is not None:
            pointer.close()


if __name__ == "__main__":
    raise SystemExit(main())
