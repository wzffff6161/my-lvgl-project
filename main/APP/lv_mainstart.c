/**
 负责创建当前主页面，包括标题、菜单按钮，以及点击按钮之后应该执行什么业务操作。
它解决的是：当前页面显示什么，用户点击之后做什么。
 */

#include "lv_mainstart.h"
#include "lvgl.h"
#include "ui_common.h"
#include <stdio.h>
#include "page_manager.h"

// 主页面功能编号
typedef enum
{
    MAIN_FUNC_DEVICE = 0,
    MAIN_FUNC_SETTINGS,
    MAIN_FUNC_NETWORK,
    MAIN_FUNC_ABOUT,
    MAIN_FUNC_GAME,
    MAIN_FUNC_WEATHER,
    MAIN_FUNC_SAOLEI
} main_func_id_t;

// 主页面入口配置
typedef struct
{
    const char *title;      // 按钮显示文字
    main_func_id_t func_id; // 对应的功能编号
} main_menu_item_t;

// 主页面功能列表：新增入口时优先修改这里
static const main_menu_item_t main_menu_items[] =
    {
        {"Device", MAIN_FUNC_DEVICE},
        {"Settings", MAIN_FUNC_SETTINGS},
        {"Network", MAIN_FUNC_NETWORK},
        {"About", MAIN_FUNC_ABOUT},
        {"Game", MAIN_FUNC_GAME},
        {"Weather", MAIN_FUNC_WEATHER},
        {"Saolei", MAIN_FUNC_SAOLEI}
    };

// 主页面功能入口数量
#define MAIN_MENU_COUNT \
    (sizeof(main_menu_items) / sizeof(main_menu_items[0]))

// 页面控件
static lv_obj_t *main_grid;

// 主页面统一按钮事件回调
static void main_menu_event_cb(lv_event_t *e)
{
    // 获取触发事件的按钮
    lv_obj_t *button = lv_event_get_target(e);

    // 获取创建按钮时绑定的功能配置
    const main_menu_item_t *item =
        (const main_menu_item_t *)lv_event_get_user_data(e);

    if (button == NULL || item == NULL)
    {
        return;
    }

    // 先验证入口事件，后续接入页面管理模块
    switch (item->func_id)
    {
    case MAIN_FUNC_DEVICE:
        page_manager_navigate(PAGE_ID_DEVICE);
        printf("[MAIN] Device clicked\n");
        break;

    case MAIN_FUNC_SETTINGS:
        page_manager_navigate(PAGE_ID_SETTINGS);
        printf("[MAIN] Settings clicked\n");
        break;

    case MAIN_FUNC_NETWORK:
        page_manager_navigate(PAGE_ID_NETWORK);
        printf("[MAIN] Network clicked\n");
        break;

    case MAIN_FUNC_ABOUT:
        page_manager_navigate(PAGE_ID_ABOUT);
        printf("[MAIN] About clicked\n");
        break;

    case MAIN_FUNC_GAME:
        page_manager_navigate(PAGE_ID_GAME);
        printf("[MAIN] Game clicked\n");
        break;

    case MAIN_FUNC_WEATHER:
        page_manager_navigate(PAGE_ID_WEATHER);
        printf("[MAIN] Weather clicked\n");
        break;

    case MAIN_FUNC_SAOLEI:
        page_manager_navigate(PAGE_ID_SAOLEI);
        printf("[MAIN] Saolei clicked\n");
        break;

    default:
        printf("[MAIN] Unknown function\n");
        break;
    }
}

// 创建主页面功能入口
static void main_create_menu(void)
{
    for (uint32_t i = 0; i < MAIN_MENU_COUNT; i++)
    {
        /* 创建通用按钮 */
        lv_obj_t *button = ui_common_create_button(
            main_grid,
            main_menu_items[i].title);

        if (button == NULL)
        {
            printf("[MAIN] Failed to create menu: %s\n",
                   main_menu_items[i].title);
            continue;
        }

        /*
         * Grid 自动排列：
         * 第 0 个参数是列索引，第 1 个参数是行索引。
         * 双列布局下，入口会自动从左到右、从上到下排列。
         */
        lv_obj_set_grid_cell(
            button,
            LV_GRID_ALIGN_STRETCH, i % 2, 1,
            LV_GRID_ALIGN_STRETCH, i / 2, 1);

        /* 绑定当前入口自己的配置数据 */
        ui_common_set_click_event(
            button,
            main_menu_event_cb,
            (void *)&main_menu_items[i]);
    }
}

/**
 * @brief 创建主页面
 */
void lv_mainstart(void)
{
    lv_obj_t *screen = lv_scr_act();

    /* 设置主页面背景 */
    lv_obj_set_style_bg_color(
        screen, lv_color_hex(0xF2F5F9), LV_PART_MAIN);

    /* 创建页面标题 */
    if (ui_common_create_title(screen, "DEVICE CONTROL") == NULL)
    {
        printf("[MAIN] Failed to create title\n");
        return;
    }

    /* 创建双列 Grid 容器 */
    lv_coord_t screen_width = lv_obj_get_width(screen);
    lv_coord_t screen_height = lv_obj_get_height(screen);

    main_grid = ui_common_create_grid(
        screen,
        screen_width * 9 / 10,
        screen_height * 2 / 3);

    if (main_grid == NULL)
    {
        printf("[MAIN] Failed to create grid\n");
        return;
    }

    /* 根据功能配置自动生成入口 */
    main_create_menu();
}