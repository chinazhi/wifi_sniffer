/**
 * Copyright (c) 2018, witt.wang <wangwt9551@gmail.com>
 * ESP8266
 * WiFi Sniffer.
 */

#include "wifi_sniffer.h"
#include "app_main.h"
#include "mqtt.h"
#include "bc20.h"
#include "my_nvs.h"
#include "cJSON.h"

/*TCP��������ͷ�ļ� */
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

int monitor_mac_nums = 0;               /* Ҫ��ص�MAC����       */
uint8_t monitor_mac[6] = {0};               /* ����MAC�Ĵ洢        */
uint8_t all_monitor_mac[MAX_MAC_NUM][6] = {0};/* ȫ��MAC�Ĵ洢 */

static const char *TAG = "wifi";

static wifi_country_t wifi_country = {.cc="CN", .schan=1, .nchan=13, .policy=WIFI_COUNTRY_POLICY_AUTO}; 
static wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
static wifi_ap_config_t ap_cfg = {.ssid=ESP_WIFI_SSID, .password=ESP_WIFI_PASS, .ssid_len=8, .channel=1, 
                                  .authmode=WIFI_AUTH_WPA2_PSK, .ssid_hidden=0, .max_connection=4};     /* 8266_APģʽ�������� */

/* ��ȡ����ļ��mac��Ϣ */
void Read_Nvs_Mac(void)
{  
    int i = 0, j = 0;
    monitor_mac_nums = get_total_nvs("macNum");
    size_t length = sizeof(monitor_mac);
    for (i = 0; i < monitor_mac_nums; i++)
    {
        get_mac_nvs(i, monitor_mac, &length);
        for(j = 0; j < 6; j++)  
            all_monitor_mac[i][j] = monitor_mac[j];
    }
}

/*  WiFi�¼������ص�����         */
esp_err_t event_handler(void *ctx, system_event_t *event)
{
    return ESP_OK;
}

/*����WiFiǰ���ȳ�ʼ��   �¼�ѭ��(event loop)���������¼�ѭ����ʼ�������˳�ʼ�����ڴ�      */
/*WiFi-APģʽ��ʼ��*/ 
void wifi_softap_init(void)
{
    /* ��ʼ�������������� */
    nvs_flash_init();
    /* ��ʼ��TCP/IP��ջ */
    tcpip_adapter_init();    
    /* ��ʼ��APģʽWiFi���� */
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_country(&wifi_country) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, (wifi_config_t *)&ap_cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_FLASH) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );   
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(TAG, "wifi_softap_init finished. SSID: %s password: %s.", ESP_WIFI_SSID, ESP_WIFI_PASS);    
}

/*  WiFi-TCP����������       */
void tcp_server_task(void)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    /* ��������״̬������ʾ�� */
    xTaskCreate((TaskFunction_t)MQTT_ERROR_LED_task, "MQTT_ERROR_LED_task", 256, NULL, 5, NULL);
    
    while (1)
    {
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (listen_sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket binded");

        err = listen(listen_sock, 1);
        if (err != 0) {
            ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in sourceAddr;

        uint addrLen = sizeof(sourceAddr);
        int sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket accepted");

        while (1) {
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            /*Error occured during receiving*/
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            /*Connection closed*/
            else if (len == 0) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
            /*Data received*/
            else 
            {
                inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                rx_buffer[len] = 0; 
                ESP_LOGI(TAG, "%s", rx_buffer);
                if(strstr(rx_buffer, "{") != NULL)
                {
                    /* JSON�ַ�����cJSON��ʽ */
                    cJSON *cjson = cJSON_Parse(rx_buffer);
                    if(cjson == NULL) 
                        printf("json pack into cjson error...\r\n"); 
                    /* ��ȡ�ֶ�ֵ */
                    char * username = cJSON_GetObjectItem( cjson, "username")->valuestring;
                    char * password = cJSON_GetObjectItem( cjson, "password")->valuestring;

                    /* �����ݱ��浽nvs */
                    save_str_nvs("username", username);
                    save_str_nvs("password", password);
                    
                    /* �ر�cjson */
                    cJSON_Delete(cjson);
                }
                
                int err = send(sock, rx_buffer, len, 0);
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                    break;
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    /* �˳�����״̬�ر���ʾ��,�ر�wifi */
    MQTT_ERR_flag = 0;
    esp_wifi_deinit();
}

/*  WiFi��̽ģʽ��ʼ��        */
void wifi_sniffer_init(void)
{
    /* ��ʼ�������������� */
    nvs_flash_init();
    tcpip_adapter_init();
    //ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );    
    /* ��ʼ�� WiFi */
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_country(&wifi_country) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );   
    ESP_ERROR_CHECK( esp_wifi_start() );

    /* ���û���ģʽ���������ݰ�������� */
    ESP_ERROR_CHECK( esp_wifi_set_promiscuous(true) );
    ESP_ERROR_CHECK( esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler));
    
    /* ��ȡ����ļ��mac��Ϣ */
    Read_Nvs_Mac();
    ESP_LOGI(TAG, "wifi sniffer init over");        
}

/*�״ο����豸ǿ�����ò���*/
void Set_Mqtt_User_Password(void)
{  
    char username[20] = {0};
    size_t length = sizeof(username); 
    if(get_str_nvs("username", username, &length))
    {
        wifi_softap_init(); /*  WiFi-APģʽ��ʼ��      */
        tcp_server_task(); /* TCP��������������,������Ϲر�wifi */
        esp_restart();
    }
}

/*  WiFi̽���ŵ�����       */
void wifi_sniffer_set_channel(uint8_t channel)
{   
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

/* �������ݰ�֡����          */
int wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type)
{
	switch(type)
    {
    	case WIFI_PKT_MGMT: return 2; //2 ����֡
    	case WIFI_PKT_DATA: return 3; //3 ����֡
    	default:	
    	case WIFI_PKT_MISC: return 1; //1 ����֡
	}
}

/* �ڻ���ģʽ��ע���RX�ص�����              */
void wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type)
{
    uint8_t i = 0;
    unsigned long j = 0;
    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
    const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
    const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;
     
    for (i = 0; i < monitor_mac_nums; i++)
    {    
        /* δ��ϸ����ͷ��Э��ֱ�ӶԱ�src mac */
        if(memcmp( hdr->addr2, all_monitor_mac[i], 6) == 0)
        {    
            /* WiFi̽��������״̬ */
            sniffer_work_state++;
            /* ����Ŀ��mac �������� */
            sprintf( my_monitor_data[i].ID, "%02x%02x%02x%02x%02x%02x",
                    all_monitor_mac[i][0], all_monitor_mac[i][1], all_monitor_mac[i][2], 
                    all_monitor_mac[i][3], all_monitor_mac[i][4], all_monitor_mac[i][5]);
            my_monitor_data[i].Type = wifi_sniffer_packet_type2str(type);
            my_monitor_data[i].Rssi = ppkt->rx_ctrl.rssi;
            my_monitor_data[i].Channel = ppkt->rx_ctrl.channel; 
            my_monitor_data[i].Frequency += 1;
            if(my_monitor_data[i].Frequency < 2)
            {
                if((j = BC20_get_time()) == 0)
                {
                    my_monitor_data[i].Ts_in = BC20_get_time();
                    my_monitor_data[i].Ts_out = 0;
                }
                else
                {
                    my_monitor_data[i].Ts_in = j;
                    my_monitor_data[i].Ts_out = 0;
                }
            }   
            else
            {
                if((j = BC20_get_time()) == 0)
                    my_monitor_data[i].Ts_out = BC20_get_time();
                else
                    my_monitor_data[i].Ts_out = j;
            }
            
            ESP_LOGI(TAG, ""BLUE"id:%s, %d, %d, %d, %d, %lu, %lu", my_monitor_data[i].ID, my_monitor_data[i].Type,
                                               my_monitor_data[i].Rssi, my_monitor_data[i].Channel,
                                               my_monitor_data[i].Frequency,my_monitor_data[i].Ts_in,
                                               my_monitor_data[i].Ts_out);  
        }
    }

}

/* WiFi̽������ */
void wifi_sniffer_task(void) 
{
    uint8_t channel = 1;
    /* ̽��ģʽ��ʼ��        ���ҵ��ϱ�     */ 
    wifi_sniffer_init();
    /* ̽����ѭ�� */    
    while(1) 
    {       
        vTaskDelay(WIFI_INTERVAL);
        channel = (channel % WIFI_CHANNEL_MAX) + 1;
        wifi_sniffer_set_channel(channel);
    }
}



