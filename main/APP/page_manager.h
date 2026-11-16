#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "lvgl.h"

/* 页面统一编号 */
typedef enum
{
    PAGE_ID_MAIN = 0,
    PAGE_ID_DEVICE,
    PAGE_ID_SETTINGS,
    PAGE_ID_NETWORK,
    PAGE_ID_ABOUT,
    PAGE_ID_GAME,
    PAGE_ID_WEATHER,
    PAGE_ID_SAOLEI,
    PAGE_ID_MAX
} page_id_t;

/* 初始化页面管理模块 */
void page_manager_init(void);

/* 跳转到指定页面 */
void page_manager_navigate(page_id_t page_id);

/* 返回上一个页面 */
void page_manager_back(void);

/* 获取当前页面编号 */
page_id_t page_manager_get_current(void);

#endif