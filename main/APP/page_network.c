/**
 * @file    page_network.c
 * @brief   Network 网络页面
 *
 * 功能：
 * 1. 扫描附近 WiFi 并显示 AP 列表（SSID / 信号强度 / 是否加密）；
 * 2. 点击 AP 手动输入密码连接（开放网络直接连接）；
 * 3. 显示连接状态（Scanning / Connecting / Connected+IP / Failed）。
 *
 * 说明：
 * 页面通过 lv_timer 轮询 wifi_service 的状态并刷新 UI。
 * 所有 LVGL 调用都发生在轮询定时器（LVGL 任务）里，wifi_service 的
 * 事件回调只更新内部状态，二者靠互斥锁解耦，保证跨线程安全。
 */

#include "page_network.h"
#include "page_manager.h"
#include "ui_common.h"
#include "wifi_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*==========================================================
 * 常量与数据结构
 *==========================================================*/
#define NET_MAX_AP_ITEMS   20      /* 最多显示的 AP 数量 */
#define NET_POLL_PERIOD_MS 300     /* 轮询刷新周期（毫秒） */

typedef struct
{
    char ssid[33];          /* AP 名称 */
    wifi_auth_mode_t authmode; /* 认证模式，用于判断是否需要密码 */
} net_ap_item_t;

/*==========================================================
 * 静态状态
 *==========================================================*/
static net_ap_item_t g_ap_items[NET_MAX_AP_ITEMS];

static lv_obj_t *g_ap_list_obj = NULL;      /* AP 列表容器 */
static lv_obj_t *g_status_label = NULL;     /* 状态标签 */
static lv_timer_t *g_poll_timer = NULL;     /* 轮询定时器 */
static wifi_service_state_t g_prev_state = WIFI_SERVICE_STATE_IDLE;

static lv_obj_t *g_dialog = NULL;           /* 密码输入浮层 */
static lv_obj_t *g_dialog_ta = NULL;        /* 密码文本框 */
static char g_dialog_ssid[33];              /* 待连接的 SSID */

/*==========================================================
 * 状态标签辅助
 *==========================================================*/
static void net_set_status(const char *text)
{
    if (g_status_label != NULL)
    {
        lv_label_set_text(g_status_label, text);
    }
}

static void net_set_status_fmt(const char *fmt, ...)
{
    char buf[64];
    va_list args;

    if (g_status_label == NULL)
    {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    lv_label_set_text(g_status_label, buf);
}

/*==========================================================
 * 密码输入浮层
 *==========================================================*/
static void net_close_dialog(void)
{
    if (g_dialog != NULL)
    {
        lv_obj_del(g_dialog);
        g_dialog = NULL;
    }

    g_dialog_ta = NULL;
}

/* 键盘 OK 按钮：读取密码并连接 */
static void net_dialog_ready_cb(lv_event_t *e)
{
    if (g_dialog_ta == NULL)
    {
        return;
    }

    const char *password = lv_textarea_get_text(g_dialog_ta);

    wifi_service_connect(g_dialog_ssid, password);

    net_close_dialog();
    net_set_status("Connecting...");
}

/* 键盘关闭按钮：取消输入 */
static void net_dialog_cancel_cb(lv_event_t *e)
{
    net_close_dialog();
}

/* 弹出密码输入浮层 */
static void net_open_password_dialog(const char *ssid)
{
    if (ssid == NULL)
    {
        return;
    }

    snprintf(g_dialog_ssid, sizeof(g_dialog_ssid), "%s", ssid);

    /* 全屏遮罩容器 */
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());

    if (overlay == NULL)
    {
        return;
    }

    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0xF2F5F9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);

    g_dialog = overlay;

    /* 标题 */
    lv_obj_t *title = lv_label_create(overlay);
    if (title != NULL)
    {
        lv_label_set_text(title, "Enter Password");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    }

    /* 显示待连接的 SSID */
    lv_obj_t *ssid_label = lv_label_create(overlay);
    if (ssid_label != NULL)
    {
        lv_label_set_text_fmt(ssid_label, "SSID: %s", g_dialog_ssid);
        lv_obj_align(ssid_label, LV_ALIGN_TOP_MID, 0, 35);
    }

    /* 密码输入框 */
    lv_obj_t *ta = lv_textarea_create(overlay);
    if (ta != NULL)
    {
        lv_obj_set_size(ta, 340, 40);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 70);
        lv_textarea_set_password_mode(ta, true);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_placeholder_text(ta, "Password");
        lv_textarea_set_max_length(ta, 63);
        g_dialog_ta = ta;
    }

    /* 键盘（自带 OK / 关闭按钮），默认占据底部一半 */
    lv_obj_t *kb = lv_keyboard_create(overlay);
    if (kb != NULL)
    {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_add_event_cb(kb, net_dialog_ready_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(kb, net_dialog_cancel_cb, LV_EVENT_CANCEL, NULL);
    }
}

/*==========================================================
 * AP 列表
 *==========================================================*/
/* 点击某个 AP 行 */
static void net_ap_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    net_ap_item_t *item = (net_ap_item_t *)lv_event_get_user_data(e);

    if (item == NULL)
    {
        return;
    }

    if (item->authmode == WIFI_AUTH_OPEN)
    {
        /* 开放网络直接连接 */
        wifi_service_connect(item->ssid, "");
        net_set_status("Connecting...");
    }
    else
    {
        /* 加密网络弹出密码输入 */
        net_open_password_dialog(item->ssid);
    }
}

/* 创建单个 AP 行 */
static void net_create_ap_row(uint16_t idx, const wifi_ap_record_t *ap)
{
    net_ap_item_t *item = &g_ap_items[idx];

    snprintf(item->ssid, sizeof(item->ssid), "%s", (const char *)ap->ssid);
    item->authmode = ap->authmode;

    lv_obj_t *row = lv_btn_create(g_ap_list_obj);

    if (row == NULL)
    {
        return;
    }

    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);

    /* SSID 名称，过长时滚动显示 */
    lv_obj_t *ssid_label = lv_label_create(row);
    if (ssid_label != NULL)
    {
        lv_label_set_text(ssid_label, item->ssid);
        lv_obj_set_width(ssid_label, 240);
        lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }

    /* 信号强度 + 加密标记 */
    lv_obj_t *info_label = lv_label_create(row);
    if (info_label != NULL)
    {
        const char *lock = (ap->authmode == WIFI_AUTH_OPEN) ? "open" : "lock";
        lv_label_set_text_fmt(info_label, "%s  %ddBm", lock, ap->rssi);
    }

    lv_obj_add_event_cb(row, net_ap_click_cb, LV_EVENT_CLICKED, item);
}

/* 根据扫描结果重建 AP 列表 */
static void net_rebuild_ap_list(void)
{
    if (g_ap_list_obj == NULL)
    {
        return;
    }

    lv_obj_clean(g_ap_list_obj);

    uint16_t count = wifi_service_get_ap_count();

    if (count > NET_MAX_AP_ITEMS)
    {
        count = NET_MAX_AP_ITEMS;
    }

    for (uint16_t i = 0; i < count; i++)
    {
        wifi_ap_record_t ap;

        if (!wifi_service_get_ap(i, &ap))
        {
            continue;
        }

        net_create_ap_row(i, &ap);
    }
}

/*==========================================================
 * 状态刷新
 *==========================================================*/
static void net_refresh_ui(wifi_service_state_t state)
{
    switch (state)
    {
    case WIFI_SERVICE_STATE_SCANNING:
        net_set_status("Scanning...");
        break;

    case WIFI_SERVICE_STATE_SCAN_DONE:
        net_rebuild_ap_list();
        net_set_status_fmt("Found %u APs", wifi_service_get_ap_count());
        break;

    case WIFI_SERVICE_STATE_CONNECTING:
        net_set_status("Connecting...");
        break;

    case WIFI_SERVICE_STATE_CONNECTED:
    {
        char ip[16];
        wifi_service_get_ip(ip, sizeof(ip));
        net_set_status_fmt("Connected  %s", ip);
        break;
    }

    case WIFI_SERVICE_STATE_FAILED:
        net_set_status("Connect failed");
        break;

    default:
        break;
    }
}

/* 轮询定时器：状态变化时刷新 UI */
static void net_poll_timer_cb(lv_timer_t *timer)
{
    wifi_service_state_t state = wifi_service_get_state();

    if (state == g_prev_state)
    {
        return;
    }

    g_prev_state = state;
    net_refresh_ui(state);
}

/*==========================================================
 * 事件回调
 *==========================================================*/
static void net_scan_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        wifi_service_scan_start();
    }
}

static void net_disconnect_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        wifi_service_disconnect();
        net_set_status("Disconnected");
    }
}

static void net_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        page_manager_back();
    }
}

/*
 * 页面销毁回调：
 * page_manager 切换页面时用 lv_obj_clean 清理，只删对象不删定时器，
 * 因此这里手动删除轮询定时器，并清空静态指针，避免悬空引用。
 */
static void net_page_delete_cb(lv_event_t *e)
{
    if (g_poll_timer != NULL)
    {
        lv_timer_del(g_poll_timer);
        g_poll_timer = NULL;
    }

    g_ap_list_obj = NULL;
    g_status_label = NULL;
    g_dialog = NULL;
    g_dialog_ta = NULL;
    g_prev_state = WIFI_SERVICE_STATE_IDLE;
}

/*==========================================================
 * 创建 Network 页面
 *==========================================================*/
void page_network_create(void)
{
    lv_obj_t *screen = lv_scr_act();

    if (screen == NULL)
    {
        return;
    }

    /* 进入页面时初始化 WiFi（幂等） */
    wifi_service_init();

    /* 页面总容器 */
    lv_obj_t *container = lv_obj_create(screen);

    if (container == NULL)
    {
        return;
    }

    lv_obj_set_size(container, 460, 300);
    lv_obj_center(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(container, 8, LV_PART_MAIN);

    /* 页面销毁时清理定时器与静态指针 */
    lv_obj_add_event_cb(container, net_page_delete_cb, LV_EVENT_DELETE, NULL);

    /* 页面标题 */
    lv_obj_t *title = lv_label_create(container);
    if (title != NULL)
    {
        lv_label_set_text(title, "NETWORK");
        lv_obj_set_width(title, LV_PCT(100));
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    /* 按钮行：Scan + Disconnect */
    lv_obj_t *btn_row = lv_obj_create(container);
    if (btn_row != NULL)
    {
        lv_obj_set_size(btn_row, LV_PCT(100), 40);
        lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(btn_row, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);

        lv_obj_t *scan_btn = ui_common_create_button(btn_row, "Scan");
        if (scan_btn != NULL)
        {
            lv_obj_set_size(scan_btn, 100, 38);
            lv_obj_add_event_cb(scan_btn, net_scan_click_cb,
                                LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t *disc_btn = ui_common_create_button(btn_row, "Disconnect");
        if (disc_btn != NULL)
        {
            lv_obj_set_size(disc_btn, 120, 38);
            lv_obj_add_event_cb(disc_btn, net_disconnect_click_cb,
                                LV_EVENT_CLICKED, NULL);
        }
    }

    /* 状态标签 */
    g_status_label = lv_label_create(container);
    if (g_status_label != NULL)
    {
        lv_label_set_text(g_status_label, "Press Scan to search");
        lv_obj_set_width(g_status_label, LV_PCT(100));
    }

    /* AP 列表（可垂直滚动） */
    g_ap_list_obj = lv_obj_create(container);
    if (g_ap_list_obj != NULL)
    {
        lv_obj_set_size(g_ap_list_obj, LV_PCT(100), 120);
        lv_obj_set_flex_flow(g_ap_list_obj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(g_ap_list_obj, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(g_ap_list_obj, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_row(g_ap_list_obj, 6, LV_PART_MAIN);
        lv_obj_set_scroll_dir(g_ap_list_obj, LV_DIR_VER);
    }

    /* 返回按钮 */
    lv_obj_t *back_button = ui_common_create_button(container, "Back");
    if (back_button != NULL)
    {
        lv_obj_set_size(back_button, 100, 38);
        lv_obj_add_event_cb(back_button, net_back_event_cb,
                            LV_EVENT_CLICKED, NULL);
    }

    /* 轮询定时器：刷新扫描列表与连接状态 */
    g_poll_timer = lv_timer_create(net_poll_timer_cb, NET_POLL_PERIOD_MS, NULL);

    /* 初次进入时同步一次当前状态 */
    g_prev_state = wifi_service_get_state();
    net_refresh_ui(g_prev_state);
}
