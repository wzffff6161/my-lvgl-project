//纯游戏业务逻辑，**完全不依赖LVGL**：蛇结构体、移动、碰撞、食物生成、分数、重置游戏
#include "game_logic.h"
#include <stdlib.h>

//随机生成食物
static grid_point_t rand_food_pos(void)
{
    grid_point_t p;
    p.x = rand() % GRID_CNT;
   p.y = rand() % GRID_CNT;
   return p;
}

//初始化游戏全局上下文
void game_logic_init(game_logic_t *ctx)
{
    ctx->high_score = 0;
    // 调用重置函数完成单局初始化
    game_logic_reset(ctx);
}

//重置单局游戏状态
void game_logic_reset(game_logic_t *ctx)
{

    // 蛇初始状态
    ctx->snake_len = 3;
    ctx->snake_body[0].x = 10;   // 蛇头
    ctx->snake_body[0].y = 10;
    ctx->snake_body[1].x = 9;    // 第二节
    ctx->snake_body[1].y = 10;
    ctx->snake_body[2].x = 8;    // 第三节
    ctx->snake_body[2].y = 10;

    // 初始方向向右
    ctx->cur_dir = DIR_RIGHT;
    ctx->dir_head = 0;
    ctx->dir_tail = 0;

    //游戏状态
    ctx->score = 0;
    ctx->is_game_over = false;
    ctx->is_paused = false;
    
    //生成初始食物
    do{
        ctx->food = rand_food_pos();
    }while(game_logic_pos_on_snake(ctx, ctx->food.x, ctx->food.y));
}

// ---------- 方向输入队列（环形） ----------
// 缓存连续转向，解决快速连按被丢弃、手感“不跟手”的问题

//队列是否为空
static bool dir_queue_empty(const game_logic_t *ctx)
{
    return ctx->dir_head == ctx->dir_tail;
}

//队列当前长度
static uint8_t dir_queue_len(const game_logic_t *ctx)
{
    return (uint8_t)((ctx->dir_tail - ctx->dir_head + DIR_QUEUE_LEN) % DIR_QUEUE_LEN);
}

//队尾方向（最近一次入队的方向）
static game_dir_t dir_queue_last(const game_logic_t *ctx)
{
    return ctx->dir_queue[(ctx->dir_tail - 1 + DIR_QUEUE_LEN) % DIR_QUEUE_LEN];
}

//入队
static void dir_queue_push(game_logic_t *ctx, game_dir_t d)
{
    ctx->dir_queue[ctx->dir_tail] = d;
    ctx->dir_tail = (ctx->dir_tail + 1) % DIR_QUEUE_LEN;
}

//出队（取队首）
static game_dir_t dir_queue_pop(game_logic_t *ctx)
{
    game_dir_t d = ctx->dir_queue[ctx->dir_head];
    ctx->dir_head = (ctx->dir_head + 1) % DIR_QUEUE_LEN;
    return d;
}

//是否互为反向（180度掉头）
static bool is_reverse_dir(game_dir_t a, game_dir_t b)
{
    return (a == DIR_UP    && b == DIR_DOWN)  ||
           (a == DIR_DOWN  && b == DIR_UP)    ||
           (a == DIR_LEFT  && b == DIR_RIGHT) ||
           (a == DIR_RIGHT && b == DIR_LEFT);
}

//移动方向逻辑
void game_logic_set_dir(game_logic_t *ctx, game_dir_t new_dir)
{
    // 反转参考方向：队列里已有待执行方向时以队尾为准，否则以当前方向为准
    game_dir_t ref = dir_queue_empty(ctx) ? ctx->cur_dir : dir_queue_last(ctx);

    // 禁止180度掉头、忽略与参考方向相同的重复输入
    if (is_reverse_dir(ref, new_dir) || ref == new_dir)
    {
        return;
    }

    // 队列满则丢弃多余输入（环形队列最多存 DIR_QUEUE_LEN-1 个）
    if (dir_queue_len(ctx) >= DIR_QUEUE_LEN - 1)
    {
        return;
    }

    // 入队缓存，当前帧不立即生效，下一帧开始逐步执行
    dir_queue_push(ctx, new_dir);
}

//判断坐标是否在蛇身体上
bool game_logic_pos_on_snake(game_logic_t *ctx, uint8_t x, uint8_t y)
{
    for(uint16_t i = 0; i < ctx->snake_len; i++)
    {
       if (ctx->snake_body[i].x == x && ctx->snake_body[i].y == y)
        {
            return true;
        }
    }
    return false;
}

//游戏帧逻辑 执行顺序：更新方向 → 计算新蛇头 → 撞墙检测 → 撞自身检测 → 移动蛇身 → 吃食物处理
void game_logic_tick(game_logic_t *ctx)
{
    //游戏结束或暂停时，不执行任何逻辑
    if(ctx->is_game_over || ctx->is_paused)
    {
        return;
    }

    //更新移动方向：优先取队列中缓存的下一步方向
    if (!dir_queue_empty(ctx))
    {
        ctx->cur_dir = dir_queue_pop(ctx);
    }
    //蛇头坐标
    grid_point_t new_head = ctx->snake_body[0];
    switch (ctx->cur_dir)
    {
        case DIR_UP:    new_head.y--; break;
        case DIR_DOWN:  new_head.y++; break;
        case DIR_LEFT:  new_head.x--; break;
        case DIR_RIGHT: new_head.x++; break;
        default: break;
    }

    //撞墙判断
    if(new_head.x >= GRID_CNT || new_head.y >= GRID_CNT)
    {
        ctx->is_game_over = true;
        // 更新最高分
        if (ctx->score > ctx->high_score)
        {
            ctx->high_score = ctx->score;
        }
        return;
    }
    
    //撞自身检测
    if(game_logic_pos_on_snake(ctx, new_head.x, new_head.y))
    {
        ctx->is_game_over = true;
        if(ctx->score > ctx->high_score)
        {
            ctx->high_score = ctx->score;
        }
        return;
    }

    //蛇身前移运动
    for(int i = ctx->snake_len - 1; i > 0; i--)
    {
        ctx->snake_body[i] = ctx->snake_body[i - 1];
    }
    // 新蛇头放到数组最前面
    ctx->snake_body[0] = new_head;

    //吃到食物处理
    if(new_head.x == ctx->food.x && new_head.y == ctx->food.y)
    {
        ctx->snake_len++;
        ctx->score += 10;
        if (ctx->score > ctx->high_score)
        {
            ctx->high_score = ctx->score;
        }
        // 生成新的食物，避开蛇身
        do {
            ctx->food = rand_food_pos();
        } while (game_logic_pos_on_snake(ctx, ctx->food.x, ctx->food.y));
    }

}