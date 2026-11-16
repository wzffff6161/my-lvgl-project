/**
 * @file    page_network.h
 * @brief   Network 网络页面
 *
 * 由 page_manager.c 调用。
 * 提供 WiFi 扫描、AP 列表显示、手动密码连接和连接状态显示。
 */

#ifndef PAGE_NETWORK_H
#define PAGE_NETWORK_H

#include "lvgl.h"

/* 创建 Network 网络页面 */
void page_network_create(void);

#endif /* PAGE_NETWORK_H */
