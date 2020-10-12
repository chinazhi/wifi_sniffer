
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>

static const char* TAG = "my_nvs";
#define RED 		"\033[0;31m"

/*�ַ���ת16������*/
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
    /* NVS�����ľ���������� rtosϵͳ�����񴴽����صľ����*/
    nvs_handle TotalNvs;
        
    /* ע��ȡֵ��Χ�����������ҵ������������������ */
    int32_t Total = save_num;
    
    /* �����ݿ⣬��һ�����ݿ���൱�ڻ᷵��һ����� */
    if (nvs_open("Total", NVS_READWRITE, &TotalNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }
    
    /* ���� */
    int32_t err = nvs_set_i32(TotalNvs, num_name, Total);
        
    if (err != ESP_OK)
        ESP_LOGE(TAG, ""RED"Save NVS u8 error !");
    
    /* �ύ�£��൱��������� ��Ӧ�á� ��ť����û�ر���壡*/
    nvs_commit(TotalNvs);
        
    /*�ر����ݿ⣬�ر����!*/
    nvs_close(TotalNvs);
    
}

int32_t get_total_nvs(const char* num_name)
{
    /* NVS�����ľ���������� rtosϵͳ�����񴴽����صľ����*/
    nvs_handle TotalNvs;
  
    int32_t Total = 0;
        
    /* �����ݿ⣬��һ�����ݿ���൱�ڻ᷵��һ����� */
    if (nvs_open("Total", NVS_READWRITE, &TotalNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }

    /* ��ȡ */
    int32_t err = nvs_get_i32(TotalNvs, num_name, &Total);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"get nvs total error");
        nvs_close(TotalNvs);
        return 1;
    }

    /*�ر����ݿ⣬�ر����!*/
    nvs_close(TotalNvs); 

    return Total;

}



void save_mac_nvs(int num, uint8_t *monitor_mac, size_t length)
{
    /* NVS�����ľ���������� rtosϵͳ�����񴴽����صľ����*/
    nvs_handle MacNvs;

    /* ��ǰmac*/
    char value[10] ={0}; 
    sprintf(value, "Mac_%d", num);
    
    /* �����ݿ⣬��һ�����ݿ���൱�ڻ᷵��һ����� */
    if (nvs_open("Mac", NVS_READWRITE, &MacNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }
    
    /* ����*/
    int32_t err = nvs_set_blob(MacNvs, value, monitor_mac, length);
        
    if (err != ESP_OK)
        ESP_LOGE(TAG, ""RED"Save group  Fail!");

    /* �ύ�£��൱��������� ��Ӧ�á� ��ť����û�ر���壡*/
    nvs_commit(MacNvs);
        
    /*�ر����ݿ⣬�ر����!*/
    nvs_close(MacNvs);
    
}


int get_mac_nvs(int num, uint8_t *monitor_mac, size_t* length)
{
    /* NVS�����ľ���������� rtosϵͳ�����񴴽����صľ����*/
    nvs_handle MacNvs;

    /* ��ǰmac����*/
    char value[10] ={0}; 
    sprintf(value, "Mac_%d", num);
    
    /* �����ݿ⣬��һ�����ݿ���൱�ڻ᷵��һ����� */
    if (nvs_open("Mac", NVS_READWRITE, &MacNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }

    unsigned char mac[6] = {0};
    size_t  mac_length = 6;
    
    /* ��ȡ array */
    int32_t err = nvs_get_blob(MacNvs, value, mac, &mac_length);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"get array error!");
        nvs_close(MacNvs);
        return 1;
    }
    /* �����ַ� */
    memcpy(monitor_mac, mac, 6);
    
    /*�ر����ݿ⣬�ر����!*/
    nvs_close(MacNvs);

    return 0;
}

/*�����ַ��� ��ǰ���15���ַ�*/
void save_str_nvs(char * name, char* my_value)
{
    /* NVS�����ľ���������� rtosϵͳ�����񴴽����صľ����*/
    nvs_handle LocationNvs_save;
          
    /* �����ݿ⣬��һ�����ݿ���൱�ڻ᷵��һ����� */
    if (nvs_open("location", NVS_READWRITE, &LocationNvs_save) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }
    /* �����ַ� */
    char buffer[15];
    memcpy(buffer, my_value, 15);
    
    /* ���� */
    int32_t err = nvs_set_str(LocationNvs_save, name, buffer);
        
    if (err != ESP_OK)
        ESP_LOGE(TAG, ""RED"Save nvs str error !");
    
    /* �ύ */
    nvs_commit(LocationNvs_save);
        
    /* �ر����ݿ�      */
    nvs_close(LocationNvs_save);
    
}

int get_str_nvs(char* name, char*out_value, size_t * out_value_len)
{
    /* NVS�����ľ���������� rtosϵͳ�����񴴽����صľ����*/
    nvs_handle LocationNvs_get;  
        
    /* �����ݿ⣬��һ�����ݿ���൱�ڻ᷵��һ����� */
    if (nvs_open("location", NVS_READWRITE, &LocationNvs_get) != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"Open NVS Table fail!");
    }

    char buffer[15] = {0};
    size_t  length = 15;
   
    /* ��ȡ */
    int32_t err = nvs_get_str(LocationNvs_get, name, buffer, &length);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, ""RED"get nvs str error");
        nvs_close(LocationNvs_get);
        return 1;
     }   
    /* �����ַ� */
    memcpy(out_value, buffer, 15);
    
    /*�ر����ݿ⣬�ر����!*/
    nvs_close(LocationNvs_get);

    return 0;
}







