/**
 ******************************************************************************
 * @file        main.c
 * @version     V1.0
 * @brief       LVGL lv_obj(基础对象) 实验
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

 #include "nvs_flash.h"
 #include "myiic.h"
 #include "xl9555.h"
 #include "lvgl_demo.h"


 /**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
void app_main(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();             /* 初始化NVS */
    
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

	myiic_init();                       /* IIC初始化 */
    xl9555_init();                      /* IO扩展芯片初始化 */
	lvgl_demo();                        /* 运行LVGL例程 */
}
