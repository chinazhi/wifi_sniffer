/*********************************************************************************************************
*
* File                : mqtt.c
* Hardware Environment: esp8266
* Build Environment   : freertos
* Version             : V1.0
* By                  : hik_gz
* Time                :
*********************************************************************************************************/
#include "mqtt.h"
#include "cJSON.h"
#include "my_nvs.h"
#include "string.h"
#include "app_main.h"
#include "bc20.h"
#include "wifi_sniffer.h"


#include "adc.h"

#define TRUE  1
#define FALSE 0

static const char* TAG = "MQTT";

static config_msg system_config;               /* 服务器下发的设备配置信息 */

static Factory_msg Factory_config;             /* 工厂服务器下发的设备配置信息 */

monitor_data my_monitor_data[MAX_MAC_NUM];     /* 设备监测的数据信息 */

unsigned int sniffer_work_state = 0;    /* WiFi探针数据状态指示标志 0为未产生数据 大于0为正在产生数据                     */

char    ATCMD_RceBuff[120];             /* 接收AT命令buff */
char    Subscribe_topic_name[60] = {0}; /* 命令订阅主题 */
char    Subscribe_test_name[60] = {0};  /* 工厂测试订阅主题 */
char    Publish_topic_name[60] = {0};   /* 数据发布主题 */
char    stat_topic_name[60] = {0};      /* 命令状态发布主题 */
char    rsp_topic_name[60] = {0};       /* 命令请求、回复发布主题 */
char    StationID[20] = {0};            /* 设备身份,当前为设备mac地址 */


/*************************************************
Function:    Set_MQTT_Client
Description: 配置MQTT客户端（即告诉客户端要往哪个地址、端口发数据）并开启网络
Input:       tcpconnectID：MQTT的套接字标识符。范围是0-5
             host_name：服务器的地址。它可以是IP地址或域名。最大大小为100字节
             port：服务器的端口。范围是1-65535             
Return:      0表示成功，-1表示失败或超时
Others:      AT+QMTOPEN  +QMTOPEN: <tcpconnectID>,<result>
*************************************************/
int32_t Set_MQTT_Client(int tcpconnectID, char* host_name, unsigned int port)
{
    int timeout = 0;
    char Recv_flags[25] = {0};
    char ATCmd[60] = {0};
    sprintf(ATCmd, "AT+QMTOPEN=%d,\"%s\",%d\r\n", tcpconnectID, host_name, port);
	WaitUartIdle();

	ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	if(Reply_Recv())
    {   
        vTaskDelay(300 / portTICK_RATE_MS);
        sprintf(Recv_flags, "+QMTOPEN: %d,", tcpconnectID);
        /* 等待响应，最大超时75s，由网络决定 目前设定为2*6s*/
        while(strstr((char*)ATCMD_RceBuff, Recv_flags) == NULL)
        {
            vTaskDelay(2000 / portTICK_RATE_MS);
            Reply_Recv();
            timeout++;
            if(timeout == 6)
            {
                ESP_LOGI(TAG, "Set MQTT Client timeout!");
                ReleaseUart();
                return -1;
            }
        }  
        /* 收到客户端网络打开返回信息,判断设置情况*/
		memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTOPEN: %d,0", tcpconnectID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ESP_LOGI(TAG, "Set MQTT Client success");
            ReleaseUart();
            /* 0表示打开MQTT客户端成功 */
			return 0; 
        }
        else
        {
            ESP_LOGI(TAG, "Set MQTT Client error:%s", ATCMD_RceBuff);
			ReleaseUart();
            /* -1表示打开MQTT客户端异常 */
            return -1;
        }          
    }
    ESP_LOGI(TAG, "Set MQTT Client error");
    ReleaseUart();
	return -1;
}


/*************************************************
Function:    Check_MQTT_Client
Description: 查询MQTT客户端开启信息
Return:      TRUE:为查询成功，并打印信息
             FALSE：为查询失败
other:       AT+QMTOPEN?  +QMTOPEN: <tcpconnectID>,“<host_name>”,<port>
*************************************************/
int8_t Check_MQTT_Client(void)
{
    char * pTmp1 = NULL;
	char * pTmp2 = NULL;
    char * ATCmd="AT+QMTOPEN?\r\n";
    char Recv_info[50] = {0};
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	Reply_Recv();
	if((pTmp1 = strstr((char*)ATCMD_RceBuff, "+QMTOPEN:")) != NULL)
    {     
		pTmp1 += strlen("+QMTOPEN:");
		pTmp2 = strstr(pTmp1,"\r\n");
		if(pTmp2 != NULL)
		{
			memcpy(Recv_info, pTmp1, pTmp2-pTmp1);
            ESP_LOGI(TAG, "MQTT客户端配置信息:%s", Recv_info);
		}
        ReleaseUart();
	    return TRUE;
    } 	 
	ReleaseUart();
	return FALSE;
} 


/*************************************************
Function:    Connect_MQTT_Server
Description: 将客户机连接到MQTT服务器（即告诉服务器我是谁）
Input:       tcpconnectID：MQTT的套接字标识符。范围是0-5
             clientID：客户机标识符字符串
             
Return:      0表示成功，-1表示失败或超时
Others:      AT+QMTCONN  +QMTCONN: <tcpconnectID>,<result>[,<ret_code>]
*************************************************/
int32_t Connect_MQTT_Server(int tcpconnectID, char* clientID, char* username, char* password)
{
    int  timeout = 0;
    char Recv_flags[25] = {0};
    char ATCmd[80] = {0};
    sprintf(ATCmd, "AT+QMTCONN=%d,\"%s\",\"%s\",\"%s\"\r\n", tcpconnectID, clientID, username, password);
	WaitUartIdle();
    
	ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	if(Reply_Recv())
    { 
        vTaskDelay(300 / portTICK_RATE_MS);
        sprintf(Recv_flags, "+QMTCONN: %d,", tcpconnectID);
        while(strstr((char*)ATCMD_RceBuff, Recv_flags) == NULL)
        {
            /* 等待响应，默认10s，由网络决定*/
            vTaskDelay(1000 / portTICK_RATE_MS);
            Reply_Recv();
            timeout++;
            if(timeout == 10)
            {
                ESP_LOGI(TAG, "Connect MQTT Server timeout!");
                ReleaseUart();
                return -1;
            }
        }
        
        /* 收到客户端连接MQTT服务器返回信息,判断连接情况*/
		memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTCONN: %d,0", tcpconnectID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ESP_LOGI(TAG, "Connect MQTT Server success!");
            ReleaseUart();
			return 0;/* 0表示客户端连接服务器成功 */
         }
         else
         {
            ESP_LOGI(TAG, "Connect MQTT Server error；%s!", ATCMD_RceBuff);
			ReleaseUart();
            /* -1表示客户端连接服务器异常 */
            return -1;
         }
    }    
    ESP_LOGI(TAG, "Connect MQTT Server error!");          
	ReleaseUart();
	return -1;
}


/*************************************************
Function:    Check_MQTT_Server
Description: 查询客户端连接MQTT服务器状态
Return:      TRUE:为查询成功，并打印信息 <state> 1为MQTT初始化，2为MQTT正在连接，3为MQTT已连接，4为MQTT未连接
             FALSE：为查询失败
other:       AT+QMTCONN?  +QMTCONN: <tcpconnectID>,<state>
*************************************************/
int8_t Check_MQTT_Server(void)
{
    char * ATCmd="AT+QMTCONN?\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(600 / portTICK_RATE_MS);
	Reply_Recv();
	if(strstr((char*)ATCMD_RceBuff, "3") != NULL)
    {      
        ESP_LOGI(TAG, "MQTT connect OK!");
        ReleaseUart();
        return TRUE;
    }
    ESP_LOGI(TAG, "MQTT connect error!");
	ReleaseUart();
	return FALSE;
} 


/*************************************************
Function:    Disconnect_MQTT_Server
Description: 断开客户机与MQTT服务器的连接
Input:       tcpconnectID：MQTT的套接字标识符。范围是0-5
                         
Return:      0表示成功，-1表示失败或超时
Others:      AT+QMTDISC  +QMTDISC: <tcpconnectID>,<result>
*************************************************/
int32_t Disconnect_MQTT_Server(int tcpconnectID)
{
    char Recv_flags[25] = {0};
    char ATCmd[30] = {0};
    sprintf(ATCmd, "AT+QMTDISC=%d\r\n", tcpconnectID);
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	Reply_Recv();
    sprintf(Recv_flags, "+QMTDISC: %d,", tcpconnectID);
	if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL) 
    {
        /* 收到客户端断开MQTT服务器返回信息*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTDISC: %d,0", tcpconnectID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ESP_LOGI(TAG, ""BLUE"Client disconnected success!");	
            ReleaseUart();
			return 0;   /* 0表示客户端断开连接成功 */
        }
        else
        {
            ESP_LOGI(TAG, "Client disconnected error!");
		    ReleaseUart();      
            return -1;  /* -1表示客户端断开连接异常 */
        }    
    }
    
    ESP_LOGI(TAG, ""RED"Client disconnected error!");    	 
	ReleaseUart();
	return -1;
}


/*************************************************
Function:    Subscribe_MQTT_Topics
Description: 客户端配置并订阅一个MQTT主题
Input:       tcpconnectID：MQTT的套接字标识符。范围是0-5
             msgID：数据包的消息标识符。范围是1-65535
             topic：客户端希望订阅或取消订阅的主题
             qos：客户端希望发布消息的QoS级别。0为最多一次;1为至少一次;2为恰好一次
             
Return:      0表示成功，-1表示失败或超时
Others:      AT+QMTSUB  +QMTSUB: <tcpconnectID>,<msgID>,<result>[,<value>]
*************************************************/
int32_t Subscribe_MQTT_Topics(int tcpconnectID, unsigned int msgID, char* topic, size_t topic_len, int qos)
{
    int  timeout = 0;
    char Recv_flags[25] = {0};
    size_t n = sizeof("AT+QMTSUB=,,"",\r\n") + 12 + topic_len;
    char* ATCmd = (char*)  malloc(n);
    sprintf(ATCmd, "AT+QMTSUB=%d,%d,\"%s\",%d\r\n", tcpconnectID, msgID, topic, qos);
	WaitUartIdle();

    ATCmd_Send(ATCmd, n);
	vTaskDelay(300 / portTICK_RATE_MS);
	if(Reply_Recv()) 
    {
        vTaskDelay(300 / portTICK_RATE_MS);
        sprintf(Recv_flags, "+QMTSUB: %d,%d", tcpconnectID, msgID);
        /* 等待响应，默认40s，由网络决定, 目前为12Ss*/
        while(strstr((char*)ATCMD_RceBuff, Recv_flags) == NULL)
        {
            vTaskDelay(2000 / portTICK_RATE_MS);
            Reply_Recv(); 
            timeout++;
            if(timeout == 6)
            {
                ESP_LOGI(TAG, "Subscribe MQTT Topics timeout!");
                free(ATCmd);
                ReleaseUart();
                return -1;
            }
        }
        
        /* 收到客户端订阅MQTT主题返回信息，判断订阅情况*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTSUB: %d,%d,0", tcpconnectID, msgID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
			/* 0表示客户端订阅MQTT主题成功 */
            ESP_LOGI(TAG, "Subscribe MQTT Topics success!");
            free(ATCmd);
            ReleaseUart();
			return 0;
        }
        else
        {
            ESP_LOGI(TAG, "Subscribe MQTT Topics error:%s!", ATCMD_RceBuff);
            /* -1表示客户端订阅MQTT主题异常 */
            free(ATCmd);
            ReleaseUart();
            return -1;
        }
    }
    ESP_LOGI(TAG, "Subscribe MQTT Topics error!");   	 
    free(ATCmd);
    ReleaseUart();
	return -1;
}


/*************************************************
Function:    Unsubscribe_MQTT_Topics
Description: 客户端取消订阅MQTT主题
Input:       tcpconnectID：MQTT的套接字标识符。范围是0-5
             msgID：数据包的消息标识符。范围是1-65535
             topic：客户端希望订阅或取消订阅的主题
             
Return:      0表示成功，-1表示失败或超时
Others:      AT+QMTUNS  +QMTUNS: <tcpconnectID>,<msgID>,<result>
*************************************************/
int32_t Unsubscribe_MQTT_Topics(int tcpconnectID, unsigned int msgID, char* topic)
{
    int  timeout = 0;
    char Recv_flags[25] = {0};
    char ATCmd[60] = {0};
    sprintf(ATCmd, "AT+QMTUNS=%d,%d,\"%s\"\r\n", tcpconnectID, msgID, topic);
	WaitUartIdle();

	ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);	
    if(Reply_Recv()) 
    {
        vTaskDelay(300 / portTICK_RATE_MS);
        sprintf(Recv_flags, "+QMTUNS: %d,%d", tcpconnectID, msgID);
        /* 等待响应，默认40s，由网络决定，目前15s*/
        while(strstr((char*)ATCMD_RceBuff, Recv_flags) == NULL)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);
            Reply_Recv(); 
            timeout++;
            if(timeout == 15)
            {
                ESP_LOGI(TAG, "Unsubscribe MQTT Topics timeout!");
                return -1;
            }
        }
        
        /* 收到客户端取消订阅MQTT主题返回信息,判断是否成功*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTUNS: %d,%d,0", tcpconnectID, msgID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ReleaseUart();
            ESP_LOGI(TAG, "Unsubscribe MQTT Topics success!");
			/* 0表示客户端取消订阅MQTT主题成功 */
			return 0;
        }
        else
        {
            ESP_LOGI(TAG, "Unsubscribe MQTT Topics error:%s!", ATCMD_RceBuff);
			ReleaseUart();
            /* -1表示客户端取消订阅MQTT主题异常 */
            return -1;
        }
    }
    ESP_LOGI(TAG, "Unsubscribe MQTT Topics error!");  	 
	ReleaseUart();
	return -1;
}


/*************************************************
Function:    MQTT_Publish_Msg
Description: 客户端发布MQTT消息
Input:       tcpconnectID：MQTT的套接字标识符。范围是0-5
             msgID：数据包的消息标识符。范围是1-65535
             qos：客户端希望发布消息的QoS级别。0为最多一次;1为至少一次;2为恰好一次
             retain:在消息被发送到当前订阅服务器后，服务器是否保留该消息。0为不保留，1为保留
             topic：需要发布到的主题
             msg：需要发布的消息
             
Return:      0表示成功，-1表示失败或超时
Others:      AT+QMTPUB  +QMTPUB: <tcpconnectID>,<msgID>,<result>[,<value>]
*************************************************/
int32_t MQTT_Publish_Msg(int tcpconnectID, unsigned int msgID, int qos, int retain, char* topic, 
                                                            size_t topic_len, char* msg, size_t msg_len )
{
    int timeout = 0;
    char Recv_flags[20] = {0};
    size_t n = sizeof("AT+QMTPUB=,,,,"",""\r\n") + 16 + topic_len + msg_len;
    char* ATCmd = (char*) malloc(n);
    memset(ATCmd, 0, n);
    sprintf(ATCmd, "AT+QMTPUB=%d,%d,%d,%d,\"%s\",\"%s\"\r\n", tcpconnectID, msgID, qos, retain, topic, msg);

    uart_flush(UART_NUM_0);
    printf("%s\r\n", ATCmd);
    
	WaitUartIdle();
	ATCmd_Send(ATCmd, n);
	vTaskDelay(300 / portTICK_RATE_MS);    
	if(Reply_Recv())
    { 
        vTaskDelay(300 / portTICK_RATE_MS);
        sprintf(Recv_flags, "+QMTPUB: %d,%d", tcpconnectID, msgID);
        /* 等待响应，默认40s，由网络决定，目前设定为8*2s*/
        while(strstr((char*)ATCMD_RceBuff, Recv_flags) == NULL)
        {
            vTaskDelay(2000 / portTICK_RATE_MS);
            Reply_Recv();
            timeout++;
            if(timeout == 8)
            {
                ESP_LOGI(TAG, "The client publishes a MQTT message timeout!");
                memset(ATCmd, 0, n);
                free(ATCmd);
                ReleaseUart();
                return -1;
            }
        }
        /* 收到客户端发布MQTT消息返回信息*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTPUB: %d,%d,0", tcpconnectID, msgID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {  
            ESP_LOGI(TAG, "Client published the message success!");
            uart_flush(UART_NUM_0);
            memset(ATCmd, 0, n);
            free(ATCmd);
            ReleaseUart();
			return 0;   /* 0表示客户端发布MQTT消息成功 */
        }
        else
        {
            ESP_LOGI(TAG, "MQTT Publish Msg error:%s!", ATCMD_RceBuff);
            uart_flush(UART_NUM_0);
            memset(ATCmd, 0, n);
            free(ATCmd);
            ReleaseUart();
            return -1;  /* -1表示客户端发布MQTT消息异常 */
        }
           
    }
    ESP_LOGI(TAG, "MQTT Publish Msg error!");
    uart_flush_input(UART_NUM_0);
    memset(ATCmd, 0, n);
    free(ATCmd);
    ReleaseUart();
	return -1;
}

/*************************************************
Function:    
Description: 将要接收到的json数据分解出来                         
Return:     
Others:     
*************************************************/
int32_t Recive_json_data(char * receiveData)
{
    uint8_t monitor_mac[6] = {0};
    /* JSON字符串到cJSON格式 */
    cJSON *pJsonRoot = cJSON_Parse(receiveData);
    if (pJsonRoot != NULL)
    {
        /* 获取字段值,判断字段类型，若是则保存至本地 */  
        cJSON * function = cJSON_GetObjectItem( pJsonRoot, "function");
        if (function)                                      
        {
            if (cJSON_IsString(function))                            
            {
                system_config.function = function->valuestring;
            }
        }  
        cJSON *  timesync = cJSON_GetObjectItem( pJsonRoot, "timesync");
        if (timesync)                                      
        {
            if (cJSON_IsNumber(timesync))                           
            {
                system_config.timesync = timesync->valueint;
            }
        }       
        cJSON * StationID = cJSON_GetObjectItem( pJsonRoot, "StationID");
        if (StationID)                                      
        {
            if (cJSON_IsString(StationID))                            
            {
                system_config.StationID = StationID->valuestring;
                //ESP_LOGE(TAG, "StationID:%s", system_config.StationID);
            }
        }       
        cJSON * StationX = cJSON_GetObjectItem( pJsonRoot, "StationX");
        if (StationX)                                      
        {
            if (cJSON_IsString(StationX))                            
            {
                system_config.StationX = StationX->valuestring;
                //ESP_LOGE(TAG, "StationX:%s", system_config.StationX);
            }
        }
        cJSON * StationY = cJSON_GetObjectItem( pJsonRoot, "StationY");
        if (StationY)                                      
        {
            if (cJSON_IsString(StationY))                            
            {
                system_config.StationY = StationY->valuestring;
                //ESP_LOGE(TAG, "StationY:%s", system_config.StationY);
            }
        } 
        cJSON * usrnm = cJSON_GetObjectItem( pJsonRoot, "usrnm");
        if (usrnm)                                      
        {
            if (cJSON_IsString(usrnm))                            
            {
                system_config.usrnm = usrnm->valuestring;
                //ESP_LOGE(TAG, "usrnm:%s", system_config.usrnm);
            }
        }     
        cJSON * usrpw = cJSON_GetObjectItem( pJsonRoot, "usrpw");
        if (usrpw)                                      
        {
            if (cJSON_IsString(usrpw))                            
            {
                system_config.usrpw = usrpw->valuestring;
                //ESP_LOGE(TAG, "usrpw:%s", system_config.usrpw);
            }
        }  
        cJSON * srvip = cJSON_GetObjectItem( pJsonRoot, "srvip");
        if (srvip)                                      
        {
            if (cJSON_IsString(srvip))                            
            {
                system_config.srvip = srvip->valuestring;
                //ESP_LOGE(TAG, "srvip:%s", system_config.srvip);
            }
        }      
        cJSON *  macNum = cJSON_GetObjectItem( pJsonRoot, "macNum");
        if (macNum)                                      
        {
            if (cJSON_IsNumber(macNum))                           
            {
                system_config.macNum = macNum->valueint;
            }
        }  
        if(system_config.macNum > 0)
            save_num_nvs("macNum", system_config.macNum);
        
        /*解析字段数组内容      */
        cJSON *pArryInfo = cJSON_GetObjectItem(pJsonRoot, "macList");
        cJSON *pInfoItem = NULL;
        cJSON *pInfoObj = NULL;
        if(pArryInfo)                                    // 判断info字段是否json格式
        {
            int arryLength = cJSON_GetArraySize(pArryInfo);          // 获取数组长度
            int i;
            for (i = 0; i < arryLength; i++)
            {
                pInfoItem = cJSON_GetArrayItem(pArryInfo, i);        // 获取数组中JSON对象
                if(NULL != pInfoItem)
                {
                    pInfoObj = cJSON_GetObjectItem(pInfoItem,"mac_adders");// 解析mac_adders字段字符串内容   
                    if(pInfoObj)
                    {
                        char * pMacAdress = pInfoObj->valuestring;
                        String_to_hex(pMacAdress, monitor_mac);
                        save_mac_nvs( i, monitor_mac, (size_t) sizeof(monitor_mac));
                    }
                }                                                      
            }
        }
        
        cJSON *  reboot = cJSON_GetObjectItem( pJsonRoot, "reboot");
        if (reboot)                                      
        {
            if (cJSON_IsNumber(reboot))                           
            {
                system_config.reboot = reboot->valueint;
            }
        } 
        ESP_LOGE(TAG, ""GREEN"json is ok!");
        /* 关闭cjson */
        cJSON_Delete(pJsonRoot);
        return 0;    
    }
    
    ESP_LOGE(TAG, "json pack into cjson error!");
    /* 关闭cjson */
    cJSON_Delete(pJsonRoot);
    return -1;

}


/*************************************************
Function:    Recive_MQTT_Buff
Description: 接收JSON格式数据包，并将数据储存在nvs中
Return:       0为正常接收到 1为服务器超时返回 -1为接收错误
Others:   +QMTRECV: <tcpconnectID>,<msgID>,<topic>,<payload>   
*************************************************/
int32_t Recive_MQTT_Buff(void) 
{      
    size_t size;
    int     timeout = 0;
    char    *ptmp2 = NULL;
    char    *RceBuff = NULL;
    uart_flush_input(UART_NUM_0);
    WaitUartIdle();
    /*等待平台下发数据       */
    while(1)
    {      
        vTaskDelay(5000 / portTICK_RATE_MS);     
        uart_get_buffered_data_len(UART_NUM_0 , &size);
        /* 判断串口缓冲区是否有数据 */
        while((int) size < 86)
        {
           timeout++;
           uart_flush_input(UART_NUM_0);
           ESP_LOGE(TAG, "The server is not responding!");
           vTaskDelay(5000 / portTICK_RATE_MS);
           uart_get_buffered_data_len(UART_NUM_0 , &size);
           /* 超过一定时间无响应则退出 5s*24 */
           if(timeout == 24)
           {
               ESP_LOGE(TAG, "Timeout waiting for server reply!");
               free(RceBuff);
               ReleaseUart();
               return 1;
           }
        }       
        /* 申请空间取出数据并判断是否是指定数据若不是舍弃数据 */
        RceBuff = (char*)  malloc(size);
        memset(RceBuff, 0, size);
        uart_read_bytes(UART_NUM_0, (uint8_t*)RceBuff, (uint32_t)size, 20 / portTICK_RATE_MS); 
        if((strstr((char*)RceBuff, "+QMTRECV")) != NULL)
            break;
        else
        {
            free(RceBuff);
            ESP_LOGE(TAG, "Error receiving message!");
            uart_flush_input(UART_NUM_0);
        }
    }
    
    /* 获取json格式数据  */
    if((ptmp2=strstr(RceBuff, "req")) != NULL)
    {   
        /* 去除开头字符串'"' */
        ptmp2 += 6;
        /*把末尾'"'去掉，若有其他无用字符产生一并去除 */
        char *p = ptmp2;    
        p += strlen(ptmp2);
        while(*p != '"')  
        {
            *p = 0;
            p--;
        }
        *p = 0; 
        if(0 == Recive_json_data(ptmp2))
        {
            free(RceBuff);
            ReleaseUart();
    	    return 0;
        }
        else
        {
            ESP_LOGE(TAG, "recv get json error");
            free(RceBuff);
            ReleaseUart();
            return -1; 
        }
	}
    ESP_LOGE(TAG, "recv error");
    free(RceBuff);
    ReleaseUart();
    return -1;     
}


/*************************************************
Function: 	 MQTT_Connect_Subscribe
Description: 注册连接MQTT服务器，订阅服务。
Input: 		 tcpconnectID：MQTT的套接字标识符。范围是0-5； host_name：服务器IP；          port：端口；
			 clientID：设备名称；msgID：数据包的消息标识符。范围是1-65535；topic：订阅主题名；QOS：级别（0-2）
Return:		 0 成功；-1 失败。
other：      当订阅主题为NULL；表示不订阅主题，msgID，qos可填0。
example:    MQTT_Connect_Subscribe(0, "47.101.31.29", 1883, "PU9440ST" , 1, "server", 0)
*************************************************/
int32_t MQTT_Connect_Subscribe(int tcpconnectID, char* host_name, unsigned int port,
											char* clientID , unsigned int msgID, char* topic, size_t topic_len, int qos)
{
    if( BC20_Check_ESP() == 1)
    {
        ESP_LOGE(TAG, "Network  is ok, start connecting subscription platform!");

        if(-1 == Set_MQTT_Client( tcpconnectID, host_name, port))
        {
            ESP_LOGE(TAG, "Set MQTT Client ERROR!");
			return -1;
        }

        /* 读取MQTT用户名密码 */
        char username[20] = {0};
        char password[20] = {0};
        size_t length = sizeof(username);
        get_str_nvs("username", username, &length); 
        length = sizeof(password);
        get_str_nvs("password", password, &length);
        
        if(-1 == Connect_MQTT_Server( tcpconnectID, clientID, username, password))
        {
            ESP_LOGE(TAG, "Connect MQTT Server ERROR!");
			return -1;
        }

		if( topic != NULL)
		{
        	if(-1 == Subscribe_MQTT_Topics(tcpconnectID, msgID, topic, topic_len, qos))
        	{
                ESP_LOGE(TAG, "Subscribe MQTT Topics ERROR!");
				return -1;
        	}
		}
		return 0;
    }
    else
    {
        ESP_LOGE(TAG, "Network is not ok, please check BC20 status!");
		return -1;
    }
}

/*************************************************
Function: try_connect_subscribe 
Description: 尝试连接订阅MQTT。
other：  如果一直失败会一直尝试连接  如果topic为空不执行订阅
*************************************************/
void try_connect_subscribe(int tcpconnectID, char* host_name, unsigned int port, char* clientID, unsigned int msgID,
                                            char* topic,  size_t topic_len, int qos)
{
    int i = 0;
    /* 异常指示灯线程 */
    xTaskCreate((TaskFunction_t)MQTT_ERROR_LED_task, "MQTT_ERROR_LED_task", 256, NULL, 5, NULL); 
    Disconnect_MQTT_Server(0);
    while(-1 == MQTT_Connect_Subscribe(tcpconnectID, host_name, port, clientID, msgID, topic, topic_len, qos))
    {   
        i++; 
        ESP_LOGI(TAG, ""RED"Connection failed! 3s after reconnection!");
        /* 如果2次尝试连接订阅失败,则检测BC20 */
        if( i == 2 )
        {
            i = 0;
            Check_BC20();    
        }
        Disconnect_MQTT_Server(0);
        vTaskDelay(3000 / portTICK_RATE_MS);
        AT();   /* 同步串口 */
    }
    MQTT_ERR_flag = 0;
}

/*************************************************
Function: recv_and_save 
Description: 接收服务器消息并保存 
*************************************************/
void recv_and_save(void)
{   
    int i = 0, json_len = 0;
    AT();
    /*  publish mac config request @witt */
    while (-1 == MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Request_json_data(StationID), 225))
    {
        
        ESP_LOGE(TAG, "Msg %d Failed to send!", i++);
        vTaskDelay(1000 / portTICK_RATE_MS); 
        /* 连续失败3次，尝试重新建立MQTT连接 */
        if(i == 3)   
        {
            i = 0;
            Disconnect_MQTT_Server(0);
            try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_topic_name, sizeof(Subscribe_topic_name), 1);
        }
        AT(); 
    } 
    AT();
    /* 尝试接收,若无,转为本地MAC监测 */
    while((i = Recive_MQTT_Buff()) == -1)
    {   
        AT();
        /*  publish mac request over @witt */
        MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Reply_json_data(-1, &json_len), json_len+50);
        ESP_LOGE(TAG, ""RED"Get Server data error! Get Server data again");      
    }
    
    if( i == 0) 
    {
        AT();
        /*  publish mac request over @witt */
        MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Reply_json_data(0, &json_len), json_len+50);
        ESP_LOGE(TAG, "Get Server data success! MAC to start monitoring!"); 
    }
    else if(i == 1)
    {
        AT();
        /*  publish mac request over @witt */
        MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Reply_json_data(1, &json_len), json_len+50);
        ESP_LOGE(TAG, ""YELLOW"Get Server data timeout! Enable local MAC to start monitoring!");
    }
        
}

/*************************************************
Function:    
Description: 数据转化为json格式                       
Return:     
Others:     
*************************************************/
 char* Request_json_data(char * StationID)
 {
     /* 创建JSON根部结构体   、  创建JSON子叶结构体 */
     cJSON *pRoot = cJSON_CreateObject();
     cJSON *pValue = cJSON_CreateObject();
     
     /*  添加字符串类型数据到根部结构体  */
     cJSON_AddStringToObject( pRoot,"function", "config_req");
     cJSON_AddStringToObject( pRoot,"result", "request");
     cJSON_AddStringToObject( pRoot,"StationID", StationID);  
     
     cJSON_AddItemToObject(pRoot, "params",pValue);
     /* 添加整型数据到子叶结构体 */
     cJSON_AddNumberToObject(pValue,"timesync",0);
     cJSON_AddNumberToObject(pValue,"macNum",0);
     
     /* 从cJSON对象中获取有格式的JSON对象 */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* 释放cJSON_Print ()分配出来的内存空间 */
     cJSON_free((void *) s);
     
     /* 释放cJSON_CreateObject ()分配出来的内存空间 */
     cJSON_Delete(pRoot);
 
     return d;
     
 }
 
 /* json回复服务器 0为正常接收到 1为服务器超时返回 -1为接收错误 */
 char* Reply_json_data(int i, int* json_len)
 {
     uint8_t monitor_mac[6] = {0};
     char local_mac[12] = {0};
     size_t length = sizeof(monitor_mac);

     /* 创建JSON根部结构体/创建JSON子叶结构体 */
     cJSON *pRoot = cJSON_CreateObject();
     cJSON *pValue = cJSON_CreateObject();
     if(i == 0)
     {
         /*  添加字符串类型数据到根部结构体  */
         cJSON_AddStringToObject( pRoot,"function", "config");
         cJSON_AddStringToObject( pRoot,"result", "success");
         
         /*  添加数据到子叶结构体  */
         cJSON_AddItemToObject(pRoot, "params", pValue);
         cJSON_AddNumberToObject(pValue,"timesync", system_config.timesync);
         cJSON_AddStringToObject(pValue,"StationID", system_config.StationID);
         cJSON_AddStringToObject(pValue,"StationX", system_config.StationX);
         cJSON_AddStringToObject(pValue,"StationY", system_config.StationY);
         cJSON_AddStringToObject(pValue,"usrnm", system_config.usrnm);
         cJSON_AddStringToObject(pValue,"usrpw", system_config.usrpw);
         cJSON_AddStringToObject(pValue,"srvip", system_config.srvip);
         cJSON_AddNumberToObject(pValue,"macNum", system_config.macNum);
         cJSON_AddNumberToObject(pValue,"reboot", system_config.reboot);
         /*  创建数组类型结构体到子叶结构体  */    
         cJSON * pArray = cJSON_CreateArray();
         cJSON_AddItemToObject(pValue,"macList",pArray);
         for(int i = 0; i < system_config.macNum; i++)
         {
             /*  创建子叶结构体到组类型结构体  */
             cJSON * pArray_relay = cJSON_CreateObject();
             cJSON_AddItemToArray(pArray, pArray_relay);
       
             get_mac_nvs(i, monitor_mac, &length);
             sprintf(local_mac, "%02x%02x%02x%02x%02x%02x",monitor_mac[0], monitor_mac[1], monitor_mac[2], monitor_mac[3], monitor_mac[4], monitor_mac[5]);

             cJSON_AddStringToObject(pArray_relay, "mac_adders", local_mac);
         }
     } 
     else if(i == 1)
     {
        /*  添加字符串类型数据到根部结构体  */
         cJSON_AddStringToObject( pRoot,"function", "config");
         cJSON_AddStringToObject( pRoot,"result", "other");
     }
     else if(i == -1)
     {
        /*  添加字符串类型数据到根部结构体  */
         cJSON_AddStringToObject( pRoot,"function", "config");
         cJSON_AddStringToObject( pRoot,"result", "error");       
     }
     /* 从cJSON对象中获取有格式的JSON对象 */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* 释放cJSON_Print ()分配出来的内存空间 */
     cJSON_free((void *) s);
     
     /* 释放cJSON_CreateObject ()分配出来的内存空间 */
     cJSON_Delete(pRoot);
        
     *json_len = strlen(d);
     
     return d;
     
 }
 
  /* json格式监控数据上报 */
 char* Monitor_json_data(void)
 {
     int  i = 0, num1 = 0, num2 = 0;
     /* 创建JSON根部结构体   、  创建数组类型结构体 */
     cJSON *pRoot = cJSON_CreateObject();
         
     /*  添加数据到根部结构体            */
     for(num1 = 0; num1 < MAX_MAC_NUM; num1++)
     {
        if(my_monitor_data[num1].Frequency > 0)
            num2++;
     }
     cJSON_AddNumberToObject(pRoot,"num",num2);     
     cJSON_AddStringToObject( pRoot,"StationID", StationID);  
 
     /* 创建添加数组到根部结构体 */
     cJSON * pArray = cJSON_CreateArray();
     cJSON_AddItemToObject(pRoot,"dp",pArray);
 
     cJSON * pArray_relay[MAX_MAC_NUM];
     
     for(num1 = 0; num1 < MAX_MAC_NUM; num1++)
     {
        if(my_monitor_data[num1].Frequency > 0)
        {
             /* 创建JSON子叶结构体并添加到数组结构体 */
             pArray_relay[i] = cJSON_CreateObject();
             cJSON_AddItemToArray(pArray,pArray_relay[i]);
             /*  添加数据到子叶数组结构体             */
             cJSON_AddStringToObject(pArray_relay[i], "ID", my_monitor_data[num1].ID); 
             cJSON_AddNumberToObject(pArray_relay[i],"Type", my_monitor_data[num1].Type);
             cJSON_AddNumberToObject(pArray_relay[i],"Rssi", my_monitor_data[num1].Rssi);
             cJSON_AddNumberToObject(pArray_relay[i],"Channel", my_monitor_data[num1].Channel);
             cJSON_AddNumberToObject(pArray_relay[i],"Frequency", my_monitor_data[num1].Frequency);
             cJSON_AddNumberToObject(pArray_relay[i],"Ts_in", my_monitor_data[num1].Ts_in);
             cJSON_AddNumberToObject(pArray_relay[i],"Ts_out", my_monitor_data[num1].Ts_out);
             /* 组建好json格式数据后清除抓取的次数                */
             my_monitor_data[num1].Frequency = 0;
             i++;
        }     
     }
     
     /* 从cJSON对象中获取有格式的JSON对象 */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* 释放cJSON_Print ()分配出来的内存空间 */
     cJSON_free((void *) s);
     
     /* 释放cJSON_CreateObject ()分配出来的内存空间 */
     cJSON_Delete(pRoot);
 
     return d;
 
 }

 /* 状态数据json */
 char* status_json_data(char* local_appsw, char* local_service, char* local_ver,  char* local_StationID, char* local_imsi)
 {
     /* 创建JSON根部结构体        */
     cJSON *pRoot = cJSON_CreateObject();
     
     /* 创建储存定位数组 */
     char out_value[15] = {0};
     size_t length = 15;
     
     /*  添加字符串类型数据到根部结构体  */
     cJSON_AddStringToObject( pRoot,"appsw", local_appsw);
     cJSON_AddStringToObject( pRoot,"service", local_service);
     cJSON_AddStringToObject( pRoot,"ver", local_ver);
 
     /* 获取电量数据 转换成百分比*/ //ggzhi 20190831
     int vol = ((574*(adc_read()-850))/1000) + ((574*(adc_read()-850))/1000);
     if(vol < 0 ){
        vol = 0;
     }
     cJSON_AddNumberToObject( pRoot,"vol", vol/2);
     
     cJSON_AddStringToObject( pRoot,"run", "app");
     cJSON_AddStringToObject( pRoot,"StationID", local_StationID);
     
     /* 获取定位数据 */
     get_str_nvs("latitude", out_value, &length);
     cJSON_AddStringToObject( pRoot,"StationX", out_value);
     memset(out_value, 0, sizeof(out_value));
     get_str_nvs("longitude", out_value, &length);
     cJSON_AddStringToObject( pRoot,"StationY", out_value);
     
     cJSON_AddStringToObject( pRoot,"imsi", local_imsi);
     
     /* 从cJSON对象中获取有格式的JSON对象 */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* 释放cJSON_Print ()分配出来的内存空间 */
     cJSON_free((void *) s);
     
     /* 释放cJSON_CreateObject ()分配出来的内存空间 */
     cJSON_Delete(pRoot);
 
     return d;
 
 }

/*************************************************
Function:     Publish_Monitor_Data_task 
Description: 推送监控到的数据                          
Others:     xTaskCreate(Publish_Monitor_Data_task, "Publish_Monitor_Data_task", 2*1024, NULL, 6, NULL); 
*************************************************/
void Publish_Monitor_Data_task(void)
{
    unsigned int state = 0, time = 235, restart_time = 0, j = 0;
    while(1)
    {
        time++;
        state = sniffer_work_state;
        vTaskDelay(15000 / portTICK_RATE_MS);  
        /* 计时前状态不为零,计时后状态不变。意为有数据产生且探针处于空闲状态，此时需要发送数据 */
        if((state > 0) && (sniffer_work_state == state))  
        {       
            ESP_LOGE(TAG, "Start Publishing Monitor Data!"); 
            char* data = Monitor_json_data();
            /* 同步   */
            AT();
            while (-1 == MQTT_Publish_Msg(0, 0, 0, 0, Publish_topic_name, sizeof(Publish_topic_name), data, strlen(data)+50))
            {
                ESP_LOGE(TAG, "MAC Failed to send!");
                vTaskDelay(1000 / portTICK_RATE_MS);     
                AT();
                /* 不可用开启3s闪灯线程 */
                if(!Check_MQTT_Server())
                {
                    ESP_LOGE(TAG, "MAC reconnection!"); 
                    try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 0, NULL, 0, 0);
                    ESP_LOGE(TAG, "connect success!");
                }
                else
                    break;
            }
           /******************************/ 
           // AT();
           // MQTT_Publish_Msg(0, 0, 0, 0, stat_topic_name, sizeof(stat_topic_name), status_json_data(appsw, service, ver, StationID, BC20_IMSI), 500);
           /*****************************/ 
            sniffer_work_state = 0;
            j = 0;
        }
        /* 计时后状态有变化.意为有数据产生且探针处于忙碌状态,此时不发送数据 */
        else if(sniffer_work_state > state)
        { 
           sniffer_work_state = 1;
           ESP_LOGE(TAG, "sniffer is working !");
           j = 0;
        }
        /* 计时前没工作,计时后还没工作.意为探针空闲没数据 */
        else if((state == 0) && (sniffer_work_state == 0) )
        {
            if(j == 0)
            {
                ESP_LOGE(TAG, "Sniffer have no data !");
                j = 1;
            }
        }
        /*定时上报状态*/
        if(time >= 235)
        {
            /* 同步   */
            AT();
            while (-1 == MQTT_Publish_Msg(0, 0, 0, 0, stat_topic_name, sizeof(stat_topic_name), status_json_data(appsw, service, ver, StationID, BC20_IMSI), 500))
            {
                ESP_LOGE(TAG, "status Failed to send!");
                vTaskDelay(1000 / portTICK_RATE_MS);     
                AT();
                /* 不可用开启3s闪灯线程 */
                if(!Check_MQTT_Server())
                {
                    ESP_LOGE(TAG, "MAC reconnection!"); 
                    try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 0, NULL, 0, 0);
                    ESP_LOGE(TAG, "connect success!");
                }
                else
                    break;
            }
            time = 0;
            restart_time++; 
        }
        /*正常情况下大约12小时重启一次*/
        if(restart_time >= 13)
        {
            ESP_LOGE(TAG, ""YELLOW"restart! restart! restart! restart!");
            esp_restart();
        }
    }
    vTaskDelete(NULL);
}

/* 工厂测试函数 */
char* Factory_Request_json(char * StationID)
{
     /* 创建JSON根部结构体        */
     cJSON *pRoot = cJSON_CreateObject();
     
     /*  添加字符串类型数据到根部结构体  */
     cJSON_AddStringToObject( pRoot,"function", "Factory_req");
     cJSON_AddStringToObject( pRoot,"result", "request");
     cJSON_AddStringToObject( pRoot,"StationID", StationID);  
     
     /* 从cJSON对象中获取有格式的JSON对象 */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* 释放cJSON_Print ()分配出来的内存空间 */
     cJSON_free((void *) s);
     
     /* 释放cJSON_CreateObject ()分配出来的内存空间 */
     cJSON_Delete(pRoot);
 
     return d;
     
}
/* 解析服务器下发的工厂测试数据JSON包 */
int32_t Factory_Recive_json(char * receiveData)
{
    uint8_t monitor_mac[6] = {0};
    /* JSON字符串到cJSON格式 */
    cJSON *pJsonRoot = cJSON_Parse(receiveData);
    if (pJsonRoot != NULL)
    {
        /* 获取字段值,判断字段类型，若是则保存至本地 */  
        cJSON * function = cJSON_GetObjectItem( pJsonRoot, "function");
        if (function)                                      
        {
            if (cJSON_IsString(function))                            
            {
                Factory_config.function = function->valuestring;
            }
        }  
        cJSON *  nb = cJSON_GetObjectItem( pJsonRoot, "nb");
        if (nb)                                      
        {
            if (cJSON_IsNumber(nb))                           
            {
                Factory_config.nb = nb->valueint;
            }
        }       
        cJSON *  vol = cJSON_GetObjectItem( pJsonRoot, "vol");
        if (vol)                                      
        {
            if (cJSON_IsNumber(vol))                           
            {
                Factory_config.vol = vol->valueint;
            }
        } 
        cJSON *  gnss = cJSON_GetObjectItem( pJsonRoot, "gnss");
        if (gnss)                                      
        {
            if (cJSON_IsNumber(gnss))                           
            {
                Factory_config.gnss = vol->valueint;
            }
        }
        cJSON *  macNum = cJSON_GetObjectItem( pJsonRoot, "macNum");
        if (macNum)                                      
        {
            if (cJSON_IsNumber(macNum))                           
            {
                Factory_config.macNum = macNum->valueint;
            }
        }  
        if(system_config.macNum > 0)
            save_num_nvs("macNum", Factory_config.macNum);
        
        /*解析字段数组内容      */
        cJSON *pArryInfo = cJSON_GetObjectItem(pJsonRoot, "macList");
        cJSON *pInfoItem = NULL;
        cJSON *pInfoObj = NULL;
        if(pArryInfo)                                    // 判断info字段是否json格式
        {
            int arryLength = cJSON_GetArraySize(pArryInfo);          // 获取数组长度
            int i;
            for (i = 0; i < arryLength; i++)
            {
                pInfoItem = cJSON_GetArrayItem(pArryInfo, i);        // 获取数组中JSON对象
                if(NULL != pInfoItem)
                {
                    pInfoObj = cJSON_GetObjectItem(pInfoItem,"mac_adders");// 解析mac_adders字段字符串内容   
                    if(pInfoObj)
                    {
                        char * pMacAdress = pInfoObj->valuestring;
                        String_to_hex(pMacAdress, monitor_mac);
                        save_mac_nvs( i, monitor_mac, (size_t) sizeof(monitor_mac));
                    }
                }                                                      
            }
        }
        
        ESP_LOGE(TAG, ""GREEN"json is ok!");
        /* 关闭cjson */
        cJSON_Delete(pJsonRoot);
        return 0;    
    }
    
    ESP_LOGE(TAG, "json pack into cjson error!");
    /* 关闭cjson */
    cJSON_Delete(pJsonRoot);
    return -1;

}
/* 接收服务器下发的工厂测试数据 */
int32_t Factory_Recive_Buff(void) 
{      
    size_t size;
    int     timeout = 0;
    char    *ptmp2 = NULL;
    char    *RceBuff = NULL;
    uart_flush_input(UART_NUM_0);
    WaitUartIdle();
    /*等待平台下发数据       */
    while(1)
    {      
        vTaskDelay(5000 / portTICK_RATE_MS);     
        uart_get_buffered_data_len(UART_NUM_0 , &size);
        /* 判断串口缓冲区是否有数据 */
        while((int) size < 20)
        {
           timeout++;
           uart_flush_input(UART_NUM_0);
           ESP_LOGE(TAG, "The server is not responding!");
           vTaskDelay(5000 / portTICK_RATE_MS);
           uart_get_buffered_data_len(UART_NUM_0 , &size);
        }       
        /* 申请空间取出数据并判断是否是指定数据若不是舍弃数据 */
        RceBuff = (char*)  malloc(size);
        memset(RceBuff, 0, size);
        uart_read_bytes(UART_NUM_0, (uint8_t*)RceBuff, (uint32_t)size, 20 / portTICK_RATE_MS); 
        if((strstr((char*)RceBuff, "+QMTRECV")) != NULL)
            break;
        else
        {
            free(RceBuff);
            ESP_LOGE(TAG, "Error receiving message!");
            uart_flush_input(UART_NUM_0);
        }
    }
    
    /* 获取json格式数据  */
    if((ptmp2=strstr(RceBuff, "req")) != NULL)
    {   
        /* 去除开头字符串'"' */
        ptmp2 += 6;
        /*把末尾'"'去掉，若有其他无用字符产生一并去除 */
        char *p = ptmp2;    
        p += strlen(ptmp2);
        while(*p != '"')  
        {
            *p = 0;
            p--;
        }
        *p = 0; 
        if(0 == Factory_Recive_json(ptmp2))
        {
            free(RceBuff);
            ReleaseUart();
    	    return 0;
        }
        else
        {
            ESP_LOGE(TAG, "recv get json error");
            free(RceBuff);
            ReleaseUart();
            return -1; 
        }
	}
    ESP_LOGE(TAG, "recv error");
    free(RceBuff);
    ReleaseUart();
    return -1;     
}
/* 工厂测试回复的JSON包 */
char* Factory_Reply_json(int i, int* json_len)
 {
     /* 创建JSON根部结构体 */
     cJSON *pRoot = cJSON_CreateObject();
     if(i == 0)
     {
         /*  添加字符串类型数据到根部结构体  */
         cJSON_AddStringToObject( pRoot,"function", "factory");
         cJSON_AddStringToObject( pRoot,"result", Factory_config.result);     
         cJSON_AddNumberToObject(pRoot,"nb", Factory_config.nb);  
         cJSON_AddNumberToObject( pRoot,"vol", Factory_config.vol);
         cJSON_AddNumberToObject(pRoot,"gnss", Factory_config.gnss);    
         cJSON_AddNumberToObject(pRoot,"macNum", Factory_config.macNum);   
     } 
     else if(i == -1)
     {
        /*  添加字符串类型数据到根部结构体  */
         cJSON_AddStringToObject( pRoot,"function", "factory");
         cJSON_AddStringToObject( pRoot,"result", "recv error");       
     }
     /* 从cJSON对象中获取有格式的JSON对象 */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* 释放cJSON_Print ()分配出来的内存空间 */
     cJSON_free((void *) s);
     
     /* 释放cJSON_CreateObject ()分配出来的内存空间 */
     cJSON_Delete(pRoot);
        
     *json_len = strlen(d);
     
     return d;
     
 }
/* 工厂测试主流程 */
void Factory_test(void)
{   
    if(1 == get_total_nvs("Factory_test"))   /*返回值为1，代表工厂需要测试，0代表测试通过*/
    {
        int i = 0, json_len = 0, j = 0, flag = 0;
        /*检测BC20是否可用*/
        Check_BC20();     
        /*连接和订阅*/
        try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_test_name, sizeof(Subscribe_test_name), 0);
        /*请求并接收服务器监测mac*/
        AT();
        while (-1 == MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Factory_Request_json(StationID), 225))
        { 
            ESP_LOGE(TAG, "Msg Failed to send!");
            vTaskDelay(1000 / portTICK_RATE_MS); 
            /* 连续失败3次，尝试重新建立MQTT连接 */
            if(i == 3)
            {
                i = 0;
                Disconnect_MQTT_Server(0);
                try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_test_name, sizeof(Subscribe_test_name), 0);
            }
            AT();
        }     
        /* 尝试接收*/
        while((i = Factory_Recive_Buff()) == -1)
        {   
            AT();
            /*  publish mac request over @witt */
            MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Factory_Reply_json(-1, &json_len), json_len+50);
            ESP_LOGE(TAG, ""RED"Get Server data error! Get Server data again");      
        }
         
        /*开启探针任务*/
        xTaskCreate((TaskFunction_t)wifi_sniffer_task, "wifi_sniffer_task", 1024*2, NULL, 6, NULL);
        
        /*延时后上报测试结果*/
        while(1)
        {     
            vTaskDelay(15000/portTICK_RATE_MS);      
            /* NB测试情况 */
            Factory_config.nb = 0;
            
            /* 电量测试数据 */
            Factory_config.vol = (367*(adc_read()-750))/1000;
            
            /* GNSS测试情况 */
            Factory_config.gnss = 0;
            
            /* 探针测试数据 */ 
            for(int num1 = 0; num1 < MAX_MAC_NUM; num1++)
            {
                if(my_monitor_data[num1].Frequency > 0)
                    j++;
            }
            if(j == Factory_config.macNum)
            {
                Factory_config.result = "success";
                save_num_nvs("Factory_test", 0);
                flag = -1;
            }
            else
            {
                Factory_config.result = "error";
                save_num_nvs("Factory_test", 1);
            }    
            Factory_config.macNum = j;
                      
            AT();        
            MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Factory_Reply_json(0, &json_len), json_len+50);    
            j = 0;
            if(flag == -1 || flag == 4)
                break;
            else
                flag++;
        }
        /* 关机 电源控制脚拉低*/
        gpio_set_level(CRT_VBAT_PIN, 0); 
        while(1)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    }
    /*工厂已测试通过*/
    else
        ESP_LOGE(TAG, "The device had passed factory tests!");
}

