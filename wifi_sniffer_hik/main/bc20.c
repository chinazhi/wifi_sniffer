/*********************************************************************************************************
*
* File                : bc20.c
* Hardware Environment: esp8266
* Build Environment   : freertos
* Version             : V1.0
* By                  : hik_gz
* Time                :
*********************************************************************************************************/
#include "app_main.h"
#include "bc20.h"
#include "my_nvs.h"
#include "adc.h"

#define TRUE  1
#define FALSE 0
#define UART_IDLE   0
#define UART_BUSY   1

char    BC20_IMSI[30] = {0};
uint8_t BC20_UartStat = UART_IDLE;         /* 串口使用状态 */
char    ATCMD_RceBuff[120];              /* 接收AT命令buff */
char    BC20_RceBuff[100];               /* 接收BC20发来的buff */
uint8_t BC20_RSSI = 0;                   /* RSSI值 */ 
int     MQTT_ERR_flag = 0;               /* 显示MQTT状态，0为正常，1为异常      */
int     BC20_ERR_flag = 0;               /* 显示bc20状态，0为正常，1为异常      */
int     BC20_GNSS_flag = 0;             /* GNSS定位，0为定位成功，1为异常      */

static  const char* TAG = "BC20";

/* 判断串口使用状态 */
void WaitUartIdle(void)
{
    while(BC20_UartStat == UART_BUSY)
    {
        vTaskDelay(600 / portTICK_RATE_MS); //600ms
    }
    BC20_UartStat = UART_BUSY;
}

/* 串口闲置状态 */
void ReleaseUart(void) 
{
    BC20_UartStat = UART_IDLE;
}

/* AT指令发送函数 */
int8_t ATCmd_Send(const char *ATCmd, uint32_t Len) 
{ 
    if(Len <= 0)
    {
        return FALSE;
    }	
    uart_write_bytes(UART_NUM_0, ATCmd, Len);
    return TRUE;
}

/* 接收数据函数 */
int8_t Reply_Recv(void)
{
    memset(ATCMD_RceBuff, 0, sizeof(ATCMD_RceBuff));
    int len = uart_read_bytes(UART_NUM_0, (uint8_t*)ATCMD_RceBuff, 120, 20 / portTICK_RATE_MS);
    if(len < 0)
    {
        return FALSE;
    }	
    return TRUE;
}

/* 设备与BC20同步操作 */
int8_t AT(void)
{
    char * ATCmd="AT\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	Reply_Recv();
	if(strstr((char*)ATCMD_RceBuff, "OK") != NULL)
    {
	     ReleaseUart();
         return TRUE;
    }
        	 
	ReleaseUart();
	return FALSE;
}

/* 设置命令回波为关 */
int8_t ATE0_Command(void)
{
    char * ATCmd="ATE0\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	Reply_Recv();
	if(strstr((char*)ATCMD_RceBuff, "OK") != NULL)
    {
	     ReleaseUart();
         return TRUE;
    }
      
	ReleaseUart();
	return FALSE;
}

/* 查询模块信号强度 */
int8_t BC20_Check_CSQ(void)
{
    int     CSQ = 0; 
    char    *Ptmp1 = NULL;
	char    *Ptmp2 = NULL;
	char    *ATCmd = "AT+CSQ\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd,strlen(ATCmd));
    vTaskDelay(300 /portTICK_RATE_MS); 
    Reply_Recv();    
    if((Ptmp1=strstr(ATCMD_RceBuff, "+CSQ:")) != NULL)
    {   
		Ptmp1 += strlen("+CSQ:");
		Ptmp2 = strchr(Ptmp1, ',');	
		*Ptmp2 = '\0';						
		CSQ = atoi(Ptmp1);	
        BC20_RSSI = 113 - 2*CSQ;
		ESP_LOGE(TAG, "BC20 CSQ  <- %u dBm>", BC20_RSSI);
		ReleaseUart();
        if(CSQ == 0)
            return FALSE;
        else
            return TRUE;	    
	}  	 
  	 
    ESP_LOGE(TAG, "CSQ信息接收失败");    
	ReleaseUart();
	return FALSE;
}

/* 设置默认的PSD连接 (Need to reboot) */
int8_t BC20_Set_PSD(void)
{
    char * ATCmd="AT+QCGDEFCONT=\"IP\",\"spe.inetd.vodafone.nbiot\"\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	Reply_Recv();
	if(strstr((char*)ATCMD_RceBuff, "OK") != NULL)
    {
	     ReleaseUart();
         return TRUE;
    }
  	 
	ReleaseUart();
	return FALSE;
}

/* 询问设置的UE的功能 */
int8_t BC20_Check_UE(void)
{
    char    fun[2]; 
    char    *Ptmp1 = NULL;
	char    *Ptmp2 = NULL;
    char    *ATCmd="AT+CFUN?\r\n";
	WaitUartIdle();

    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	Reply_Recv();
    if((Ptmp1=strstr(ATCMD_RceBuff, "+CFUN:")) != NULL)
    {   
		Ptmp1 += strlen("+CFUN:");
		Ptmp2 = strstr(Ptmp1, "\r\n");	
        memcpy(fun, Ptmp1, Ptmp2-Ptmp1);
        ESP_LOGE(TAG, "Check CFUN %s", fun);
		ReleaseUart();
		return TRUE;
	}
    
	ReleaseUart();
	return FALSE;
}

/* 查询IMSI编号并保存 */
int8_t BC20_Check_IMSI(void)
{
    char    *ATCmd = "AT+CIMI\r\n";
    char    * pTmp1 = NULL;	 
	WaitUartIdle();
    
    ATCmd_Send(ATCmd,strlen(ATCmd));
    vTaskDelay(300 / portTICK_RATE_MS);
    Reply_Recv();
    if(strstr((char*)ATCMD_RceBuff, "OK") != NULL)
    {
        if((pTmp1 = strstr(ATCMD_RceBuff, "460")) != NULL) 
		{
			memcpy(BC20_IMSI, pTmp1, 15);
            ESP_LOGE(TAG, "IMSI: %s", BC20_IMSI);
            ReleaseUart();
		    return TRUE;
		}
    } 
    
    ReleaseUart();
    return FALSE;
}

/* 查询网络是否被激活*/
int32_t BC20_Check_PS(void)  
{
    char    *ATCmd = "AT+CGATT?\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, strlen(ATCmd));
    vTaskDelay(300 / portTICK_RATE_MS);
    Reply_Recv();
    if(strstr((char*)ATCMD_RceBuff, "+CGATT: 1") != NULL) 
    {
	    ReleaseUart();
        /* 1表示已成功连接到网络 */
	    return 1;
    }
    else if(strstr((char*)ATCMD_RceBuff, "+CGATT: 0") != NULL)
    {
        ReleaseUart();
        /* 0表示尚未连接到网络 */
	    return 0;
    }
    
	ReleaseUart();
    return -1;
}

/* 查询ESP网络注册状态 */
int32_t BC20_Check_ESP(void)  
{
    char    *ATCmd = "AT+CEREG?\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, strlen(ATCmd));
    vTaskDelay(300 / portTICK_RATE_MS);
    Reply_Recv();
    if(strstr((char*)ATCMD_RceBuff, "+CEREG: 0,0") != NULL) 
    {
	    ReleaseUart();
        /* 0表示未注册 */
	    return 0;
    }
    else if((strstr((char*)ATCMD_RceBuff, "+CEREG: 0,1") != NULL) || (strstr((char*)ATCMD_RceBuff, "+CEREG: 0,5") != NULL))  //本地或漫游
    {
	    ReleaseUart();
        /* 1表示已在网络上注册 */
	    return 1;
    }
    else if(strstr((char*)ATCMD_RceBuff, "+CEREG: 0,2") != NULL)
    {
        ReleaseUart();
        /* 2表示在搜素网络 */
	    return 2;
    }
       
	ReleaseUart();
    return -1;
}

/* 查询信号连接状态 */
int32_t BC20_Check_Connection_Status(void)  
{
    char    *ATCmd = "AT+CSCON?\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, strlen(ATCmd));
    vTaskDelay(300 / portTICK_RATE_MS);
    Reply_Recv();
    if(strstr((char*)ATCMD_RceBuff, "+CSCON: 0,1") != NULL)  
    {
	    ReleaseUart();
        /* 1表示已连接*/
	    return 1;
    }
    else if(strstr((char*)ATCMD_RceBuff, "+CSCON: 0,0") != NULL)
    {
        ReleaseUart();
        /* 0表示空闲*/
	    return 0;
    }
        
	ReleaseUart();
    return -1;
}

/* 查询模块信号强度 */
unsigned long BC20_get_time(void)
{
    uint16_t year = 0,mon = 0,day = 0,hour = 0,min = 0,sec = 0;
    char    *Ptmp1 = NULL;
	char    *Ptmp2 = NULL;
    char    Ptmp3[30] = {0};
	char    *ATCmd = "AT+CCLK?\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd,strlen(ATCmd));
    vTaskDelay(300 /portTICK_RATE_MS); 
    Reply_Recv();   
    if((Ptmp1=strstr(ATCMD_RceBuff, "+CCLK: ")) != NULL)
    {   
        /* 取出年份 2019/7/3,7:41:39GMT+8 */
        memset(Ptmp3, 0, sizeof(Ptmp3));
		Ptmp1 += strlen("+CCLK: "); 
		Ptmp2 = strchr(Ptmp1, '/');
        memcpy(Ptmp3, Ptmp1, Ptmp2-Ptmp1);
		year = atoi(Ptmp3);
        /* 取出月份 7/3,7:41:39GMT+8 */
        memset(Ptmp3, 0, sizeof(Ptmp3));
        Ptmp1 += (Ptmp2-Ptmp1) + 1;  
        Ptmp2 = strchr(Ptmp1, '/');
        memcpy(Ptmp3, Ptmp1, Ptmp2-Ptmp1);
        mon = atoi(Ptmp3);
        /* 取出日期 3,7:41:39GMT+8 */
        memset(Ptmp3, 0, sizeof(Ptmp3));
        Ptmp1 += (Ptmp2-Ptmp1) + 1;
        Ptmp2 = strchr(Ptmp1, ',');
        memcpy(Ptmp3, Ptmp1, Ptmp2-Ptmp1);
        day = atoi(Ptmp3);
        /* 取出小时 7:41:39GMT+8 */
        memset(Ptmp3, 0, sizeof(Ptmp3));
        Ptmp1 += (Ptmp2-Ptmp1) + 1; 
        Ptmp2 = strchr(Ptmp1, ':');
        memcpy(Ptmp3, Ptmp1, Ptmp2-Ptmp1);
        hour = atoi(Ptmp3);
        /* 取出分钟 41:39GMT+8 */
        memset(Ptmp3, 0, sizeof(Ptmp3));
        Ptmp1 += (Ptmp2-Ptmp1) + 1; 
        Ptmp2 = strchr(Ptmp1, ':');
        memcpy(Ptmp3, Ptmp1, Ptmp2-Ptmp1);
        min = atoi(Ptmp3);
        /* 取出秒数 39GMT+8 */
        memset(Ptmp3, 0, sizeof(Ptmp3));          
        Ptmp1 += (Ptmp2-Ptmp1) + 1; 
        Ptmp2 = strchr(Ptmp1, 'G');
        memcpy(Ptmp3, Ptmp1, Ptmp2-Ptmp1);
        sec = atoi(Ptmp3);
        /* 计算绝对秒数 */
        if (0 >= (int)(mon -= 2)){/*1..12 ->11,12,1..10*/
	        mon += 12;			/*Puts Feb last since it has leap day*/
	        year -= 1;
        }
    	unsigned long time = ((((unsigned long)(year/4 - year/100 + year/400 +367*mon/12 +day) + year*365 -719499
    			)*24 + hour/*now have hours*/
    		)*60 + min/*now have minutes*/
    	)*60 + sec;/*finally seconds*/  
        
		ReleaseUart();
        return time;	    
	}
        	 
    ESP_LOGI(TAG, ""RED"get time error!");   
	ReleaseUart();
	return 0;
}

/* 查询模块整体网络状态 （该函数只是手动查询模块网络状态，上电设备会自动注册网络）*/
int32_t BC20_Check_Network_status(void)
{
    vTaskDelay(11000 / portTICK_RATE_MS);    
    ESP_LOGI(TAG, "Start Check Network!");

    if(!AT())
	{	
	   ESP_LOGI(TAG, "AT Error, need reboot！");/* 为致命错误，需重启模块。*/
	   return 1;
	}
    vTaskDelay(600 / portTICK_RATE_MS);
    
    if(!ATE0_Command())
	{	   
       ESP_LOGI(TAG, "Check ATE0 Cmd Timeout，ignore！");
	}
    vTaskDelay(600 / portTICK_RATE_MS);
    
    if(!BC20_Check_UE())
	{	   
       ESP_LOGI(TAG, "UE Timeout！ignore！");
	}
    vTaskDelay(600 / portTICK_RATE_MS);
    
    if(!BC20_Check_IMSI())
	{	   
       ESP_LOGI(TAG, "Check IMSI Timeout ignore！");
	}
    vTaskDelay(600 / portTICK_RATE_MS);
    
    if(!BC20_Check_CSQ())
	{	   
       ESP_LOGI(TAG, "Check CSQ Timeout，need reboot！");/* 为致命错误，需重启模块。*/  
	   return 1;
	}
    vTaskDelay(600 / portTICK_RATE_MS);

    if(BC20_Check_PS() != 1) 
    {   
        /* 为致命错误，需重启模块。*/
        ESP_LOGI(TAG, "The network is not active or timed out, need reboot！");
        return 1; 
    }
    vTaskDelay(600 / portTICK_RATE_MS);

    if(BC20_Check_ESP() != 1)
	{	   
       /* 为致命错误，需重启模块。*/
       ESP_LOGI(TAG, "Network registration status Timeout，need reboot！"); 
	   return 1;
	}
    vTaskDelay(600 / portTICK_RATE_MS);
    
    return 0;
}


/* GNSS功能开关    ，1为开启，0为关闭*/
int32_t BC20_Switch_GNSS(int i)
{

	char    ATCmd[20] = {0};
    sprintf(ATCmd, "AT+QGNSSC=%d\r\n", i);
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, (uint32_t)strlen(ATCmd));
	vTaskDelay(300 / portTICK_RATE_MS);
	Reply_Recv();
	if(strstr((char*)ATCMD_RceBuff, "OK") != NULL)
    {
	     ReleaseUart();
         return 0;
    }
        	 
	ReleaseUart();
	return -1;
}


/* BC20 GNSS搜星(NMEA/RMC)*/
int32_t BC20_Check_GNSS(char *NMEA)  
{
	char    *Ptmp1 = NULL;
	char    *Ptmp2 = NULL;
    char    latitude[15] = {0};
    char    longitude[15] = {0};
	char    ATCmd[30] = {0};
    sprintf(ATCmd, "AT+QGNSSRD=\"%s\"\r\n", NMEA);
	WaitUartIdle();
	
    ATCmd_Send(ATCmd, strlen(ATCmd));
    vTaskDelay(300 / portTICK_RATE_MS);
    Reply_Recv();
    /* 使用NMEA/RMC 模式搜星执行以下操作*/
    if(strstr((char*)ATCMD_RceBuff, ",A,") != NULL) 
    {
	    if(strstr(ATCMD_RceBuff, ",N,") != NULL)
	    {   
            Ptmp1 = strstr(ATCMD_RceBuff, ",A,");
            Ptmp1 += strlen(",A,");
            Ptmp2 = strstr(Ptmp1, ",N,");
            memset(latitude, 0, sizeof(latitude));
            memcpy(latitude, Ptmp1, Ptmp2-Ptmp1);
            save_str_nvs("latitude", latitude);
            //ESP_LOGE(TAG, "北纬: %s", latitude);
            
            Ptmp2 += strlen(",N,");  //11711.93480,E,0.000,,310818,,,A,V*1F
            Ptmp1 = strstr(Ptmp2, ",E,");//,E,0.000,,310818,,,A,V*1F
            memset(longitude, 0, sizeof(longitude));
            memcpy(longitude, Ptmp2, Ptmp1-Ptmp2);
            save_str_nvs("longitude", longitude);
            //ESP_LOGE(TAG, "东经: %s", longitude);	   
		}     
		//if(strstr(ATCMD_RceBuff, ",E,") != NULL)
	    //{   
	    //	Ptmp1 = strstr(ATCMD_RceBuff, ",N,"); 
        //    Ptmp1 += strlen(",N,");
        //    Ptmp2 = strstr(Ptmp1, ",E,");
        //    memcpy(longitude, Ptmp1, Ptmp2-Ptmp1);
        //    save_str_nvs("longitude", longitude);
        //    ESP_LOGE(TAG, "东经: %s", longitude);			
		//}
		ReleaseUart();
	    return 0;   /* 0表示已成功获取到定位 */
    }
    else if(strstr((char*)ATCMD_RceBuff, ",V,") != NULL)
    {
        ReleaseUart();  
	    return -1;  /* -1表示未成功获取到定位 */
    } 
        
	ReleaseUart();
    return -1;
}


/* 测试用 GNSS搜星 打印全部信息*/
int32_t Test_Check_GNSS(void)  
{ 
	char    *ATCmd = "AT+QGNSSRD?\r\n";
	WaitUartIdle();
	
    ATCmd_Send(ATCmd,strlen(ATCmd));
    vTaskDelay(1000 /portTICK_RATE_MS); 
    Reply_Recv();
    if(strstr((char*)ATCMD_RceBuff, "+QGNSSRD:") != NULL)
    { 
        ESP_LOGE(TAG, "**********GNSS信息**********");
        printf("%s",ATCMD_RceBuff);
        while(strstr((char*)ATCMD_RceBuff, "OK") == NULL)
        {
            Reply_Recv();
            printf("%s",ATCMD_RceBuff);

        }
        ESP_LOGE(TAG, "**********GNSS结束**********");
		ReleaseUart();
        return 0;	     
    }	 
    ESP_LOGE(TAG, "GNSS 无响应信息!");
	ReleaseUart();
	return -1;
}


/*************************************************
Function: BC20_Module_switch
Description: 模块开关函数
Input: 输入0为开启动作，1为关闭动作  
*************************************************/
void BC20_Module_switch(int i)
{
    /* 使能BC20模块 */
    gpio_config_t  BC20_EN_conf = {
        .mode = GPIO_MODE_OUTPUT, 
        .pull_up_en = 1, 
        .pin_bit_mask = ((uint32_t) 1 << BC20_EN_PIN)
    };
    gpio_config(&BC20_EN_conf);       
    gpio_set_level(BC20_EN_PIN, i);    
}
/*************************************************
Function: BC20_ERROR_LED_task
Description: BC20错误，LED提示任务 
Return:  
*************************************************/
void BC20_ERROR_LED_task(void)
{
    BC20_ERR_flag = 1;
    while(BC20_ERR_flag != 0)
    {
        gpio_set_level(LED_BC20_PIN, 0);
        vTaskDelay( 2 * 1000 / portTICK_RATE_MS);
        gpio_set_level(LED_BC20_PIN, 1);
        vTaskDelay( 2 * 1000 / portTICK_RATE_MS);  
    }
    vTaskDelete(NULL);
}

/*************************************************
Function: Check_BC20
Description: 检测BC20是否可以使用，不可以则尝试关机重启
Return:  
*************************************************/
void Check_BC20(void)
{
    BC20_Module_switch(0); /* 开启BC20 */
    xTaskCreate((TaskFunction_t)BC20_ERROR_LED_task, "BC20_ERROR_LED_task", 256, NULL, 5, NULL);
    while(1 == BC20_Check_Network_status())
    {
        BC20_Module_switch(1);
        vTaskDelay( 1000 / portTICK_RATE_MS);
        BC20_Module_switch(0);        
    }
    BC20_ERR_flag = 0;
}

/*************************************************
Function: MQTT_ERROR_LED_task
Description: MQTT错误，LED提示任务 
Return:  
*************************************************/
void MQTT_ERROR_LED_task(void)
{
    MQTT_ERR_flag = 1;
    while(MQTT_ERR_flag != 0)
    {
        gpio_set_level(LED_MQTT_PIN, 0);
        vTaskDelay( 2 * 1000 / portTICK_RATE_MS);
        gpio_set_level(LED_MQTT_PIN, 1);
        vTaskDelay( 2 * 1000 / portTICK_RATE_MS);  
    }
    vTaskDelete(NULL);
}

/*************************************************
Function: shu_xue_qi_wang  
Description: 取十次定位信息，将取到的信息(ddmm.mmmm)小数点后四位求数学期望 //ggzhi 20190831
Return:  
*************************************************/
void shu_xue_qi_wang(int  *   get_num, char* buf)
{
    int i , realnum =  0;	
    for(  i = 0; i< 10;i++)
    {
        realnum += get_num[i] * 0.1; 
	}
    //printf("num=%d\n",realnum);
    char a[4] = "0000";
    if(realnum < 1000 )
        sprintf(&a[1], "%d", realnum);
    else
        sprintf(a, "%d", realnum);
    //printf("haha:%s", a);
    memcpy(buf, a, 4);
    
}


/*************************************************
Function: BC20_GNSS_Nvs_task  //ggzhi 20190831
Description: 搜星任务 
Input: 
Return:  xTaskCreate(BC20_GNSS_Nvs_task, "BC20_GNSS_Nvs_task", 256, NULL, 9, NULL); 
*************************************************/
 void  GNSS_Nvs_task(void)
{
    int i = 0;                      /*记录定位失败次数*/   
    int BC20_GNSS_num = 0;          /* GNSS定位，定位到的次数      */
    int get_n[10] = {0};            /*储存纬度小数点后四位数值*/
    int get_e[10] = {0};            /*储存经度小数点后四位数值*/
    char N[15] = {0}, E[15] = {0};  /*储存定位数组字符串*/
 
    BC20_GNSS_flag = 1;
    BC20_Switch_GNSS(1);
    save_str_nvs("latitude", "N:NULL");
    save_str_nvs("longitude", "E:NULL");

    while( BC20_GNSS_flag == 1)
    {
        vTaskDelay(5000 / portTICK_RATE_MS);
        if(-1 == BC20_Check_GNSS("NMEA/RMC"))
        {   
            i++;
            ESP_LOGI(TAG, ""YELLOW"Try GNSS after 5 seconds!");
            if(i > 20)
            {
                ESP_LOGI(TAG, ""RED"Try GNSS Fail! close GNSS task");
                break;
            }       
        }
        else
        {
            size_t length = sizeof(N);
            get_str_nvs("latitude", N, &length);
            get_str_nvs("longitude", E, &length); 
            //ESP_LOGI(TAG, "N=%s, E=%s, GNSS success! close GNSS task！", N, E);
            get_n[BC20_GNSS_num] = atoi(&N[5]); //将末四位定位信息存入数组
            get_e[BC20_GNSS_num] = atoi(&E[6]);
            BC20_GNSS_num++;  
        }
        if(BC20_GNSS_num == 10)
        {
            char buf[4] = {0}; 
            shu_xue_qi_wang(get_n, buf);
            memcpy(&N[5], buf, 4);
            save_str_nvs("latitude", N);
         
            shu_xue_qi_wang(get_e, buf);
            memcpy(&E[6], buf, 4);
            save_str_nvs("longitude", E);  
            ESP_LOGI(TAG, "N=%s, E=%s, GNSS success! close GNSS task！", N, E);
            break;
        }
    }
    BC20_Switch_GNSS(0);
    vTaskDelete(NULL);
}



