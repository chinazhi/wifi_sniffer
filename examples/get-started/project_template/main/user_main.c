

/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include "sys/time.h"
#include "esp_system.h"

/******************************************************************************
 * FunctionName : app_main
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void app_main(void)
{
    while(1)
    {
      int i;
      printf("SDK version:%s\n", esp_get_idf_version());
      for(i = 0; i<100000; i++)
      {
        
      } 
    }
}
