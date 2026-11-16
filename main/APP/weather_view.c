#include "weather_view.h"
#include "weather_logic.h"
#include "ui_common.h"
#include "lvgl.h"
#include <stdio.h>
#include "chinese_font.h"
#include "page_manager.h"
#include "icon/project_icons.h"

// ========== 样式常量 ==========
#define COLOR_PAGE_BG lv_color_hex(0xFFFFFF)
#define COLOR_CARD_BG lv_color_hex(0xE2F0FB)
#define COLOR_TEXT_MAIN lv_color_hex(0x203050)
#define COLOR_TEXT_SUB lv_color_hex(0x333333)
#define COLOR_ONLINE lv_color_hex(0x2ECC71)
#define COLOR_TEXT_ACCENT lv_color_hex(0x3E7FC4)  /* 中等蓝，不过深也不过浅 */

// ========== 工具函数：清除容器默认边框/阴影/背景/内边距 ==========
static void lv_obj_clear_frame(lv_obj_t *obj)
{
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_0, LV_PART_MAIN);
    // 默认主题会给普通 lv_obj 加 card 样式的内边距(PAD_DEF 16~24px)，
    // 固定高度的栏若不清零，16px 中文文字的下半部分会被裁切。
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

// ========== 内部函数声明 ==========
static lv_obj_t *create_top_bar(lv_obj_t *parent);
static lv_obj_t *create_core_time_temp(lv_obj_t *parent);
static lv_obj_t *create_date_timezone_row(lv_obj_t *parent);
static lv_obj_t *create_weather_card(lv_obj_t *parent, const weather_day_t *day_data, int idx);
static lv_obj_t *create_double_cards(lv_obj_t *parent);
static lv_obj_t *create_bottom_bar(lv_obj_t *parent);
static void refresh_timer_cb(lv_timer_t *timer);
static void weather_view_delete_cb(lv_event_t *e);
static void weather_back_btn_cb(lv_event_t *e);

// 页面根对象（刷新用）
static lv_obj_t *g_root = NULL;
static lv_timer_t *g_refresh_timer = NULL;

// ========== 卡片控件指针（供定时刷新更新） ==========
typedef struct {
    lv_obj_t *title;
    lv_obj_t *temp;
    lv_obj_t *icon;
    lv_obj_t *desc;
    lv_obj_t *rain;
} weather_card_widgets_t;

static weather_card_widgets_t g_card_widgets[2];  // [0]=今日 [1]=明日

static void weather_card_widgets_update(weather_card_widgets_t *w, const weather_day_t *d)
{
    lv_label_set_text(w->title, d->title);
    lv_label_set_text(w->temp,  d->temp_range);
    lv_img_set_src(w->icon,     d->icon);
    lv_label_set_text(w->desc,  d->weather_desc);
    lv_label_set_text(w->rain,  d->rain_prob);
}

// 页面被销毁时清理定时器，避免残留定时器访问已删除的 g_root
static void weather_view_delete_cb(lv_event_t *e)
{
    if (g_refresh_timer != NULL) {
        lv_timer_del(g_refresh_timer);
        g_refresh_timer = NULL;
    }
    g_root = NULL;
}

// 返回按钮：返回主页
static void weather_back_btn_cb(lv_event_t *e)
{
    (void)e;
    page_manager_back();
}

// ========== 页面入口 ==========
void weather_view_create(void)
{
    weather_logic_init();
    weather_logic_refresh_local_time();   /* 进入页面立刻刷成当前时间，避免先显示占位值 */

    lv_obj_t *scr = lv_scr_act();
    g_root = lv_obj_create(scr);
    lv_obj_set_size(g_root, 480, 320);
    lv_obj_center(g_root);
    lv_obj_set_flex_flow(g_root, LV_FLEX_FLOW_COLUMN);
    // 全局内边距收窄到6，行间距收窄到2，大幅节省纵向空间
    lv_obj_set_style_pad_all(g_root, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_root, 2, LV_PART_MAIN);
    // 预烘焙毛玻璃背景：柔和淡蓝渐变+柔光斑，已做误差扩散，无断层
    lv_obj_set_style_bg_img_src(g_root, &weather_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_img_opa(g_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(g_root, LV_DIR_NONE);

    create_top_bar(g_root);
    create_core_time_temp(g_root);
    create_date_timezone_row(g_root);
    create_double_cards(g_root);
    create_bottom_bar(g_root);

    // 页面被清理时删除定时器（lv_obj_clean 会触发 g_root 的 LV_EVENT_DELETE）
    lv_obj_add_event_cb(g_root, weather_view_delete_cb, LV_EVENT_DELETE, NULL);

    // 启动异步天气任务
    weather_logic_start_async_task();
    // 1秒定时器：检查数据更新并刷新界面
    g_refresh_timer = lv_timer_create(refresh_timer_cb, 1000, NULL);
}

// ========== 定时刷新回调 ==========
static void refresh_timer_cb(lv_timer_t *timer)
{
    if (g_root == NULL) return;

    // 每秒刷新本地时间/日期（NTP 同步前保持默认占位值）
    weather_logic_refresh_local_time();
    const weather_info_t *info = weather_logic_get_info();

    // 更新时间
    lv_obj_t *core_row = lv_obj_get_child(g_root, 1);
    lv_label_set_text(lv_obj_get_child(core_row, 0), info->time_now);

    // 更新日期
    lv_obj_t *date_row = lv_obj_get_child(g_root, 2);
    char date_buf[64];
    snprintf(date_buf, sizeof(date_buf), "%s  %s", info->date, info->weekday);
    lv_label_set_text(lv_obj_get_child(date_row, 0), date_buf);

    // 只有后台拉到新数据时才刷新城市/气温/卡片
    if (!weather_logic_has_update())
        return;

    // 更新顶部城市
    lv_obj_t *top_bar = lv_obj_get_child(g_root, 0);
    lv_label_set_text(lv_obj_get_child(top_bar, 0), info->city);

    // 更新气温
    lv_obj_t *temp_col = lv_obj_get_child(core_row, 1);
    lv_label_set_text(lv_obj_get_child(temp_col, 1), info->temp_now);

    // 更新两张天气卡片
    weather_card_widgets_update(&g_card_widgets[0], &info->today);
    weather_card_widgets_update(&g_card_widgets[1], &info->tomorrow);

    // 全页重绘保证卡片数据同步
    lv_obj_invalidate(lv_scr_act());
    weather_logic_clear_update_flag();
}

// ========== 1. 顶部状态栏 ==========
static lv_obj_t *create_top_bar(lv_obj_t *parent)
{
    const weather_info_t *info = weather_logic_get_info();

    lv_obj_t *bar = lv_obj_create(parent);
    // 高度压缩到30，16号中文足够显示
    lv_obj_set_size(bar, LV_PCT(100), 30);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(bar, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_frame(bar);
    lv_obj_set_scroll_dir(bar, LV_DIR_NONE);

    lv_obj_t *lab_city = lv_label_create(bar);
    lv_label_set_text(lab_city, info->city);
    lv_obj_set_style_text_font(lab_city, &lv_font_chinese_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_city, COLOR_TEXT_MAIN, LV_PART_MAIN);

    lv_obj_t *lab_online = lv_label_create(bar);
    lv_label_set_text(lab_online, info->online_status);
    lv_obj_set_style_text_font(lab_online, &lv_font_chinese_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_online, COLOR_ONLINE, LV_PART_MAIN);

    return bar;
}

// ========== 2. 核心时间+气温区 ==========
static lv_obj_t *create_core_time_temp(lv_obj_t *parent)
{
    const weather_info_t *info = weather_logic_get_info();

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 82);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);

    // 焦点色块：淡蓝渐变圆角块，作为页面视觉中心；玻璃高光边 + 柔和阴影
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(row, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_outline_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xD5E7FA), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(row, lv_color_hex(0xEDF5FD), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_VER, LV_PART_MAIN);
    // 渐变默认不抖动，RGB565 下会出现色阶断层；显式开启误差扩散让过渡更细腻
    lv_obj_set_style_bg_dither_mode(row, LV_DITHER_ERR_DIFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(row, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(row, lv_color_hex(0x7FA8D0), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(row, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 18, LV_PART_MAIN);

    // 左侧大时间
    lv_obj_t *lab_time = lv_label_create(row);
    lv_label_set_text(lab_time, info->time_now);
    lv_obj_set_style_text_font(lab_time, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_time, COLOR_TEXT_MAIN, LV_PART_MAIN);

    // 右侧气温竖排
    lv_obj_t *temp_col = lv_obj_create(row);
    lv_obj_set_size(temp_col, 130, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(temp_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(temp_col, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(temp_col, LV_FLEX_ALIGN_END, LV_PART_MAIN);
    lv_obj_clear_frame(temp_col);
    lv_obj_set_scroll_dir(temp_col, LV_DIR_NONE);

    lv_obj_t *lab_temp_title = lv_label_create(temp_col);
    lv_label_set_text(lab_temp_title, "当前气温");
    lv_obj_set_style_text_font(lab_temp_title, &lv_font_chinese_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_temp_title, COLOR_TEXT_SUB, LV_PART_MAIN);

    lv_obj_t *lab_temp_val = lv_label_create(temp_col);
    lv_label_set_text(lab_temp_val, info->temp_now);
    lv_obj_set_style_text_font(lab_temp_val, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_temp_val, COLOR_TEXT_ACCENT, LV_PART_MAIN);

    return row;
}

// ========== 3. 日期+时区行 ==========
static lv_obj_t *create_date_timezone_row(lv_obj_t *parent)
{
    const weather_info_t *info = weather_logic_get_info();

    lv_obj_t *row = lv_obj_create(parent);
    // 保持30高度，确保日期、星期文字完整不裁切
    lv_obj_set_size(row, LV_PCT(100), 30);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_frame(row);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);

    char date_buf[64];
    snprintf(date_buf, sizeof(date_buf), "%s  %s", info->date, info->weekday);
    lv_obj_t *lab_date = lv_label_create(row);
    lv_label_set_text(lab_date, date_buf);
    lv_obj_set_style_text_color(lab_date, COLOR_TEXT_SUB, LV_PART_MAIN);
    lv_obj_set_style_text_font(lab_date, &lv_font_chinese_16, LV_PART_MAIN);

    lv_obj_t *lab_tz = lv_label_create(row);
    lv_label_set_text(lab_tz, info->timezone);
    lv_obj_set_style_text_color(lab_tz, COLOR_TEXT_SUB, LV_PART_MAIN);
    lv_obj_set_style_text_font(lab_tz, &lv_font_montserrat_12, LV_PART_MAIN);

    return row;
}

// ========== 4. 通用天气卡片 ==========
static lv_obj_t *create_weather_card(lv_obj_t *parent, const weather_day_t *day_data, int idx)
{
    lv_obj_t *card = lv_obj_create(parent);

    // 玻璃拟态卡片：半透明白 + 细白高光边 + 柔和阴影
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_outline_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(card, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x7FA8D0), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, LV_PART_MAIN);
    // 卡片内边距
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 7, LV_PART_MAIN);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(card, LV_FLEX_ALIGN_START, LV_PART_MAIN);
    lv_obj_set_scroll_dir(card, LV_DIR_NONE);

    // 标题
    lv_obj_t *lab_title = lv_label_create(card);
    lv_label_set_text(lab_title, day_data->title);
    lv_obj_set_style_text_font(lab_title, &lv_font_chinese_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_title, COLOR_TEXT_MAIN, LV_PART_MAIN);

    // 温度+图标
    lv_obj_t *temp_row = lv_obj_create(card);
    lv_obj_set_size(temp_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(temp_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(temp_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(temp_row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_frame(temp_row);
    lv_obj_set_scroll_dir(temp_row, LV_DIR_NONE);

    lv_obj_t *lab_temp = lv_label_create(temp_row);
    lv_label_set_text(lab_temp, day_data->temp_range);
    lv_obj_set_style_text_font(lab_temp, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_temp, COLOR_TEXT_ACCENT, LV_PART_MAIN);

    lv_obj_t *img_icon = lv_img_create(temp_row);
    lv_img_set_src(img_icon, day_data->icon);


    // 描述+降水
    lv_obj_t *desc_row = lv_obj_create(card);
    lv_obj_set_size(desc_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(desc_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(desc_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(desc_row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_frame(desc_row);
    lv_obj_set_scroll_dir(desc_row, LV_DIR_NONE);

    lv_obj_t *lab_desc = lv_label_create(desc_row);
    lv_label_set_text(lab_desc, day_data->weather_desc);
    lv_obj_set_style_text_font(lab_desc, &lv_font_chinese_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_desc, COLOR_TEXT_SUB, LV_PART_MAIN);

    lv_obj_t *lab_rain = lv_label_create(desc_row);
    lv_label_set_text(lab_rain, day_data->rain_prob);
    lv_obj_set_style_text_font(lab_rain, &lv_font_chinese_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab_rain, COLOR_TEXT_SUB, LV_PART_MAIN);

    // 记录控件指针，供定时刷新更新
    g_card_widgets[idx].title = lab_title;
    g_card_widgets[idx].temp  = lab_temp;
    g_card_widgets[idx].icon  = img_icon;
    g_card_widgets[idx].desc  = lab_desc;
    g_card_widgets[idx].rain  = lab_rain;

    return card;
}

// ========== 5. 双卡片区域 ==========
static lv_obj_t *create_double_cards(lv_obj_t *parent)
{
    const weather_info_t *info = weather_logic_get_info();

    lv_obj_t *row = lv_obj_create(parent);
    // 高度压缩到110，给底部栏腾出空间
    lv_obj_set_size(row, LV_PCT(100), 110);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_PART_MAIN);
    lv_obj_clear_frame(row);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);

    lv_obj_t *card_today = create_weather_card(row, &info->today, 0);
    lv_obj_set_size(card_today, 210, LV_PCT(100));

    lv_obj_t *card_tomorrow = create_weather_card(row, &info->tomorrow, 1);
    lv_obj_set_size(card_tomorrow, 210, LV_PCT(100));

    return row;
}

// ========== 6. 底部更新栏 ==========
static lv_obj_t *create_bottom_bar(lv_obj_t *parent)
{
    const weather_info_t *info = weather_logic_get_info();

    lv_obj_t *bar = lv_obj_create(parent);
    // 保持24高度，确保文字完整显示
    lv_obj_set_size(bar, LV_PCT(100), 24);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_PART_MAIN);
    lv_obj_set_style_flex_cross_place(bar, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_clear_frame(bar);
    lv_obj_set_scroll_dir(bar, LV_DIR_NONE);

    lv_obj_t *lab_update = lv_label_create(bar);
    lv_label_set_text(lab_update, info->update_time);
    lv_obj_set_style_text_color(lab_update, COLOR_TEXT_SUB, LV_PART_MAIN);
    lv_obj_set_style_text_font(lab_update, &lv_font_chinese_16, LV_PART_MAIN);

    // 右下角返回按钮：返回主页
    lv_obj_t *btn_back = lv_btn_create(bar);
    lv_obj_set_size(btn_back, 56, 24);
    lv_obj_set_style_pad_top(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn_back, 4, LV_PART_MAIN);

    lv_obj_t *btn_label = lv_label_create(btn_back);
    lv_label_set_text(btn_label, "返回");
    lv_obj_set_style_text_font(btn_label, &lv_font_chinese_16, LV_PART_MAIN);
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(btn_back, weather_back_btn_cb, LV_EVENT_CLICKED, NULL);

    return bar;
}
