#ifndef __WEATHER_LOGIC_H__
#define __WEATHER_LOGIC_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"


/**
 * @brief  单日天气数据
 */
typedef struct {
    const char *title;
    const char *temp_range;
    const char *weather_desc;
    const char *rain_prob;
    const lv_img_dsc_t *icon;
} weather_day_t;

/**
 * @brief  完整天气信息结构体
 */
typedef struct {
    const char *city;
    const char *online_status;
    const char *time_now;
    const char *temp_now;
    const char *date;
    const char *weekday;
    const char *timezone;
    weather_day_t today;
    weather_day_t tomorrow;
    const char *update_time;
} weather_info_t;

/**
 * @brief  初始化天气数据（填充深圳本地默认值）
 */
void weather_logic_init(void);

/**
 * @brief  获取全局天气数据指针
 */
const weather_info_t* weather_logic_get_info(void);

/**
 * @brief  更新当前时间字符串
 */
void weather_logic_update_time(const char *time_str);

/**
 * @brief  供界面每秒调用：用当前本地时间刷新 time_now/date/weekday
 * @note   NTP 未同步前（系统时间仍为 1970）不更新，保持默认占位值
 */
void weather_logic_refresh_local_time(void);

/**
 * @brief  同步NTP网络时间
 * @note   必须在WiFi联网成功后调用
 */
void weather_logic_sync_ntp_time(void);

/**
 * @brief  从Open-Meteo拉取深圳实时天气
 * @retval 0 成功，其他 失败
 */
int weather_logic_refresh_weather(void);

/**
 * @brief  启动天气异步后台任务（联网后自动刷新）
 * @note   非阻塞，调用后立即返回
 */
void weather_logic_start_async_task(void);

/**
 * @brief  查询是否有新数据待刷新
 */
bool weather_logic_has_update(void);

/**
 * @brief  标记数据已刷新
 */
void weather_logic_clear_update_flag(void);

#endif
