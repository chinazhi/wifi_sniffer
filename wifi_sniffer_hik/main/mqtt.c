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

static config_msg system_config;               /* �������·����豸������Ϣ */

static Factory_msg Factory_config;             /* �����������·����豸������Ϣ */

monitor_data my_monitor_data[MAX_MAC_NUM];     /* �豸����������Ϣ */

unsigned int sniffer_work_state = 0;    /* WiFi̽������״ָ̬ʾ��־ 0Ϊδ�������� ����0Ϊ���ڲ�������                     */

char    ATCMD_RceBuff[120];             /* ����AT����buff */
char    Subscribe_topic_name[60] = {0}; /* ��������� */
char    Subscribe_test_name[60] = {0};  /* �������Զ������� */
char    Publish_topic_name[60] = {0};   /* ���ݷ������� */
char    stat_topic_name[60] = {0};      /* ����״̬�������� */
char    rsp_topic_name[60] = {0};       /* �������󡢻ظ��������� */
char    StationID[20] = {0};            /* �豸���,��ǰΪ�豸mac��ַ */


/*************************************************
Function:    Set_MQTT_Client
Description: ����MQTT�ͻ��ˣ������߿ͻ���Ҫ���ĸ���ַ���˿ڷ����ݣ�����������
Input:       tcpconnectID��MQTT���׽��ֱ�ʶ������Χ��0-5
             host_name���������ĵ�ַ����������IP��ַ������������СΪ100�ֽ�
             port���������Ķ˿ڡ���Χ��1-65535             
Return:      0��ʾ�ɹ���-1��ʾʧ�ܻ�ʱ
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
        /* �ȴ���Ӧ�����ʱ75s����������� Ŀǰ�趨Ϊ2*6s*/
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
        /* �յ��ͻ�������򿪷�����Ϣ,�ж��������*/
		memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTOPEN: %d,0", tcpconnectID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ESP_LOGI(TAG, "Set MQTT Client success");
            ReleaseUart();
            /* 0��ʾ��MQTT�ͻ��˳ɹ� */
			return 0; 
        }
        else
        {
            ESP_LOGI(TAG, "Set MQTT Client error:%s", ATCMD_RceBuff);
			ReleaseUart();
            /* -1��ʾ��MQTT�ͻ����쳣 */
            return -1;
        }          
    }
    ESP_LOGI(TAG, "Set MQTT Client error");
    ReleaseUart();
	return -1;
}


/*************************************************
Function:    Check_MQTT_Client
Description: ��ѯMQTT�ͻ��˿�����Ϣ
Return:      TRUE:Ϊ��ѯ�ɹ�������ӡ��Ϣ
             FALSE��Ϊ��ѯʧ��
other:       AT+QMTOPEN?  +QMTOPEN: <tcpconnectID>,��<host_name>��,<port>
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
            ESP_LOGI(TAG, "MQTT�ͻ���������Ϣ:%s", Recv_info);
		}
        ReleaseUart();
	    return TRUE;
    } 	 
	ReleaseUart();
	return FALSE;
} 


/*************************************************
Function:    Connect_MQTT_Server
Description: ���ͻ������ӵ�MQTT�������������߷���������˭��
Input:       tcpconnectID��MQTT���׽��ֱ�ʶ������Χ��0-5
             clientID���ͻ�����ʶ���ַ���
             
Return:      0��ʾ�ɹ���-1��ʾʧ�ܻ�ʱ
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
            /* �ȴ���Ӧ��Ĭ��10s�����������*/
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
        
        /* �յ��ͻ�������MQTT������������Ϣ,�ж��������*/
		memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTCONN: %d,0", tcpconnectID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ESP_LOGI(TAG, "Connect MQTT Server success!");
            ReleaseUart();
			return 0;/* 0��ʾ�ͻ������ӷ������ɹ� */
         }
         else
         {
            ESP_LOGI(TAG, "Connect MQTT Server error��%s!", ATCMD_RceBuff);
			ReleaseUart();
            /* -1��ʾ�ͻ������ӷ������쳣 */
            return -1;
         }
    }    
    ESP_LOGI(TAG, "Connect MQTT Server error!");          
	ReleaseUart();
	return -1;
}


/*************************************************
Function:    Check_MQTT_Server
Description: ��ѯ�ͻ�������MQTT������״̬
Return:      TRUE:Ϊ��ѯ�ɹ�������ӡ��Ϣ <state> 1ΪMQTT��ʼ����2ΪMQTT�������ӣ�3ΪMQTT�����ӣ�4ΪMQTTδ����
             FALSE��Ϊ��ѯʧ��
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
Description: �Ͽ��ͻ�����MQTT������������
Input:       tcpconnectID��MQTT���׽��ֱ�ʶ������Χ��0-5
                         
Return:      0��ʾ�ɹ���-1��ʾʧ�ܻ�ʱ
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
        /* �յ��ͻ��˶Ͽ�MQTT������������Ϣ*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTDISC: %d,0", tcpconnectID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ESP_LOGI(TAG, ""BLUE"Client disconnected success!");	
            ReleaseUart();
			return 0;   /* 0��ʾ�ͻ��˶Ͽ����ӳɹ� */
        }
        else
        {
            ESP_LOGI(TAG, "Client disconnected error!");
		    ReleaseUart();      
            return -1;  /* -1��ʾ�ͻ��˶Ͽ������쳣 */
        }    
    }
    
    ESP_LOGI(TAG, ""RED"Client disconnected error!");    	 
	ReleaseUart();
	return -1;
}


/*************************************************
Function:    Subscribe_MQTT_Topics
Description: �ͻ������ò�����һ��MQTT����
Input:       tcpconnectID��MQTT���׽��ֱ�ʶ������Χ��0-5
             msgID�����ݰ�����Ϣ��ʶ������Χ��1-65535
             topic���ͻ���ϣ�����Ļ�ȡ�����ĵ�����
             qos���ͻ���ϣ��������Ϣ��QoS����0Ϊ���һ��;1Ϊ����һ��;2Ϊǡ��һ��
             
Return:      0��ʾ�ɹ���-1��ʾʧ�ܻ�ʱ
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
        /* �ȴ���Ӧ��Ĭ��40s�����������, ĿǰΪ12Ss*/
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
        
        /* �յ��ͻ��˶���MQTT���ⷵ����Ϣ���ж϶������*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTSUB: %d,%d,0", tcpconnectID, msgID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
			/* 0��ʾ�ͻ��˶���MQTT����ɹ� */
            ESP_LOGI(TAG, "Subscribe MQTT Topics success!");
            free(ATCmd);
            ReleaseUart();
			return 0;
        }
        else
        {
            ESP_LOGI(TAG, "Subscribe MQTT Topics error:%s!", ATCMD_RceBuff);
            /* -1��ʾ�ͻ��˶���MQTT�����쳣 */
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
Description: �ͻ���ȡ������MQTT����
Input:       tcpconnectID��MQTT���׽��ֱ�ʶ������Χ��0-5
             msgID�����ݰ�����Ϣ��ʶ������Χ��1-65535
             topic���ͻ���ϣ�����Ļ�ȡ�����ĵ�����
             
Return:      0��ʾ�ɹ���-1��ʾʧ�ܻ�ʱ
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
        /* �ȴ���Ӧ��Ĭ��40s�������������Ŀǰ15s*/
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
        
        /* �յ��ͻ���ȡ������MQTT���ⷵ����Ϣ,�ж��Ƿ�ɹ�*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTUNS: %d,%d,0", tcpconnectID, msgID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {
            ReleaseUart();
            ESP_LOGI(TAG, "Unsubscribe MQTT Topics success!");
			/* 0��ʾ�ͻ���ȡ������MQTT����ɹ� */
			return 0;
        }
        else
        {
            ESP_LOGI(TAG, "Unsubscribe MQTT Topics error:%s!", ATCMD_RceBuff);
			ReleaseUart();
            /* -1��ʾ�ͻ���ȡ������MQTT�����쳣 */
            return -1;
        }
    }
    ESP_LOGI(TAG, "Unsubscribe MQTT Topics error!");  	 
	ReleaseUart();
	return -1;
}


/*************************************************
Function:    MQTT_Publish_Msg
Description: �ͻ��˷���MQTT��Ϣ
Input:       tcpconnectID��MQTT���׽��ֱ�ʶ������Χ��0-5
             msgID�����ݰ�����Ϣ��ʶ������Χ��1-65535
             qos���ͻ���ϣ��������Ϣ��QoS����0Ϊ���һ��;1Ϊ����һ��;2Ϊǡ��һ��
             retain:����Ϣ�����͵���ǰ���ķ������󣬷������Ƿ�������Ϣ��0Ϊ��������1Ϊ����
             topic����Ҫ������������
             msg����Ҫ��������Ϣ
             
Return:      0��ʾ�ɹ���-1��ʾʧ�ܻ�ʱ
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
        /* �ȴ���Ӧ��Ĭ��40s�������������Ŀǰ�趨Ϊ8*2s*/
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
        /* �յ��ͻ��˷���MQTT��Ϣ������Ϣ*/
        memset(Recv_flags, 0, sizeof(Recv_flags));
        sprintf(Recv_flags, "+QMTPUB: %d,%d,0", tcpconnectID, msgID);
        if(strstr((char*)ATCMD_RceBuff, Recv_flags) != NULL)
        {  
            ESP_LOGI(TAG, "Client published the message success!");
            uart_flush(UART_NUM_0);
            memset(ATCmd, 0, n);
            free(ATCmd);
            ReleaseUart();
			return 0;   /* 0��ʾ�ͻ��˷���MQTT��Ϣ�ɹ� */
        }
        else
        {
            ESP_LOGI(TAG, "MQTT Publish Msg error:%s!", ATCMD_RceBuff);
            uart_flush(UART_NUM_0);
            memset(ATCmd, 0, n);
            free(ATCmd);
            ReleaseUart();
            return -1;  /* -1��ʾ�ͻ��˷���MQTT��Ϣ�쳣 */
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
Description: ��Ҫ���յ���json���ݷֽ����                         
Return:     
Others:     
*************************************************/
int32_t Recive_json_data(char * receiveData)
{
    uint8_t monitor_mac[6] = {0};
    /* JSON�ַ�����cJSON��ʽ */
    cJSON *pJsonRoot = cJSON_Parse(receiveData);
    if (pJsonRoot != NULL)
    {
        /* ��ȡ�ֶ�ֵ,�ж��ֶ����ͣ������򱣴������� */  
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
        
        /*�����ֶ���������      */
        cJSON *pArryInfo = cJSON_GetObjectItem(pJsonRoot, "macList");
        cJSON *pInfoItem = NULL;
        cJSON *pInfoObj = NULL;
        if(pArryInfo)                                    // �ж�info�ֶ��Ƿ�json��ʽ
        {
            int arryLength = cJSON_GetArraySize(pArryInfo);          // ��ȡ���鳤��
            int i;
            for (i = 0; i < arryLength; i++)
            {
                pInfoItem = cJSON_GetArrayItem(pArryInfo, i);        // ��ȡ������JSON����
                if(NULL != pInfoItem)
                {
                    pInfoObj = cJSON_GetObjectItem(pInfoItem,"mac_adders");// ����mac_adders�ֶ��ַ�������   
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
        /* �ر�cjson */
        cJSON_Delete(pJsonRoot);
        return 0;    
    }
    
    ESP_LOGE(TAG, "json pack into cjson error!");
    /* �ر�cjson */
    cJSON_Delete(pJsonRoot);
    return -1;

}


/*************************************************
Function:    Recive_MQTT_Buff
Description: ����JSON��ʽ���ݰ����������ݴ�����nvs��
Return:       0Ϊ�������յ� 1Ϊ��������ʱ���� -1Ϊ���մ���
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
    /*�ȴ�ƽ̨�·�����       */
    while(1)
    {      
        vTaskDelay(5000 / portTICK_RATE_MS);     
        uart_get_buffered_data_len(UART_NUM_0 , &size);
        /* �жϴ��ڻ������Ƿ������� */
        while((int) size < 86)
        {
           timeout++;
           uart_flush_input(UART_NUM_0);
           ESP_LOGE(TAG, "The server is not responding!");
           vTaskDelay(5000 / portTICK_RATE_MS);
           uart_get_buffered_data_len(UART_NUM_0 , &size);
           /* ����һ��ʱ������Ӧ���˳� 5s*24 */
           if(timeout == 24)
           {
               ESP_LOGE(TAG, "Timeout waiting for server reply!");
               free(RceBuff);
               ReleaseUart();
               return 1;
           }
        }       
        /* ����ռ�ȡ�����ݲ��ж��Ƿ���ָ�������������������� */
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
    
    /* ��ȡjson��ʽ����  */
    if((ptmp2=strstr(RceBuff, "req")) != NULL)
    {   
        /* ȥ����ͷ�ַ���'"' */
        ptmp2 += 6;
        /*��ĩβ'"'ȥ�����������������ַ�����һ��ȥ�� */
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
Description: ע������MQTT�����������ķ���
Input: 		 tcpconnectID��MQTT���׽��ֱ�ʶ������Χ��0-5�� host_name��������IP��          port���˿ڣ�
			 clientID���豸���ƣ�msgID�����ݰ�����Ϣ��ʶ������Χ��1-65535��topic��������������QOS������0-2��
Return:		 0 �ɹ���-1 ʧ�ܡ�
other��      ����������ΪNULL����ʾ���������⣬msgID��qos����0��
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

        /* ��ȡMQTT�û������� */
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
Description: �������Ӷ���MQTT��
other��  ���һֱʧ�ܻ�һֱ��������  ���topicΪ�ղ�ִ�ж���
*************************************************/
void try_connect_subscribe(int tcpconnectID, char* host_name, unsigned int port, char* clientID, unsigned int msgID,
                                            char* topic,  size_t topic_len, int qos)
{
    int i = 0;
    /* �쳣ָʾ���߳� */
    xTaskCreate((TaskFunction_t)MQTT_ERROR_LED_task, "MQTT_ERROR_LED_task", 256, NULL, 5, NULL); 
    Disconnect_MQTT_Server(0);
    while(-1 == MQTT_Connect_Subscribe(tcpconnectID, host_name, port, clientID, msgID, topic, topic_len, qos))
    {   
        i++; 
        ESP_LOGI(TAG, ""RED"Connection failed! 3s after reconnection!");
        /* ���2�γ������Ӷ���ʧ��,����BC20 */
        if( i == 2 )
        {
            i = 0;
            Check_BC20();    
        }
        Disconnect_MQTT_Server(0);
        vTaskDelay(3000 / portTICK_RATE_MS);
        AT();   /* ͬ������ */
    }
    MQTT_ERR_flag = 0;
}

/*************************************************
Function: recv_and_save 
Description: ���շ�������Ϣ������ 
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
        /* ����ʧ��3�Σ��������½���MQTT���� */
        if(i == 3)   
        {
            i = 0;
            Disconnect_MQTT_Server(0);
            try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_topic_name, sizeof(Subscribe_topic_name), 1);
        }
        AT(); 
    } 
    AT();
    /* ���Խ���,����,תΪ����MAC��� */
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
Description: ����ת��Ϊjson��ʽ                       
Return:     
Others:     
*************************************************/
 char* Request_json_data(char * StationID)
 {
     /* ����JSON�����ṹ��   ��  ����JSON��Ҷ�ṹ�� */
     cJSON *pRoot = cJSON_CreateObject();
     cJSON *pValue = cJSON_CreateObject();
     
     /*  ����ַ����������ݵ������ṹ��  */
     cJSON_AddStringToObject( pRoot,"function", "config_req");
     cJSON_AddStringToObject( pRoot,"result", "request");
     cJSON_AddStringToObject( pRoot,"StationID", StationID);  
     
     cJSON_AddItemToObject(pRoot, "params",pValue);
     /* ����������ݵ���Ҷ�ṹ�� */
     cJSON_AddNumberToObject(pValue,"timesync",0);
     cJSON_AddNumberToObject(pValue,"macNum",0);
     
     /* ��cJSON�����л�ȡ�и�ʽ��JSON���� */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* �ͷ�cJSON_Print ()����������ڴ�ռ� */
     cJSON_free((void *) s);
     
     /* �ͷ�cJSON_CreateObject ()����������ڴ�ռ� */
     cJSON_Delete(pRoot);
 
     return d;
     
 }
 
 /* json�ظ������� 0Ϊ�������յ� 1Ϊ��������ʱ���� -1Ϊ���մ��� */
 char* Reply_json_data(int i, int* json_len)
 {
     uint8_t monitor_mac[6] = {0};
     char local_mac[12] = {0};
     size_t length = sizeof(monitor_mac);

     /* ����JSON�����ṹ��/����JSON��Ҷ�ṹ�� */
     cJSON *pRoot = cJSON_CreateObject();
     cJSON *pValue = cJSON_CreateObject();
     if(i == 0)
     {
         /*  ����ַ����������ݵ������ṹ��  */
         cJSON_AddStringToObject( pRoot,"function", "config");
         cJSON_AddStringToObject( pRoot,"result", "success");
         
         /*  ������ݵ���Ҷ�ṹ��  */
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
         /*  �����������ͽṹ�嵽��Ҷ�ṹ��  */    
         cJSON * pArray = cJSON_CreateArray();
         cJSON_AddItemToObject(pValue,"macList",pArray);
         for(int i = 0; i < system_config.macNum; i++)
         {
             /*  ������Ҷ�ṹ�嵽�����ͽṹ��  */
             cJSON * pArray_relay = cJSON_CreateObject();
             cJSON_AddItemToArray(pArray, pArray_relay);
       
             get_mac_nvs(i, monitor_mac, &length);
             sprintf(local_mac, "%02x%02x%02x%02x%02x%02x",monitor_mac[0], monitor_mac[1], monitor_mac[2], monitor_mac[3], monitor_mac[4], monitor_mac[5]);

             cJSON_AddStringToObject(pArray_relay, "mac_adders", local_mac);
         }
     } 
     else if(i == 1)
     {
        /*  ����ַ����������ݵ������ṹ��  */
         cJSON_AddStringToObject( pRoot,"function", "config");
         cJSON_AddStringToObject( pRoot,"result", "other");
     }
     else if(i == -1)
     {
        /*  ����ַ����������ݵ������ṹ��  */
         cJSON_AddStringToObject( pRoot,"function", "config");
         cJSON_AddStringToObject( pRoot,"result", "error");       
     }
     /* ��cJSON�����л�ȡ�и�ʽ��JSON���� */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* �ͷ�cJSON_Print ()����������ڴ�ռ� */
     cJSON_free((void *) s);
     
     /* �ͷ�cJSON_CreateObject ()����������ڴ�ռ� */
     cJSON_Delete(pRoot);
        
     *json_len = strlen(d);
     
     return d;
     
 }
 
  /* json��ʽ��������ϱ� */
 char* Monitor_json_data(void)
 {
     int  i = 0, num1 = 0, num2 = 0;
     /* ����JSON�����ṹ��   ��  �����������ͽṹ�� */
     cJSON *pRoot = cJSON_CreateObject();
         
     /*  ������ݵ������ṹ��            */
     for(num1 = 0; num1 < MAX_MAC_NUM; num1++)
     {
        if(my_monitor_data[num1].Frequency > 0)
            num2++;
     }
     cJSON_AddNumberToObject(pRoot,"num",num2);     
     cJSON_AddStringToObject( pRoot,"StationID", StationID);  
 
     /* ����������鵽�����ṹ�� */
     cJSON * pArray = cJSON_CreateArray();
     cJSON_AddItemToObject(pRoot,"dp",pArray);
 
     cJSON * pArray_relay[MAX_MAC_NUM];
     
     for(num1 = 0; num1 < MAX_MAC_NUM; num1++)
     {
        if(my_monitor_data[num1].Frequency > 0)
        {
             /* ����JSON��Ҷ�ṹ�岢��ӵ�����ṹ�� */
             pArray_relay[i] = cJSON_CreateObject();
             cJSON_AddItemToArray(pArray,pArray_relay[i]);
             /*  ������ݵ���Ҷ����ṹ��             */
             cJSON_AddStringToObject(pArray_relay[i], "ID", my_monitor_data[num1].ID); 
             cJSON_AddNumberToObject(pArray_relay[i],"Type", my_monitor_data[num1].Type);
             cJSON_AddNumberToObject(pArray_relay[i],"Rssi", my_monitor_data[num1].Rssi);
             cJSON_AddNumberToObject(pArray_relay[i],"Channel", my_monitor_data[num1].Channel);
             cJSON_AddNumberToObject(pArray_relay[i],"Frequency", my_monitor_data[num1].Frequency);
             cJSON_AddNumberToObject(pArray_relay[i],"Ts_in", my_monitor_data[num1].Ts_in);
             cJSON_AddNumberToObject(pArray_relay[i],"Ts_out", my_monitor_data[num1].Ts_out);
             /* �齨��json��ʽ���ݺ����ץȡ�Ĵ���                */
             my_monitor_data[num1].Frequency = 0;
             i++;
        }     
     }
     
     /* ��cJSON�����л�ȡ�и�ʽ��JSON���� */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* �ͷ�cJSON_Print ()����������ڴ�ռ� */
     cJSON_free((void *) s);
     
     /* �ͷ�cJSON_CreateObject ()����������ڴ�ռ� */
     cJSON_Delete(pRoot);
 
     return d;
 
 }

 /* ״̬����json */
 char* status_json_data(char* local_appsw, char* local_service, char* local_ver,  char* local_StationID, char* local_imsi)
 {
     /* ����JSON�����ṹ��        */
     cJSON *pRoot = cJSON_CreateObject();
     
     /* �������涨λ���� */
     char out_value[15] = {0};
     size_t length = 15;
     
     /*  ����ַ����������ݵ������ṹ��  */
     cJSON_AddStringToObject( pRoot,"appsw", local_appsw);
     cJSON_AddStringToObject( pRoot,"service", local_service);
     cJSON_AddStringToObject( pRoot,"ver", local_ver);
 
     /* ��ȡ�������� ת���ɰٷֱ�*/ //ggzhi 20190831
     int vol = ((574*(adc_read()-850))/1000) + ((574*(adc_read()-850))/1000);
     if(vol < 0 ){
        vol = 0;
     }
     cJSON_AddNumberToObject( pRoot,"vol", vol/2);
     
     cJSON_AddStringToObject( pRoot,"run", "app");
     cJSON_AddStringToObject( pRoot,"StationID", local_StationID);
     
     /* ��ȡ��λ���� */
     get_str_nvs("latitude", out_value, &length);
     cJSON_AddStringToObject( pRoot,"StationX", out_value);
     memset(out_value, 0, sizeof(out_value));
     get_str_nvs("longitude", out_value, &length);
     cJSON_AddStringToObject( pRoot,"StationY", out_value);
     
     cJSON_AddStringToObject( pRoot,"imsi", local_imsi);
     
     /* ��cJSON�����л�ȡ�и�ʽ��JSON���� */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* �ͷ�cJSON_Print ()����������ڴ�ռ� */
     cJSON_free((void *) s);
     
     /* �ͷ�cJSON_CreateObject ()����������ڴ�ռ� */
     cJSON_Delete(pRoot);
 
     return d;
 
 }

/*************************************************
Function:     Publish_Monitor_Data_task 
Description: ���ͼ�ص�������                          
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
        /* ��ʱǰ״̬��Ϊ��,��ʱ��״̬���䡣��Ϊ�����ݲ�����̽�봦�ڿ���״̬����ʱ��Ҫ�������� */
        if((state > 0) && (sniffer_work_state == state))  
        {       
            ESP_LOGE(TAG, "Start Publishing Monitor Data!"); 
            char* data = Monitor_json_data();
            /* ͬ��   */
            AT();
            while (-1 == MQTT_Publish_Msg(0, 0, 0, 0, Publish_topic_name, sizeof(Publish_topic_name), data, strlen(data)+50))
            {
                ESP_LOGE(TAG, "MAC Failed to send!");
                vTaskDelay(1000 / portTICK_RATE_MS);     
                AT();
                /* �����ÿ���3s�����߳� */
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
        /* ��ʱ��״̬�б仯.��Ϊ�����ݲ�����̽�봦��æµ״̬,��ʱ���������� */
        else if(sniffer_work_state > state)
        { 
           sniffer_work_state = 1;
           ESP_LOGE(TAG, "sniffer is working !");
           j = 0;
        }
        /* ��ʱǰû����,��ʱ��û����.��Ϊ̽�����û���� */
        else if((state == 0) && (sniffer_work_state == 0) )
        {
            if(j == 0)
            {
                ESP_LOGE(TAG, "Sniffer have no data !");
                j = 1;
            }
        }
        /*��ʱ�ϱ�״̬*/
        if(time >= 235)
        {
            /* ͬ��   */
            AT();
            while (-1 == MQTT_Publish_Msg(0, 0, 0, 0, stat_topic_name, sizeof(stat_topic_name), status_json_data(appsw, service, ver, StationID, BC20_IMSI), 500))
            {
                ESP_LOGE(TAG, "status Failed to send!");
                vTaskDelay(1000 / portTICK_RATE_MS);     
                AT();
                /* �����ÿ���3s�����߳� */
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
        /*��������´�Լ12Сʱ����һ��*/
        if(restart_time >= 13)
        {
            ESP_LOGE(TAG, ""YELLOW"restart! restart! restart! restart!");
            esp_restart();
        }
    }
    vTaskDelete(NULL);
}

/* �������Ժ��� */
char* Factory_Request_json(char * StationID)
{
     /* ����JSON�����ṹ��        */
     cJSON *pRoot = cJSON_CreateObject();
     
     /*  ����ַ����������ݵ������ṹ��  */
     cJSON_AddStringToObject( pRoot,"function", "Factory_req");
     cJSON_AddStringToObject( pRoot,"result", "request");
     cJSON_AddStringToObject( pRoot,"StationID", StationID);  
     
     /* ��cJSON�����л�ȡ�и�ʽ��JSON���� */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* �ͷ�cJSON_Print ()����������ڴ�ռ� */
     cJSON_free((void *) s);
     
     /* �ͷ�cJSON_CreateObject ()����������ڴ�ռ� */
     cJSON_Delete(pRoot);
 
     return d;
     
}
/* �����������·��Ĺ�����������JSON�� */
int32_t Factory_Recive_json(char * receiveData)
{
    uint8_t monitor_mac[6] = {0};
    /* JSON�ַ�����cJSON��ʽ */
    cJSON *pJsonRoot = cJSON_Parse(receiveData);
    if (pJsonRoot != NULL)
    {
        /* ��ȡ�ֶ�ֵ,�ж��ֶ����ͣ������򱣴������� */  
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
        
        /*�����ֶ���������      */
        cJSON *pArryInfo = cJSON_GetObjectItem(pJsonRoot, "macList");
        cJSON *pInfoItem = NULL;
        cJSON *pInfoObj = NULL;
        if(pArryInfo)                                    // �ж�info�ֶ��Ƿ�json��ʽ
        {
            int arryLength = cJSON_GetArraySize(pArryInfo);          // ��ȡ���鳤��
            int i;
            for (i = 0; i < arryLength; i++)
            {
                pInfoItem = cJSON_GetArrayItem(pArryInfo, i);        // ��ȡ������JSON����
                if(NULL != pInfoItem)
                {
                    pInfoObj = cJSON_GetObjectItem(pInfoItem,"mac_adders");// ����mac_adders�ֶ��ַ�������   
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
        /* �ر�cjson */
        cJSON_Delete(pJsonRoot);
        return 0;    
    }
    
    ESP_LOGE(TAG, "json pack into cjson error!");
    /* �ر�cjson */
    cJSON_Delete(pJsonRoot);
    return -1;

}
/* ���շ������·��Ĺ����������� */
int32_t Factory_Recive_Buff(void) 
{      
    size_t size;
    int     timeout = 0;
    char    *ptmp2 = NULL;
    char    *RceBuff = NULL;
    uart_flush_input(UART_NUM_0);
    WaitUartIdle();
    /*�ȴ�ƽ̨�·�����       */
    while(1)
    {      
        vTaskDelay(5000 / portTICK_RATE_MS);     
        uart_get_buffered_data_len(UART_NUM_0 , &size);
        /* �жϴ��ڻ������Ƿ������� */
        while((int) size < 20)
        {
           timeout++;
           uart_flush_input(UART_NUM_0);
           ESP_LOGE(TAG, "The server is not responding!");
           vTaskDelay(5000 / portTICK_RATE_MS);
           uart_get_buffered_data_len(UART_NUM_0 , &size);
        }       
        /* ����ռ�ȡ�����ݲ��ж��Ƿ���ָ�������������������� */
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
    
    /* ��ȡjson��ʽ����  */
    if((ptmp2=strstr(RceBuff, "req")) != NULL)
    {   
        /* ȥ����ͷ�ַ���'"' */
        ptmp2 += 6;
        /*��ĩβ'"'ȥ�����������������ַ�����һ��ȥ�� */
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
/* �������Իظ���JSON�� */
char* Factory_Reply_json(int i, int* json_len)
 {
     /* ����JSON�����ṹ�� */
     cJSON *pRoot = cJSON_CreateObject();
     if(i == 0)
     {
         /*  ����ַ����������ݵ������ṹ��  */
         cJSON_AddStringToObject( pRoot,"function", "factory");
         cJSON_AddStringToObject( pRoot,"result", Factory_config.result);     
         cJSON_AddNumberToObject(pRoot,"nb", Factory_config.nb);  
         cJSON_AddNumberToObject( pRoot,"vol", Factory_config.vol);
         cJSON_AddNumberToObject(pRoot,"gnss", Factory_config.gnss);    
         cJSON_AddNumberToObject(pRoot,"macNum", Factory_config.macNum);   
     } 
     else if(i == -1)
     {
        /*  ����ַ����������ݵ������ṹ��  */
         cJSON_AddStringToObject( pRoot,"function", "factory");
         cJSON_AddStringToObject( pRoot,"result", "recv error");       
     }
     /* ��cJSON�����л�ȡ�и�ʽ��JSON���� */
     char* d = NULL;
     char* s = cJSON_Print(pRoot);
     d = s;
         
     /* �ͷ�cJSON_Print ()����������ڴ�ռ� */
     cJSON_free((void *) s);
     
     /* �ͷ�cJSON_CreateObject ()����������ڴ�ռ� */
     cJSON_Delete(pRoot);
        
     *json_len = strlen(d);
     
     return d;
     
 }
/* �������������� */
void Factory_test(void)
{   
    if(1 == get_total_nvs("Factory_test"))   /*����ֵΪ1����������Ҫ���ԣ�0�������ͨ��*/
    {
        int i = 0, json_len = 0, j = 0, flag = 0;
        /*���BC20�Ƿ����*/
        Check_BC20();     
        /*���ӺͶ���*/
        try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_test_name, sizeof(Subscribe_test_name), 0);
        /*���󲢽��շ��������mac*/
        AT();
        while (-1 == MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Factory_Request_json(StationID), 225))
        { 
            ESP_LOGE(TAG, "Msg Failed to send!");
            vTaskDelay(1000 / portTICK_RATE_MS); 
            /* ����ʧ��3�Σ��������½���MQTT���� */
            if(i == 3)
            {
                i = 0;
                Disconnect_MQTT_Server(0);
                try_connect_subscribe(0, SERVER_DOMAIN, SERVER_PORT, StationID, 1, Subscribe_test_name, sizeof(Subscribe_test_name), 0);
            }
            AT();
        }     
        /* ���Խ���*/
        while((i = Factory_Recive_Buff()) == -1)
        {   
            AT();
            /*  publish mac request over @witt */
            MQTT_Publish_Msg(0, 0, 0, 0, rsp_topic_name, sizeof(rsp_topic_name), Factory_Reply_json(-1, &json_len), json_len+50);
            ESP_LOGE(TAG, ""RED"Get Server data error! Get Server data again");      
        }
         
        /*����̽������*/
        xTaskCreate((TaskFunction_t)wifi_sniffer_task, "wifi_sniffer_task", 1024*2, NULL, 6, NULL);
        
        /*��ʱ���ϱ����Խ��*/
        while(1)
        {     
            vTaskDelay(15000/portTICK_RATE_MS);      
            /* NB������� */
            Factory_config.nb = 0;
            
            /* ������������ */
            Factory_config.vol = (367*(adc_read()-750))/1000;
            
            /* GNSS������� */
            Factory_config.gnss = 0;
            
            /* ̽��������� */ 
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
        /* �ػ� ��Դ���ƽ�����*/
        gpio_set_level(CRT_VBAT_PIN, 0); 
        while(1)
        {
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    }
    /*�����Ѳ���ͨ��*/
    else
        ESP_LOGE(TAG, "The device had passed factory tests!");
}

