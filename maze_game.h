#ifndef MAZE_GAME_H
#define MAZE_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAZE_SIZE 21
#define MAZE_CELLS (MAZE_SIZE * MAZE_SIZE)
#define HINT_COST 3

typedef struct { int x, y; } Point;
typedef struct { unsigned char cells[MAZE_SIZE][MAZE_SIZE]; } Maze;
typedef enum { EASY, NORMAL, HARD, DIFFICULTY_COUNT } Difficulty;
typedef enum { PLAYING, WON, LOST } Result;

typedef struct {
    Maze maze;
    long px, py;
    Point exit, hint;
    Difficulty difficulty;
    Result result;
    uint32_t seed;
    int energy, energy_limit, steps, hints_used, shortest;
    bool limited_sight, started, hint_active;
    bool visited[MAZE_SIZE][MAZE_SIZE];
    char message[96];
} Game;

static int wrap_coordinate(long value) {
    int result = (int)(value % MAZE_SIZE);
    return result < 0 ? result + MAZE_SIZE : result;
}

static bool maze_floor(const Maze *maze, long x, long y) {
    return maze->cells[wrap_coordinate(y)][wrap_coordinate(x)] != 0;
}

static uint32_t random_next(uint32_t *state) {
    uint32_t x = *state ? *state : UINT32_C(0x6d2b79f5);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void maze_generate(Maze *maze, uint32_t seed) {
    static const int dx[4] = { 0, 0, -2, 2 };
    static const int dy[4] = { -2, 2, 0, 0 };
    Point stack[MAZE_CELLS];
    uint32_t rng = seed;
    int top = 1;
    memset(maze, 0, sizeof(*maze));
    stack[0].x = 1;
    stack[0].y = 1;
    maze->cells[1][1] = 1;
    while (top > 0) {
        Point current = stack[top - 1];
        int choices[4], count = 0;
        for (int i = 0; i < 4; ++i) {
            int x = current.x + dx[i], y = current.y + dy[i];
            if (x > 0 && x < MAZE_SIZE - 1 && y > 0 && y < MAZE_SIZE - 1
                && !maze->cells[y][x]) choices[count++] = i;
        }
        if (!count) { --top; continue; }
        int direction = choices[random_next(&rng) % (uint32_t)count];
        Point next = { current.x + dx[direction], current.y + dy[direction] };
        maze->cells[current.y + dy[direction] / 2][current.x + dx[direction] / 2] = 1;
        maze->cells[next.y][next.x] = 1;
        stack[top++] = next;
    }
    /* Matching portals make every seam traversable in both directions. */
    for (int i = 0; i < 2; ++i) {
        int x = 1 + 2 * (int)(random_next(&rng) % (MAZE_SIZE / 2));
        int y = 1 + 2 * (int)(random_next(&rng) % (MAZE_SIZE / 2));
        maze->cells[0][x] = maze->cells[MAZE_SIZE - 1][x] = 1;
        maze->cells[y][0] = maze->cells[y][MAZE_SIZE - 1] = 1;
    }
}

/* BFS uses the same wrapping rules as movement and hints. */
static void maze_distances(const Maze *maze, Point origin, int distances[MAZE_CELLS]) {
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    int queue[MAZE_CELLS], head = 0, tail = 0;
    int start = origin.y * MAZE_SIZE + origin.x;
    for (int i = 0; i < MAZE_CELLS; ++i) distances[i] = -1;
    if (!maze_floor(maze, origin.x, origin.y)) return;
    distances[start] = 0;
    queue[tail++] = start;
    while (head < tail) {
        int current = queue[head++];
        for (int direction = 0; direction < 4; ++direction) {
            int x = wrap_coordinate(current % MAZE_SIZE + dx[direction]);
            int y = wrap_coordinate(current / MAZE_SIZE + dy[direction]);
            int next = y * MAZE_SIZE + x;
            if (!maze->cells[y][x] || distances[next] >= 0) continue;
            distances[next] = distances[current] + 1;
            queue[tail++] = next;
        }
    }
}

static Point choose_exit(const Maze *maze, Difficulty difficulty, uint32_t seed, int *shortest) {
    static const int low[DIFFICULTY_COUNT] = { 14, 24, 38 };
    static const int high[DIFFICULTY_COUNT] = { 22, 36, 55 };
    int distances[MAZE_CELLS], selected = 0, farthest = 0, count = 0;
    Point start = { 1, 1 };
    uint32_t rng = seed;
    maze_distances(maze, start, distances);
    for (int i = 0; i < MAZE_CELLS; ++i) {
        if (distances[i] > distances[farthest]) farthest = i;
        if (distances[i] >= low[difficulty] && distances[i] <= high[difficulty]) {
            ++count;
            if (random_next(&rng) % (uint32_t)count == 0) selected = i;
        }
    }
    if (!count) selected = farthest;
    *shortest = distances[selected];
    Point exit = { selected % MAZE_SIZE, selected / MAZE_SIZE };
    return exit;
}

static void game_retry(Game *game) {
    game->px = game->py = 1;
    game->steps = game->hints_used = 0;
    game->hint_active = false;
    game->result = PLAYING;
    game->energy = game->energy_limit;
    memset(game->visited, 0, sizeof(game->visited));
    game->visited[1][1] = true;
    snprintf(game->message, sizeof(game->message), "Find X. Matching openings lead into the next copy of the maze.");
}

static void game_new(Game *game, Difficulty difficulty, uint32_t seed) {
    static const int minimum[DIFFICULTY_COUNT] = { 140, 120, 90 };
    static const int extra[DIFFICULTY_COUNT] = { 100, 65, 30 };
    game->difficulty = difficulty;
    game->seed = seed;
    maze_generate(&game->maze, seed);
    game->exit = choose_exit(&game->maze, difficulty, seed, &game->shortest);
    game->energy_limit = game->shortest + extra[difficulty];
    if (game->energy_limit < minimum[difficulty]) game->energy_limit = minimum[difficulty];
    game->started = true;
    game_retry(game);
}

static bool game_move(Game *game, int dx, int dy) {
    if (!game->started || game->result != PLAYING) return false;
    if (!maze_floor(&game->maze, game->px + dx, game->py + dy)) {
        snprintf(game->message, sizeof(game->message), "A wall blocks the way. No energy spent.");
        return false;
    }
    game->px += dx;
    game->py += dy;
    ++game->steps;
    --game->energy;
    game->hint_active = false;
    int x = wrap_coordinate(game->px), y = wrap_coordinate(game->py);
    game->visited[y][x] = true;
    if (x == game->exit.x && y == game->exit.y) game->result = WON;
    else if (game->energy <= 0) game->result = LOST;
    game->message[0] = '\0';
    return true;
}

static bool game_next_step(const Game *game, Point *next) {
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    int distances[MAZE_CELLS];
    int x = wrap_coordinate(game->px), y = wrap_coordinate(game->py);
    maze_distances(&game->maze, game->exit, distances);
    int current = distances[y * MAZE_SIZE + x];
    if (current <= 0) return false;
    for (int i = 0; i < 4; ++i) {
        int nx = wrap_coordinate(x + dx[i]), ny = wrap_coordinate(y + dy[i]);
        if (distances[ny * MAZE_SIZE + nx] == current - 1) {
            next->x = nx;
            next->y = ny;
            return true;
        }
    }
    return false;
}

static bool game_hint(Game *game) {
    if (!game->started || game->result != PLAYING) return false;
    if (game->hint_active) {
        snprintf(game->message, sizeof(game->message), "Follow the yellow ? to take one step toward X.");
        return true;
    }
    if (game->energy <= HINT_COST) {
        snprintf(game->message, sizeof(game->message), "Not enough energy for a hint. R retries this maze.");
        return false;
    }
    if (!game_next_step(game, &game->hint)) return false;
    game->energy -= HINT_COST;
    ++game->hints_used;
    game->hint_active = true;
    snprintf(game->message, sizeof(game->message), "Follow the yellow ? (-%d energy).", HINT_COST);
    return true;
}

#endif
