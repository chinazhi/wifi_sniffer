/*********************************************************************************************************
* File                : app_main.h
* Hardware Environment: 
* Build Environment   : 
* Version             : V1.0
* Author              : hik_gz
* Time                :
* Brief               : 
*********************************************************************************************************/
#ifndef _APP_MAIN_H
#define _APP_MAIN_H

#include "includes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "sys/time.h"

#define DEVICE_NAME "PU9340ST"
#define HAD_VERSION "V1.0"
#define SOF_VERSION "V1.0.0"
#define BUILD_DATA  "190812"

/*自定义打印信息颜色  */
#define NONE 		"\033[0m"
#define BLACK 		"\033[0;30m"
#define BLUE 		"\033[0;34m"
#define LIGHT_BLUE 	"\033[1;34m"
#define GREEN 		"\033[0;32m"
#define RED 		"\033[0;31m"
#define YELLOW 		"\033[1;33m"

/*8266引脚定义  */
#define	BC20_EN_PIN			    GPIO_NUM_5  /* BC使能引脚 */
#define	LED_MQTT_PIN			GPIO_NUM_4  /* MQTT工作灯 */
#define	LED_BC20_PIN			GPIO_NUM_14  /* BC20工作灯 */
#define	CRT_VBAT_PIN			GPIO_NUM_13 /* 电源开关控制引脚 */
#define	LED_VBAT_PIN			GPIO_NUM_12 /* 电量灯引脚 */
#define	KEY_GPIO_PIN			GPIO_NUM_16 /* 按键引脚 */

/*设备软件基本信息  */
#define appsw "V1.0.0-190831" 
#define ver "V1.0.0"
#define service "st" 


#endif



