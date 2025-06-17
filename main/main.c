#include "main_common.h"

static const char* TAG = "main";

ambient_light_t lights[NUM_LIGHTS];

SemaphoreHandle_t current_color_lock = NULL;
StaticSemaphore_t semaphore_buffer;
rgb_t current_color = START_COLOR;

void app_main(void)
{
  current_color_lock = xSemaphoreCreateBinaryStatic( &semaphore_buffer );
  if (current_color_lock == NULL) {
    ESP_LOGE(TAG, "Failed to create current color mutex");
    return;
  }
  xSemaphoreGive(current_color_lock); // Initialize the semaphore to be available

  ESP_LOGI(TAG, "Starting light tasks...");
  init_ambient_light(&lights[DASHBOARD_INDEX], CONFIG_DASHBOARD_GPIO, CONFIG_DASHBOARD_MAX_LEDS);
  init_ambient_light(&lights[DOOR_INDEX], CONFIG_DOOR_GPIO, CONFIG_DOOR_MAX_LEDS);

  ESP_LOGI(TAG, "Starting HTTP and CAN sniffer...");
  ESP_ERROR_CHECK(start_can_sniffer_task());
  ESP_ERROR_CHECK(start_http_server_task());

  vTaskDelete(NULL);
}
