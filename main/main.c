#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "main_common.h"

static const char* TAG = "main";

ambient_light_t lights[NUM_LIGHTS];

void app_main(void)
{
  ESP_LOGI(TAG, "Starting light tasks...");
  init_ambient_light(&lights[DASHBOARD_INDEX], CONFIG_DASHBOARD_GPIO, CONFIG_DASHBOARD_MAX_LEDS);
  init_ambient_light(&lights[CENTER_INDEX], CONFIG_CENTER_GPIO, CONFIG_CENTER_MAX_LEDS);
  init_ambient_light(&lights[DOOR_INDEX], CONFIG_DOOR_GPIO, CONFIG_DOOR_MAX_LEDS);

  ESP_LOGI(TAG, "Starting HTTP and CAN sniffer...");
  ESP_ERROR_CHECK(start_can_sniffer_task());
  ESP_ERROR_CHECK(start_http_server_task());
  
  vTaskDelete(NULL);
}
