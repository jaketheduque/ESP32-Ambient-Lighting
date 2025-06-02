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
  ESP_LOGI(TAG, "Starting light tasks...");
  init_ambient_light(&lights[DASHBOARD_INDEX], CONFIG_DASHBOARD_GPIO, CONFIG_DASHBOARD_MAX_LEDS);
  init_ambient_light(&lights[CENTER_INDEX], CONFIG_CENTER_GPIO, CONFIG_CENTER_MAX_LEDS);
  init_ambient_light(&lights[DOOR_INDEX], CONFIG_DOOR_GPIO, CONFIG_DOOR_MAX_LEDS);

  Command set_white_command = {
    .type = COMMAND_SET_COLOR,
    .data.color = { .red = 255, .green = 255, .blue = 255 }
  };
  xQueueSend(lights[DASHBOARD_INDEX].command_queue, &set_white_command, portMAX_DELAY);

  Command sequential_animation_command = {
    .type = COMMAND_SEQUENTIAL,
    .data.step = { .num_steps = 2, .delay_ms = 20 }
  };
  xQueueSend(lights[DASHBOARD_INDEX].command_queue, &sequential_animation_command, portMAX_DELAY);

  ESP_LOGI(TAG, "Starting HTTP and CAN sniffer...");
  // create_can_sniffer_task(0, NULL);
  ESP_ERROR_CHECK(start_http_server_task());
  
  vTaskDelete(NULL);
}
