
#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"

/* 通用 UI 模块的错误码 */
typedef enum
{
    UI_COMMON_OK = 0,
    UI_COMMON_ERR_INVALID_ARG,
    UI_COMMON_ERR_NO_MEM
} ui_common_err_t;

/* 创建页面标题 */
lv_obj_t *ui_common_create_title(lv_obj_t *parent,
                                 const char *text);

/* 创建 Grid 布局容器 */
lv_obj_t *ui_common_create_grid(lv_obj_t *parent,
                                lv_coord_t width,
                                lv_coord_t height);

/* 创建统一风格的功能按钮 */
lv_obj_t *ui_common_create_button(lv_obj_t *parent,
                                  const char *text);

/* 设置按钮点击事件 */
void ui_common_set_click_event(lv_obj_t *button,
                               lv_event_cb_t callback,
                               void *user_data);

#endif /* UI_COMMON_H */
