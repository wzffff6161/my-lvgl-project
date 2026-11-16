
#ifndef PAGE_DEVICE_H
#define PAGE_DEVICE_H

#include "lvgl.h"

/*
 * @brief 创建 Device 设备控制页面
 *
 * 由 page_manager.c 调用。
 * 负责创建页面中的设备列表、开关和返回按钮。
 */
void page_device_create(void);

#endif /* PAGE_DEVICE_H */
