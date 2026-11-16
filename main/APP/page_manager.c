//管理当前页面、创建页面、返回上一个页面
#include "page_manager.h"
#include "lv_mainstart.h"
#include "page_device.h"
#include "page_network.h"
#include "game_view.h"
#include <stdio.h>
#include "lvgl_demo.h"
#include "weather_view.h"
#include "saolei_view.h"




/* 页面创建函数类型 */
typedef void (*page_create_cb_t)(void);

/* 页面管理状态 */
typedef struct
{
    page_id_t current_page;                 // 当前页面 ID
    page_id_t history[PAGE_ID_MAX];         // 页面返回历史
    uint8_t history_top;                    // 历史栈当前元素数量
} page_manager_t;

/* 页面管理对象，整个程序只需要一份 */
static page_manager_t g_page_manager =
{
    .current_page = PAGE_ID_MAIN,
    .history_top = 0
};

/*
 * 页面创建函数表
 *
 * 数组下标与 page_id_t 的页面编号对应。
 * 暂未实现的页面先使用占位函数。
 */
static void page_create_main(void);
static void page_create_device(void);
static void page_create_settings(void);
static void page_create_network(void);
static void page_create_about(void);
static void page_create_game(void);
static void page_create_weather(void);
static void page_create_saolei(void);

static const page_create_cb_t page_create_table[PAGE_ID_MAX] =
{
    [PAGE_ID_MAIN]     = page_create_main,
    [PAGE_ID_DEVICE]   = page_create_device,
    [PAGE_ID_SETTINGS] = page_create_settings,
    [PAGE_ID_NETWORK]  = page_create_network,
    [PAGE_ID_ABOUT]    = page_create_about,
    [PAGE_ID_GAME]     = page_create_game,
    [PAGE_ID_WEATHER]  = page_create_weather,
    [PAGE_ID_SAOLEI]   = page_create_saolei,
};

/* 检查页面编号是否合法 */
static bool page_id_is_valid(page_id_t page_id)
{
    return page_id >= PAGE_ID_MAIN && page_id < PAGE_ID_MAX;
}

/**
 * @brief  根据目标页面ID自动切换屏幕方向
 * @note   只有游戏页面用竖屏，其余所有页面保持横屏
 */
static void apply_orientation(page_id_t page_id)
{
    if (page_id == PAGE_ID_GAME || page_id == PAGE_ID_SAOLEI)
    {
        lvgl_disp_set_orientation(0);  // 游戏页 / 扫雷页 → 竖屏 320×480
    }
    else
    {
        lvgl_disp_set_orientation(1);  // 其他页 → 横屏 480×320
    }
}


/* 创建主页面 */
static void page_create_main(void)
{
    lv_mainstart();
}

/* 返回按钮事件 */
static void page_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        page_manager_back();
    }
}

/* 创建临时页面：标题 + 返回按钮 */
static void page_create_placeholder(const char *title)
{
    lv_obj_t *screen = lv_scr_act();

    /* 页面标题 */
    lv_obj_t *label = lv_label_create(screen);
    if (label == NULL)
    {
        return;
    }

    lv_label_set_text(label, title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 30);

    /* 返回按钮 */
    lv_obj_t *back_button = lv_btn_create(screen);
    if (back_button == NULL)
    {
        return;
    }

    lv_obj_set_size(back_button, 100, 44);
    lv_obj_align(back_button, LV_ALIGN_BOTTOM_MID, 0, -25);

    lv_obj_t *back_label = lv_label_create(back_button);
    if (back_label != NULL)
    {
        lv_label_set_text(back_label, "Back");
        lv_obj_center(back_label);
    }

    lv_obj_add_event_cb(
        back_button,
        page_back_event_cb,
        LV_EVENT_CLICKED,
        NULL
    );
}

static void page_create_device(void)
{
     page_device_create();
}

static void page_create_settings(void)
{
    page_create_placeholder("SETTINGS PAGE - TODO");
}

static void page_create_network(void)
{
    page_network_create();
}

static void page_create_about(void)
{
    page_create_placeholder("ABOUT PAGE - TODO");
}

static void page_create_game(void)
{
    game_view_create();
}

static void page_create_weather(void)
{
    weather_view_create();
}

static void page_create_saolei(void)
{
    saolei_view_create();
}

/* 初始化页面管理模块 */
void page_manager_init(void)
{
    g_page_manager.current_page = PAGE_ID_MAIN;
    g_page_manager.history_top = 0;

    /* 初始页面由页面管理模块统一创建 */
    page_create_table[PAGE_ID_MAIN]();
}

/* 跳转到指定页面 */
void page_manager_navigate(page_id_t page_id)
{
    if (!page_id_is_valid(page_id))
    {
        printf("[PAGE] Invalid page ID: %d\n", (int)page_id);
        return;
    }

    /* 防止重复跳转到当前页面 */
    if (page_id == g_page_manager.current_page)
    {
        return;
    }

    /* 检查目标页面是否有创建函数 */
    if (page_create_table[page_id] == NULL)
    {
        printf("[PAGE] Page has no create callback: %d\n",
               (int)page_id);
        return;
    }

    /*
     * 保存当前页面到历史记录。
     * 栈满时暂不压入，避免数组越界。
     */
    if (g_page_manager.history_top < PAGE_ID_MAX)
    {
        g_page_manager.history[g_page_manager.history_top] =
            g_page_manager.current_page;

        g_page_manager.history_top++;
    }

    /* 切换到新页面前，清理旧屏幕对象 */
    lv_obj_clean(lv_scr_act());

    /* 更新当前页面 ID */
    g_page_manager.current_page = page_id;

    //切屏幕方向
    apply_orientation(page_id);

    /* 创建目标页面 */
    page_create_table[page_id]();

    printf("[PAGE] Navigate to page: %d\n", (int)page_id);
}

/* 返回上一个页面 */
void page_manager_back(void)
{
    if (g_page_manager.history_top == 0)
    {
        printf("[PAGE] No previous page\n");
        return;
    }

    /* 从历史栈取出上一个页面 */
    g_page_manager.history_top--;

    page_id_t previous_page =
        g_page_manager.history[g_page_manager.history_top];

    if (!page_id_is_valid(previous_page) ||
        page_create_table[previous_page] == NULL)
    {
        printf("[PAGE] Invalid history page\n");
        return;
    }

    /* 清理当前页面 */
    lv_obj_clean(lv_scr_act());

    /* 更新当前页面并重新创建 */
    g_page_manager.current_page = previous_page;

    apply_orientation(previous_page);
    
    page_create_table[previous_page]();

    printf("[PAGE] Back to page: %d\n", (int)previous_page);
}

/* 获取当前页面 ID */
page_id_t page_manager_get_current(void)
{
    return g_page_manager.current_page;
}