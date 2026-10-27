# Maze Explorer

A C11 terminal maze game, developed from the CUHK ESTR1002 course project.
Explore procedurally generated mazes, follow optional shortest-path hints,
and reach the exit before running out of energy.

Windows uses the native console API with no additional runtime libraries.
Linux and macOS use curses. The maintained C sources contain ASCII only;
display symbols are encoded explicitly so source code pages do not alter them.

## Features

- Three difficulty levels with different exit distances and energy allowances.
- Limited sight and scrolling views, visited-cell trails, and shortest-path hints.
- Matching edge openings that connect repeated copies of the maze.
- Reproducible mazes using `--seed`, and retries of the same maze.
- A pause menu, elapsed time, and session win/best-step records.

## Build and play

Requires a C11 compiler and CMake 3.16 or newer. On Linux and macOS, install
the curses development library before configuring. On Windows, use MinGW-w64
or a Visual Studio C toolchain.

```sh
git clone https://github.com/yumumumule/maze-explorer.git
cd maze-explorer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For MinGW-w64 on Windows, put its `bin` directory on `PATH` and select a matching
CMake generator, such as `-G "MinGW Makefiles"`, or `-G Ninja` if Ninja is installed.
Visual Studio builds can use `cmake --build build --config Release`.

Run the game in a terminal at least 70 columns wide and 23 rows high:

```sh
# Linux/macOS
./build/maze_explorer --seed 12345
```

```powershell
# Windows, with a single-configuration generator such as Ninja or MinGW Makefiles
.\build\maze_explorer.exe --seed 12345

# Windows, with a Visual Studio generator
.\build\Release\maze_explorer.exe --seed 12345
```

Omit `--seed` for a new seed, or use `--help` for command-line usage.
Play in the terminal or game console window; an editor's Output panel is not
an interactive game screen.

You can also compile directly on Windows with GCC:

```powershell
gcc -std=c11 -Wall -Wextra main.c -o maze_explorer.exe
.\maze_explorer.exe
```

For VS Code, open this directory and configure CMake Tools or your own build/debug
tasks for your compiler. Machine-specific `.vscode` settings are not committed.

## Controls

| Key | Action |
| --- | --- |
| WASD / Arrow keys | Move; walls do not consume energy |
| H | Show the next shortest-path step, costing 3 energy |
| V | Switch limited sight / scrolling view |
| R | Retry the same maze with full energy |
| N | Generate a new maze |
| ESC | Pause and return to the menu |
| Q | Quit |

Reach **X** before energy reaches zero. **@** marks your position, **.** marks
visited cells, and **?** marks a hint. Matching edge openings connect repeated
copies of the maze. All generated exits are reachable, and each initial energy
allowance exceeds the shortest solution. The timer pauses in the menu.
Wins and best steps are recorded for the current session.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

With Visual Studio, add `-C Release` or `-C Debug` to match the built configuration.
The logic tests cover 1,200 seeded mazes, portal connectivity, shortest paths,
energy, hints, retries, and win/loss edge cases. These tests require no Python
or interactive console.

An additional Windows console integration test uses only the Python standard
library and creates a hidden console:

```powershell
py tests/test_console.py build/maze_explorer.exe
```

Adjust the executable path for a Visual Studio build. `tests/test_debugger.py`
is an optional local VS Code/GDB smoke test: it requires the C/C++ extension and
your own `.vscode/launch.json` pointing to the built executable and debugger.
It launches a game console during the check.

## Source layout

| File | Purpose |
| --- | --- |
| [main.c](main.c) | Menu, rendering, controls and application state |
| [maze_game.h](maze_game.h) | Generation, pathfinding, movement, energy and hints |
| [windows_console.h](windows_console.h) | Windows console implementation of the small curses interface used by the game |
| [tests/](tests/) | Logic tests and optional Windows integration/debugger checks |
| [archive/](archive/) | Original submission source, preserved for reference and excluded from the build |

The game and logic tests have been built and tested on Windows with MinGW-w64
GCC 13.1.0. Linux, macOS and MSVC execution have not been verified.
