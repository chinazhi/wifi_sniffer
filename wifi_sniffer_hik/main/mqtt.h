/*********************************************************************************************************
*
* File                : mqtt.h
* Hardware Environment: 
* Build Environment   : 
* Version             : V1.0
* Author              : hik_gz
* Time                :
* Brief               : 
*********************************************************************************************************/
#ifndef _MQTT_H
#define _MQTT_H

#include "includes.h"

typedef struct {
    char    ID[15];
    int     Type;
    signed int    Rssi;
    unsigned int  Channel;
    unsigned int  Frequency;
    unsigned long Ts_in;
    unsigned long Ts_out;
}monitor_data;

typedef struct {
    char *function;
    char *StationID;
    char *StationX;
    char *StationY;
    int  timesync;
    char *usrnm;
    char *usrpw;
    char *srvip;
    int  macNum;
    int  reboot;
}config_msg;

typedef struct {
    char *function;
    char *result;
    int  nb;
    int  vol;
    int  gnss;
    int  macNum;
}Factory_msg;


/*最大监测保存MAC数量*/
#define MAX_MAC_NUM    (20)

extern char    StationID[20];
extern monitor_data my_monitor_data[MAX_MAC_NUM];/* 设备监测的数据信息 */
extern unsigned int sniffer_work_state;          /* WiFi探针数据状态指示标志.0为未产生数据,大于0为正在产生数据                     */

extern char    Subscribe_test_name[60];
extern char    Subscribe_topic_name[60];
extern char    Publish_topic_name[60];
extern char    stat_topic_name[60];
extern char    rsp_topic_name[60];

extern int32_t Set_MQTT_Client(int tcpconnectID, char* host_name, unsigned int port);
extern int8_t  Check_MQTT_Client(void);
extern int32_t Connect_MQTT_Server(int tcpconnectID, char* clientID, char* username, char* password);
extern int8_t  Check_MQTT_Server(void);
extern int32_t Disconnect_MQTT_Server(int tcpconnectID);
extern int32_t Subscribe_MQTT_Topics(int tcpconnectID, unsigned int msgID, char* topic, size_t topic_len, int qos);
extern int32_t Unsubscribe_MQTT_Topics(int tcpconnectID, unsigned int msgID, char* topic);
extern int32_t MQTT_Publish_Msg(int tcpconnectID, unsigned int msgID, int qos, int retain, char* topic, 
                                                            size_t topic_len, char* msg, size_t msg_len );

extern int32_t Recive_MQTT_Buff(void);
extern int32_t MQTT_Connect_Subscribe(int tcpconnectID, char* host_name, unsigned int port,
											char* clientID , unsigned int msgID, char* topic, size_t topic_len, int qos);
extern void    try_connect_subscribe(int tcpconnectID, char* host_name, unsigned int port, char* clientID, unsigned int msgID,
                                            char* topic,  size_t topic_len, int qos);

extern void    recv_and_save(void);
extern char*   Request_json_data(char* StationID);
extern char*   Reply_json_data(int i, int* json_len);
extern char*   Monitor_json_data(void);
extern char*   status_json_data(char* local_appsw, char* local_service, char* local_ver,  char* local_StationID, char* local_imsi);
extern void    Publish_Monitor_Data_task(void);

extern void    Factory_test(void);

#endif


