
#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include <stdbool.h>

/* 设备编号：作为业务层识别设备的统一 ID */
typedef enum
{
    DEVICE_ID_MOTOR = 0,
    DEVICE_ID_PUMP,
    DEVICE_ID_FAN,
    DEVICE_ID_MAX
} device_id_t;

/* 初始化设备业务层 */
void device_service_init(void);

/* 设置指定设备的开关状态
 * true：开启
 * false：关闭
 * 返回 true 表示请求成功，false 表示参数错误
 */
bool device_service_set_enabled(device_id_t id, bool enabled);

/* 获取指定设备当前记录的状态 */
bool device_service_get_enabled(device_id_t id);

#endif
