
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

static const char* TAG = "my_nvs";
#define RED 		"\033[0;31m"

/*字符串转16进制数*/
void String_to_hex(char *str, uint8_t *r)
{
    int i = 0;
	uint8_t highByte, lowByte; 
    while(*(str+i) != '\0')
    {
    	highByte = *(str+i)-'0';
        if (highByte > ('F'-'0'))
            highByte -= ('a'-':');    
    	else if (highByte > ('9'-'0'))
			highByte -= ('A'-':');
		highByte *= 16;
		
		lowByte = *(str+i+1)-'0';    
        if (lowByte > ('F'-'0'))
           lowByte -= ('a'-':');
		else if (lowByte > ('9'-'0'))
			lowByte -= ('A'-':');
					
        i+= 2;
        *r++ = highByte + lowByte;
    }
}

void save_num_nvs(const char* num_name, int save_num)
{
    /* NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！*/
    nvs_handle TotalNvs;
        
    /* 注意取值范围，根据自身的业务需求来做保存类型 */
    int32_t Total = save_num;
    
    /* 打开数据库，打开一个数据库就相当于会返回一个句柄 */
    if (nvs_open("Total", NVS_READWRITE, &TotalNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }
    
    /* 保存 */
    int32_t err = nvs_set_i32(TotalNvs, num_name, Total);
        
    if (err != ESP_OK)
        ESP_LOGE(TAG, ""RED"Save NVS u8 error !");
    
    /* 提交下！相当于软件面板的 “应用” 按钮，并没关闭面板！*/
    nvs_commit(TotalNvs);
        
    /*关闭数据库，关闭面板!*/
    nvs_close(TotalNvs);
    
}

int32_t get_total_nvs(const char* num_name)
{
    /* NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！*/
    nvs_handle TotalNvs;
  
    int32_t Total = 0;
        
    /* 打开数据库，打开一个数据库就相当于会返回一个句柄 */
    if (nvs_open("Total", NVS_READWRITE, &TotalNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }

    /* 读取 */
    int32_t err = nvs_get_i32(TotalNvs, num_name, &Total);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"get nvs total error");
        nvs_close(TotalNvs);
        return 1;
    }

    /*关闭数据库，关闭面板!*/
    nvs_close(TotalNvs); 

    return Total;

}



void save_mac_nvs(int num, uint8_t *monitor_mac, size_t length)
{
    /* NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！*/
    nvs_handle MacNvs;

    /* 当前mac*/
    char value[10] ={0}; 
    sprintf(value, "Mac_%d", num);
    
    /* 打开数据库，打开一个数据库就相当于会返回一个句柄 */
    if (nvs_open("Mac", NVS_READWRITE, &MacNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }
    
    /* 保存*/
    int32_t err = nvs_set_blob(MacNvs, value, monitor_mac, length);
        
    if (err != ESP_OK)
        ESP_LOGE(TAG, ""RED"Save group  Fail!");

    /* 提交下！相当于软件面板的 “应用” 按钮，并没关闭面板！*/
    nvs_commit(MacNvs);
        
    /*关闭数据库，关闭面板!*/
    nvs_close(MacNvs);
    
}


int get_mac_nvs(int num, uint8_t *monitor_mac, size_t* length)
{
    /* NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！*/
    nvs_handle MacNvs;

    /* 当前mac名字*/
    char value[10] ={0}; 
    sprintf(value, "Mac_%d", num);
    
    /* 打开数据库，打开一个数据库就相当于会返回一个句柄 */
    if (nvs_open("Mac", NVS_READWRITE, &MacNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }

    unsigned char mac[6] = {0};
    size_t  mac_length = 6;
    
    /* 读取 array */
    int32_t err = nvs_get_blob(MacNvs, value, mac, &mac_length);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"get array error!");
        nvs_close(MacNvs);
        return 1;
    }
    /* 拷贝字符 */
    memcpy(monitor_mac, mac, 6);
    
    /*关闭数据库，关闭面板!*/
    nvs_close(MacNvs);

    return 0;
}

/*保存字符串 当前最大15个字符*/
void save_str_nvs(char * name, char* my_value)
{
    /* NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！*/
    nvs_handle LocationNvs_save;
          
    /* 打开数据库，打开一个数据库就相当于会返回一个句柄 */
    if (nvs_open("location", NVS_READWRITE, &LocationNvs_save) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }
    /* 拷贝字符 */
    char buffer[15];
    memcpy(buffer, my_value, 15);
    
    /* 保存 */
    int32_t err = nvs_set_str(LocationNvs_save, name, buffer);
        
    if (err != ESP_OK)
        ESP_LOGE(TAG, ""RED"Save nvs str error !");
    
    /* 提交 */
    nvs_commit(LocationNvs_save);
        
    /* 关闭数据库      */
    nvs_close(LocationNvs_save);
    
}

int get_str_nvs(char* name, char*out_value, size_t * out_value_len)
{
    /* NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！*/
    nvs_handle LocationNvs_get;  
        
    /* 打开数据库，打开一个数据库就相当于会返回一个句柄 */
    if (nvs_open("location", NVS_READWRITE, &LocationNvs_get) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }

    char buffer[15] = {0};
    size_t  length = 15;
   
    /* 读取 */
    int32_t err = nvs_get_str(LocationNvs_get, name, buffer, &length);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"get nvs str error");
        nvs_close(LocationNvs_get);
        return 1;
     }   
    /* 拷贝字符 */
    memcpy(out_value, buffer, 15);
    
    /*关闭数据库，关闭面板!*/
    nvs_close(LocationNvs_get);

    return 0;
}







