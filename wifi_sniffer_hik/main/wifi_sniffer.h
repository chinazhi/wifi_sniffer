/*********************************************************************************************************
* File                : wifi_sniffer.h
* Hardware Environment: 
* Build Environment   : 
* Version             : V1.0
* Author              : hik_gz
* Time                :
* Brief               : 
*********************************************************************************************************/
#ifndef __WIFI_SNIFFER_H__
#define __WIFI_SNIFFER_H__

#include "includes.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#define	WIFI_CHANNEL_MAX	(13)    /* wifi最大信道 */
#define	WIFI_INTERVAL	    (500)   /* WiFi信道切换时间 */

#define ESP_WIFI_SSID      ("PU9340ST") /* 设备生成的WiFi名称 */
#define ESP_WIFI_PASS      ("hikchina") /* 设备生成的WiFi密码 */
#define PORT               (8080)       /* 设备TCP连接端口号 */

typedef struct {
	unsigned frame_ctrl:16;
	unsigned duration_id:16;
	uint8_t addr1[6]; /* receiver address */
	uint8_t addr2[6]; /* sender address */
	uint8_t addr3[6]; /* filtering address */
	unsigned sequence_ctrl:16;
	uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct {
	wifi_ieee80211_mac_hdr_t hdr;
	uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

extern void Read_Nvs_Mac(void);
extern esp_err_t event_handler(void *ctx, system_event_t *event);
extern void wifi_softap_init(void);
extern void tcp_server_task(void);
extern void wifi_sniffer_init(void);
extern void Set_Mqtt_User_Password(void); /*设备进入AP模式*/
extern void wifi_sniffer_set_channel(uint8_t channel);
extern  int wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type);
extern void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type);
extern void wifi_sniffer_task(void); 


#endif /* __WIFI_SNIFFER_H__ */

