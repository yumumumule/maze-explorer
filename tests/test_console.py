"""Windows console integration test; standard library only, no desktop input."""
import collections
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import re
import subprocess
import sys
import time

if sys.platform != "win32":
    raise SystemExit("This integration test uses the Windows console API.")

kernel = c.WinDLL("kernel32", use_last_error=True)
kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
kernel.CreateFileW.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.AttachConsole.argtypes = [w.DWORD]


class Coord(c.Structure):
    _fields_ = [("x", c.c_short), ("y", c.c_short)]


class Rect(c.Structure):
    _fields_ = [("left", c.c_short), ("top", c.c_short), ("right", c.c_short), ("bottom", c.c_short)]


class Info(c.Structure):
    _fields_ = [("size", Coord), ("cursor", Coord), ("attrs", w.WORD), ("window", Rect), ("maximum", Coord)]


class Key(c.Structure):
    _fields_ = [("down", w.BOOL), ("repeat", w.WORD), ("vk", w.WORD), ("scan", w.WORD), ("char", w.WCHAR), ("control", w.DWORD)]


class Record(c.Structure):
    _fields_ = [("type", w.WORD), ("key", Key)]


kernel.GetConsoleScreenBufferInfo.argtypes = [w.HANDLE, c.POINTER(Info)]
kernel.ReadConsoleOutputCharacterW.argtypes = [w.HANDLE, w.LPWSTR, w.DWORD, Coord, c.POINTER(w.DWORD)]
kernel.WriteConsoleInputW.argtypes = [w.HANDLE, c.POINTER(Record), w.DWORD, c.POINTER(w.DWORD)]
kernel.SetConsoleScreenBufferSize.argtypes = [w.HANDLE, Coord]
kernel.SetConsoleWindowInfo.argtypes = [w.HANDLE, w.BOOL, c.POINTER(Rect)]

executable = Path(sys.argv[1] if len(sys.argv) > 1 else "main.exe").resolve()
for arguments in (["--seed", "-1"], ["--seed", "abc"], ["--seed", "4294967296"]):
    assert subprocess.run([str(executable), *arguments], capture_output=True).returncode == 1
assert b"Maze Explorer" in subprocess.check_output([str(executable), "--help"])

# A private, hidden console tests real keyboard events without stealing focus.
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
game = subprocess.Popen([str(executable), "--seed", "12345"], stdin=subprocess.DEVNULL,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
kernel.FreeConsole()
input_handle = None


def screen():
    handle = kernel.CreateFileW("CONOUT$", 0xC0000000, 3, None, 3, 0, None)
    try:
        info = Info()
        assert kernel.GetConsoleScreenBufferInfo(handle, c.byref(info))
        count = w.DWORD()
        buffer = c.create_unicode_buffer(info.size.x * info.size.y + 1)
        assert kernel.ReadConsoleOutputCharacterW(handle, buffer, len(buffer) - 1, Coord(0, 0), c.byref(count))
        text = buffer[:count.value]
        rows = [text[y * info.size.x + info.window.left:y * info.size.x + info.window.right + 1]
                for y in range(info.window.top, info.window.bottom + 1)]
        return rows
    finally:
        kernel.CloseHandle(handle)


def wait_for(text):
    deadline = time.monotonic() + 5
    contents = ""
    while time.monotonic() < deadline:
        contents = "\n".join(screen())
        if text in contents:
            return contents
        if game.poll() is not None:
            raise AssertionError(f"Game exited early: {game.returncode}")
        time.sleep(0.03)
    raise AssertionError(f"Missing {text!r}:\n{contents}")


def press(char="\0", virtual_key=0):
    event = Record(1, Key(True, 1, virtual_key or ord(char.upper()), 0, char, 0))
    written = w.DWORD()
    assert kernel.WriteConsoleInputW(input_handle, c.byref(event), 1, c.byref(written))
    time.sleep(0.05)


def resize(width, height):
    handle = kernel.CreateFileW("CONOUT$", 0xC0000000, 3, None, 3, 0, None)
    try:
        small = Rect(0, 0, 29, 9)
        assert kernel.SetConsoleWindowInfo(handle, True, c.byref(small))
        assert kernel.SetConsoleScreenBufferSize(handle, Coord(width, height))
        area = Rect(0, 0, width - 1, height - 1)
        assert kernel.SetConsoleWindowInfo(handle, True, c.byref(area))
    finally:
        kernel.CloseHandle(handle)
    time.sleep(0.3)


try:
    deadline = time.monotonic() + 5
    while not kernel.AttachConsole(game.pid):
        assert time.monotonic() < deadline, "Could not attach to the private console."
        time.sleep(0.03)
    input_handle = kernel.CreateFileW("CONIN$", 0xC0000000, 3, None, 3, 0, None)
    wait_for("MAZE EXPLORER")
    resize(100, 40)
    press("s")
    press("\r", 13)
    wait_for("Difficulty: HARD")
    press("\r", 13)
    wait_for("Difficulty: EASY")
    press("\r", 13)
    wait_for("Difficulty: NORMAL")
    press(virtual_key=40)
    press("\r", 13)
    wait_for("Limited sight (11 x 11): OFF")
    press("s")
    press("\r", 13)
    wait_for("HOW TO PLAY")
    press("\x1b", 27)
    for _ in range(3):
        press(virtual_key=38)
    press("\r", 13)
    text = wait_for("SCROLL VIEW")
    energy = int(re.search(r"\] (\d+)/", text).group(1))
    press("h")
    text = wait_for("Hints: 1")
    assert int(re.search(r"\] (\d+)/", text).group(1)) == energy - 3
    press("h")
    assert "Hints: 1" in "\n".join(screen())
    press("v")
    wait_for("LIMITED SIGHT")
    resize(60, 18)
    wait_for("terminal too small")
    resize(100, 40)
    wait_for("LIMITED SIGHT")
    press("v")
    press("r")
    wait_for("Hints: 0")
    rows = screen()
    # In scroll mode a large console displays one complete period of the maze.
    origin_x = (len(rows[0]) - 42) // 2
    origin_y = 8 + (len(rows) - 8 - 4 - 21) // 2
    board = [[rows[origin_y + y][origin_x + 2 * x] for x in range(21)] for y in range(21)]
    start = next((x, y) for y in range(21) for x in range(21) if board[y][x] == "@")
    target = next((x, y) for y in range(21) for x in range(21) if board[y][x] == "X")
    pending = collections.deque([start])
    parent = {start: None}
    directions = [(1, 0, 39), (-1, 0, 37), (0, 1, 40), (0, -1, 38)]
    while pending and target not in parent:
        current = pending.popleft()
        for dx, dy, vk in directions:
            nxt = ((current[0] + dx) % 21, (current[1] + dy) % 21)
            if nxt not in parent and board[nxt[1]][nxt[0]] != "#":
                parent[nxt] = (current, vk)
                pending.append(nxt)
    assert target in parent, "Rendered maze has no solution."
    route = []
    current = target
    while parent[current] is not None:
        current, vk = parent[current]
        route.append(vk)
    for vk in reversed(route):
        press(virtual_key=vk)
    text = wait_for("ESCAPED!")
    assert f"Shortest: {len(route)}" in text and "Efficiency: 100%" in text
    press("\x1b", 27)
    wait_for("Wins this session: 1")
    press("\r", 13)
    wait_for("Steps: 0")
    press("n")
    assert "Seed: 12345" not in wait_for("SCROLL VIEW")
    press("q")
    assert game.wait(timeout=5) == 0
    print("PASS: CLI validation, menu, difficulty, help, hints, both views, resize, arrow-key solution, win, retry, new maze and clean exit.")
finally:
    if game.poll() is None:
        game.terminate()
        game.wait(timeout=5)
    if input_handle is not None:
        kernel.CloseHandle(input_handle)
    kernel.FreeConsole()
