/*封装标题、网格容器、按钮、点击事件注册等通用操作，统一界面样式和布局规则。
它解决的是：多个页面如何复用同一套控件创建方式。*/
#include "ui_common.h"

/* 统一的 UI 配色 */
#define UI_COLOR_BACKGROUND  0xF2F5F9
#define UI_COLOR_PRIMARY     0x2878C8
#define UI_COLOR_PRESSED     0x185A9D
#define UI_COLOR_TEXT        0x203050

/* Grid 布局描述数组必须长期有效，不能定义成局部变量 */
static const lv_coord_t grid_col_dsc[] =
{
    LV_GRID_FR(1),
    LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST
};

/* 当前通用布局最多提供 4 行，可容纳 8 个双列入口 */
static const lv_coord_t grid_row_dsc[] =
{
    LV_GRID_FR(1),
    LV_GRID_FR(1),
    LV_GRID_FR(1),
    LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST
};

/**
 * @brief 创建统一风格的页面标题
 */
lv_obj_t *ui_common_create_title(lv_obj_t *parent,
                                 const char *text)
{
    if (parent == NULL || text == NULL)
    {
        return NULL;
    }

    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL)
    {
        return NULL;
    }

    lv_label_set_text(label, text);

    lv_obj_set_style_text_font(
        label, &lv_font_montserrat_20, LV_PART_MAIN);

    lv_obj_set_style_text_color(
        label, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);

    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    return label;
}

/**
 * @brief 创建双列 Grid 布局容器
 */
lv_obj_t *ui_common_create_grid(lv_obj_t *parent,
                                lv_coord_t width,
                                lv_coord_t height)
{
    if (parent == NULL || width <= 0 || height <= 0)
    {
        return NULL;
    }

    lv_obj_t *grid = lv_obj_create(parent);

    if (grid == NULL)
    {
        return NULL;
    }

    lv_obj_set_size(grid, width, height);

    /* 去掉容器默认内边距，让布局尺寸更容易控制 */
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
    //单元格行间距
    lv_obj_set_style_pad_row(grid, 8, LV_PART_MAIN);
    //单元格列间距
    lv_obj_set_style_pad_column(grid, 8, LV_PART_MAIN);

    /* 容器本身透明，不显示边框 */
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);

    /* 启用 LVGL V8 Grid 布局 */
    lv_obj_set_grid_dsc_array(grid, grid_col_dsc, grid_row_dsc);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);

    /* 放在标题下方 */
    lv_obj_align(grid, LV_ALIGN_CENTER, 0, 35);

    return grid;
}

/**
 * @brief 创建统一风格的功能按钮
 */
lv_obj_t *ui_common_create_button(lv_obj_t *parent,
                                  const char *text)
{
    if (parent == NULL || text == NULL)
    {
        return NULL;
    }

    lv_obj_t *button = lv_btn_create(parent);

    if (button == NULL)
    {
        return NULL;
    }
    
    //常态背景色
    lv_obj_set_style_bg_color(
        button, lv_color_hex(UI_COLOR_PRIMARY), LV_PART_MAIN);

    //按下状态背景色
    lv_obj_set_style_bg_color(
        button, lv_color_hex(UI_COLOR_PRESSED),
        LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);

    /* 按钮宽高由 Grid 单元格决定 */
    lv_obj_set_size(button, LV_PCT(100), LV_PCT(100));

    lv_obj_t *label = lv_label_create(button);

    if (label == NULL)
    {
        lv_obj_del(button);
        return NULL;
    }

    lv_label_set_text(label, text);
    lv_obj_center(label);

    return button;
}

/**
 * @brief 为按钮设置点击事件
 */
void ui_common_set_click_event(lv_obj_t *button,
                               lv_event_cb_t callback,
                               void *user_data)
{
    if (button == NULL || callback == NULL)
    {
        return;
    }

    lv_obj_add_event_cb(
        button, callback, LV_EVENT_CLICKED, user_data);
}
