/*********************************************************************************************************
*
* File                : my_nvs.h
* Hardware Environment: 
* Build Environment   : 
* Version             : V1.0
* Author              : hik_gz
* Time                :
* Brief               : 
*********************************************************************************************************/


#ifndef _MY_NVS_H
#define _MY_NVS_H


extern void String_to_hex(char *str, uint8_t *r);
extern void save_num_nvs(const char* num_name, int save_num);
extern int32_t get_total_nvs(const char* num_name);
extern void save_mac_nvs(int num, uint8_t *monitor_mac, size_t length);
extern int get_mac_nvs(int num, uint8_t *monitor_mac, size_t* length);
extern void save_str_nvs(char * name, char* my_value);
extern int get_str_nvs(char* name, char*out_value, size_t * out_value_len);


#endif


