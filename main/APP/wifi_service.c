/**
 * @file    wifi_service.c
 * @brief   WiFi 业务层实现
 *
 * 说明：
 * 1. 内部状态（状态机、AP 列表、IP）用 FreeRTOS 互斥锁保护，
 *    因为 esp_event 回调运行在系统事件任务，而页面在 LVGL 任务里读取。
 * 2. 事件回调里只更新内部状态，绝不调用 LVGL，保证跨线程安全。
 * 3. 扫描使用非阻塞方式（block = false），避免卡住 LVGL 界面。
 */

#include "wifi_service.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "wifi_service"

/* 最多保存的 AP 数量 */
#define WIFI_SERVICE_MAX_AP 20

/*========== 内部状态（互斥锁保护）==========*/
static bool g_inited = false;                                   // WiFi服务是否已经初始化（幂等，防止重复初始化）               
static wifi_service_state_t g_state = WIFI_SERVICE_STATE_IDLE;  // WiFi状态机     
static wifi_ap_record_t g_ap_list[WIFI_SERVICE_MAX_AP];         // 扫描结果缓存数组，ESP‑IDF原生AP结构体
static uint16_t g_ap_count = 0;                                 // 实际扫描到WiFi热点数量
static char g_ip[16] = {0};                                     // 保存获取到的IP地址字符串 "192.168.x.x"
static SemaphoreHandle_t g_mutex = NULL;                        // FreeRTOS互斥锁
 
/* 事件回调：WIFI_EVENT */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    // 事件1：WIFI_EVENT_SCAN_DONE ————【非阻塞扫描完成事件】
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        uint16_t number = WIFI_SERVICE_MAX_AP;

        xSemaphoreTake(g_mutex, portMAX_DELAY);

        memset(g_ap_list, 0, sizeof(g_ap_list));

        if (esp_wifi_scan_get_ap_records(&number, g_ap_list) == ESP_OK)
        {
            g_ap_count = number;
        }
        else
        {
            g_ap_count = 0;
        }

        g_state = WIFI_SERVICE_STATE_SCAN_DONE;

        xSemaphoreGive(g_mutex);

        ESP_LOGI(TAG, "Scan done, %u APs found", g_ap_count);
    }
    // 事件2：WIFI_EVENT_STA_CONNECTED：WiFi链路层握手成功（还没有IP！）
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_state = WIFI_SERVICE_STATE_CONNECTING; /* 链路已通，等待 IP */
        xSemaphoreGive(g_mutex);

        ESP_LOGI(TAG, "STA connected, waiting for IP");
    }
    // 事件3：WIFI_EVENT_STA_DISCONNECTED：WiFi断开
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d =
            (wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = (d != NULL) ? d->reason : 0xFF;

        xSemaphoreTake(g_mutex, portMAX_DELAY);

        /* ASSOC_LEAVE 表示用户主动断开，其余视为连接失败 / 掉线 */
        if (reason == WIFI_REASON_ASSOC_LEAVE)
        {
            g_state = WIFI_SERVICE_STATE_IDLE;
            g_ip[0] = '\0';
        }
        else
        {
            g_state = WIFI_SERVICE_STATE_FAILED;
        }

        xSemaphoreGive(g_mutex);

        ESP_LOGI(TAG, "STA disconnected, reason=%u", reason);
    }
}

/* 事件回调：IP_EVENT */
static void ip_event_handler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        if (event == NULL)
        {
            return;
        }

        xSemaphoreTake(g_mutex, portMAX_DELAY);

        esp_ip4addr_ntoa(&event->ip_info.ip, g_ip, sizeof(g_ip));
        g_state = WIFI_SERVICE_STATE_CONNECTED;

        xSemaphoreGive(g_mutex);

        ESP_LOGI(TAG, "Got IP: %s", g_ip);
    }
}

/* 初始化 WiFi（幂等） */
void wifi_service_init(void)
{
    if (g_inited)
    {
        return;
    }

    if (g_mutex == NULL)
    {
        g_mutex = xSemaphoreCreateMutex();
    }

    /* 网卡初始化 */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 创建默认事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 创建 STA 网卡 */
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    /* WiFi 配置初始化 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 设置 STA 模式 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* 注册事件回调 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               ip_event_handler, NULL));

    /* 启动 WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    g_inited = true;

    ESP_LOGI(TAG, "WiFi service initialized");
}

/* 启动一次非阻塞扫描 */
bool wifi_service_scan_start(void)
{
    if (!g_inited)
    {
        wifi_service_init();
    }

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_state = WIFI_SERVICE_STATE_SCANNING;
    g_ap_count = 0;
    xSemaphoreGive(g_mutex);

    return esp_wifi_scan_start(NULL, false) == ESP_OK;
}

/* 获取当前状态 */
wifi_service_state_t wifi_service_get_state(void)
{
    wifi_service_state_t state;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    state = g_state;
    xSemaphoreGive(g_mutex);

    return state;
}

/* 获取 AP 数量 */
uint16_t wifi_service_get_ap_count(void)
{
    uint16_t count;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    count = g_ap_count;
    xSemaphoreGive(g_mutex);

    return count;
}

/* 按索引拷贝一条 AP 记录 */
bool wifi_service_get_ap(uint16_t idx, wifi_ap_record_t *out)
{
    bool ok = false;

    if (out == NULL)
    {
        return false;
    }

    xSemaphoreTake(g_mutex, portMAX_DELAY);

    if (idx < g_ap_count)
    {
        *out = g_ap_list[idx];
        ok = true;
    }

    xSemaphoreGive(g_mutex);

    return ok;
}

/* 连接指定 AP */
bool wifi_service_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0')
    {
        return false;
    }

    if (password == NULL)
    {
        password = "";
    }

    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid, ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password,
            sizeof(wifi_config.sta.password) - 1);

    /*
     * 若已连接，set_config 会返回 ESP_ERR_WIFI_CONN，
     * 因此先断开旧连接再设置新配置。
     */
    esp_wifi_disconnect();

    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK)
    {
        return false;
    }

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_state = WIFI_SERVICE_STATE_CONNECTING;
    g_ip[0] = '\0';
    xSemaphoreGive(g_mutex);

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    return esp_wifi_connect() == ESP_OK;
}

/* 断开当前连接 */
bool wifi_service_disconnect(void)
{
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_state = WIFI_SERVICE_STATE_IDLE;
    g_ip[0] = '\0';
    xSemaphoreGive(g_mutex);

    return esp_wifi_disconnect() == ESP_OK;
}

/* 获取当前 IP 字符串 */
void wifi_service_get_ip(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return;
    }

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    strncpy(buf, g_ip, len - 1);
    buf[len - 1] = '\0';
    xSemaphoreGive(g_mutex);
}
