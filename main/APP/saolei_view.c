#include "saolei_view.h"
#include "saolei_logic.h"
#include "page_manager.h"
#include "lvgl.h"
#include "esp_heap_caps.h"

#include <stdint.h>
#include <stdio.h>

/* 图标资源：LVGL 转换器生成的 32×32 带透明通道图（见 main/APP/icon/Bomb.c、Flag.c） */
extern const lv_img_dsc_t Bomb;
extern const lv_img_dsc_t Flag;

/* 棋盘绘制参数 */
#define BOARD_GAP       2                                   /* 格子间距 */
#define CELL_PX         32                                  /* 每格 32×32 像素 */
#define BOARD_CELLS     (GRID_CNT - 2)                      /* 内部可玩 9×9 */
#define BOARD_SIZE      (BOARD_CELLS * CELL_PX + (BOARD_CELLS - 1) * BOARD_GAP)  /* 304 */

/* 颜色 */
#define COLOR_PAGE_BG   0x0B1830
#define COLOR_BOARD_BG  0x0F2043
#define COLOR_CELL      0x3A6FC0
#define COLOR_FLAG      0xF2B84B
#define COLOR_OPENED    0xDCE6F2
#define COLOR_TEXT      0x203050
#define COLOR_BOMB      0xFF4444
#define COLOR_TITLE_TEXT 0xEAF2FF   /* 标题文字：近白带蓝 */
#define COLOR_STATUS_TEXT 0xFFC24B  /* 状态文字：琥珀色 */

/* 全局状态 */
static saolei_game_t g_game;
static lv_obj_t *g_canvas = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_color_t *g_canvas_buf = NULL;

/* 前置声明 */
static void draw_board(void);
static void update_status_label(void);
static void canvas_event_cb(lv_event_t *e);
static void canvas_delete_cb(lv_event_t *e);
static void back_button_cb(lv_event_t *e);


/* 把整盘逻辑状态绘制到画布上 */
static void draw_board(void)
{
    if (g_canvas == NULL) {
        return;
    }

    /* 先填满棋盘背景色 */
    lv_canvas_fill_bg(g_canvas, lv_color_hex(COLOR_BOARD_BG), LV_OPA_COVER);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = 4;
    rect_dsc.border_width = 0;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.font = &lv_font_montserrat_20;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;

    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);

    lv_coord_t line_h = lv_font_get_line_height(label_dsc.font);

    for (uint8_t row = 1; row < GRID_CNT - 1; row++) {
        for (uint8_t col = 1; col < GRID_CNT - 1; col++) {
            saolei_cell_t *cell = &g_game.cells[row][col];
            lv_coord_t x = (lv_coord_t)(col - 1) * (CELL_PX + BOARD_GAP);
            lv_coord_t y = (lv_coord_t)(row - 1) * (CELL_PX + BOARD_GAP);

            char text[2] = "";
            const lv_img_dsc_t *img = NULL;

            if (cell->status == SAOLEI_CELL_FLAGGED) {
                /* 插旗：琥珀底色 + 旗子图标 */
                rect_dsc.bg_color = lv_color_hex(COLOR_FLAG);
                img = &Flag;
            } else if (cell->status == SAOLEI_CELL_UNOPENED) {
                rect_dsc.bg_color = lv_color_hex(COLOR_CELL);
            } else {
                rect_dsc.bg_color = lv_color_hex(COLOR_OPENED);
                if (cell->has_bomb) {
                    /* 踩雷：亮色底 + 炸弹图标 */
                    img = &Bomb;
                } else if (cell->nearby_bombs > 0) {
                    label_dsc.color = lv_color_hex(COLOR_TEXT);
                    text[0] = (char)('0' + cell->nearby_bombs);
                }
            }

            lv_canvas_draw_rect(g_canvas, x, y, CELL_PX, CELL_PX, &rect_dsc);

            if (img != NULL) {
                /* 图标 32×32 与格子等大，直接平铺 */
                lv_canvas_draw_img(g_canvas, x, y, img, &img_dsc);
            } else if (text[0] != '\0') {
                lv_coord_t text_y = y + (CELL_PX - line_h) / 2;
                lv_canvas_draw_text(g_canvas, x, text_y, CELL_PX, &label_dsc, text);
            }
        }
    }

    lv_obj_invalidate(g_canvas);
}


/* 更新顶部状态文字：旗子数或胜负提示 */
static void update_status_label(void)
{
    if (g_status_label == NULL) {
        return;
    }

    if (g_game.status == SAOLEI_GAME_DEFEAT) {
        lv_label_set_text(g_status_label, "Game Over");
        return;
    }

    if (g_game.status == SAOLEI_GAME_VICTORY) {
        lv_label_set_text(g_status_label, "You Win!");
        return;
    }

    char text[24];
    snprintf(text, sizeof(text), "Flags: %u/%u",
             (unsigned)g_game.flagged_count,
             (unsigned)BOMB_COUNT);
    lv_label_set_text(g_status_label, text);
}


/*
 * 画布触摸事件：短按翻格，长按插旗。
 * 把触摸坐标换算成内部 9×9 的 (row, col)，row/col 范围 1～9。
 */
static void canvas_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) {
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL || g_canvas == NULL) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t coords;
    lv_obj_get_coords(g_canvas, &coords);

    int rel_x = point.x - coords.x1;
    int rel_y = point.y - coords.y1;

    int c = rel_x / (CELL_PX + BOARD_GAP);
    int r = rel_y / (CELL_PX + BOARD_GAP);

    /* 点击落在两格之间的间隙里则忽略 */
    if (rel_x - c * (CELL_PX + BOARD_GAP) >= CELL_PX) {
        return;
    }
    if (rel_y - r * (CELL_PX + BOARD_GAP) >= CELL_PX) {
        return;
    }

    if (r < 0 || r >= BOARD_CELLS || c < 0 || c >= BOARD_CELLS) {
        return;
    }

    uint8_t row = (uint8_t)(r + 1);
    uint8_t col = (uint8_t)(c + 1);

    if (code == LV_EVENT_SHORT_CLICKED) {
        if (saolei_open_cell(&g_game, row, col) == SAOLEI_ACTION_IGNORED) {
            return;
        }
    } else {
        if (!saolei_toggle_flag(&g_game, row, col)) {
            return;
        }
    }

    draw_board();
    update_status_label();
}


/* 画布销毁时释放 PSRAM 缓冲 */
static void canvas_delete_cb(lv_event_t *e)
{
    (void)e;
    if (g_canvas_buf != NULL) {
        heap_caps_free(g_canvas_buf);
        g_canvas_buf = NULL;
    }
    g_canvas = NULL;
}


/* 返回上一个页面 */
static void back_button_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        page_manager_back();
    }
}


/* 创建扫雷页面 */
void saolei_view_create(void)
{
    lv_obj_t *screen = lv_scr_act();

    /* 每次进入都初始化一局新游戏 */
    saolei_init(&g_game);

    /* 根容器：与贪吃蛇一致，竖屏固定 320×480 */
    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_set_size(root, 320, 480);
    lv_obj_center(root);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_PAGE_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scroll_dir(root, LV_DIR_NONE);

    /* 返回按钮 */
    lv_obj_t *back_button = lv_btn_create(root);
    lv_obj_set_size(back_button, 64, 36);
    lv_obj_set_pos(back_button, 8, 8);
    lv_obj_add_event_cb(back_button, back_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    /* 页面标题 */
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Minesweeper");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TITLE_TEXT), LV_PART_MAIN);

    /* 旗子数量 / 胜负提示 */
    g_status_label = lv_label_create(root);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(COLOR_STATUS_TEXT), LV_PART_MAIN);
    update_status_label();

    /* 棋盘画布 */
    g_canvas = lv_canvas_create(root);
    lv_obj_set_size(g_canvas, BOARD_SIZE, BOARD_SIZE);
    lv_obj_align(g_canvas, LV_ALIGN_CENTER, 0, 24);
    lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);

    /* 画布缓冲放到 PSRAM，避免占用内部 RAM（304×304×2 ≈ 185KB） */
    if (g_canvas_buf == NULL) {
        g_canvas_buf = heap_caps_malloc(
            BOARD_SIZE * BOARD_SIZE * sizeof(lv_color_t),
            MALLOC_CAP_SPIRAM);
    }

    if (g_canvas_buf == NULL) {
        printf("[SAOLEI] 画布缓冲分配失败（PSRAM）\n");
        lv_obj_del(g_canvas);
        g_canvas = NULL;
        return;
    }

    lv_canvas_set_buffer(g_canvas, g_canvas_buf,
                         BOARD_SIZE, BOARD_SIZE, LV_IMG_CF_TRUE_COLOR);

    lv_obj_add_event_cb(g_canvas, canvas_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(g_canvas, canvas_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(g_canvas, canvas_delete_cb, LV_EVENT_DELETE, NULL);

    draw_board();

    printf("[SAOLEI] 扫雷页面创建完成（画布 %d×%d）\n",
           (int)BOARD_SIZE, (int)BOARD_SIZE);
}
