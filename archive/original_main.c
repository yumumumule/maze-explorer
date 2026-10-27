#define _XOPEN_SOURCE 700
#include <ncursesw/curses.h>
#include <locale.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#define BASE_W 21
#define BASE_H 21
#define HUD_LINES 3
#define LIMITED_SIZE 11
#define ENERGY_INIT 120
#define CELL_COLS 2

static const char *W_WALL   = "＃";
static const char *W_FLOOR  = "　";
static const char *W_PLAYER = "ｏ";
static const char *W_EXIT   = "ｘ";

typedef enum { STATE_TITLE = 0, STATE_PLAY = 1 } GameState;
typedef enum { END_NONE = 0, END_WIN = 1, END_LOSE = 2 } EndState;

typedef struct { int x, y; } Pt;

typedef struct {
    int w, h;
    unsigned char cell[BASE_H][BASE_W];
} Maze;

typedef struct {
    long px, py;
    int ex0, ey0;
    int energy;
    bool limited_sight;
    EndState end;
    bool running;
} Game;

static int irand(int n) { return (n <= 0) ? 0 : rand() % n; }

static long mod_long(long a, long m) {
    long r = a % m;
    if (r < 0) r += m;
    return r;
}

static bool in_bounds(int y, int x, int h, int w) {
    return (y >= 0 && y < h && x >= 0 && x < w);
}

static bool is_floor_global(const Maze *mz, long gy, long gx) {
    int x = (int)mod_long(gx, mz->w);
    int y = (int)mod_long(gy, mz->h);
    return mz->cell[y][x] == 1;
}

static long tile_index(long p, long size) {
    if (p >= 0) return p / size;
    return (p - (size - 1)) / size;
}

static void exit_global_near_player(const Maze *mz, const Game *g, long *out_ex, long *out_ey) {
    long tx = tile_index(g->px, mz->w);
    long ty = tile_index(g->py, mz->h);
    *out_ex = tx * mz->w + g->ex0;
    *out_ey = ty * mz->h + g->ey0;
}

static void maze_init_walls(Maze *mz, int h, int w) {
    mz->h = h; mz->w = w;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            mz->cell[y][x] = 0;
}

static void maze_carve_dfs(Maze *mz, int sy, int sx) {
    Pt stack[BASE_H * BASE_W];
    int top = 0;

    mz->cell[sy][sx] = 1;
    stack[top++] = (Pt){sx, sy};

    const int dirs[4][2] = { {0,-2},{0,2},{-2,0},{2,0} };

    while (top > 0) {
        Pt cur = stack[top - 1];

        int cand[4], cnt = 0;
        for (int i = 0; i < 4; i++) {
            int nx = cur.x + dirs[i][0];
            int ny = cur.y + dirs[i][1];
            if (in_bounds(ny, nx, mz->h, mz->w) && mz->cell[ny][nx] == 0) cand[cnt++] = i;
        }
        if (cnt == 0) { top--; continue; }

        int pick = cand[irand(cnt)];
        int nx = cur.x + dirs[pick][0];
        int ny = cur.y + dirs[pick][1];
        int wx = cur.x + dirs[pick][0] / 2;
        int wy = cur.y + dirs[pick][1] / 2;

        mz->cell[wy][wx] = 1;
        mz->cell[ny][nx] = 1;
        stack[top++] = (Pt){nx, ny};
    }
}

static void maze_open_edges(Maze *mz, int openings) {
    for (int i = 0; i < openings; i++) {
        int side = irand(4);
        int x = 0, y = 0;

        if (side == 0) { y = 0;          x = (irand(mz->w/2) * 2 + 1); }
        if (side == 1) { y = mz->h - 1;  x = (irand(mz->w/2) * 2 + 1); }
        if (side == 2) { x = 0;          y = (irand(mz->h/2) * 2 + 1); }
        if (side == 3) { x = mz->w - 1;  y = (irand(mz->h/2) * 2 + 1); }

        mz->cell[y][x] = 1;
        if (side == 0 && y + 1 < mz->h) mz->cell[y + 1][x] = 1;
        if (side == 1 && y - 1 >= 0)    mz->cell[y - 1][x] = 1;
        if (side == 2 && x + 1 < mz->w) mz->cell[y][x + 1] = 1;
        if (side == 3 && x - 1 >= 0)    mz->cell[y][x - 1] = 1;
    }
}

static void maze_generate(Maze *mz) {
    maze_init_walls(mz, BASE_H, BASE_W);
    maze_carve_dfs(mz, 1, 1);
    maze_open_edges(mz, 8);
}

static void maze_bfs_dist(const Maze *mz, int dist[BASE_H][BASE_W], int sy, int sx) {
    for (int y = 0; y < mz->h; y++)
        for (int x = 0; x < mz->w; x++)
            dist[y][x] = -1;

    Pt q[BASE_H * BASE_W];
    int qh = 0, qt = 0;

    dist[sy][sx] = 0;
    q[qt++] = (Pt){sx, sy};

    const int d4[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };

    while (qh < qt) {
        Pt cur = q[qh++];
        for (int i = 0; i < 4; i++) {
            int nx = cur.x + d4[i][0];
            int ny = cur.y + d4[i][1];
            if (!in_bounds(ny, nx, mz->h, mz->w)) continue;
            if (mz->cell[ny][nx] == 0) continue;
            if (dist[ny][nx] != -1) continue;
            dist[ny][nx] = dist[cur.y][cur.x] + 1;
            q[qt++] = (Pt){nx, ny};
        }
    }
}

static void pick_exit_teacher_friendly(const Maze *mz, int *out_ex, int *out_ey) {
    static int dist[BASE_H][BASE_W];
    int sx = 1, sy = 1;
    maze_bfs_dist(mz, dist, sy, sx);

    int lo = 12, hi = 18;
    int bestx = sx, besty = sy, bestd = -1;

    for (int y = 0; y < mz->h; y++) {
        for (int x = 0; x < mz->w; x++) {
            if (dist[y][x] < 0) continue;
            if (dist[y][x] >= lo && dist[y][x] <= hi) { *out_ex = x; *out_ey = y; return; }
            if (dist[y][x] > bestd) { bestd = dist[y][x]; bestx = x; besty = y; }
        }
    }
    *out_ex = bestx;
    *out_ey = besty;
}

static void draw_centered_text(int row, const char *s) {
    int h, w;
    getmaxyx(stdscr, h, w);
    int x = (w - (int)strlen(s)) / 2;
    if (x < 0) x = 0;
    mvaddstr(row, x, s);
}

static void draw_title(const Game *g, int selected) {
    erase();
    int h, w;
    getmaxyx(stdscr, h, w);

    draw_centered_text(2, "MAZE GAME (ncurses)");
    draw_centered_text(4, "W/S select, Enter confirm, ESC exit");

    char item2[64];
    snprintf(item2, sizeof(item2), "Limited sight (11x11): %s", g->limited_sight ? "ON" : "OFF");
    const char *items[3] = { "Start game", item2, "Exit" };

    int base_row = 8;
    for (int i = 0; i < 3; i++) {
        if (i == selected) attron(A_REVERSE);
        char line[128];
        snprintf(line, sizeof(line), "  %s  ", items[i]);
        draw_centered_text(base_row + i * 2, line);
        if (i == selected) attroff(A_REVERSE);
    }

    draw_centered_text(h - 2, "Full-width chars demo: ＃　ｏｘ");
    refresh();
}

static void draw_hud(const Game *g) {
    mvprintw(0, 0, "WSAD move | ESC title | Energy: %d", g->energy);
    clrtoeol();
    mvprintw(1, 0, "Mode: %s | Advanced: 1,2,3,4,6,7", g->limited_sight ? "LIMITED SIGHT" : "SCROLL");
    clrtoeol();
    if (g->end == END_WIN) mvprintw(2, 0, "You WIN! Press ESC to return to title.");
    else if (g->end == END_LOSE) mvprintw(2, 0, "You LOSE (energy=0)! Press ESC to return to title.");
    else mvprintw(2, 0, "Goal: reach exit '%s'.", W_EXIT);
    clrtoeol();
}

static void draw_game(const Maze *mz, const Game *g) {
    erase();

    int term_h, term_w;
    getmaxyx(stdscr, term_h, term_w);

    draw_hud(g);

    int start_y = HUD_LINES;
    int max_cells_w = term_w / CELL_COLS;
    int max_cells_h = term_h - start_y;

    int view_h = g->limited_sight ? LIMITED_SIZE : max_cells_h;
    int view_w = g->limited_sight ? LIMITED_SIZE : max_cells_w;

    if (view_h < 5) view_h = 5;
    if (view_w < 10) view_w = 10;

    long top  = g->py - view_h / 2;
    long left = g->px - view_w / 2;

    long exg, eyg;
    exit_global_near_player(mz, g, &exg, &eyg);

    for (int ry = 0; ry < view_h && (start_y + ry) < term_h; ry++) {
        int sy = start_y + ry;
        move(sy, 0);
        clrtoeol();

        for (int rx = 0; rx < view_w; rx++) {
            int sx = rx * CELL_COLS;
            if (sx >= term_w) break;

            long gy = top + ry;
            long gx = left + rx;

            const char *out = is_floor_global(mz, gy, gx) ? W_FLOOR : W_WALL;
            if (gy == eyg && gx == exg) out = W_EXIT;
            if (gy == g->py && gx == g->px) out = W_PLAYER;

            mvaddstr(sy, sx, out);
        }
    }

    refresh();
}

static void game_reset(Maze *mz, Game *g) {
    maze_generate(mz);

    g->px = 1;
    g->py = 1;
    if (!is_floor_global(mz, g->py, g->px)) {
        for (int y = 0; y < mz->h; y++) {
            for (int x = 0; x < mz->w; x++) {
                if (mz->cell[y][x] == 1) { g->py = y; g->px = x; goto done; }
            }
        }
    }
done:
    pick_exit_teacher_friendly(mz, &g->ex0, &g->ey0);
    g->energy = ENERGY_INIT;
    g->end = END_NONE;
}

static void try_move_player(const Maze *mz, Game *g, int dy, int dx) {
    if (g->end != END_NONE) return;

    long npx = g->px + dx;
    long npy = g->py + dy;

    if (!is_floor_global(mz, npy, npx)) return;

    if (g->energy <= 0) { g->end = END_LOSE; return; }

    g->px = npx;
    g->py = npy;
    g->energy--;

    long exg, eyg;
    exit_global_near_player(mz, g, &exg, &eyg);

    if (g->py == eyg && g->px == exg) g->end = END_WIN;
    else if (g->energy <= 0) g->end = END_LOSE;
}

int main(void) {
    setlocale(LC_ALL, "");
    srand((unsigned)time(NULL));

    WINDOW *win = initscr();
    if (!win) {
        printf("initscr() failed\n");
        return 1;
    }

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    Maze mz;
    Game g;
    memset(&g, 0, sizeof(g));
    g.running = true;
    g.limited_sight = true;

    GameState state = STATE_TITLE;
    int menu_sel = 0;

    draw_title(&g, menu_sel);

    while (g.running) {
        int ch = getch();

        if (state == STATE_TITLE) {
            if (ch == 27) {
                g.running = false;
                break;
            } else if (ch == 'w' || ch == 'W' || ch == KEY_UP) {
                menu_sel = (menu_sel + 2) % 3;
                draw_title(&g, menu_sel);
            } else if (ch == 's' || ch == 'S' || ch == KEY_DOWN) {
                menu_sel = (menu_sel + 1) % 3;
                draw_title(&g, menu_sel);
            } else if (ch == '\n' || ch == KEY_ENTER || ch == 10 || ch == 13) {
                if (menu_sel == 0) {
                    game_reset(&mz, &g);
                    state = STATE_PLAY;
                    draw_game(&mz, &g);
                } else if (menu_sel == 1) {
                    g.limited_sight = !g.limited_sight;
                    draw_title(&g, menu_sel);
                } else {
                    g.running = false;
                    break;
                }
            }
        } else {
            if (ch == 27) {
                state = STATE_TITLE;
                menu_sel = 0;
                draw_title(&g, menu_sel);
                continue;
            }

            if (g.end == END_NONE) {
                if (ch == 'w' || ch == 'W') try_move_player(&mz, &g, -1, 0);
                else if (ch == 's' || ch == 'S') try_move_player(&mz, &g, 1, 0);
                else if (ch == 'a' || ch == 'A') try_move_player(&mz, &g, 0, -1);
                else if (ch == 'd' || ch == 'D') try_move_player(&mz, &g, 0, 1);
            }
            draw_game(&mz, &g);
        }
    }

    endwin();
    printf("\n");
    return 0;
}