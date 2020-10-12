/*******************************************************************************
 * Copyright (c) 2014 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "MQTTClient.h"

#include "driver/gpio.h"
#include "driver/uart.h"


/* FreeRTOS event group to signal 当我们连接好或者准备发出请求时 */
static EventGroupHandle_t wifi_event_group;
/* 串口消息队列*/
static QueueHandle_t uart0_queue;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

#define MQTT_CLIENT_THREAD_NAME         "mqtt_client_thread"
#define MQTT_CLIENT_THREAD_STACK_WORDS  4096
#define MQTT_CLIENT_THREAD_PRIO         8
#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)


static const char *TAG = "example";

MQTTClient client; //储存 客户端连接MQTT平台信息
int msg_stat = 0; //接收到消息的状态
unsigned char mqtt_recv_buff[12];


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect(); //系统事件，连接wifi
        break;

    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT); //从ap处获取ip 并设置事件组字节位
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;

    default:
        break;
    }

    return ESP_OK;
}

//wifi 初始化
static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate(); //创建wifi事件
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL)); //初始化事件
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

//订阅信息回调函数
static void messageArrived(MessageData *data)
{
    memset(mqtt_recv_buff, 0, sizeof(mqtt_recv_buff));
    
    ESP_LOGI(TAG, "Message arrived[len:%u]: %.*s", data->message->payloadlen, data->message->payloadlen, (char *)data->message->payload);

    memcpy(mqtt_recv_buff, (char *)data->message->payload, data->message->payloadlen);

    ESP_LOGI(TAG, "mqtt recv buff: %s", mqtt_recv_buff);
    
    msg_stat = atoi((char *)data->message->payload);
    ESP_LOGI(TAG, "msg_stat num: %d", msg_stat);
 
}

//mqtt客户端线程
static void mqtt_client_thread(void *pvParameters)
{
    char *payload = NULL;
    //MQTTClient client;
    Network network;
    int rc = 0;
    char clientID[32] = {0};
    //uint32_t count = 0;

    ESP_LOGI(TAG, "ssid:%s passwd:%s sub:%s qos:%u pub:%s qos:%u pubinterval:%u payloadsize:%u",
             CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD, CONFIG_MQTT_SUB_TOPIC,
             CONFIG_DEFAULT_MQTT_SUB_QOS, CONFIG_MQTT_PUB_TOPIC, CONFIG_DEFAULT_MQTT_PUB_QOS,
             CONFIG_MQTT_PUBLISH_INTERVAL, CONFIG_MQTT_PAYLOAD_BUFFER);

    ESP_LOGI(TAG, "ver:%u clientID:%s keepalive:%d username:%s passwd:%s session:%d level:%u",
             CONFIG_DEFAULT_MQTT_VERSION, CONFIG_MQTT_CLIENT_ID,
             CONFIG_MQTT_KEEP_ALIVE, CONFIG_MQTT_USERNAME, CONFIG_MQTT_PASSWORD,
             CONFIG_DEFAULT_MQTT_SESSION, CONFIG_DEFAULT_MQTT_SECURITY);

    ESP_LOGI(TAG, "broker:%s port:%u", CONFIG_MQTT_BROKER, CONFIG_MQTT_PORT);

    ESP_LOGI(TAG, "sendbuf:%u recvbuf:%u sendcycle:%u recvcycle:%u",
             CONFIG_MQTT_SEND_BUFFER, CONFIG_MQTT_RECV_BUFFER,
             CONFIG_MQTT_SEND_CYCLE, CONFIG_MQTT_RECV_CYCLE);

    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;

    NetworkInit(&network);  //初始化TCP连接

    if (MQTTClientInit(&client, &network, 0, NULL, 0, NULL, 0) == false)
    {
        ESP_LOGE(TAG, "mqtt init err");
        vTaskDelete(NULL);
    }

    payload = malloc(CONFIG_MQTT_PAYLOAD_BUFFER); //申请MQTT buffer空间 初始化空间

    if (!payload) 
    {
        ESP_LOGE(TAG, "mqtt malloc err");
    } 
    else
    {
        memset(payload, 0x0, CONFIG_MQTT_PAYLOAD_BUFFER);
    }

    for (;;) //等待连接wifi 连接mqtt平台
    {
        ESP_LOGI(TAG, "wait wifi connect...");
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

        if ((rc = NetworkConnect(&network, CONFIG_MQTT_BROKER, CONFIG_MQTT_PORT)) != 0) {
            ESP_LOGE(TAG, "Return code from network connect is %d", rc);
            continue;
        }

        connectData.MQTTVersion = CONFIG_DEFAULT_MQTT_VERSION;

        sprintf(clientID, "%s_%u", CONFIG_MQTT_CLIENT_ID, esp_random());

        connectData.clientID.cstring = clientID;
        connectData.keepAliveInterval = CONFIG_MQTT_KEEP_ALIVE;

        connectData.username.cstring = CONFIG_MQTT_USERNAME;
        connectData.password.cstring = CONFIG_MQTT_PASSWORD;

        connectData.cleansession = CONFIG_DEFAULT_MQTT_SESSION;

        ESP_LOGI(TAG, "MQTT Connecting");

        if ((rc = MQTTConnect(&client, &connectData)) != 0) {
            ESP_LOGE(TAG, "Return code from MQTT connect is %d", rc);
            network.disconnect(&network);
            continue;
        }

        ESP_LOGI(TAG, "MQTT Connected");

#if defined(MQTT_TASK) //如果这个任务不成功执行，就不会自动进去回调方法：messageArrived；

        if ((rc = MQTTStartTask(&client)) != pdPASS) {
            ESP_LOGE(TAG, "Return code from start tasks is %d", rc);
        } else {
            ESP_LOGI(TAG, "Use MQTTStartTask");
        }

#endif

        if ((rc = MQTTSubscribe(&client, CONFIG_MQTT_SUB_TOPIC, CONFIG_DEFAULT_MQTT_SUB_QOS, messageArrived)) != 0) {
            ESP_LOGE(TAG, "Return code from MQTT subscribe is %d", rc);
            network.disconnect(&network);
            continue;
        }

        ESP_LOGI(TAG, "MQTT subscribe to topic %s OK", CONFIG_MQTT_SUB_TOPIC);
        char buf[20] = " Successful ";
        int xxx = 1;
        for(;;)  //定时推送消息666
        {

            MQTTMessage message;
            message.qos = CONFIG_DEFAULT_MQTT_PUB_QOS;
            message.retained = 0;
            message.payload = buf;
            message.payloadlen = sizeof(buf);
            
            if(msg_stat > 0)
            {
                if ((rc = MQTTPublish(&client, CONFIG_MQTT_PUB_TOPIC, &message)) != 0) 
                {
                   ESP_LOGE(TAG, "Return code from MQTT publish is %d", rc);
                }
                else
                {
                   ESP_LOGI(TAG, "MQTT published topic %s, len:%u heap:%u", CONFIG_MQTT_PUB_TOPIC, message.payloadlen, esp_get_free_heap_size());
                }
                if (rc != 0)
                {
                   break;
                }
                if(xxx)
                {
                    gpio_set_level(GPIO_NUM_2, 0);
                    xxx = 0;
                }
                else
                {
                    gpio_set_level(GPIO_NUM_2, 1);
                    xxx = 1;
                }
                    
                msg_stat = 0;
            }
            vTaskDelay(2000 / portTICK_RATE_MS);
            //vTaskDelay(CONFIG_MQTT_PUBLISH_INTERVAL / portTICK_RATE_MS);  
        }
        
        network.disconnect(&network); 
    }

    ESP_LOGW(TAG, "mqtt_client_thread going to be deleted");
    vTaskDelete(NULL);
    return;
}


//模块串口设置
 static void Uart_Config(void)
{
    /* 配置UART0驱动程序通信引脚的参数 */
    uart_config_t uart0_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart0_config);
    /* 配置UART1驱动程序通信引脚的参数 */
    uart_config_t uart1_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart1_config);
    
}

//串口队列事件处理线程
 static void uart_event_task(void *pvParameters)
 {
	 uart_event_t event;
	 uint8_t *dtmp = (uint8_t *) malloc(RD_BUF_SIZE);
 
	 for (;;)
	 {
		 // Waiting for UART event.
		 if (xQueueReceive(uart0_queue, (void *)&event, (portTickType)portMAX_DELAY)) {
			 bzero(dtmp, RD_BUF_SIZE);
			 ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);
 
			 switch (event.type)
			 {
				 case UART_DATA:
					 ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
					 uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
					 ESP_LOGI(TAG, "[DATA EVT]:");
					 uart_write_bytes(EX_UART_NUM, (const char *) dtmp, event.size);
					 break;
 
				 // Others
				 default:
					 ESP_LOGI(TAG, "uart event type: %d", event.type);
					 break;
			 }
		 }
	 }
 
	 free(dtmp);
	 dtmp = NULL;
	 vTaskDelete(NULL);
 }

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
	{
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

	//串口初始化
	Uart_Config(); 
    /* 安装串口驱动程序, 获取队列 */
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 4, BUF_SIZE * 4, 100, &uart0_queue);
	
    //gpio 2 初始化
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);    

    //wifi 初始化    
    initialise_wifi();

    //mqtt 线程
    ret = xTaskCreate(&mqtt_client_thread, MQTT_CLIENT_THREAD_NAME, MQTT_CLIENT_THREAD_STACK_WORDS, NULL, MQTT_CLIENT_THREAD_PRIO, NULL);
    if (ret != pdPASS)  {
        ESP_LOGE(TAG, "mqtt create client thread %s failed", MQTT_CLIENT_THREAD_NAME);
    }

	//从ISR创建一个任务来处理UART事件
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 8, NULL);	
	
}
