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
Description: ������⺯��������1��Ϊ����ָ��  
*************************************************/
static void starting_up() 
{
    gpio_config_t  VBAT_CRT_conf = { 
        .mode = GPIO_MODE_DEF_OUTPUT, 
        .pull_up_en = 1,
        .pin_bit_mask = ((uint32_t) 1 << CRT_VBAT_PIN)
    };
    gpio_config( &VBAT_CRT_conf);/* ���õ�Դ��������ģʽ */   
    gpio_config_t  VBAT_LED_conf = { 
        .mode = GPIO_MODE_OUTPUT, 
        .pull_up_en = 1,
        .pin_bit_mask = ((uint32_t) 1 << LED_VBAT_PIN)
    }; 
    gpio_config( &VBAT_LED_conf);/* ���õ����ƿ�������ģʽ */
    gpio_set_level(LED_VBAT_PIN, 1);

    
    gpio_config_t  VBAT_BC20_conf = { 
            .mode = GPIO_MODE_OUTPUT, 
            .pull_up_en = 1,
            .pin_bit_mask = ((uint32_t) 1 << LED_BC20_PIN)
        }; 
        gpio_config( &VBAT_BC20_conf);/* ����BC20��������ģʽ */
        gpio_set_level(LED_BC20_PIN, 1);
    
    gpio_set_direction(LED_MQTT_PIN, GPIO_MODE_OUTPUT);
    //gpio_set_direction(LED_BC20_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(KEY_GPIO_PIN, GPIO_MODE_INPUT);
    
    /* ��Դ���ƽ����� */
    gpio_set_level(CRT_VBAT_PIN, 1);  
    
}
    
/*************************************************
Function: Uart_Config
Description: ģ�鴮������,��ʼ���¼�ѭ��,��ʽ����
Input: uart0�����ʣ�uart1������
other��uart0Ϊģ����NBͨ�ſڣ�uart1Ϊ��ӡ��Ϣ������� 
*************************************************/
 static void Uart_Config(int uart0_Baud_rate, int uart1_Baud_rate)
{
    /* ����UART0��������ͨ�����ŵĲ��� */
    uart_config_t uart0_config = {
        .baud_rate = uart0_Baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart0_config);
    /* ����UART1��������ͨ�����ŵĲ��� */
    uart_config_t uart1_config = {
        .baud_rate = uart1_Baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart1_config);
    
    /* �¼�ѭ��(event loop)��ʼ�� ��TCP/IP��ջ��Wi-Fi��ϵͳ�ռ��¼� */
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    
    /* �������� */
    gpio_set_level(LED_MQTT_PIN, 1);
    gpio_set_level(LED_BC20_PIN, 1);
    ESP_LOGE(TAG, "Device starting up!"); 
}

 /*************************************************
 Function: Whether_Set_Mqtt_User_Password
 Description: ���ú�����ѡ���Ƿ����Mqtt�û����������� 
 *************************************************/
 static void Get_Topic_Mac(void)
 {
    esp_wifi_get_mac(WIFI_IF_STA, sta_mac);
    sprintf(StationID, "%02x%02x%02x%02x%02x%02x",sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
    sprintf(Subscribe_topic_name, "hik/%s/%s/req", service, StationID);/* ��������� */
    sprintf(Subscribe_test_name, "hik/%s/req", service);               /* �������Զ������� */
    sprintf(Publish_topic_name, "hik/%s/%s/dp", service, ver);         /* ���ݷ������� */ 
    sprintf(stat_topic_name, "hik/%s/stat", service);                  /* ����״̬�������� */
    sprintf(rsp_topic_name, "hik/%s/rsp", service);                    /* �������󡢻ظ��������� */
 }

/*************************************************
Function: Whether_Set_Mqtt_User_Password
Description: ���ú�����ѡ���Ƿ����Mqtt�û����������� 
*************************************************/
 void Whether_Set_Mqtt_User_Password()
 {
    int i = 0;
    /* �ж��Ƿ�����������ģʽ */
    while(i < 3 )
    {  
        vTaskDelay(20 / portTICK_RATE_MS); /* ��ʱ20msȥ�� */
        if(gpio_get_level(KEY_GPIO_PIN) == 0)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);/* ��ʱ1s */
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
        wifi_softap_init(); /*  WiFi-APģʽ��ʼ��      */
        tcp_server_task(); /* TCP��������������,������Ϲر�wifi */ 
        esp_restart();
    } 
 }

/*************************************************
Function: close_down_task
Description: �ػ���⺯��������5���ж�Ϊ�ػ���ָ�� 
*************************************************/
static void close_down_task()
{
    int i = 0;
    while(i < 4 )
    {     
        vTaskDelay(20 / portTICK_RATE_MS); /* ��ʱ20msȥ�� */
        if(gpio_get_level(KEY_GPIO_PIN) == 0)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);/* ��ʱ1s */
            if(gpio_get_level(KEY_GPIO_PIN) == 0)
                i++;
            else
                i = 0;
        }  
        else
        {
            /* ����������20%��˸ */ //ggzhi 20190831 
            if(884 > adc_read())
            {
                gpio_set_level(LED_VBAT_PIN, 0);    
            }
            vTaskDelay( 1000 / portTICK_RATE_MS);
            gpio_set_level(LED_VBAT_PIN, 1);
        }
        /* ������� �������1023 800 3.1v 850 3.3v ��������0% �ػ�*/
        if(adc_read() < 850)
        {
            break;
        }     
    }
    printf("Shut down!\r\n");
    /* �������ݡ�ж�������� */
    gpio_set_level(LED_MQTT_PIN, 0);
    gpio_set_level(LED_BC20_PIN, 0);
    /* �ػ� ��Դ���ƽ�����*/
    gpio_set_level(CRT_VBAT_PIN, 0); 
    while(1)
    {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

/* ������ */
void app_main(void)
{
    starting_up(); /* ����GPIO,���� */
    
    Uart_Config(115200,115200); /* ���ڳ�ʼ�� �¼�ѭ����ʼ��*/

    /* ��װ������������, ��ȡ���� */
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 4, BUF_SIZE * 4, 100, &uart0_queue);

    Get_Topic_Mac(); /* ��ȡMqtt���⡢����MAC��ַ*/

    Factory_test();   /*�������Գ������*/

    Whether_Set_Mqtt_User_Password();  /* �ж��û��Ƿ�Ҫ�����������ģʽ */ 

    xTaskCreate(close_down_task, "close_down_task", 512, NULL, 5, NULL); /* �������ػ����� */

    Set_Mqtt_User_Password();    /* �豸��һ�ο���Ĭ�Ͽ�����������ģʽ */
       
    Check_BC20();   /* ���BC20�Ƿ���� */

    xTaskCreate((TaskFunction_t)GNSS_Nvs_task, "GNSS_Nvs_task", 1024, NULL, 5, NULL); /* ������������ */  
 
    /* ���ӺͶ��� */
    try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_topic_name, sizeof(Subscribe_topic_name), 1);

    /* ���󲢽��շ��������mac */
    recv_and_save();    

    /* ����̽������ */
    xTaskCreate((TaskFunction_t)wifi_sniffer_task, "wifi_sniffer_task", 1024*2, NULL, 6, NULL);

    /* �������ݷ���,״̬��ʱ�ϱ����� */
    xTaskCreate((TaskFunction_t)Publish_Monitor_Data_task, "Publish_Monitor_Data_task", 1024*2, NULL, 6, NULL);

}

		
			
		
                
