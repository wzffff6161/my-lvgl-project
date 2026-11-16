#ifndef __GAME_LOGIC_H__
#define __GAME_LOGIC_H__

#include <stdint.h>
#include <stdbool.h>

#define GRID_CNT        20
#define SNAKE_MAX_LEN   (GRID_CNT * GRID_CNT)

// 移动方向
typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} game_dir_t;

// 网格坐标点
typedef struct {
    uint8_t x;
    uint8_t y;
} grid_point_t;

// 方向输入队列长度：缓存连续转向，避免快速连按被丢弃
#define DIR_QUEUE_LEN   4

// 游戏上下文
typedef struct {
    grid_point_t snake_body[SNAKE_MAX_LEN];
    uint16_t snake_len;

    grid_point_t food;

    game_dir_t cur_dir;
    game_dir_t dir_queue[DIR_QUEUE_LEN];   // 方向输入队列（环形）
    uint8_t dir_head;                      // 队首：下一个要执行的方向
    uint8_t dir_tail;                      // 队尾：下一个写入位置

    uint16_t score;
    uint16_t high_score;

    bool is_game_over;
    bool is_paused;
} game_logic_t;

// 对外API
void game_logic_init(game_logic_t *ctx);
void game_logic_reset(game_logic_t *ctx);
void game_logic_set_dir(game_logic_t *ctx, game_dir_t new_dir);
void game_logic_tick(game_logic_t *ctx);
bool game_logic_pos_on_snake(game_logic_t *ctx, uint8_t x, uint8_t y);

#endif
