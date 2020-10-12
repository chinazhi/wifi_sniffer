/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"


#define	CRT_VBAT_PIN			GPIO_NUM_13 /* 电源开关控制引脚 */
#define	LED_VBAT_PIN			GPIO_NUM_12 /* 电量灯引脚 */
#define	KEY_GPIO_PIN			GPIO_NUM_16 /* 按键引脚 */

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "sc";





void smartconfig_example_task(void * parm);

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void sc_callback(smartconfig_status_t status, void *pdata)
{
    switch (status) {
        case SC_STATUS_WAIT:
            ESP_LOGI(TAG, "SC_STATUS_WAIT");
            break;
        case SC_STATUS_FIND_CHANNEL:
            ESP_LOGI(TAG, "SC_STATUS_FINDING_CHANNEL");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            ESP_LOGI(TAG, "SC_STATUS_GETTING_SSID_PSWD");
            break;
        case SC_STATUS_LINK:
            ESP_LOGI(TAG, "SC_STATUS_LINK");
            wifi_config_t *wifi_config = pdata;
            ESP_LOGI(TAG, "SSID:%s", wifi_config->sta.ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", wifi_config->sta.password);
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        case SC_STATUS_LINK_OVER:
            ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t phone_ip[4] = { 0 };
                memcpy(phone_ip, (uint8_t* )pdata, 4);
                ESP_LOGI(TAG, "Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
            }
            xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
    }
}

void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
    while (1) {
        uxBits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

/*************************************************
Function: starting_up
Description: 开机检测函数，长按3秒判断为开机指令  
Others: 需要程序启动一开始就判断
*************************************************/
static void starting_up() 
{
    gpio_config_t  VBAT_CRT_conf = { 
        .mode = GPIO_MODE_DEF_OUTPUT, 
        .pull_up_en = 1,
        .pin_bit_mask = ((uint32_t) 1 << CRT_VBAT_PIN)
    };
    gpio_config( &VBAT_CRT_conf);/* 设置电源控制引脚模式 */   
    gpio_config_t  VBAT_LED_conf = { 
        .mode = GPIO_MODE_OUTPUT, 
        .pull_up_en = 1,
        .pin_bit_mask = ((uint32_t) 1 << LED_VBAT_PIN)
    }; 
    gpio_config( &VBAT_LED_conf);/* 设置电量灯控制引脚模式 */
    gpio_set_level(LED_VBAT_PIN, 1);
    
    gpio_set_direction(LED_MQTT_PIN, GPIO_MODE_OUTPUT); 
    gpio_set_direction(KEY_GPIO_PIN, GPIO_MODE_INPUT);
    
    int i = 0;
    /* 判断是否开机 */
    while(i < 3 )
    {  
        vTaskDelay(20 / portTICK_RATE_MS); /* 延时20ms去抖 */
        if(gpio_get_level(KEY_GPIO_PIN) == 0)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);/* 延时1s */
            if(gpio_get_level(KEY_GPIO_PIN) == 0)
                i++;
            else
                i = 0;
        }  
    }
    /* 开机灯亮、电源控制脚拉高 */
    gpio_set_level(CRT_VBAT_PIN, 1);  
    gpio_set_level(LED_GPIO_PIN, 1);
    printf("开机！\r\n");
    
}


/*************************************************
Function: close_down_task
Description: 关机检测函数，长按5秒判断为关机机指令 
Return:  
*************************************************/
static void close_down_task()
{
    int i = 0;
    while(i < 5 )
    {     
        vTaskDelay(20 / portTICK_RATE_MS); /* 延时20ms去抖 */
        if(gpio_get_level(KEY_GPIO_PIN) == 0)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);/* 延时1s */
            if(gpio_get_level(KEY_GPIO_PIN) == 0)
                i++;
            else
                i = 0;
        }  
        else
        {
            /* 电量检测 满电输出1023 */
            if(900 > adc_read())
            {
                gpio_set_level(LED_VBAT_PIN, 0);    
            }
            vTaskDelay( 1000 / portTICK_RATE_MS);
            gpio_set_level(LED_VBAT_PIN, 1);
        }
    }
    printf("关机！\r\n");
    /* 保存数据、卸载驱动等 */
    //uart_driver_delete(EX_UART_NUM);
    gpio_set_level(LED_GPIO_PIN, 0);
    
    /* 关机 电源控制脚拉低*/
    gpio_set_level(CRT_VBAT_PIN, 0);
    vTaskDelay( 2000 / portTICK_RATE_MS);
    vTaskDelete(NULL);
}

void app_main()
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
}

