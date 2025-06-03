#include <string.h>

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main_common.h"

/* TWAI configuration */
static twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NORMAL);
static twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
static twai_filter_config_t f_config = {
  .acceptance_code = TWAI_ACCEPTANCE_CODE,
  .acceptance_mask = TWAI_ACCEPTANCE_MASK,
  .single_filter = true
};

/* Previous Data */
static uint8_t previous_light_data[8];
static uint8_t previous_display_data[8];

static const char *TAG = "can_sniffer";

/* CAN sniffer FreeRTOS task function */
void can_sniffer_task() {
  while (1) {
    /* Wait for the message to be received */
    twai_message_t message;
    esp_err_t receive_status = twai_receive(&message, pdMS_TO_TICKS(1000));
    if (receive_status == ESP_ERR_TIMEOUT) {
      ESP_LOGD(TAG, "Timed out waiting for message");
      continue;
    } else if (receive_status != ESP_OK) {
      ESP_LOGE(TAG, "Error receiving message");
      continue;
    }

    ESP_LOGD(TAG, "Received CAN message with identifier: 0x%" PRIx32, message.identifier);

    /* Process CAN bus messages */
    if (message.identifier == LIGHTS_CAN_ID) {
      if (memcmp(message.data, previous_light_data, message.data_length_code) != 0) {
        ESP_LOGI(TAG, "Received new lights CAN message");

        /* Code for light stuff goes here */

        memcpy(previous_light_data, message.data, message.data_length_code);
      }
    }

    if (message.identifier == DISPLAY_CAN_ID) {
      if (memcmp(message.data, previous_display_data, message.data_length_code) != 0) {
        ESP_LOGI(TAG, "Received new display CAN message");

        if (RISING_CHANGE(message.data[DISPLAY_STATUS_BYTE_INDEX], previous_display_data[DISPLAY_STATUS_BYTE_INDEX], DISPLAY_POWER_MASK)) {
          ESP_LOGI(TAG, "Display has powered on");

          command_t *door_command = calloc(1, sizeof(command_t));
          *door_command = (command_t) {
            .type = COMMAND_SEQUENTIAL,
            .data.step = {
              .num_steps = DEFAULT_SEQUENTIAL_STEPS,
              .delay_ms = DEFAULT_SEQUENTIAL_DELAY_MS,
              .reverse = false
            },
          };

          command_t *center_command = calloc(1, sizeof(command_t));
          *center_command = (command_t) {
            .type = COMMAND_SEQUENTIAL,
            .data.step = {
              .num_steps = DEFAULT_SEQUENTIAL_STEPS,
              .delay_ms = DEFAULT_SEQUENTIAL_DELAY_MS,
              .reverse = false
            },
          };

          command_t *dashboard_command = calloc(1, sizeof(command_t));
          *dashboard_command = (command_t) {
            .type = COMMAND_SEQUENTIAL,
            .data.step = {
              .num_steps = DEFAULT_SEQUENTIAL_STEPS,
              .delay_ms = DEFAULT_SEQUENTIAL_DELAY_MS,
              .reverse = true
            },
            .chained_command_queue = lights[DOOR_INDEX].command_queue,
            .chained_command = door_command
          };

          /* Send commands to the queues */
          if (xQueueSend(lights[CENTER_INDEX].command_queue, &center_command, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send center command to queue");
            free(center_command);
          }
          if (xQueueSend(lights[DASHBOARD_INDEX].command_queue, &dashboard_command, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send dashboard command to queue");
            free(dashboard_command);
          }
        } else if (FALLING_CHANGE(message.data[DISPLAY_STATUS_BYTE_INDEX], previous_display_data[DISPLAY_STATUS_BYTE_INDEX], DISPLAY_POWER_MASK)) {
          ESP_LOGI(TAG, "Display has powered off");

          command_t *door_command = calloc(1, sizeof(command_t));
          *door_command = (command_t) {
            .type = COMMAND_FADE_TO,
            .data.color = COLOR_OFF,
            .data.step = {
              .num_steps = DEFAULT_FADE_STEPS,
              .delay_ms = DEFAULT_FADE_DELAY_MS,
            },
          };

          command_t *center_command = calloc(1, sizeof(command_t));
          *center_command = (command_t) {
            .type = COMMAND_FADE_TO,
            .data.color = COLOR_OFF,
            .data.step = {
              .num_steps = DEFAULT_FADE_STEPS,
              .delay_ms = DEFAULT_FADE_DELAY_MS,
            },
          };

          command_t *dashboard_command = calloc(1, sizeof(command_t));
          *dashboard_command = (command_t) {
            .type = COMMAND_FADE_TO,
            .data.color = COLOR_OFF,
            .data.step = {
              .num_steps = DEFAULT_FADE_STEPS,
              .delay_ms = DEFAULT_FADE_DELAY_MS,
            },
          };

          /* Send commands to the queues */
          if (xQueueSend(lights[CENTER_INDEX].command_queue, &center_command, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send center command to queue");
            free(center_command);
          }
          if (xQueueSend(lights[DASHBOARD_INDEX].command_queue, &dashboard_command, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send dashboard command to queue");
            free(dashboard_command);
          }
          if (xQueueSend(lights[DOOR_INDEX].command_queue, &door_command, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send door command to queue");
            free(door_command);
          }
        }

        memcpy(previous_display_data, message.data, message.data_length_code);
      }
    }
  }

  /* Delete the task if it exits the loop */
  ESP_LOGI(TAG, "Exiting CAN sniffer task");
  vTaskDelete(NULL);
  return;
}

esp_err_t start_can_sniffer_task() {
  /* Install TWAI driver */
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    ESP_LOGI(TAG, "TWAI Driver installed");
  } else {
    ESP_LOGE(TAG, "Failed to install driver");
    return -1;
  }

  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    ESP_LOGI(TAG, "TWAI Driver started");
  } else {
    ESP_LOGE(TAG, "Failed to start driver");
    return -1;
  }

  /* Create FreeRTOS task for CAN sniffer loop */
  xTaskCreatePinnedToCore(
      can_sniffer_task,                  // Task function
      "can_sniffer_task",                // Task name
      4096,                              // Stack size
      NULL,                              // Task parameters
      CONFIG_CAN_SNIFFER_TASK_PRIORITY,  // Priority
      NULL,                              // Task handle
      CONFIG_CAN_SNIFFER_TASK_CORE       // Core affinity
  );
  
  return 0;
}
