/*********************************************************************************************************
*
* File                : bc20.h
* Hardware Environment: 
* Build Environment   : 
* Version             : V1.0
* Author              : hik_gz
* Time                :
* Brief               : 
*********************************************************************************************************/
#ifndef _BC20_H
#define _BC20_H

#include "includes.h"

/*MQTT服务器域名或IP，端口号*/
#define SERVER_DOMAIN "mqtt.zhigege.club"
#define SERVER_PORT (1883)

extern int     MQTT_ERR_flag; /*MQTT错误标志位*/
extern char    BC20_IMSI[30];

extern void     WaitUartIdle(void);     /*判断串口使用状态*/
extern void     ReleaseUart(void);      /*串口闲置状态*/
extern int8_t   AT(void);
extern int8_t   ATCmd_Send(const char *ATCmd, uint32_t Len);
extern int8_t   Reply_Recv(void);
extern int8_t   BC20_Check_CSQ(void);
extern int32_t  BC20_Check_ESP(void);
extern unsigned long BC20_get_time(void);
extern int32_t  BC20_Check_Network_status(void);/*查询模块整体网络状态*/
extern int32_t  BC20_Switch_GNSS(int i);
extern int32_t  BC20_Check_GNSS(char *NMEA);    /* BC20 GNSS搜星(NMEA/RMC)*/
extern int32_t  Test_Check_GNSS(void);
extern void     BC20_Module_switch(int i);
extern void     Check_BC20(void);
extern void     MQTT_ERROR_LED_task(void);
extern void     GNSS_Nvs_task(void);


#endif


