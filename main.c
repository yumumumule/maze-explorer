#define _XOPEN_SOURCE 700
#ifdef _WIN32
#include "windows_console.h"
#else
#include <curses.h>
#endif
#include "maze_game.h"
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <time.h>

#define VIEW_LIMIT 11
#define MIN_COLS 70
#define MIN_ROWS 23
#define MAP_TOP 8
#define CELL_COLS 2

enum { STYLE_TEXT = 1, STYLE_TITLE, STYLE_WALL, STYLE_PLAYER, STYLE_EXIT,
       STYLE_TRAIL, STYLE_HINT, STYLE_DANGER, STYLE_SELECTED };
typedef enum { MENU, GAME, HELP } Screen;
typedef struct {
    Game game;
    Screen screen;
    Difficulty difficulty;
    uint32_t next_seed;
    int selected, elapsed, wins, best_steps;
    time_t resumed_at;
    bool running, result_recorded;
} App;

static const char *difficulty_name(Difficulty difficulty) {
    static const char *names[] = { "EASY", "NORMAL", "HARD" };
    return names[difficulty];
}

static void style(int pair, bool bold) {
    attrset(COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
}

static void centered(int row, const char *text, int width) {
    int column = (width - (int)strlen(text)) / 2;
    if (column < 0) column = 0;
    mvaddstr(row, column, text);
}

static void setup_colors(void) {
    if (!has_colors()) return;
    start_color();
    init_pair(STYLE_TEXT, COLOR_WHITE, COLOR_BLACK);
    init_pair(STYLE_TITLE, COLOR_CYAN, COLOR_BLACK);
    init_pair(STYLE_WALL, COLOR_CYAN, COLOR_BLACK);
    init_pair(STYLE_PLAYER, COLOR_BLACK, COLOR_CYAN);
    init_pair(STYLE_EXIT, COLOR_BLACK, COLOR_GREEN);
    init_pair(STYLE_TRAIL, COLOR_BLUE, COLOR_BLACK);
    init_pair(STYLE_HINT, COLOR_YELLOW, COLOR_BLACK);
    init_pair(STYLE_DANGER, COLOR_RED, COLOR_BLACK);
    init_pair(STYLE_SELECTED, COLOR_BLACK, COLOR_WHITE);
}

static int elapsed_now(const App *app) {
    int seconds = app->elapsed;
    if (app->screen == GAME && !app->result_recorded)
        seconds += (int)difftime(time(NULL), app->resumed_at);
    return seconds < 0 ? 0 : seconds;
}

static void pause_clock(App *app) {
    app->elapsed = elapsed_now(app);
    app->resumed_at = time(NULL);
}

static void start_new(App *app) {
    game_new(&app->game, app->difficulty, app->next_seed);
    app->next_seed += UINT32_C(0x9e3779b9);
    app->elapsed = 0;
    app->resumed_at = time(NULL);
    app->result_recorded = false;
    app->screen = GAME;
}

static void draw_menu(const App *app, int height, int width) {
    char difficulty[80], sight[80], stats[100];
    const Game *game = &app->game;
    bool can_resume = game->started && game->result == PLAYING;
    snprintf(difficulty, sizeof(difficulty), "Difficulty: %s (new maze)", difficulty_name(app->difficulty));
    snprintf(sight, sizeof(sight), "Limited sight (11 x 11): %s", game->limited_sight ? "ON" : "OFF");
    const char *items[] = { can_resume ? "Resume maze" : "Start a new maze",
                            difficulty, sight, "How to play", "Quit" };
    style(STYLE_TITLE, true);
    centered(2, "+----------------------------------+", width);
    centered(3, "|          MAZE EXPLORER           |", width);
    centered(4, "+----------------------------------+", width);
    style(STYLE_TEXT, false);
    centered(6, "One maze. Endless copies. Find your way out.", width);
    for (int i = 0; i < 5; ++i) {
        char line[100];
        bool selected = i == app->selected;
        style(selected ? STYLE_SELECTED : STYLE_TEXT, false);
        snprintf(line, sizeof(line), " %c %-38s ", selected ? '>' : ' ', items[i]);
        centered(8 + i * 2, line, width);
    }
    style(STYLE_TEXT, false);
    centered(height - 4, "W/S or Up/Down: choose    Enter: confirm    Q/ESC: quit", width);
    snprintf(stats, sizeof(stats), "Wins this session: %d  |  Best winning run: %s", app->wins,
             app->best_steps ? "recorded below" : "--");
    if (app->best_steps)
        snprintf(stats, sizeof(stats), "Wins this session: %d  |  Best winning run: %d steps", app->wins, app->best_steps);
    style(STYLE_TITLE, false);
    centered(height - 2, stats, width);
}

static void draw_help(int height, int width) {
    static const char *lines[] = {
        "Reach the green X before your energy runs out.",
        "WASD or Arrow keys move. Walls never cost energy.",
        "Each successful move costs 1 energy.",
        "H marks one shortest-path step with ? (cost: 3 energy).",
        "V switches between limited sight and a larger scrolling view.",
        "R retries the same maze. N generates a new maze.",
        "ESC pauses and returns to the menu. Q quits.",
        "Edges wrap: matching openings lead to another maze copy.",
        "Dots show places you have already visited.",
        "Difficulty changes the exit distance and energy allowance.",
        "The timer pauses in the menu and stops when a run ends."
    };
    style(STYLE_TITLE, true);
    centered(2, "HOW TO PLAY", width);
    style(STYLE_TEXT, false);
    for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); ++i)
        centered(5 + i, lines[i], width);
    style(STYLE_HINT, false);
    centered(height - 3, "Press Enter or ESC to return to the menu.", width);
}

static void draw_game(const App *app, int height, int width) {
    const Game *game = &app->game;
    int seconds = elapsed_now(app);
    char bar[21];
    int filled = 20 * game->energy / game->energy_limit;
    for (int i = 0; i < 20; ++i) bar[i] = i < filled ? '=' : ' ';
    bar[20] = '\0';
    style(STYLE_TITLE, true);
    mvaddstr(0, 2, "MAZE EXPLORER");
    style(game->energy * 5 < game->energy_limit ? STYLE_DANGER : STYLE_HINT, true);
    mvprintw(1, 2, "Energy [%s] %d/%d", bar, game->energy, game->energy_limit);
    style(STYLE_TEXT, false);
    mvprintw(2, 2, "Steps: %-5d  Time: %02d:%02d  Hints: %d", game->steps,
             seconds / 60, seconds % 60, game->hints_used);
    mvprintw(3, 2, "%s | %s | Seed: %lu", difficulty_name(game->difficulty),
             game->limited_sight ? "LIMITED SIGHT" : "SCROLL VIEW", (unsigned long)game->seed);
    mvaddstr(4, 2, "WASD/Arrows: move | H: hint (-3) | V: view");
    mvaddstr(5, 2, "R: retry | N: new maze | ESC: menu | Q: quit");
    style(STYLE_PLAYER, true); mvaddstr(6, 2, "@ ");
    style(STYLE_TEXT, false); mvaddstr(6, 4, "you   ");
    style(STYLE_EXIT, true); mvaddstr(6, 10, "X ");
    style(STYLE_TEXT, false); mvaddstr(6, 12, "exit   . trail   ? hint");

    int view_width = game->limited_sight ? VIEW_LIMIT : MAZE_SIZE;
    int view_height = view_width;
    int available_width = (width - 6) / CELL_COLS;
    int available_height = height - MAP_TOP - 4;
    if (view_width > available_width) view_width = available_width;
    if (view_height > available_height) view_height = available_height;
    int origin_x = (width - view_width * CELL_COLS) / 2;
    int origin_y = MAP_TOP + (available_height - view_height) / 2;
    long left = game->px - view_width / 2;
    long top = game->py - view_height / 2;
    style(STYLE_TRAIL, false);
    for (int x = 0; x < view_width * CELL_COLS + 2; ++x) {
        mvaddstr(origin_y - 1, origin_x - 1 + x, "-");
        mvaddstr(origin_y + view_height, origin_x - 1 + x, "-");
    }
    for (int row = 0; row < view_height; ++row) {
        style(STYLE_TRAIL, false);
        mvaddstr(origin_y + row, origin_x - 1, "|");
        mvaddstr(origin_y + row, origin_x + view_width * CELL_COLS, "|");
        for (int column = 0; column < view_width; ++column) {
            long gx = left + column, gy = top + row;
            int x = wrap_coordinate(gx), y = wrap_coordinate(gy);
            const char *cell = "  ";
            int color = STYLE_TEXT;
            bool bold = false;
            if (!maze_floor(&game->maze, gx, gy)) {
                cell = "##"; color = STYLE_WALL;
            } else if (game->visited[y][x]) {
                cell = ". "; color = STYLE_TRAIL; bold = true;
            }
            if (game->hint_active && x == game->hint.x && y == game->hint.y) {
                cell = "? "; color = STYLE_HINT; bold = true;
            }
            if (x == game->exit.x && y == game->exit.y) {
                cell = "X "; color = STYLE_EXIT; bold = true;
            }
            if (gx == game->px && gy == game->py) {
                cell = "@ "; color = STYLE_PLAYER; bold = true;
            }
            style(color, bold);
            mvaddstr(origin_y + row, origin_x + column * CELL_COLS, cell);
        }
    }
    if (game->result == WON) {
        style(STYLE_EXIT, true);
        mvprintw(height - 2, 2, " ESCAPED! %d steps | Shortest: %d | Efficiency: %d%% | R retry / N new ",
                 game->steps, game->shortest, game->steps ? 100 * game->shortest / game->steps : 100);
    } else if (game->result == LOST) {
        style(STYLE_DANGER, true);
        mvaddstr(height - 2, 2, "OUT OF ENERGY. R retries this maze; N starts a new one.");
    } else {
        style(STYLE_TEXT, false);
        mvprintw(height - 2, 2, "%.*s", width - 4, game->message);
    }
}

static void draw(const App *app) {
    int height, width;
    getmaxyx(stdscr, height, width);
    style(STYLE_TEXT, false);
    erase();
    if (height < MIN_ROWS || width < MIN_COLS) {
        mvaddstr(0, 0, "MAZE EXPLORER - terminal too small");
        mvprintw(1, 0, "Resize to at least %d columns x %d rows.", MIN_COLS, MIN_ROWS);
        mvaddstr(2, 0, "Q quits. Your current maze is preserved.");
    } else if (app->screen == MENU) draw_menu(app, height, width);
    else if (app->screen == HELP) draw_help(height, width);
    else draw_game(app, height, width);
    refresh();
}

static void menu_input(App *app, int key) {
    if (key == 'q' || key == 27) app->running = false;
    else if (key == 'w' || key == KEY_UP) app->selected = (app->selected + 4) % 5;
    else if (key == 's' || key == KEY_DOWN) app->selected = (app->selected + 1) % 5;
    else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
        switch (app->selected) {
        case 0:
            if (!app->game.started || app->game.result != PLAYING) start_new(app);
            else { app->screen = GAME; app->resumed_at = time(NULL); }
            break;
        case 1:
            app->difficulty = (Difficulty)((app->difficulty + 1) % DIFFICULTY_COUNT);
            app->game.started = false;
            break;
        case 2: app->game.limited_sight = !app->game.limited_sight; break;
        case 3: app->screen = HELP; break;
        case 4: app->running = false; break;
        }
    }
}

static void play_input(App *app, int key) {
    Game *game = &app->game;
    switch (key) {
    case 27:
        pause_clock(app);
        app->screen = MENU;
        app->selected = 0;
        return;
    case 'q': app->running = false; return;
    case 'r':
        game_retry(game);
        app->elapsed = 0;
        app->resumed_at = time(NULL);
        app->result_recorded = false;
        return;
    case 'n': start_new(app); return;
    case 'v': game->limited_sight = !game->limited_sight; return;
    case 'h': game_hint(game); break;
    case 'w': case KEY_UP: game_move(game, 0, -1); break;
    case 's': case KEY_DOWN: game_move(game, 0, 1); break;
    case 'a': case KEY_LEFT: game_move(game, -1, 0); break;
    case 'd': case KEY_RIGHT: game_move(game, 1, 0); break;
    default: break;
    }
    if (game->result != PLAYING && !app->result_recorded) {
        pause_clock(app);
        app->result_recorded = true;
        if (game->result == WON) {
            ++app->wins;
            if (!app->best_steps || game->steps < app->best_steps) app->best_steps = game->steps;
        }
    }
}

int main(int argc, char **argv) {
    uint32_t seed = (uint32_t)time(NULL) ^ (uint32_t)clock();
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("Maze Explorer\nUsage: %s [--seed NUMBER]\nWASD/Arrows move, H hint, V view, R retry, N new maze, ESC menu, Q quit.\n", argv[0]);
        return 0;
    }
    if (argc != 1) {
        char *end = NULL;
        unsigned long value;
        if (argc != 3 || strcmp(argv[1], "--seed") != 0 || argv[2][0] == '-') {
            fprintf(stderr, "Usage: %s [--seed NUMBER]\n", argv[0]);
            return 1;
        }
        errno = 0;
        value = strtoul(argv[2], &end, 10);
        if (errno || end == argv[2] || *end || value > UINT32_MAX) {
            fprintf(stderr, "Seed must be an integer between 0 and 4294967295.\n");
            return 1;
        }
        seed = (uint32_t)value;
    }
    setlocale(LC_ALL, "");
    if (!initscr()) {
        fprintf(stderr, "Could not initialize the terminal. Run in an interactive console.\n");
        return 1;
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(200);
    setup_colors();
    App app;
    memset(&app, 0, sizeof(app));
    app.running = true;
    app.difficulty = NORMAL;
    app.game.limited_sight = true;
    app.next_seed = seed;
    while (app.running) {
        draw(&app);
        int key = getch();
        if (key == ERR || key == KEY_RESIZE) continue;
        if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
        if (key == 'q') app.running = false;
        else if (app.screen == MENU) menu_input(&app, key);
        else if (app.screen == GAME) play_input(&app, key);
        else if (key == 27 || key == '\n' || key == '\r' || key == KEY_ENTER) app.screen = MENU;
    }
    endwin();
    return 0;
}
