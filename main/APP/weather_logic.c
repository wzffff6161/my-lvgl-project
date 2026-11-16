#include "weather_logic.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "wifi_service.h"
#include "esp_event.h"
#include "esp_log.h"
#include "icon/project_icons.h"



static const char *TAG = "weather_logic";

/**
 * @brief  全局唯一天气数据实例
 */
static weather_info_t g_weather_info;

/**
 * @brief  后台任务句柄 + 数据更新标志
 */
static TaskHandle_t g_weather_task_hdl = NULL;
static volatile bool g_data_updated = false;
static bool g_initialized = false;   /* 默认数据只初始化一次，避免重进页面被重置 */

/**
 * @brief  WMO天气编码映射表
 */
typedef struct {
    int code;
    const char *desc;
    const lv_img_dsc_t *icon;
} weather_code_map_t;

static const weather_code_map_t g_weather_code_map[] = {
    {0,   "晴",      &sun},            // 0 = 晴天
    {1,   "多云",    &Cloudy},         // 1 = 大部多云
    {2,   "多云",    &Cloudy},         // 2 = 局部多云
    {3,   "阴",      &Cloudy},         // 3 = 阴天
    {45,  "雾",      &Cloudy},         // 45 = 雾（暂无图标，暂用多云代替）
    {48,  "雾凇",    &Cloudy},         // 48 = 雾凇
    {51,  "小雨",    &Light_rain},     // 51 = 小毛毛雨
    {53,  "小雨",    &Light_rain},     // 53 = 毛毛雨
    {55,  "中雨",    &moderate_rain},  // 55 = 大毛毛雨
    {61,  "小雨",    &Light_rain},     // 61 = 小雨
    {63,  "中雨",    &moderate_rain},  // 63 = 中雨
    {65,  "大雨",    &heavy_rain},     // 65 = 大雨
    {80,  "阵雨",    &shoers},         // 80 = 小阵雨
    {81,  "阵雨",    &shoers},         // 81 = 阵雨
    {82,  "暴雨",    &torrential_rain},// 82 = 强阵雨/暴雨
};


static void get_weather_by_code(int code, const char **out_desc, const lv_img_dsc_t **out_icon);

static void weather_ip_event_cb(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void weather_task(void *pvParameters);

/**
 * @brief  初始化默认天气数据
 */
void weather_logic_init(void)
{
    /* 只初始化一次：重进天气页时不要再把时间/天气数据重置成默认值，
       否则刚进入会先显示占位时间、再跳变到实时时间（“卡一下”）。 */
    if (g_initialized) {
        return;
    }
    g_initialized = true;

    g_weather_info.city = "广东·深圳";
    g_weather_info.online_status = "·已联网";
    g_weather_info.time_now = "14:25:30";
    g_weather_info.temp_now = "26°C";
    g_weather_info.date = "2026年10月15日";
    g_weather_info.weekday = "星期四";
    g_weather_info.timezone = "UTC+8";
    g_weather_info.update_time = "更新 14:25 · Open-Meteo 网格预报";


    g_weather_info.today.title = "今日天气";
    g_weather_info.today.temp_range = "24~29°C";
    g_weather_info.today.weather_desc = "多云";
    g_weather_info.today.rain_prob = "降水20%";
    g_weather_info.today.icon = &Cloudy;

    g_weather_info.tomorrow.title = "明日天气";
    g_weather_info.tomorrow.temp_range = "25~30°C";
    g_weather_info.tomorrow.weather_desc = "晴";
    g_weather_info.tomorrow.rain_prob = "降水0%";
    g_weather_info.tomorrow.icon = &sun;
}

const weather_info_t* weather_logic_get_info(void)
{
    return &g_weather_info;
}

void weather_logic_update_time(const char *time_str)
{
    g_weather_info.time_now = time_str;
}

/**
 * @brief  供界面每秒调用：用当前本地时间刷新 time_now/date/weekday
 * @note   NTP 尚未同步（系统时间仍为 1970 epoch）时不更新，保持默认占位值
 */
void weather_logic_refresh_local_time(void)
{
    time_t now = time(NULL);
    if (now < 100000000) return;   // 时间未授时，保持默认

    struct tm local_tm;
    localtime_r(&now, &local_tm);  // 用可重入版本，避免与后台任务并发读静态缓冲区

    static char time_str[16];
    static char date_str[32];
    static char weekday_str[16];   // "星期X"=9字节+NUL，16留足余量

    strftime(time_str, sizeof(time_str), "%H:%M:%S", &local_tm);
    strftime(date_str, sizeof(date_str), "%Y年%m月%d日", &local_tm);

    const char *weekdays[] = {"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
    snprintf(weekday_str, sizeof(weekday_str), "%s", weekdays[local_tm.tm_wday]);

    g_weather_info.time_now = time_str;
    g_weather_info.date = date_str;
    g_weather_info.weekday = weekday_str;
}

static void get_weather_by_code(int code, const char **out_desc, const lv_img_dsc_t **out_icon)
{
    int cnt = sizeof(g_weather_code_map) / sizeof(g_weather_code_map[0]);
    for (int i = 0; i < cnt; i++) {
        if (g_weather_code_map[i].code == code) {
            *out_desc = g_weather_code_map[i].desc;
            *out_icon = g_weather_code_map[i].icon;
            return;
        }
    }
    *out_desc = "多云";
    *out_icon = &Cloudy;
}

/**
 * @brief  NTP网络授时
 */
void weather_logic_sync_ntp_time(void)
{
    // 设置东八区时区
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "ntp1.aliyun.com");
    esp_sntp_init();

    // 等待同步
    time_t now = 0;
    int retry = 0;
    while (now < 100000000 && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(100));
        time(&now);
        retry++;
    }

    // 格式化时间日期
    struct tm *local_tm = localtime(&now);
    static char time_str[16];
    static char date_str[32];
    static char weekday_str[10];

    strftime(time_str, sizeof(time_str), "%H:%M:%S", local_tm);
    strftime(date_str, sizeof(date_str), "%Y年%m月%d日", local_tm);
    
    const char *weekdays[] = {"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
    snprintf(weekday_str, sizeof(weekday_str), "%s", weekdays[local_tm->tm_wday]);

    g_weather_info.time_now = time_str;
    g_weather_info.date = date_str;
    g_weather_info.weekday = weekday_str;
}

/**
 * @brief  Open-Meteo天气拉取
 */
int weather_logic_refresh_weather(void)
{
    const char *url = "http://api.open-meteo.com/v1/forecast?"
                     "latitude=22.54&longitude=114.06"
                     "&current=temperature_2m,weather_code,precipitation_probability"
                     "&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max"
                     "&timezone=Asia/Shanghai&forecast_days=2";

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,  // 超时从500ms改到5000ms，避免网络慢导致请求失败
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return -1;
    }

    // 不能用 esp_http_client_perform()：它内部会把响应体读完并关闭连接，
    // 之后再 read() 只能读到 0 字节，导致 JSON 解析为空、数据永远是默认值。
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed, err=%d", err);
        esp_http_client_cleanup(client);
        return -2;
    }

    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "HTTP content_length=%d", content_length);

    // 循环读取完整响应（static 避免占用 2KB 任务栈，weather_task 栈本就不大）
    static char recv_buf[2048];
    int total_len = 0;
    int read_len = 0;
    while (total_len < (int)sizeof(recv_buf) - 1) {
        read_len = esp_http_client_read(client, recv_buf + total_len,
                                       sizeof(recv_buf) - 1 - total_len);
        if (read_len <= 0) break;
        total_len += read_len;
    }
    recv_buf[total_len] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP status=%d, body len=%d", status, total_len);
        return -2;
    }
    if (total_len >= (int)sizeof(recv_buf) - 1) {
        ESP_LOGW(TAG, "HTTP body truncated at %d bytes", total_len);
    }

    // 解析JSON
    cJSON *root = cJSON_Parse(recv_buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed: %.200s", recv_buf);
        return -3;
    }

    cJSON *current = cJSON_GetObjectItem(root, "current");
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (!current || !daily) {
        ESP_LOGE(TAG, "JSON missing current/daily");
        cJSON_Delete(root);
        return -4;
    }

    // 逐项判空，避免字段缺失时对 NULL 解引用直接崩溃
    cJSON *temp_now_item = cJSON_GetObjectItem(current, "temperature_2m");
    if (!temp_now_item) {
        ESP_LOGE(TAG, "JSON missing current.temperature_2m");
        cJSON_Delete(root);
        return -4;
    }
    float temp_now = temp_now_item->valuedouble;

    cJSON *temp_max_arr = cJSON_GetObjectItem(daily, "temperature_2m_max");
    cJSON *temp_min_arr = cJSON_GetObjectItem(daily, "temperature_2m_min");
    cJSON *code_arr = cJSON_GetObjectItem(daily, "weather_code");
    cJSON *rain_arr = cJSON_GetObjectItem(daily, "precipitation_probability_max");
    if (!temp_max_arr || !temp_min_arr || !code_arr || !rain_arr) {
        ESP_LOGE(TAG, "JSON missing daily arrays");
        cJSON_Delete(root);
        return -4;
    }

    // 今日
    cJSON *t_max0_item = cJSON_GetArrayItem(temp_max_arr, 0);
    cJSON *t_min0_item = cJSON_GetArrayItem(temp_min_arr, 0);
    cJSON *code0_item = cJSON_GetArrayItem(code_arr, 0);
    cJSON *rain0_item = cJSON_GetArrayItem(rain_arr, 0);
    // 明日
    cJSON *t_max1_item = cJSON_GetArrayItem(temp_max_arr, 1);
    cJSON *t_min1_item = cJSON_GetArrayItem(temp_min_arr, 1);
    cJSON *code1_item = cJSON_GetArrayItem(code_arr, 1);
    cJSON *rain1_item = cJSON_GetArrayItem(rain_arr, 1);
    if (!t_max0_item || !t_min0_item || !code0_item || !rain0_item ||
        !t_max1_item || !t_min1_item || !code1_item || !rain1_item) {
        ESP_LOGE(TAG, "JSON daily array items missing");
        cJSON_Delete(root);
        return -4;
    }

    float t_max0 = t_max0_item->valuedouble;
    float t_min0 = t_min0_item->valuedouble;
    int code0 = code0_item->valueint;
    int rain0 = rain0_item->valueint;
    float t_max1 = t_max1_item->valuedouble;
    float t_min1 = t_min1_item->valuedouble;
    int code1 = code1_item->valueint;
    int rain1 = rain1_item->valueint;

    cJSON_Delete(root);

    // 更新全局数据
    static char temp_now_str[16];
    snprintf(temp_now_str, sizeof(temp_now_str), "%.0f°C", temp_now);
    g_weather_info.temp_now = temp_now_str;

    // ========== 新增：今日天气 + 图标同步更新 ==========
    const char *desc0 = NULL;
    const lv_img_dsc_t *icon0 = NULL;
    get_weather_by_code(code0, &desc0, &icon0);
    
    static char temp_range0[32];
    snprintf(temp_range0, sizeof(temp_range0), "%.0f~%.0f°C", t_min0, t_max0);
    g_weather_info.today.temp_range = temp_range0;
    g_weather_info.today.weather_desc = desc0;
    g_weather_info.today.icon = icon0;  // 赋值图标
    
    static char rain0_str[16];
    snprintf(rain0_str, sizeof(rain0_str), "降水%d%%", rain0);
    g_weather_info.today.rain_prob = rain0_str;

    // ========== 新增：明日天气 + 图标同步更新 ==========
    const char *desc1 = NULL;
    const lv_img_dsc_t *icon1 = NULL;
    get_weather_by_code(code1, &desc1, &icon1);
    
    static char temp_range1[32];
    snprintf(temp_range1, sizeof(temp_range1), "%.0f~%.0f°C", t_min1, t_max1);
    g_weather_info.tomorrow.temp_range = temp_range1;
    g_weather_info.tomorrow.weather_desc = desc1;
    g_weather_info.tomorrow.icon = icon1;  // 赋值图标
    
    static char rain1_str[16];
    snprintf(rain1_str, sizeof(rain1_str), "降水%d%%", rain1);
    g_weather_info.tomorrow.rain_prob = rain1_str;

    // 更新时间戳
    static char update_str[64];
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&now);
    snprintf(update_str, sizeof(update_str), "更新 %02d:%02d · Open-Meteo 网格预报",
             local_tm->tm_hour, local_tm->tm_min);
    g_weather_info.update_time = update_str;

    ESP_LOGI(TAG, "weather refreshed ok");
    return 0;
}


/**
 * @brief  IP事件回调：WiFi连上后通知后台任务
 */
static void weather_ip_event_cb(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xTaskNotifyGive(g_weather_task_hdl);
    }
}

/**
 * @brief  天气后台任务
 */
static void weather_task(void *pvParameters)
{
    // 注册IP事件
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                              weather_ip_event_cb, NULL);

    // 等待首次联网
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // 首次：同步时间 + 拉取天气
    weather_logic_sync_ntp_time();
    weather_logic_refresh_weather();
    g_data_updated = true;

    // 之后每30分钟重新拉取一次天气
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000));
        weather_logic_refresh_weather();
        g_data_updated = true;
    }
}

void weather_logic_start_async_task(void)
{
    if (g_weather_task_hdl != NULL) return;

    // 栈从 4096 提到 8192：HTTP 库内部调用链 + 响应解析需要较多栈空间
    xTaskCreate(weather_task, "weather_task", 8192,
                NULL, 3, &g_weather_task_hdl);

    // 已联网则立即触发一次
    if (wifi_service_get_state() == WIFI_SERVICE_STATE_CONNECTED) {
        xTaskNotifyGive(g_weather_task_hdl);
    }
}

bool weather_logic_has_update(void)
{
    return g_data_updated;
}

void weather_logic_clear_update_flag(void)
{
    g_data_updated = false;
}
