"""Exercise VS Code's installed C/C++ adapter and the project launch settings."""
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import threading
import time

root = Path(__file__).resolve().parent.parent
adapters = sorted((Path.home() / ".vscode/extensions").glob("ms-vscode.cpptools-*/debugAdapters/bin/OpenDebugAD7.exe"))
assert adapters, "Install the VS Code C/C++ extension first."
settings = json.loads((root / ".vscode/launch.json").read_text())["configurations"][0]


def expand(value):
    if isinstance(value, str):
        value = value.replace("${workspaceFolder}", str(root))
        return re.sub(r"\$\{env:([^}]+)\}", lambda match: os.environ.get(match[1], ""), value)
    if isinstance(value, list):
        return [expand(item) for item in value]
    if isinstance(value, dict):
        return {key: expand(item) for key, item in value.items()}
    return value


configuration = expand(settings)
for key in ("name", "isDefault", "preLaunchTask"):
    configuration.pop(key, None)
configuration["args"] = ["--seed", "12345"]
configuration["stopAtEntry"] = False
adapter = subprocess.Popen([str(adapters[-1])], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
messages = queue.Queue()
sequence = 0
history = []
terminals = []


def receive():
    try:
        while True:
            line = adapter.stdout.readline()
            if not line:
                return
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":", 1)[1])
                adapter.stdout.readline()
                messages.put(json.loads(adapter.stdout.read(length)))
    except Exception as error:
        messages.put({"type": "readerError", "message": str(error)})


def send(message):
    global sequence
    sequence += 1
    message["seq"] = sequence
    body = json.dumps(message).encode()
    adapter.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
    adapter.stdin.flush()
    return sequence


def request(command, arguments=None):
    return send({"type": "request", "command": command, "arguments": arguments or {}})


threading.Thread(target=receive, daemon=True).start()
kernel = c.WinDLL("kernel32", use_last_error=True)
kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
kernel.CreateFileW.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.AttachConsole.argtypes = [w.DWORD]
kernel.GetConsoleWindow.restype = w.HWND
kernel.ReadConsoleOutputCharacterW.argtypes = [w.HANDLE, w.LPWSTR, w.DWORD, w.DWORD, c.POINTER(w.DWORD)]


class Key(c.Structure):
    _fields_ = [("down", w.BOOL), ("repeat", w.WORD), ("vk", w.WORD), ("scan", w.WORD), ("char", w.WCHAR), ("control", w.DWORD)]


class Record(c.Structure):
    _fields_ = [("type", w.WORD), ("key", Key)]


kernel.WriteConsoleInputW.argtypes = [w.HANDLE, c.POINTER(Record), w.DWORD, c.POINTER(w.DWORD)]
kernel.FreeConsole()
game_pid = None
attached = False
sent_quit = False
exited = False
launch_succeeded = False
deadline = time.monotonic() + 20
request("initialize", {"clientID": "vscode", "adapterID": "cppdbg", "pathFormat": "path",
                       "linesStartAt1": True, "columnsStartAt1": True,
                       "supportsRunInTerminalRequest": True})
try:
    while time.monotonic() < deadline:
        try:
            message = messages.get(timeout=0.1)
        except queue.Empty:
            message = {}
        if message:
            history.append(message)
        if message.get("type") == "response":
            assert message.get("success"), message
            if message.get("command") == "initialize":
                request("launch", configuration)
            elif message.get("command") == "launch":
                launch_succeeded = True
        elif message.get("type") == "event":
            event = message.get("event")
            if event == "initialized":
                request("configurationDone")
            elif event == "process":
                game_pid = message["body"]["systemProcessId"]
            elif event == "exited":
                assert message["body"]["exitCode"] == 0, message
                exited = True
                break
            elif event == "stopped":
                raise AssertionError(f"Debugger stopped unexpectedly: {message}")
        elif message.get("type") == "request" and message.get("command") == "runInTerminal":
            arguments = message["arguments"]
            environment = dict(os.environ)
            environment.update({key: value for key, value in arguments.get("env", {}).items() if value is not None})
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            terminal = subprocess.Popen(arguments["args"], cwd=arguments.get("cwd"), env=environment,
                                        creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
            terminals.append(terminal)
            send({"type": "response", "request_seq": message["seq"], "command": "runInTerminal",
                  "success": True, "body": {"processId": terminal.pid}})
        if game_pid and not attached:
            attached = bool(kernel.AttachConsole(game_pid))
        if attached and not sent_quit:
            output = kernel.CreateFileW("CONOUT$", 0xC0000000, 3, None, 3, 0, None)
            try:
                buffer = c.create_unicode_buffer(12000)
                read = w.DWORD()
                kernel.ReadConsoleOutputCharacterW(output, buffer, 11999, 0, c.byref(read))
                if "MAZE EXPLORER" in buffer[:read.value]:
                    input_handle = kernel.CreateFileW("CONIN$", 0xC0000000, 3, None, 3, 0, None)
                    record = Record(1, Key(True, 1, ord("Q"), 0, "q", 0))
                    written = w.DWORD()
                    assert kernel.WriteConsoleInputW(input_handle, c.byref(record), 1, c.byref(written))
                    kernel.CloseHandle(input_handle)
                    sent_quit = True
            finally:
                kernel.CloseHandle(output)
    assert launch_succeeded and sent_quit and exited, "Debugger did not reach the game menu and exit cleanly."
    print("PASS: VS Code C/C++ debug adapter -> configured GDB -> game menu -> Q -> exit code 0.")
except Exception:
    for message in history:
        if message.get("type") == "response" or message.get("event") == "output":
            print(json.dumps(message))
    raise
finally:
    try:
        request("disconnect", {"terminateDebuggee": True})
        adapter.wait(timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        adapter.terminate()
        adapter.wait(timeout=5)
    for terminal in terminals:
        if terminal.poll() is None:
            terminal.terminate()
    kernel.FreeConsole()
