#include "saolei_logic.h"
#include "esp_random.h"

/*
 * 清空棋盘和本局统计信息。
 * 整个 11×11 数组都会清空，外围保护格也保持无雷。
 */
static void clear_board(saolei_game_t *game)
{
    for (uint8_t row = 0; row < GRID_CNT; row++) {
        for (uint8_t col = 0; col < GRID_CNT; col++) {
            game->cells[row][col].has_bomb = false;
            game->cells[row][col].nearby_bombs = 0;
            game->cells[row][col].status = SAOLEI_CELL_UNOPENED;
        }
    }

    game->flagged_count = 0;
    game->opened_count = 0;
    game->status = SAOLEI_GAME_PLAYING;
}

/*
 * 只在内部 9×9 区域布雷。
 * GRID_CNT 为 11 时，实际布雷坐标范围是 1～9；
 * 第 0 行、第 10 行、第 0 列、第 10 列留作保护边界。
 */
static void place_bombs(saolei_game_t *game)
{
    uint8_t bombs_placed = 0;

    while (bombs_placed < BOMB_COUNT) {
        uint8_t row = (uint8_t)(1 + esp_random() % (GRID_CNT - 2));
        uint8_t col = (uint8_t)(1 + esp_random() % (GRID_CNT - 2));

        /* 如果随机位置已经有雷，就重新抽取。 */
        if (!game->cells[row][col].has_bomb) {
            game->cells[row][col].has_bomb = true;
            bombs_placed++;
        }
    }
}

/*
 * 计算内部可玩格周围 8 个格子中的雷数。
 * 外围保护格让 row±1、col±1 的访问保持在数组范围内。
 */
static void calculate_nearby_bombs(saolei_game_t *game)
{
    for (uint8_t row = 1; row < GRID_CNT - 1; row++) {
        for (uint8_t col = 1; col < GRID_CNT - 1; col++) {
            uint8_t count = 0;

            for (int8_t dr = -1; dr <= 1; dr++) {
                for (int8_t dc = -1; dc <= 1; dc++) {
                    /* 跳过当前格，只检查周围 8 个格子。 */
                    if (dr == 0 && dc == 0) {
                        continue;
                    }

                    if (game->cells[row + dr][col + dc].has_bomb) {
                        count++;
                    }
                }
            }

            game->cells[row][col].nearby_bombs = count;
        }
    }
}

/*
 * 初始化或重新开始游戏：
 * 清空棋盘、布雷、计算周围雷数，并将游戏状态设为进行中。
 */
void saolei_init(saolei_game_t *game)
{
    if (game == NULL) {
        return;
    }

    clear_board(game);
    place_bombs(game);
    calculate_nearby_bombs(game);
}

//自动补上邻居
/* 展开起始空白格周围的安全格。 */
static void open_empty_area(saolei_game_t *game, uint8_t start_row, uint8_t start_col)
{
    /* 棋盘最多有 9×9 个可玩格，因此队列最多需要 81 个位置。 */
    uint8_t queue[9 * 9];
    uint8_t head = 0;
    uint8_t tail = 0;

    /* 把已经翻开的起始格放进队列，开始检查它的邻居。 */
    queue[tail++] = (uint8_t)(start_row * GRID_CNT + start_col);

    while (head < tail) {
        /* 取出一个格子，再从一维位置还原行、列。 */
        uint8_t position = queue[head++];
        uint8_t row = position / GRID_CNT;
        uint8_t col = position % GRID_CNT;

        /* 检查周围 8 个位置。 */
        for (int8_t dr = -1; dr <= 1; dr++) {
            for (int8_t dc = -1; dc <= 1; dc++) {
                if (dr == 0 && dc == 0) {
                    continue;  /* 跳过格子自身 */
                }

                uint8_t next_row = (uint8_t)(row + dr);
                uint8_t next_col = (uint8_t)(col + dc);

                /* 只处理内部 9×9，外围一圈不显示也不展开。 */
                if (next_row == 0 || next_row >= GRID_CNT - 1 ||
                    next_col == 0 || next_col >= GRID_CNT - 1) {
                    continue;
                }

                saolei_cell_t *cell = &game->cells[next_row][next_col];

                /* 插旗、已翻开和有雷的格子都不自动翻开。 */
                if (cell->status != SAOLEI_CELL_UNOPENED || cell->has_bomb) {
                    continue;
                }

                /* 先标记已翻开，避免同一格被重复加入队列。 */
                cell->status = SAOLEI_CELL_OPENED;
                game->opened_count++;

                /* 只有周围没有雷的格子才继续向外展开。 */
                if (cell->nearby_bombs == 0) {
                    queue[tail++] =
                        (uint8_t)(next_row * GRID_CNT + next_col);
                }
            }
        }
    }
}


bool saolei_toggle_flag(saolei_game_t *game, uint8_t row, uint8_t col)
{
        /* 只允许操作内部可玩区：row、col 都是 1～9。 */
        if (game == NULL ||
            row == 0 || row >= GRID_CNT - 1 ||
            col == 0 || col >= GRID_CNT - 1 ||
            game->status != SAOLEI_GAME_PLAYING) {
            return false;
        }

        saolei_cell_t *cell = &game->cells[row][col];

    /* 已翻开的格子不能插旗或取消旗子。 */
    if (cell->status == SAOLEI_CELL_OPENED) {
        return false;
    }

    /* 已插旗则取消旗子。 */
    if (cell->status == SAOLEI_CELL_FLAGGED) {
        cell->status = SAOLEI_CELL_UNOPENED;

        if (game->flagged_count > 0) {
            game->flagged_count--;
        }

        return true;
    }

    /* 未翻开的格子可以插旗，但旗子不能超过雷数。 */
    if (game->flagged_count >= BOMB_COUNT) {
        return false;
    }

    cell->status = SAOLEI_CELL_FLAGGED;
    game->flagged_count++;

    return true;
}

saolei_action_result_t saolei_open_cell(
    saolei_game_t *game,
    uint8_t row,
    uint8_t col
)
{
    /* 只允许操作内部可玩区：row、col 都是 1～9。 */
    if (game == NULL ||
        row == 0 || row >= GRID_CNT - 1 ||
        col == 0 || col >= GRID_CNT - 1 ||
        game->status != SAOLEI_GAME_PLAYING) {
        return SAOLEI_ACTION_IGNORED;
    }

    saolei_cell_t *cell = &game->cells[row][col];

    /* 已翻开或已插旗的格子不能再次翻开。 */
    if (cell->status == SAOLEI_CELL_OPENED ||
        cell->status == SAOLEI_CELL_FLAGGED) {
        return SAOLEI_ACTION_IGNORED;
    }

    /* 踩到雷：翻开该格，并结束游戏。 */
    if (cell->has_bomb) {
        cell->status = SAOLEI_CELL_OPENED;
        game->status = SAOLEI_GAME_DEFEAT;
        return SAOLEI_ACTION_HIT_BOMB;
    }

    /* 翻开安全格并更新计数。 */
    cell->status = SAOLEI_CELL_OPENED;
    game->opened_count++;
    if (cell->nearby_bombs == 0) {
    open_empty_area(game, row, col);
}

    /* 9×9 可玩区共 81 格，减去 10 颗雷，需翻开 71 个安全格获胜。 */
    const uint8_t safe_cell_count =
        (GRID_CNT - 2) * (GRID_CNT - 2) - BOMB_COUNT;

    if (game->opened_count >= safe_cell_count) {
        game->status = SAOLEI_GAME_VICTORY;
        return SAOLEI_ACTION_WON;
    }

    return SAOLEI_ACTION_OPENED;
}

