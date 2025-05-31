#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "main_common.h"

static const char* TAG = "Main";

ambient_light_t lights[NUM_LIGHTS];

void app_main(void)
{
  init_ambient_light(&lights[DRIVER_DASHBOARD_INDEX], 12, 110);
  // init_ambient_light(&lights[PASSENGER_DASHBOARD_INDEX], 14, 110);
  // init_ambient_light(&lights[DRIVER_CENTER_INDEX], 7, 110);
  // init_ambient_light(&lights[PASSENGER_CENTER_INDEX], 8, 110);
  // init_ambient_light(&lights[DRIVER_DOOR_INDEX], 9, 110);
  // init_ambient_light(&lights[PASSENGER_DOOR_INDEX], 10, 110);

  ESP_LOGI(TAG, "Starting tasks...");
  // create_can_sniffer_task(0, NULL);
  ESP_ERROR_CHECK(start_http_server_task());
  
  vTaskDelete(NULL);
}
