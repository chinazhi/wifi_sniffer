/*********************************************************************************************************
*
* File                : app_main.c
* Hardware Environment: 
* Build Environment   : ESP8266_RTOS_SDK  Version: 3.1
* Version             : V1.0
* By                  : hik_gz
* Time                : 2019.05.20
*********************************************************************************************************/

#include "app_main.h"
#include "bc20.h"
#include "mqtt.h"
#include "wifi_sniffer.h"
#include "adc.h"

#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)

uint8_t sta_mac[6] = {0};
int BC20_ERROR_num = 0;
int sever_request = 0;

static QueueHandle_t uart0_queue;
static const char *TAG = "Main_App";

/*************************************************
Function: starting_up
Description: 开机检测函数，长按1秒为开机指令  
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

    
    gpio_config_t  VBAT_BC20_conf = { 
            .mode = GPIO_MODE_OUTPUT, 
            .pull_up_en = 1,
            .pin_bit_mask = ((uint32_t) 1 << LED_BC20_PIN)
        }; 
        gpio_config( &VBAT_BC20_conf);/* 设置BC20控制引脚模式 */
        gpio_set_level(LED_BC20_PIN, 1);
    
    gpio_set_direction(LED_MQTT_PIN, GPIO_MODE_OUTPUT);
    //gpio_set_direction(LED_BC20_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(KEY_GPIO_PIN, GPIO_MODE_INPUT);
    
    /* 电源控制脚拉高 */
    gpio_set_level(CRT_VBAT_PIN, 1);  
    
}
    
/*************************************************
Function: Uart_Config
Description: 模块串口设置,初始化事件循环,正式开机
Input: uart0波特率，uart1波特率
other：uart0为模块与NB通信口，uart1为打印信息输出引脚 
*************************************************/
 static void Uart_Config(int uart0_Baud_rate, int uart1_Baud_rate)
{
    /* 配置UART0驱动程序通信引脚的参数 */
    uart_config_t uart0_config = {
        .baud_rate = uart0_Baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart0_config);
    /* 配置UART1驱动程序通信引脚的参数 */
    uart_config_t uart1_config = {
        .baud_rate = uart1_Baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart1_config);
    
    /* 事件循环(event loop)初始化 从TCP/IP堆栈和Wi-Fi子系统收集事件 */
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    
    /* 开机灯亮 */
    gpio_set_level(LED_MQTT_PIN, 1);
    gpio_set_level(LED_BC20_PIN, 1);
    ESP_LOGE(TAG, "Device starting up!"); 
}

 /*************************************************
 Function: Whether_Set_Mqtt_User_Password
 Description: 配置函数，选择是否进行Mqtt用户名密码配置 
 *************************************************/
 static void Get_Topic_Mac(void)
 {
    esp_wifi_get_mac(WIFI_IF_STA, sta_mac);
    sprintf(StationID, "%02x%02x%02x%02x%02x%02x",sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
    sprintf(Subscribe_topic_name, "hik/%s/%s/req", service, StationID);/* 命令订阅主题 */
    sprintf(Subscribe_test_name, "hik/%s/req", service);               /* 工厂测试订阅主题 */
    sprintf(Publish_topic_name, "hik/%s/%s/dp", service, ver);         /* 数据发布主题 */ 
    sprintf(stat_topic_name, "hik/%s/stat", service);                  /* 命令状态发布主题 */
    sprintf(rsp_topic_name, "hik/%s/rsp", service);                    /* 命令请求、回复发布主题 */
 }

/*************************************************
Function: Whether_Set_Mqtt_User_Password
Description: 配置函数，选择是否进行Mqtt用户名密码配置 
*************************************************/
 void Whether_Set_Mqtt_User_Password()
 {
    int i = 0;
    /* 判断是否进入参数配置模式 */
    while(i < 3 )
    {  
        vTaskDelay(20 / portTICK_RATE_MS); /* 延时20ms去抖 */
        if(gpio_get_level(KEY_GPIO_PIN) == 0)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);/* 延时1s */
            if(gpio_get_level(KEY_GPIO_PIN) == 0)
                i++;
            else
                break;
        }
        else
            break;
    }
    if(i == 3)
    {
        wifi_softap_init(); /*  WiFi-AP模式初始化      */
        tcp_server_task(); /* TCP服务器接收配置,配置完毕关闭wifi */ 
        esp_restart();
    } 
 }

/*************************************************
Function: close_down_task
Description: 关机检测函数，长按5秒判断为关机机指令 
*************************************************/
static void close_down_task()
{
    int i = 0;
    while(i < 4 )
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
            /* 电量检测低于20%闪烁 */ //ggzhi 20190831 
            if(884 > adc_read())
            {
                gpio_set_level(LED_VBAT_PIN, 0);    
            }
            vTaskDelay( 1000 / portTICK_RATE_MS);
            gpio_set_level(LED_VBAT_PIN, 1);
        }
        /* 电量检测 满电输出1023 800 3.1v 850 3.3v 电量低于0% 关机*/
        if(adc_read() < 850)
        {
            break;
        }     
    }
    printf("Shut down!\r\n");
    /* 保存数据、卸载驱动等 */
    gpio_set_level(LED_MQTT_PIN, 0);
    gpio_set_level(LED_BC20_PIN, 0);
    /* 关机 电源控制脚拉低*/
    gpio_set_level(CRT_VBAT_PIN, 0); 
    while(1)
    {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

/* 主函数 */
void app_main(void)
{
    starting_up(); /* 配置GPIO,开机 */
    
    Uart_Config(115200,115200); /* 串口初始化 事件循环初始化*/

    /* 安装串口驱动程序, 获取队列 */
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 4, BUF_SIZE * 4, 100, &uart0_queue);

    Get_Topic_Mac(); /* 获取Mqtt主题、本机MAC地址*/

    Factory_test();   /*工厂测试程序入口*/

    Whether_Set_Mqtt_User_Password();  /* 判断用户是否要进入参数配置模式 */ 

    xTaskCreate(close_down_task, "close_down_task", 512, NULL, 5, NULL); /* 开启监测关机任务 */

    Set_Mqtt_User_Password();    /* 设备第一次开机默认开启参数配置模式 */
       
    Check_BC20();   /* 检测BC20是否可用 */

    xTaskCreate((TaskFunction_t)GNSS_Nvs_task, "GNSS_Nvs_task", 1024, NULL, 5, NULL); /* 开启搜星任务 */  
 
    /* 连接和订阅 */
    try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_topic_name, sizeof(Subscribe_topic_name), 1);

    /* 请求并接收服务器监测mac */
    recv_and_save();    

    /* 开启探针任务 */
    xTaskCreate((TaskFunction_t)wifi_sniffer_task, "wifi_sniffer_task", 1024*2, NULL, 6, NULL);

    /* 开启数据发布,状态定时上报任务 */
    xTaskCreate((TaskFunction_t)Publish_Monitor_Data_task, "Publish_Monitor_Data_task", 1024*2, NULL, 6, NULL);

}

		
			
		
                
