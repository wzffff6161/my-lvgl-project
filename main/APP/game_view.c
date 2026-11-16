// UI视图层：棋盘画布、顶部分数栏、暂停按钮、十字按键、弹窗（暂停/游戏结束弹窗）、LVGL对象管理、定时器生命周期管理
#include "game_view.h"
#include "game_logic.h"
#include "page_manager.h"
#include "ui_common.h"
#include "lvgl.h"
#include "chinese_font.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include <stdio.h>
#include "lcd.h"

//游戏参数配置
#define GRID_CNT        20      // 棋盘格子数 20×20
#define CELL_PX         15      // 单个格子像素大小 15×15
#define CANVAS_SIZE     (GRID_CNT * CELL_PX)  // 画布总像素 300×300
#define GAME_TICK_MS    200     // 游戏逻辑刷新周期，毫秒，越小蛇越快

//全局静态变量
// 游戏逻辑上下文（全局唯一实例）
static game_logic_t g_game_ctx;
// LVGL画布对象，用于绘制游戏画面
static lv_obj_t *g_canvas = NULL;
// 游戏定时器句柄，每200ms触发一次逻辑帧
static lv_timer_t *g_game_timer = NULL;

// 分数标签、最高分标签
static lv_obj_t *lab_score = NULL;
static lv_obj_t *lab_highscore = NULL;

// 暂停弹窗、游戏结束弹窗句柄
static lv_obj_t *popup_pause = NULL;
static lv_obj_t *popup_gameover = NULL;


static lv_color_t *canvas_buf = NULL;

//颜色定义
#define COLOR_BOARD_BG   lv_color_hex(0x0F2043)   // 棋盘深蓝色背景
#define COLOR_BOARD_BG2  lv_color_hex(0x142A52)   // 棋盘浅色格（深浅交替）
#define COLOR_SNAKE      lv_color_hex(0x39D353)   // 蛇身绿色
#define COLOR_SNAKE_DARK lv_color_hex(0x2BA84A)   // 蛇身深绿（交替配色）
#define COLOR_SNAKE_HEAD lv_color_hex(0x5CF06B)   // 蛇头亮绿
#define COLOR_EYE_WHITE  lv_color_hex(0xFFFFFF)   // 蛇眼眼白
#define COLOR_EYE_PUPIL  lv_color_hex(0x10202B)   // 蛇眼瞳孔
#define COLOR_FOOD       lv_color_hex(0xFF4444)   // 食物红色
#define COLOR_FOOD_LIGHT lv_color_hex(0xFF8F8F)   // 食物高光
#define COLOR_PAD_BG     lv_color_hex(0x182B4A)   // 方向键区面板背景
#define COLOR_PAD_BORDER lv_color_hex(0x2C4A78)   // 方向键区面板边框
#define COLOR_BTN        lv_color_hex(0x3A6FC0)   // 方向键常态背景
#define COLOR_BTN_PRESSED lv_color_hex(0x1D4E96)  // 方向键按下背景
#define COLOR_PAGE_BG    lv_color_hex(0x0B1830)   // 页面背景深蓝
#define COLOR_TITLE_TEXT lv_color_hex(0xEAF2FF)   // 标题文字（近白带蓝）
#define COLOR_SCORE_PILL lv_color_hex(0x1B3A5C)   // 分数胶囊背景
#define COLOR_SCORE_TEXT lv_color_hex(0xFFC24B)   // 分数文字（琥珀）
#define COLOR_HIGH_TEXT  lv_color_hex(0x4AE06B)   // 最高分文字（绿）

//最高分持久化（NVS）：退出页面/断电后仍保留
#define NVS_NAMESPACE   "snake_game"
#define NVS_KEY_HIGH    "high_score"

//已保存到 NVS 的最高分，用于避免每帧重复写 Flash
static uint16_t g_saved_highscore = 0;

//从 NVS 读取最高分，读取失败返回 0
static uint16_t highscore_load(void)
{
    nvs_handle_t handle;
    uint16_t val = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK)
    {
        if (nvs_get_u16(handle, NVS_KEY_HIGH, &val) != ESP_OK)
        {
            val = 0;
        }
        nvs_close(handle);
    }
    return val;
}

//保存最高分到 NVS
static void highscore_save(uint16_t val)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_set_u16(handle, NVS_KEY_HIGH, val);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

//函数前置声明
static void game_view_render(void);          // 重新渲染画面
static void game_timer_cb(lv_timer_t *timer);
static void close_all_popup(void);           // 关闭弹窗

//方向按钮回调函数
static void cb_dir_up(lv_event_t *e)
{
    game_logic_set_dir(&g_game_ctx, DIR_UP);
}
static void cb_dir_down(lv_event_t *e)
{
    game_logic_set_dir(&g_game_ctx, DIR_DOWN);
}
static void cb_dir_left(lv_event_t *e)
{
    game_logic_set_dir(&g_game_ctx, DIR_LEFT);
}
static void cb_dir_right(lv_event_t *e)
{
    game_logic_set_dir(&g_game_ctx, DIR_RIGHT);
}

// 弹窗内部按钮回调
//暂停弹窗:继续游戏按钮
static void cb_pause_continue(lv_event_t *e)
{
   g_game_ctx.is_paused = false;
   lv_obj_del(popup_pause);
   popup_pause = NULL;
}

//弹窗：返回主页按钮
static void cb_popup_back_home(lv_event_t *e)
{
    close_all_popup();
    page_manager_back();
}

//游戏结束弹窗：再来一局按钮
static void cb_gameover_restart(lv_event_t *e)
{
    // 重置游戏逻辑
    game_logic_reset(&g_game_ctx);
    // 重新渲染画面
    game_view_render();
    // 关闭结束弹窗
    lv_obj_del(popup_gameover);
    popup_gameover = NULL;
}

// 弹窗创建
//创建暂停弹窗
static void popup_pause_create(void)
{
    // 先关闭其他弹窗，避免重叠
    close_all_popup();

    //创建弹窗容器
    popup_pause= lv_obj_create(lv_layer_top());
    lv_obj_set_size(popup_pause,200,160);
    lv_obj_center(popup_pause);
    lv_obj_set_flex_flow(popup_pause, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(popup_pause, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(popup_pause, 8, LV_PART_MAIN); 
    lv_obj_set_scroll_dir(popup_pause, LV_DIR_NONE);
    lv_obj_set_style_text_font(popup_pause, &lv_font_chinese_16, LV_PART_MAIN);

    //标题文字
    lv_obj_t *lab = lv_label_create(popup_pause);
    lv_label_set_text(lab,"游戏已暂停");
    lv_obj_center(lab);

    //继续游戏按钮
    lv_obj_t *btn_continue = ui_common_create_button(popup_pause, "继续游戏");
    lv_obj_remove_style(btn_continue, NULL, LV_PART_MAIN);
    lv_obj_set_size(btn_continue, 140, 36);  // 宽140 高36
    ui_common_set_click_event(btn_continue, cb_pause_continue, NULL);

    // 返回主页按钮
    lv_obj_t *btn_home = ui_common_create_button(popup_pause, "返回主页");
    lv_obj_remove_style(btn_home, NULL, LV_PART_MAIN);
    lv_obj_set_size(btn_home, 140, 36);
    ui_common_set_click_event(btn_home, cb_popup_back_home, NULL);
}
//创建游戏结束弹窗：显示本次得分、最高分，提供再来一局和返回主页
static void popup_gameover_create(void)
{
    close_all_popup();

    popup_gameover = lv_obj_create(lv_layer_top());
    lv_obj_set_size(popup_gameover, 220, 190);
    lv_obj_center(popup_gameover);
    lv_obj_set_flex_flow(popup_gameover, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(popup_gameover, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(popup_gameover, 6, LV_PART_MAIN);
    lv_obj_set_scroll_dir(popup_gameover, LV_DIR_NONE);  // 禁止滚动
    lv_obj_set_style_text_font(popup_gameover, &lv_font_chinese_16, LV_PART_MAIN);

    // 标题
    lv_obj_t *lab_title = lv_label_create(popup_gameover);
    lv_label_set_text(lab_title, "游戏结束");

    // 得分信息
    lv_obj_t *lab_info = lv_label_create(popup_gameover);
    char buf[64];
    snprintf(buf, sizeof(buf), "得分:%d\n最高分:%d", g_game_ctx.score, g_game_ctx.high_score);
    lv_label_set_text(lab_info, buf);
    

    // 再来一局按钮
    lv_obj_t *btn_restart = ui_common_create_button(popup_gameover, "再来一局");
    lv_obj_remove_style(btn_restart, NULL, LV_PART_MAIN);
    lv_obj_set_size(btn_restart, 140, 36);
    ui_common_set_click_event(btn_restart, cb_gameover_restart, NULL);

    // 返回主页按钮
    lv_obj_t *btn_home = ui_common_create_button(popup_gameover, "返回主页");
    lv_obj_remove_style(btn_home, NULL, LV_PART_MAIN);
    lv_obj_set_size(btn_home, 140, 36);
    ui_common_set_click_event(btn_home, cb_popup_back_home, NULL);
}
//关闭所有打开的弹窗:进入页面、退出页面、新建弹窗前都要调用，防止残留
static void close_all_popup(void)
{
    if (popup_pause != NULL)
    {
        lv_obj_del(popup_pause);
        popup_pause = NULL;
    }
    if (popup_gameover != NULL)
    {
        lv_obj_del(popup_gameover);
        popup_gameover = NULL;
    }
}

static void game_view_delete_cb(lv_event_t *e)
{
    // 1. 删除游戏定时器，停止游戏tick
    if(g_game_timer != NULL)
    {
        lv_timer_del(g_game_timer);
        g_game_timer = NULL;
    }
    //2. 关闭弹窗
    close_all_popup();

    //3. PSRAM画布内存释放，现在canvas_buf是全局静态，可以直接访问
    if(canvas_buf != NULL)
    {
        heap_caps_free(canvas_buf);
        canvas_buf = NULL;
    }
    g_canvas = NULL;
    lab_score = NULL;
    lab_highscore = NULL;
}

//顶部标题栏按钮回调
//右上角暂停按钮
static void cb_pause_btn(lv_event_t *e)
{
    // 游戏结束状态下，暂停按钮无效
    if (g_game_ctx.is_game_over) return;
    g_game_ctx.is_paused = true;
    popup_pause_create();
}
//左上角返回按钮
static void cb_back_btn(lv_event_t *e)
{
    // 返回前关闭所有弹窗，防止残留
    close_all_popup();
    page_manager_back();
}

//用圆角矩形在画布上近似画圆（radius = 直径/2）
static void canvas_draw_circle(lv_obj_t *canvas, uint16_t x, uint16_t y, uint16_t d, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = d / 2;
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    lv_canvas_draw_rect(canvas, x, y, d, d, &dsc);
}

//绘制蛇头：亮色圆角主体 + 朝向移动方向的两只眼睛
static void draw_snake_head(lv_obj_t *canvas, uint16_t x, uint16_t y, game_dir_t dir)
{
    // 头部主体，比身体更亮，便于区分
    canvas_draw_circle(canvas, x + 1, y + 1, CELL_PX - 2, COLOR_SNAKE_HEAD);

    // 头部左上角（13×13 区域）作为眼睛布局基准
    uint16_t hx = x + 1;
    uint16_t hy = y + 1;

    // 两只眼白坐标、两个瞳孔坐标，按方向摆放
    uint16_t ex1, ey1, ex2, ey2;
    uint16_t px1, py1, px2, py2;
    switch (dir)
    {
        case DIR_UP:
            ex1 = hx + 1; ey1 = hy + 1;
            ex2 = hx + 7; ey2 = hy + 1;
            px1 = hx + 2; py1 = hy + 1;
            px2 = hx + 8; py2 = hy + 1;
            break;
        case DIR_DOWN:
            ex1 = hx + 1; ey1 = hy + 8;
            ex2 = hx + 7; ey2 = hy + 8;
            px1 = hx + 2; py1 = hy + 10;
            px2 = hx + 8; py2 = hy + 10;
            break;
        case DIR_LEFT:
            ex1 = hx + 1; ey1 = hy + 1;
            ex2 = hx + 1; ey2 = hy + 7;
            px1 = hx + 1; py1 = hy + 2;
            px2 = hx + 1; py2 = hy + 8;
            break;
        case DIR_RIGHT:
        default:
            ex1 = hx + 8; ey1 = hy + 1;
            ex2 = hx + 8; ey2 = hy + 7;
            px1 = hx + 10; py1 = hy + 2;
            px2 = hx + 10; py2 = hy + 8;
            break;
    }

    // 眼白
    canvas_draw_circle(canvas, ex1, ey1, 4, COLOR_EYE_WHITE);
    canvas_draw_circle(canvas, ex2, ey2, 4, COLOR_EYE_WHITE);
    // 瞳孔
    canvas_draw_circle(canvas, px1, py1, 2, COLOR_EYE_PUPIL);
    canvas_draw_circle(canvas, px2, py2, 2, COLOR_EYE_PUPIL);
}

//创建方向键：可见背景 + 按下反馈，方向在按下瞬间生效（响应更快）
static lv_obj_t *create_dir_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);                       // 去掉默认主题样式，完全自定义外观
    lv_obj_set_size(btn, 44, 44);
    lv_obj_set_style_bg_color(btn, COLOR_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, COLOR_BTN_PRESSED, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, symbol);
    lv_obj_center(lab);

    // 用 PRESSED 事件：手指一按下就改变方向，比 CLICKED（按下再抬起）跟手得多
    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, NULL);
    return btn;
}

//创建顶部小按钮（返回/暂停）：圆角、可见背景、按下变色
static lv_obj_t *create_top_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 52, 30);
    lv_obj_set_style_bg_color(btn, COLOR_BTN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, COLOR_BTN_PRESSED, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_center(lab);

    // 返回/暂停不追求瞬时响应，用 CLICKED 避免误触
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

//画布渲染函数
//把游戏逻辑数据渲染到LVGL画布
static void game_view_render(void)
{
    if(g_canvas == NULL) return;

    //用棋盘背景色填充整个画布
    lv_canvas_fill_bg(g_canvas, COLOR_BOARD_BG, LV_OPA_COVER);

    //绘制深浅交替的棋盘格，增强立体感和距离感
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.radius = 0;
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.bg_color = COLOR_BOARD_BG2;
    for (uint16_t gy = 0; gy < GRID_CNT; gy++)
    {
        for (uint16_t gx = 0; gx < GRID_CNT; gx++)
        {
            if ((gx + gy) & 1)
            {
                lv_canvas_draw_rect(g_canvas, gx * CELL_PX, gy * CELL_PX, CELL_PX, CELL_PX, &rect_dsc);
            }
        }
    }

    //绘制蛇身：圆角珠子，深浅交替，蛇头单独绘制
    for (uint16_t i = 1; i < g_game_ctx.snake_len; i++)
    {
        uint16_t x = g_game_ctx.snake_body[i].x * CELL_PX;
        uint16_t y = g_game_ctx.snake_body[i].y * CELL_PX;
        lv_color_t c = (i & 1) ? COLOR_SNAKE : COLOR_SNAKE_DARK;
        canvas_draw_circle(g_canvas, x + 1, y + 1, CELL_PX - 2, c);
    }

    //绘制蛇头：带眼睛，朝向当前移动方向
    uint16_t hx = g_game_ctx.snake_body[0].x * CELL_PX;
    uint16_t hy = g_game_ctx.snake_body[0].y * CELL_PX;
    draw_snake_head(g_canvas, hx, hy, g_game_ctx.cur_dir);

    //绘制食物：圆形主体 + 左上角高光
    uint16_t fx = g_game_ctx.food.x * CELL_PX;
    uint16_t fy = g_game_ctx.food.y * CELL_PX;
    canvas_draw_circle(g_canvas, fx + 1, fy + 1, CELL_PX - 2, COLOR_FOOD);
    canvas_draw_circle(g_canvas, fx + 3, fy + 3, 4, COLOR_FOOD_LIGHT);

    //通知LVGL画布内容已修改，刷新显示
    lv_obj_invalidate(g_canvas);

    //更新顶部分数文字
    char buf[32];
    snprintf(buf,sizeof(buf), "分数:%d", g_game_ctx.score);
    lv_label_set_text(lab_score,buf);
    snprintf(buf, sizeof(buf), "最高分:%d", g_game_ctx.high_score);
    lv_label_set_text(lab_highscore, buf);

    //游戏结束自动弹出结束弹窗
    if (g_game_ctx.is_game_over && popup_gameover == NULL)
    {
        popup_gameover_create();
    }
}


//游戏定时器回调:每200ms触发一次，驱动游戏前进一帧 先调用逻辑层tick，再调用视图层渲染
static void game_timer_cb(lv_timer_t *timer)
{
    game_logic_tick(&g_game_ctx);

    //最高分提升时立即写入 NVS，退出页面/断电也不丢失
    if (g_game_ctx.high_score > g_saved_highscore)
    {
        g_saved_highscore = g_game_ctx.high_score;
        highscore_save(g_saved_highscore);
    }

    game_view_render();
}
//页面创建入口（page_manager调用）
void game_view_create(void)
{
    
    if (g_game_timer != NULL)
    {
        lv_timer_del(g_game_timer);
        g_game_timer = NULL;
    }
    close_all_popup();

    //页面根容器
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_set_size(root, 320, 480);
    lv_obj_center(root);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(root, 4, LV_PART_MAIN);
    lv_obj_set_scroll_dir(root, LV_DIR_NONE);       // 禁止整个页面滚动，固定内容
    lv_obj_set_style_bg_color(root, COLOR_PAGE_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(root, &lv_font_chinese_16, LV_PART_MAIN);

    //绑定页面销毁回调
    lv_obj_add_event_cb(root, game_view_delete_cb, LV_EVENT_DELETE, NULL);

    //顶部标题栏：返回+标题+暂停
    lv_obj_t*top_bar = lv_obj_create(root);
    lv_obj_set_size(top_bar,LV_PCT(100),36);
    lv_obj_set_scroll_dir(top_bar, LV_DIR_NONE);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(top_bar, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(top_bar, 0, LV_PART_MAIN);       // 去掉默认内边距，否则36px高装不下16px文字，下半被裁剪
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);  // 去掉默认边框

    // 左上角返回按钮（圆角、可见背景、按下变色）
    lv_obj_t *btn_back = create_top_button(top_bar, "返回", cb_back_btn);

    // 中间标题（浅色文字，深色背景上更醒目）
    lv_obj_t *title = lv_label_create(top_bar);
    lv_label_set_text(title, "贪吃蛇");
    lv_obj_set_style_text_color(title, COLOR_TITLE_TEXT, LV_PART_MAIN);

    // 右上角暂停按钮
    lv_obj_t *btn_pause = create_top_button(top_bar, "暂停", cb_pause_btn);

    //分数状态栏：左分数，右最高分
    lv_obj_t *score_bar = lv_obj_create(root);
    lv_obj_set_size(score_bar, LV_PCT(100), 22);
    lv_obj_set_scroll_dir(score_bar, LV_DIR_NONE);
    lv_obj_set_flex_flow(score_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(score_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(score_bar, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(score_bar, 0, LV_PART_MAIN);       // 去掉默认内边距，否则22px高装不下16px文字
    lv_obj_set_style_border_width(score_bar, 0, LV_PART_MAIN);  // 去掉默认边框
    lv_obj_set_style_flex_cross_place(score_bar, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);  // 文字在22px内垂直居中

    // 分数与最高分做成胶囊样式：深底 + 彩色文字，视觉更清晰
    lab_score = lv_label_create(score_bar);
    lv_label_set_text(lab_score, "分数:0");
    lv_obj_set_style_bg_color(lab_score, COLOR_SCORE_PILL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lab_score, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(lab_score, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(lab_score, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(lab_score, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_score, COLOR_SCORE_TEXT, LV_PART_MAIN);

    lab_highscore = lv_label_create(score_bar);
    lv_label_set_text(lab_highscore, "最高分:0");
    lv_obj_set_style_bg_color(lab_highscore, COLOR_SCORE_PILL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lab_highscore, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(lab_highscore, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(lab_highscore, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(lab_highscore, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_highscore, COLOR_HIGH_TEXT, LV_PART_MAIN);

    // 游戏画布 300×300
    g_canvas = lv_canvas_create(root);
    lv_obj_set_size(g_canvas, CANVAS_SIZE, CANVAS_SIZE);
    lv_obj_center(g_canvas);

    // 画布缓冲区分配到 PSRAM，避免占用内部 DRAM（300×300×2 = 180KB）
   
    if (canvas_buf == NULL)
    {
        canvas_buf = heap_caps_malloc(CANVAS_SIZE * CANVAS_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    lv_canvas_set_buffer(g_canvas, canvas_buf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);


    //十字方向按键区：加圆角背景面板，明确可触摸区域，避免误触
    lv_obj_t *pad = lv_obj_create(root);
    lv_obj_set_size(pad, LV_PCT(100), 104);
    lv_obj_set_scroll_dir(pad, LV_DIR_NONE);
    lv_obj_set_style_bg_color(pad, COLOR_PAD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pad, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(pad, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(pad, COLOR_PAD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pad, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(pad, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(pad, 8, LV_PART_MAIN);
    lv_obj_set_layout(pad, LV_LAYOUT_GRID);

    //自定义3列2行网格，专门用于十字键布局
    static const lv_coord_t pad_col[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t pad_row[] = {
        LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    //按键区使用十字键布局
    lv_obj_set_grid_dsc_array(pad, pad_col, pad_row);

    //// ---------- 上键 ----------
    lv_obj_t *btn_up = create_dir_button(pad, LV_SYMBOL_UP, cb_dir_up);
    lv_obj_set_grid_cell(btn_up,
        LV_GRID_ALIGN_CENTER, 1, 1,
        LV_GRID_ALIGN_CENTER, 0, 1);

    // ---------- 左键 ----------
    lv_obj_t *btn_left = create_dir_button(pad, LV_SYMBOL_LEFT, cb_dir_left);
    lv_obj_set_grid_cell(btn_left,
        LV_GRID_ALIGN_CENTER, 0, 1,
        LV_GRID_ALIGN_CENTER, 1, 1);

    // ---------- 下键 ----------
    lv_obj_t *btn_down = create_dir_button(pad, LV_SYMBOL_DOWN, cb_dir_down);
    lv_obj_set_grid_cell(btn_down,
        LV_GRID_ALIGN_CENTER, 1, 1,
        LV_GRID_ALIGN_CENTER, 1, 1);

    // ---------- 右键 ----------
    lv_obj_t *btn_right = create_dir_button(pad, LV_SYMBOL_RIGHT, cb_dir_right);
    lv_obj_set_grid_cell(btn_right,
        LV_GRID_ALIGN_CENTER, 2, 1,
        LV_GRID_ALIGN_CENTER, 1, 1);

    //初始化渲染第一帧
    game_logic_init(&g_game_ctx);
    //载入持久化的最高分（game_logic_init 会先清零，需在其后覆盖）
    g_game_ctx.high_score = highscore_load();
    g_saved_highscore = g_game_ctx.high_score;
    game_view_render();

    //创建游戏定时器，启动游戏循环
    g_game_timer = lv_timer_create(game_timer_cb, GAME_TICK_MS, NULL);
}