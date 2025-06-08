#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/idf_additions.h"

#include "led_strip.h"

#include "main_common.h"

static const char *TAG = "light_controller";

void lights_task(void *arg) {
  ambient_light_t *light = (ambient_light_t *) arg;
  led_strip_config_t strip_config = light->strip_config;
  led_strip_rmt_config_t rmt_config = light->rmt_config;

  /* Initialize the LED strip */
  led_strip_handle_t led_strip;
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);

  command_t* command;
  while (1) {
    /* Wait for a command from the queue */
    if (xQueueReceive(light->command_queue, &command, portMAX_DELAY)) {
      switch (command->type) {
        case COMMAND_TURN_OFF:
          led_strip_clear(led_strip);
          light->current_color = COLOR_OFF;
          break;
        case COMMAND_TURN_ON:
          light->current_color = command->data.color;
          for (int i = 0 ; i < strip_config.max_leds ; i++) {
              led_strip_set_pixel(led_strip, i, light->current_color.red, light->current_color.green, light->current_color.blue);
          }
          led_strip_refresh(led_strip);
          break;
        case COMMAND_SEQUENTIAL:
          /* Reset LED strip before sequential animation */
          led_strip_clear(led_strip);
          led_strip_refresh(led_strip);

          /* Set current color to data color */
          light->current_color = command->data.color;

          rgb_t temp_color = {0, 0, 0};

          /* Increment each LED brightness by number_steps until reaching current_color, then move to next LED */
          if (command->data.step.reverse) {
            /* Reverse order: start from the last LED and move to the first */
            for (int led_index = strip_config.max_leds - 1; led_index >= 0; led_index--) {
              for (int step = 0; step < command->data.step.num_steps; step++) {
                temp_color.red = (light->current_color.red * (step + 1)) / command->data.step.num_steps;
                temp_color.green = (light->current_color.green * (step + 1)) / command->data.step.num_steps;
                temp_color.blue = (light->current_color.blue * (step + 1)) / command->data.step.num_steps;

                led_strip_set_pixel(led_strip, led_index, temp_color.red, temp_color.green, temp_color.blue);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(command->data.step.delay_ms));
              }
            }
          } else {
            /* Normal order: start from the first LED and move to the last */
            for (int led_index = 0; led_index < strip_config.max_leds; led_index++) {
              for (int step = 0; step < command->data.step.num_steps; step++) {
                temp_color.red = (light->current_color.red * (step + 1)) / command->data.step.num_steps;
                temp_color.green = (light->current_color.green * (step + 1)) / command->data.step.num_steps;
                temp_color.blue = (light->current_color.blue * (step + 1)) / command->data.step.num_steps;

                led_strip_set_pixel(led_strip, led_index, temp_color.red, temp_color.green, temp_color.blue);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(command->data.step.delay_ms));
              }
            }
          }

          /* For good measure, set the entire strip to the new color */
          for (int i = 0; i < strip_config.max_leds; i++) {
            led_strip_set_pixel(led_strip, i, light->current_color.red, light->current_color.green, light->current_color.blue);
          }
          led_strip_refresh(led_strip);

          break;
        case COMMAND_FADE_TO:
          int8_t red_step = ((int)command->data.color.red - (int)light->current_color.red) / command->data.step.num_steps;
          int8_t green_step = ((int)command->data.color.green - (int)light->current_color.green) / command->data.step.num_steps;
          int8_t blue_step = ((int)command->data.color.blue - (int)light->current_color.blue) / command->data.step.num_steps;

          /* Fade each LED to the target color in steps */
          for (int step = 0; step < command->data.step.num_steps; step++) {
            rgb_t temp_color;
            temp_color.red = light->current_color.red + (red_step * (step + 1));
            temp_color.green = light->current_color.green + (green_step * (step + 1));
            temp_color.blue = light->current_color.blue + (blue_step * (step + 1));

            for (int i = 0; i < strip_config.max_leds; i++) {
              led_strip_set_pixel(led_strip, i, temp_color.red, temp_color.green, temp_color.blue);
            }

            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(command->data.step.delay_ms));
          }

          /* Update the current color to the newly set color */
          light->current_color = command->data.color;

          /* For good measure, set the entire strip to the new color */
          for (int i = 0; i < strip_config.max_leds; i++) {
            led_strip_set_pixel(led_strip, i, light->current_color.red, light->current_color.green, light->current_color.blue);
          }
          led_strip_refresh(led_strip);

          break;
        case COMMAND_SET_COLOR:
          light->current_color = command->data.color;
          break;
      }

      /* Check if there is a valid chained command */
      if (command->chained_command != NULL) {
        /* If there is a chained command, send it to the queue */
        if (xQueueSend(command->chained_command_queue, &command->chained_command, portMAX_DELAY) != pdTRUE) {
          ESP_LOGE(TAG, "Failed to send chained command to queue");
        }
      }

      /* Deallocate command memory */
      free(command);
      command = NULL;
    }
  }

  /* Delete the task if it exits the loop */
  ESP_LOGI(TAG, "Exiting light controller task");
  vTaskDelete(NULL);
  return;
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
  /* Initialize the LED strip configuration */
  light->strip_config.strip_gpio_num = gpio_num; // GPIO pin for the LED strip
  light->strip_config.max_leds = max_leds; // Number of LEDs in the strip
  light->strip_config.led_model = LED_MODEL_WS2812; // LED strip model
  light->strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB; // Color format
  light->strip_config.flags.invert_out = false; // Do not invert output signal

  /* Initialize the RMT configuration */
  light->rmt_config.clk_src = RMT_CLK_SRC_DEFAULT; // Default clock source
  light->rmt_config.resolution_hz = 10 * 1000 * 1000; // RMT counter clock frequency: 10MHz
  light->rmt_config.mem_block_symbols = 64; // Memory size of each RMT channel
  light->rmt_config.flags.with_dma = false; // Disable DMA feature

  /* Create a command queue for handling commands */
  light->command_queue = xQueueCreate(10, sizeof(command_t*));
  
  if (light->command_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create command queue");
    return ESP_FAIL; // Return error if queue creation fails
  }

  /* Initialize the LED strip */
  led_strip_handle_t led_strip;
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&light->strip_config, &light->rmt_config, &led_strip));
  led_strip_clear(led_strip);

  /* Set the initial color */
  light->current_color = START_COLOR;  

  /* Start the lights_task using FreeRTOS */
  BaseType_t task_result = xTaskCreatePinnedToCore(
    lights_task,                           // Task function
    "light_task",                          // Name of the task
    4096,                                  // Stack size in words
    light,                                 // Task input parameter (ambient_light_t*)
    CONFIG_LIGHT_CONTROLLER_TASK_PRIORITY, // Task priority
    NULL,                                  // Task handle (not used)
    CONFIG_LIGHT_CONTROLLER_TASK_CORE      // Core to run the task on
  );

  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create lights_task");
    return ESP_FAIL;
  }

  return ESP_OK;
}