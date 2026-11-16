/**
 * @file    wifi_service.h
 * @brief   WiFi 业务层：初始化、扫描、连接、状态查询
 *
 * 与 device_service 同一层，只负责 WiFi 逻辑，不包含任何 UI。
 * WiFi 操作是异步的（扫描、连接都在后台完成），上层页面通过
 * wifi_service_get_state() / wifi_service_get_ap() 轮询读取结果。
 */

#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_wifi.h"

/* WiFi 业务层状态机 */
typedef enum
{
    WIFI_SERVICE_STATE_IDLE = 0,      /* 未初始化 / 空闲 */
    WIFI_SERVICE_STATE_SCANNING,      /* 正在扫描 */
    WIFI_SERVICE_STATE_SCAN_DONE,     /* 扫描完成，可读取 AP 列表 */
    WIFI_SERVICE_STATE_CONNECTING,    /* 正在连接（等待拿到 IP） */
    WIFI_SERVICE_STATE_CONNECTED,     /* 已连接 */
    WIFI_SERVICE_STATE_FAILED         /* 连接失败 / 已断开 */
} wifi_service_state_t;

/*
 * 初始化 WiFi（幂等，可重复调用）。
 * 首次进入 Network 页面时调用，避免影响程序启动流程。
 */
void wifi_service_init(void);

/* 启动一次非阻塞扫描，返回 false 表示启动失败 */
bool wifi_service_scan_start(void);

/* 获取当前状态 */
wifi_service_state_t wifi_service_get_state(void);

/* 获取扫描到的 AP 数量 */
uint16_t wifi_service_get_ap_count(void);

/* 按索引拷贝一条 AP 记录，返回 false 表示索引越界 */
bool wifi_service_get_ap(uint16_t idx, wifi_ap_record_t *out);

/*
 * 连接指定 AP。
 * password 传空字符串表示开放网络（无需密码）。
 */
bool wifi_service_connect(const char *ssid, const char *password);

/* 断开当前连接 */
bool wifi_service_disconnect(void);

/* 获取当前 IP 地址字符串，形如 192.168.1.100 */
void wifi_service_get_ip(char *buf, size_t len);

#endif /* WIFI_SERVICE_H */
