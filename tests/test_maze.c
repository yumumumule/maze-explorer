#include "../maze_game.h"
#include <assert.h>
#include <stdlib.h>

static void follow_step(Game *game, Point next) {
    int dx = next.x - wrap_coordinate(game->px);
    int dy = next.y - wrap_coordinate(game->py);
    if (dx > 1) dx = -1;
    if (dx < -1) dx = 1;
    if (dy > 1) dy = -1;
    if (dy < -1) dy = 1;
    assert(abs(dx) + abs(dy) == 1);
    assert(game_move(game, dx, dy));
}

static void test_generated_maze(uint32_t seed, Difficulty difficulty) {
    Game game;
    Maze duplicate;
    int distances[MAZE_CELLS];
    Point start = { 1, 1 };
    memset(&game, 0, sizeof(game));
    game.limited_sight = true;
    game_new(&game, difficulty, seed);
    maze_generate(&duplicate, seed);
    assert(memcmp(&game.maze, &duplicate, sizeof(duplicate)) == 0);
    assert(game.started && game.limited_sight);
    assert(game.shortest > 0 && game.energy_limit > game.shortest);
    maze_distances(&game.maze, start, distances);
    assert(distances[game.exit.y * MAZE_SIZE + game.exit.x] == game.shortest);
    for (int y = 0; y < MAZE_SIZE; ++y) {
        for (int x = 0; x < MAZE_SIZE; ++x)
            if (game.maze.cells[y][x]) assert(distances[y * MAZE_SIZE + x] >= 0);
        assert(game.maze.cells[y][0] == game.maze.cells[y][MAZE_SIZE - 1]);
        assert(game.maze.cells[0][y] == game.maze.cells[MAZE_SIZE - 1][y]);
    }
    int before = game.energy;
    assert(game_hint(&game));
    assert(game.hint_active && game.energy == before - HINT_COST && game.hints_used == 1);
    Point first_hint = game.hint;
    assert(game_hint(&game) && game.energy == before - HINT_COST);
    follow_step(&game, first_hint);
    assert(!game.hint_active && game.steps == 1);
    game_retry(&game);
    assert(game.energy == game.energy_limit && game.steps == 0 && game.hints_used == 0);
    assert(game.px == 1 && game.py == 1 && game.result == PLAYING);
    while (game.result == PLAYING) {
        Point next;
        assert(game_next_step(&game, &next));
        follow_step(&game, next);
        assert(game.steps <= game.shortest);
    }
    assert(game.result == WON && game.steps == game.shortest);
    assert(game.energy == game.energy_limit - game.shortest);
    assert(!game_move(&game, 1, 0) && !game_hint(&game));
    assert(!game_next_step(&game, &first_hint));
    game_retry(&game);
    while (game.steps < game.shortest - 1) {
        Point next;
        assert(game_next_step(&game, &next));
        follow_step(&game, next);
    }
    game.energy = 1;
    follow_step(&game, game.exit);
    assert(game.result == WON && game.energy == 0);
}

static void test_edge_cases(void) {
    Game game;
    memset(&game, 0, sizeof(game));
    assert(!game_move(&game, 1, 0) && !game_hint(&game));
    game_new(&game, NORMAL, 12345);
    /* The corner is always a wall, so this is a guaranteed collision. */
    game.px = 0;
    game.py = 1;
    int energy = game.energy;
    assert(!game_move(&game, 0, -1));
    assert(game.energy == energy && game.steps == 0);
    game_retry(&game);
    for (int x = 0; x < MAZE_SIZE; ++x) {
        if (!game.maze.cells[0][x]) continue;
        game.px = x;
        game.py = 0;
        assert(game_move(&game, 0, -1));
        assert(game.py == -1 && wrap_coordinate(game.py) == MAZE_SIZE - 1);
        assert(game_move(&game, 0, 1));
        assert(game.py == 0);
        break;
    }
    for (int y = 0; y < MAZE_SIZE; ++y) {
        if (!game.maze.cells[y][0]) continue;
        game.px = 0;
        game.py = y;
        assert(game_move(&game, -1, 0));
        assert(game.px == -1 && wrap_coordinate(game.px) == MAZE_SIZE - 1);
        assert(game_move(&game, 1, 0));
        assert(game.px == 0);
        break;
    }
    game_retry(&game);
    game.energy = HINT_COST;
    assert(!game_hint(&game) && game.energy == HINT_COST && game.hints_used == 0);
    Point next;
    assert(game_next_step(&game, &next));
    game.energy = 1;
    follow_step(&game, next);
    assert(game.result == LOST && game.energy == 0);
    assert(!game_move(&game, 0, 1));
    game_retry(&game);
    assert(game.result == PLAYING && game.energy == game.energy_limit);
    assert(wrap_coordinate(-MAZE_SIZE - 1) == MAZE_SIZE - 1);
}

int main(void) {
    test_edge_cases();
    for (uint32_t seed = 0; seed < 400; ++seed)
        for (int difficulty = 0; difficulty < DIFFICULTY_COUNT; ++difficulty)
            test_generated_maze(seed, (Difficulty)difficulty);
    puts("PASS: 1200 seeded mazes, reachability, portal seams, hints, retries, wall collisions, winning and losing.");
    return 0;
}
