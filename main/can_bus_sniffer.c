#include <string.h>

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main_common.h"
#include "commands.c"

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

    ESP_LOGV(TAG, "Received CAN message with identifier: 0x%" PRIx32, message.identifier);

    /* Process CAN bus messages */
    if (message.identifier == LIGHTS_CAN_ID) {
      if (memcmp(message.data, previous_light_data, message.data_length_code) != 0) {
        ESP_LOGD(TAG, "New lights CAN message received");

        /**
         * There are two different cases for turning on the ambient lights:
         * 1) Display is off, meaning that this lights should wait until display goes into the standard UI (refer to display CAN section)
         * 2) Display is on, meaning that this is an ambient light only event, leading to a fade animation
         */
        if (message.data[AMBIENT_LIGHT_BYTE_INDEX] && (previous_light_data[AMBIENT_LIGHT_BYTE_INDEX] == 0)) {
          if (previous_display_data[DISPLAY_STATUS_BYTE_INDEX] & DISPLAY_STANDARD_UI_MASK) {
            ESP_LOGI(TAG, "Ambient lighting has turned on");

            xSemaphoreTake(current_color_lock, portMAX_DELAY);
            command_t* door_command = create_default_fade_to_command(current_color);
            command_t* dashboard_command = create_default_fade_to_command(current_color);
            xSemaphoreGive(current_color_lock);

            xQueueSend(lights[DASHBOARD_INDEX].command_queue, &dashboard_command, portMAX_DELAY);
            xQueueSend(lights[DOOR_INDEX].command_queue, &door_command, portMAX_DELAY);
          }
        }

        /* If ambient lighting has turned off, turn off lights */
        if ((message.data[AMBIENT_LIGHT_BYTE_INDEX] == 0) && previous_light_data[AMBIENT_LIGHT_BYTE_INDEX]) {
          ESP_LOGI(TAG, "Ambient lighting has turned off");

          command_t* door_command = create_default_fade_to_command(COLOR_OFF);
          command_t* dashboard_command = create_default_fade_to_command(COLOR_OFF);

          /* Send commands to the queues */
          xQueueSend(lights[DASHBOARD_INDEX].command_queue, &dashboard_command, portMAX_DELAY);
          xQueueSend(lights[DOOR_INDEX].command_queue, &door_command, portMAX_DELAY);
        }

        char data_str[3 * TWAI_FRAME_MAX_DLC] = {0};
        int offset = 0;
        for (int i = 0; i < message.data_length_code; ++i) {
            offset += snprintf(data_str + offset, sizeof(data_str) - offset, "%02X ", message.data[i]);
        }
        ESP_LOGD(TAG, "Lights CAN message data: %s", data_str);

        memcpy(previous_light_data, message.data, message.data_length_code);
      }
    }

    if (message.identifier == DISPLAY_CAN_ID) {
      if (memcmp(message.data, previous_display_data, message.data_length_code) != 0) {
        ESP_LOGD(TAG, "New display CAN message received");

        if ((message.data[DISPLAY_STATUS_BYTE_INDEX] & DISPLAY_STANDARD_UI_MASK) && !(previous_display_data[DISPLAY_STATUS_BYTE_INDEX] & DISPLAY_STANDARD_UI_MASK)) {
          ESP_LOGI(TAG, "Display swapped to normal UI");

          /* If lights are already on, then skip */
          if (lights[0].state == LIGHT_OFF) {
            command_t* door_command = create_default_sequential_command(current_color, false);
            command_t* dashboard_command = create_default_sequential_command(current_color, false);

            dashboard_command->chained_command_queue = lights[DOOR_INDEX].command_queue;
            dashboard_command->chained_command = door_command;

            xQueueSend(lights[DASHBOARD_INDEX].command_queue, &dashboard_command, portMAX_DELAY);
          }
        }

        char data_str[3 * TWAI_FRAME_MAX_DLC] = {0};
        int offset = 0;
        for (int i = 0; i < message.data_length_code; ++i) {
            offset += snprintf(data_str + offset, sizeof(data_str) - offset, "%02X ", message.data[i]);
        }
        ESP_LOGD(TAG, "Display CAN message data: %s", data_str);

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
