
#include "device_service.h"

/* 当前版本先模拟设备状态，后续再接入真实硬件 */
static bool g_device_enabled[DEVICE_ID_MAX] = {false};

/* 初始化业务层 */
void device_service_init(void)
{
    for (int i = 0; i < DEVICE_ID_MAX; i++)
    {
        g_device_enabled[i] = false;
    }
}

/* 设置指定设备状态 */
bool device_service_set_enabled(device_id_t id, bool enabled)
{
    /* 检查设备编号是否合法 */
    if (id < DEVICE_ID_MOTOR || id >= DEVICE_ID_MAX)
    {
        return false;
    }

    /* 暂时只更新软件状态，不操作 GPIO */
    g_device_enabled[id] = enabled;

    return true;
}

/* 获取指定设备状态 */
bool device_service_get_enabled(device_id_t id)
{
    /* 无效编号时返回 false */
    if (id < DEVICE_ID_MOTOR || id >= DEVICE_ID_MAX)
    {
        return false;
    }

    return g_device_enabled[id];
}
