#include "device_service.h"
#include "page_device.h"
#include "page_manager.h"
#include "ui_common.h"

#include <stdbool.h>
#include <stddef.h>

/*==========================================================
 * 一、设备数据结构
 *==========================================================*/
typedef struct
{
    device_id_t id;   // 业务层设备编号
    const char *name; // 设备名称
    bool enabled;     // true：开启，false：关闭

    lv_obj_t *row;          // 当前设备行
    lv_obj_t *status_label; // ON/OFF 状态文字
    lv_obj_t *switch_obj;   // 当前设备的开关

} device_item_t;

/*==========================================================
 * 二、设备列表
 *==========================================================*/
static device_item_t g_devices[] =
    {
        {DEVICE_ID_MOTOR, "MOTOR", false, NULL, NULL, NULL},
        {DEVICE_ID_PUMP, "PUMP", false, NULL, NULL, NULL},
        {DEVICE_ID_FAN, "FAN", false, NULL, NULL, NULL}};

#define DEVICE_COUNT \
    (sizeof(g_devices) / sizeof(g_devices[0]))

/*==========================================================
 * 三、颜色定义
 *==========================================================*/
#define DEVICE_COLOR_ON 0xDCFCE7  // 开启：淡绿色背景
#define DEVICE_COLOR_OFF 0xF4F6F8 // 关闭：浅灰色背景

#define DEVICE_TEXT_ON 0x16A34A  // 开启：绿色文字
#define DEVICE_TEXT_OFF 0x6B7280 // 关闭：灰色文字

/*==========================================================
 * 四、动画回调：改变设备行背景透明度
 *
 * var   ：当前设备行对象
 * value ：动画当前值，范围 0~255
 *==========================================================*/
static void device_row_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_t *row = (lv_obj_t *)var;

    if (row == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_opa(
        row,
        (lv_opa_t)value,
        LV_PART_MAIN);
}

/*==========================================================
 * 五、动画回调：改变状态文字透明度
 *
 * var   ：当前设备的状态文字对象
 * value ：动画当前值，范围 0~255
 *==========================================================*/
static void device_text_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_t *label = (lv_obj_t *)var;

    if (label == NULL)
    {
        return;
    }

    lv_obj_set_style_text_opa(
        label,
        (lv_opa_t)value,
        LV_PART_MAIN);
}

/*==========================================================
 * 六、启动背景淡入动画
 *==========================================================*/
static void device_start_row_animation(lv_obj_t *row)
{
    if (row == NULL)
    {
        return;
    }

    /*
     * 删除同一个对象上尚未结束的同类动画，
     * 防止快速连续点击时多个动画相互干扰。
     */
    lv_anim_del(row, device_row_opa_anim_cb);

    /* 先让背景透明，再逐渐显示 */
    lv_obj_set_style_bg_opa(
        row,
        LV_OPA_TRANSP,
        LV_PART_MAIN);

    lv_anim_t anim;
    lv_anim_init(&anim);

    /* 动画作用对象：当前设备行 */
    lv_anim_set_var(&anim, row);

    /* 透明度从 0 变化到完全不透明 */
    lv_anim_set_values(
        &anim,
        LV_OPA_TRANSP,
        LV_OPA_COVER);

    /* 动画持续 220 毫秒 */
    lv_anim_set_time(&anim, 220);

    /* 使用缓出效果 */
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);

    /* 注册动画执行回调 */
    lv_anim_set_exec_cb(&anim, device_row_opa_anim_cb);

    /* 启动动画 */
    lv_anim_start(&anim);
}

/*==========================================================
 * 七、启动状态文字淡入动画
 *==========================================================*/
static void device_start_text_animation(lv_obj_t *label)
{
    if (label == NULL)
    {
        return;
    }

    /* 删除当前文字对象上尚未结束的同类动画 */
    lv_anim_del(label, device_text_opa_anim_cb);

    /* 文字先透明，再逐渐显示 */
    lv_obj_set_style_text_opa(
        label,
        LV_OPA_TRANSP,
        LV_PART_MAIN);

    lv_anim_t anim;
    lv_anim_init(&anim);

    lv_anim_set_var(&anim, label);

    lv_anim_set_values(
        &anim,
        LV_OPA_TRANSP,
        LV_OPA_COVER);

    lv_anim_set_time(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);

    lv_anim_set_exec_cb(
        &anim,
        device_text_opa_anim_cb);

    lv_anim_start(&anim);
}

/*==========================================================
 * 八、更新设备状态文字和颜色
 *==========================================================*/
static void device_update_status(device_item_t *device)
{
    if (device == NULL || device->status_label == NULL)
    {
        return;
    }

    if (device->enabled)
    {
        lv_label_set_text(device->status_label, "ON");

        lv_obj_set_style_text_color(
            device->status_label,
            lv_color_hex(DEVICE_TEXT_ON),
            LV_PART_MAIN);
    }
    else
    {
        lv_label_set_text(device->status_label, "OFF");

        lv_obj_set_style_text_color(
            device->status_label,
            lv_color_hex(DEVICE_TEXT_OFF),
            LV_PART_MAIN);
    }
}

/*==========================================================
 * 九、更新设备行背景颜色
 *==========================================================*/
static void device_update_row_color(device_item_t *device)
{
    if (device == NULL || device->row == NULL)
    {
        return;
    }

    if (device->enabled)
    {
        lv_obj_set_style_bg_color(
            device->row,
            lv_color_hex(DEVICE_COLOR_ON),
            LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_bg_color(
            device->row,
            lv_color_hex(DEVICE_COLOR_OFF),
            LV_PART_MAIN);
    }
}

/*==========================================================
 * 十、开关事件回调
 *==========================================================*/

static void device_switch_event_cb(lv_event_t *e)
{
    /* 只处理开关状态变化事件 */
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    /* 获取当前开关绑定的设备数据 */
    device_item_t *device =
        (device_item_t *)lv_event_get_user_data(e);

    /* 检查设备数据和开关对象是否有效 */
    if (device == NULL || device->switch_obj == NULL)
    {
        return;
    }

    /* 1. 读取 LVGL 开关当前的实际状态 */
    bool enabled = lv_obj_has_state(
        device->switch_obj,
        LV_STATE_CHECKED
    );

    /* 2.请求业务层设置设备状态 */
    if (!device_service_set_enabled(device->id, enabled))
    {
        return;
    }

    /* 3. 更新 UI 层缓存的设备状态 */
    device->enabled = enabled;

    /* 4. 更新 ON/OFF 状态文字 */
    device_update_status(device);

    /* 5. 更新设备行背景颜色 */
    device_update_row_color(device);

    /* 6. 启动背景淡入动画 */
    device_start_row_animation(device->row);

    /* 7. 启动状态文字淡入动画 */
    device_start_text_animation(device->status_label);
}


/*==========================================================
 * 十一、创建单个设备行
 *==========================================================*/
static void device_create_item(
    lv_obj_t *parent,
    device_item_t *device)
{
    if (parent == NULL || device == NULL)
    {
        return;
    }

    /* 创建设备行 */
    lv_obj_t *row = lv_obj_create(parent);

    if (row == NULL)
    {
        return;
    }

    device->row = row;

    /* 行尺寸 */
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 52);

    /* 横向 Flex：名称、状态、开关 */
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    lv_obj_set_flex_align(
        row,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    /* 内边距和控件间距 */
    lv_obj_set_style_pad_hor(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 12, LV_PART_MAIN);

    /* 设备行外观 */
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);

    /* 创建设备名称 */
    lv_obj_t *name_label = lv_label_create(row);

    if (name_label == NULL)
    {
        device->row = NULL;
        lv_obj_del(row);
        return;
    }

    lv_label_set_text(name_label, device->name);

    /* 名称占据剩余横向空间 */
    lv_obj_set_flex_grow(name_label, 1);

    /* 创建状态文字 */
    device->status_label = lv_label_create(row);

    if (device->status_label == NULL)
    {
        device->row = NULL;
        device->status_label = NULL;
        lv_obj_del(row);
        return;
    }

    /* 创建开关 */
    device->switch_obj = lv_switch_create(row);

    if (device->switch_obj == NULL)
    {
        device->row = NULL;
        device->status_label = NULL;
        device->switch_obj = NULL;
        lv_obj_del(row);
        return;
    }

    /* 恢复设备之前保存的开关状态 */
    if (device->enabled)
    {
        lv_obj_add_state(
            device->switch_obj,
            LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(
            device->switch_obj,
            LV_STATE_CHECKED);
    }

    /* 初始化状态文字和背景颜色 */
    device_update_status(device);
    device_update_row_color(device);

    /* 初始显示时保持完全不透明 */
    lv_obj_set_style_bg_opa(
        row,
        LV_OPA_COVER,
        LV_PART_MAIN);

    lv_obj_set_style_text_opa(
        device->status_label,
        LV_OPA_COVER,
        LV_PART_MAIN);

    /* 绑定当前设备的开关事件 */
    lv_obj_add_event_cb(
        device->switch_obj,
        device_switch_event_cb,
        LV_EVENT_VALUE_CHANGED,
        device);
}

/*==========================================================
 * 十二、返回按钮事件回调
 *==========================================================*/
static void device_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        page_manager_back();
    }
}

/*==========================================================
 * 十三、创建 Device 页面
 *==========================================================*/
void page_device_create(void)
{
    lv_obj_t *screen = lv_scr_act();

    if (screen == NULL)
    {
        return;
    }

    /* 创建页面总容器 */
    lv_obj_t *container = lv_obj_create(screen);

    if (container == NULL)
    {
        return;
    }

    lv_obj_set_size(container, 460, 300);
    lv_obj_center(container);

    /* 总容器纵向 Flex 排列 */
    lv_obj_set_flex_flow(
        container,
        LV_FLEX_FLOW_COLUMN);

    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_all(
        container, 8, LV_PART_MAIN);

    lv_obj_set_style_pad_row(
        container, 8, LV_PART_MAIN);

    /* 页面标题 */
    lv_obj_t *title = lv_label_create(container);

    if (title != NULL)
    {
        lv_label_set_text(title, "DEVICE CONTROL");

        lv_obj_set_width(title, LV_PCT(100));

        lv_obj_set_style_text_align(
            title,
            LV_TEXT_ALIGN_CENTER,
            LV_PART_MAIN);
    }

    /* 循环创建设备行 */
    for (size_t i = 0; i < DEVICE_COUNT; i++)
    {
        device_create_item(
            container,
            &g_devices[i]);
    }

    /* 创建返回按钮 */
    lv_obj_t *back_button =
        ui_common_create_button(container, "Back");

    if (back_button != NULL)
    {
        lv_obj_set_size(back_button, 100, 38);

        lv_obj_add_event_cb(
            back_button,
            device_back_event_cb,
            LV_EVENT_CLICKED,
            NULL);
    }
}
