#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/idf_additions.h"

#include "led_strip.h"

#include "main_common.h"

static const char *TAG = "LightsController";

void lights_task(void *arg) {
  ambient_light_t *light = (ambient_light_t *) arg;
  led_strip_config_t strip_config = light->strip_config;
  led_strip_rmt_config_t rmt_config = light->rmt_config;

  /* Initialize the LED strip */
  led_strip_handle_t led_strip;
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);

  Command command;
  while (1) {
    /* Wait for a command from the queue */
    if (xQueueReceive(light->command_queue, &command, portMAX_DELAY)) {
      switch (command.type) {
        case COMMAND_TURN_OFF:
          led_strip_clear(led_strip);
          break;
        case COMMAND_TURN_ON:
          for (int i = 0 ; i < strip_config.max_leds ; i++) {
              led_strip_set_pixel(led_strip, i, light->current_color.red, light->current_color.green, light->current_color.blue);
          }
          led_strip_refresh(led_strip);
          break;
        case COMMAND_SET_COLOR:
          light->current_color = command.data.color;
          for (int i = 0 ; i < strip_config.max_leds ; i++) {
              led_strip_set_pixel(led_strip, i, light->current_color.red, light->current_color.green, light->current_color.blue);
          }
          led_strip_refresh(led_strip);
          break;
      }
    }
  }
}

/**
 * @brief Initializes the ambient light controller and its resources.
 *
 * This function sets up the configuration for the LED strip and RMT (Remote Control) peripheral,
 * creates a command queue for handling lighting commands, initializes the LED strip to an "off" state,
 * and starts the FreeRTOS task responsible for controlling the lights.
 *
 * @param[in,out] light      Pointer to the ambient_light_t structure to initialize.
 * @param[in]     gpio_num   GPIO pin number connected to the LED strip.
 * @param[in]     max_leds   Maximum number of LEDs in the strip.
 *
 * @return ESP_OK on success, or ESP_FAIL if initialization fails (e.g., queue or task creation fails).
 */
esp_err_t init_ambient_light(ambient_light_t *light, const int gpio_num, const int max_leds) {
  // Initialize the LED strip configuration
  light->strip_config.strip_gpio_num = gpio_num; // GPIO pin for the LED strip
  light->strip_config.max_leds = max_leds; // Number of LEDs in the strip
  light->strip_config.led_model = LED_MODEL_WS2812; // LED strip model
  light->strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB; // Color format
  light->strip_config.flags.invert_out = false; // Do not invert output signal

  // Initialize the RMT configuration
  light->rmt_config.clk_src = RMT_CLK_SRC_DEFAULT; // Default clock source
  light->rmt_config.resolution_hz = 10 * 1000 * 1000; // RMT counter clock frequency: 10MHz
  light->rmt_config.mem_block_symbols = 64; // Memory size of each RMT channel, in words (4 bytes)
  light->rmt_config.flags.with_dma = false; // Disable DMA feature

  // Create a command queue for handling commands
  light->command_queue = xQueueCreate(10, sizeof(Command));
  
  if (light->command_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create command queue");
    return ESP_FAIL; // Return error if queue creation fails
  }

  // Initialize the LED strip
  led_strip_handle_t led_strip;
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&light->strip_config, &light->rmt_config, &led_strip));
  led_strip_clear(led_strip);

  // Set the initial color
  light->current_color = (RGB){0, 0, 0}; // Default to off
  for (int i = 0; i < light->strip_config.max_leds; i++) {
    led_strip_set_pixel(led_strip, i, light->current_color.red, light->current_color.green, light->current_color.blue);
  }
  led_strip_refresh(led_strip);

  // Start the lights_task using FreeRTOS, pinned to core 0
  if (xTaskCreatePinnedToCore(lights_task, "light_task", 4096, light, 5, NULL, 0) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create lights_task");
    return ESP_FAIL;
  }

  return ESP_OK;
}