#ifndef MAZE_WINDOWS_CONSOLE_H
#define MAZE_WINDOWS_CONSOLE_H

/* The small curses interface used by main.c, backed by the Windows console. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define KEY_UP 256
#define KEY_DOWN 257
#define KEY_ENTER 258
#define KEY_RESIZE 259
#define KEY_LEFT 260
#define KEY_RIGHT 261
#define ERR (-1)
#define A_REVERSE 1
#define A_BOLD 2
#define COLOR_PAIR(number) ((number) << 8)
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

typedef struct {
    HANDLE input, output, original_output;
    DWORD original_input_mode;
    int width, height, x, y;
    WORD attributes;
    CHAR_INFO *cells;
    BOOL input_owned, output_owned, console_owned;
    WCHAR original_title[256];
} WINDOW;

static WINDOW maze_console;
#define stdscr (&maze_console)
static int console_wait_ms = -1;
static WORD console_pairs[16];

static void timeout(int milliseconds) { console_wait_ms = milliseconds; }
static int has_colors(void) { return 1; }
static void start_color(void) { }

static void init_pair(short number, short foreground, short background) {
    static const WORD colors[] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    if (number > 0 && number < 16 && foreground >= 0 && foreground < 8
        && background >= 0 && background < 8)
        console_pairs[number] = colors[foreground] | (WORD)(colors[background] << 4);
}

static void attrset(int attributes) {
    int pair = (attributes >> 8) & 15;
    WORD color = pair ? console_pairs[pair] : 7;
    if (attributes & A_BOLD) color |= FOREGROUND_INTENSITY;
    if (attributes & A_REVERSE) color = (WORD)(((color & 15) << 4) | ((color >> 4) & 15));
    stdscr->attributes = color;
}

static BOOL WINAPI console_control(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        SetConsoleMode(stdscr->input, stdscr->original_input_mode);
        SetConsoleActiveScreenBuffer(stdscr->original_output);
    }
    return FALSE;
}

static void console_size(WINDOW *win, int *height, int *width) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(win->output, &info)) {
        int w = info.srWindow.Right - info.srWindow.Left + 1;
        int h = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (w != win->width || h != win->height) {
            CHAR_INFO *cells = calloc((size_t)w * h, sizeof(*cells));
            if (cells) {
                free(win->cells);
                win->cells = cells;
                win->width = w;
                win->height = h;
            }
        }
    }
    *height = win->height;
    *width = win->width;
}

#define getmaxyx(win, height, width) console_size((win), &(height), &(width))

static WINDOW *initscr(void) {
    WINDOW *win = stdscr;
    win->input = GetStdHandle(STD_INPUT_HANDLE);
    win->original_output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(win->input, &win->original_input_mode)) {
        win->input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (win->input == INVALID_HANDLE_VALUE) {
            if (!AllocConsole()) return NULL;
            win->console_owned = TRUE;
            win->input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        }
        win->input_owned = TRUE;
        if (!GetConsoleMode(win->input, &win->original_input_mode)) {
            CloseHandle(win->input);
            if (win->console_owned) FreeConsole();
            return NULL;
        }
    }
    DWORD output_mode;
    if (!GetConsoleMode(win->original_output, &output_mode)) {
        win->original_output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        win->output_owned = TRUE;
    }
    win->output = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    if (win->output == INVALID_HANDLE_VALUE) {
        if (win->input_owned) CloseHandle(win->input);
        if (win->output_owned) CloseHandle(win->original_output);
        if (win->console_owned) FreeConsole();
        return NULL;
    }
    console_size(win, &win->height, &win->width);
    if (!win->cells || !SetConsoleActiveScreenBuffer(win->output)) {
        free(win->cells);
        win->cells = NULL;
        CloseHandle(win->output);
        if (win->input_owned) CloseHandle(win->input);
        if (win->output_owned) CloseHandle(win->original_output);
        if (win->console_owned) FreeConsole();
        return NULL;
    }
    SetConsoleMode(win->input,
        (win->original_input_mode | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS)
        & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE));
    win->attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    GetConsoleTitleW(win->original_title, 256);
    SetConsoleTitleW(L"Maze Explorer - WASD / Arrow keys");
    SetConsoleCtrlHandler(console_control, TRUE);
    return win;
}

static void endwin(void) {
    SetConsoleCtrlHandler(console_control, FALSE);
    SetConsoleMode(stdscr->input, stdscr->original_input_mode);
    SetConsoleActiveScreenBuffer(stdscr->original_output);
    CloseHandle(stdscr->output);
    free(stdscr->cells);
    stdscr->cells = NULL;
    SetConsoleTitleW(stdscr->original_title);
    if (stdscr->input_owned) CloseHandle(stdscr->input);
    if (stdscr->output_owned) CloseHandle(stdscr->original_output);
    if (stdscr->console_owned) FreeConsole();
}

/* Input modes are configured together in initscr(). */
#define cbreak() ((void)0)
#define noecho() ((void)0)
#define keypad(win, enabled) ((void)(win), (void)(enabled))

static void curs_set(int visible) {
    CONSOLE_CURSOR_INFO cursor = { 25, visible != 0 };
    SetConsoleCursorInfo(stdscr->output, &cursor);
}

static void move(int y, int x) {
    stdscr->y = y;
    stdscr->x = x;
}

static void clrtoeol(void) {
    if (stdscr->y < 0 || stdscr->y >= stdscr->height) return;
    for (int x = stdscr->x < 0 ? 0 : stdscr->x; x < stdscr->width; x++) {
        CHAR_INFO *cell = &stdscr->cells[stdscr->y * stdscr->width + x];
        cell->Char.UnicodeChar = L' ';
        cell->Attributes = stdscr->attributes;
    }
}

static void erase(void) {
    int h, w;
    console_size(stdscr, &h, &w);
    for (int y = 0; y < h; y++) {
        move(y, 0);
        clrtoeol();
    }
    move(0, 0);
}

static void mvaddstr(int y, int x, const char *text) {
    WCHAR wide[1024];
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 1024);
    move(y, x);
    if (count <= 0 || y < 0 || y >= stdscr->height) return;
    for (int i = 0; i < count - 1 && stdscr->x < stdscr->width; i++) {
        WCHAR ch = wide[i];
        int columns = (ch == 0x3000 || (ch >= 0xff01 && ch <= 0xff60)) ? 2 : 1;
        if (stdscr->x >= 0 && stdscr->x + columns <= stdscr->width) {
            CHAR_INFO *cell = &stdscr->cells[y * stdscr->width + stdscr->x];
            cell->Char.UnicodeChar = ch;
            cell->Attributes = stdscr->attributes;
            if (columns == 2) {
                cell->Attributes |= COMMON_LVB_LEADING_BYTE;
                cell[1].Char.UnicodeChar = ch;
                cell[1].Attributes = stdscr->attributes | COMMON_LVB_TRAILING_BYTE;
            }
        }
        stdscr->x += columns;
    }
}

static void mvprintw(int y, int x, const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    mvaddstr(y, x, text);
}

static void refresh(void) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(stdscr->output, &info)) return;
    COORD size = { (SHORT)stdscr->width, (SHORT)stdscr->height };
    COORD origin = { 0, 0 };
    SMALL_RECT area = info.srWindow;
    WriteConsoleOutputW(stdscr->output, stdscr->cells, size, origin, &area);
}

static int getch(void) {
    INPUT_RECORD event;
    DWORD count;
    for (;;) {
        DWORD wait_result = WaitForSingleObject(stdscr->input,
            console_wait_ms < 0 ? INFINITE : (DWORD)console_wait_ms);
        if (wait_result == WAIT_TIMEOUT) return ERR;
        if (wait_result != WAIT_OBJECT_0 || !ReadConsoleInputW(stdscr->input, &event, 1, &count)) return 27;
        if (event.EventType == WINDOW_BUFFER_SIZE_EVENT) return KEY_RESIZE;
        if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown) continue;
        KEY_EVENT_RECORD *key = &event.Event.KeyEvent;
        if (key->wVirtualKeyCode == VK_UP) return KEY_UP;
        if (key->wVirtualKeyCode == VK_DOWN) return KEY_DOWN;
        if (key->wVirtualKeyCode == VK_LEFT) return KEY_LEFT;
        if (key->wVirtualKeyCode == VK_RIGHT) return KEY_RIGHT;
        if (key->wVirtualKeyCode == VK_RETURN) return KEY_ENTER;
        if (key->wVirtualKeyCode == VK_ESCAPE) return 27;
        if (key->uChar.UnicodeChar != 0) return key->uChar.UnicodeChar;
    }
}

#endif
