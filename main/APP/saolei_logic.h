#ifndef SAOLEI_LOGIC_H
#define SAOLEI_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

/* ==================== 棋盘参数 ==================== */

/* 棋盘的行数和列数：11 × 11，共 121 个格子 */
#define GRID_CNT            11

/* 棋盘中的雷总数 */
#define BOMB_COUNT          10

/* 棋盘格子总数 */
#define SAOLEI_MAX_LEN      (GRID_CNT * GRID_CNT)


/* ==================== 格子与游戏状态 ==================== */

/**
 * @brief 单个格子的状态
 */
typedef enum {
    SAOLEI_CELL_UNOPENED = 0,  /* 尚未翻开 */
    SAOLEI_CELL_OPENED,        /* 已经翻开 */
    SAOLEI_CELL_FLAGGED        /* 已插旗，表示玩家认为这里有雷 */
} saolei_cell_status_t;


/**
 * @brief 当前一局游戏的状态
 */
typedef enum {
    SAOLEI_GAME_PLAYING = 0,   /* 游戏进行中 */
    SAOLEI_GAME_VICTORY,       /* 玩家胜利 */
    SAOLEI_GAME_DEFEAT         /* 玩家踩雷，游戏失败 */
} saolei_game_status_t;


/**
 * @brief 单个扫雷格子保存的信息
 */
typedef struct {
    bool has_bomb;                     /* 当前格子是否有雷 */
    uint8_t nearby_bombs;              /* 周围 8 个格子中的雷数，范围为 0～8 */
    saolei_cell_status_t status;       /* 当前格子的翻开/插旗状态 */
} saolei_cell_t;


/**
 * @brief 一局扫雷游戏的完整数据
 */
typedef struct {
    saolei_cell_t cells[GRID_CNT][GRID_CNT]; /* 棋盘；通过 cells[row][col] 访问 */
    uint8_t flagged_count;                   /* 当前已插旗的格子数 */
    uint8_t opened_count;                    /* 已翻开的非雷格子数 */
    saolei_game_status_t status;             /* 当前游戏状态 */
} saolei_game_t;


/* ==================== 操作结果 ==================== */

/**
 * @brief 翻开格子操作的结果
 *
 * 注意：如果翻开的格子周围没有雷，逻辑层可能会自动展开周围的安全格。
 */
typedef enum {
    SAOLEI_ACTION_IGNORED = 0, /* 操作无效：游戏已结束、格子已翻开或已插旗等 */
    SAOLEI_ACTION_OPENED,      /* 成功翻开格子，游戏仍在进行 */
    SAOLEI_ACTION_HIT_BOMB,    /* 翻开了有雷的格子，游戏失败 */
    SAOLEI_ACTION_WON          /* 翻开所有安全格，游戏胜利 */
} saolei_action_result_t;


/* ==================== 对外接口 ==================== */

/**
 * @brief 初始化或重新开始一局扫雷游戏
 * @param game 要初始化的游戏数据，不能为 NULL
 *
 * 初始化时应清空棋盘状态、随机布置雷、计算各格周围的雷数，
 * 并将游戏状态设为进行中。
 */
void saolei_init(saolei_game_t *game);


/**
 * @brief 翻开指定位置的格子
 * @param game 游戏数据，不能为 NULL
 * @param row 行下标，范围为 1～9；0 和 10 是外围保护边界
 * @param col 列下标，范围为 1～9；0 和 10 是外围保护边界
 * @return 翻格操作结果，具体含义见 saolei_action_result_t
 *
 * 已插旗的格子不会被翻开。
 * 若目标格周围没有雷，逻辑层可以继续展开周围的安全格。
 */
saolei_action_result_t saolei_open_cell(
    saolei_game_t *game,
    uint8_t row,
    uint8_t col
);


/**
 * @brief 在指定位置插旗或取消旗子
 * @param game 游戏数据，不能为 NULL
 * @param row  行下标，范围为 1～9；0 和 10 是外围保护边界
 * @param col  列下标，范围为 1～9；0 和 10 是外围保护边界
 * @return true 表示旗子状态成功切换；false 表示操作无效
 *
 * 只有尚未翻开的格子可以插旗或取消旗子。
 */
bool saolei_toggle_flag(
    saolei_game_t *game,
    uint8_t row,
    uint8_t col
);

#endif /* SAOLEI_LOGIC_H */