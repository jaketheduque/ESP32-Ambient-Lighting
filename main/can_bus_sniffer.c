#include <string.h>

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main_common.h"

/* TWAI configuration */
static twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NORMAL);
static twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
static twai_filter_config_t f_config;

static const char *TAG = "CAN Sniffer";

/* CAN sniffer FreeRTOS task function */
void can_sniffer_task(void *pvParameters) {
  uint8_t data[8];

  while (1) {
    /* Wait for the message to be received */
    twai_message_t message;
    esp_err_t receive_status = twai_receive(&message, pdMS_TO_TICKS(1000));
    if (receive_status == ESP_ERR_TIMEOUT) {
      ESP_LOGI(TAG, "Timed out waiting for message");
      continue;
    } else if (receive_status != ESP_OK) {
      ESP_LOGE(TAG, "Error receiving message");
      vTaskDelete(NULL);
    }

    /* Process CAN bus messages */
    if (message.identifier == LIGHTS_CAN_ID || message.identifier == DISPLAY_CAN_ID) {
      ESP_LOGI(TAG, "ID is 0x%03X", (unsigned int) message.identifier);
      // Process received message
      if (message.extd) {
        ESP_LOGI(TAG, "Message is in Extended Format");
      } else {
        ESP_LOGI(TAG, "Message is in Standard Format");
      }
      if (!(message.rtr)) {
        /* Copy new message data */
        memcpy(data, message.data, message.data_length_code);

        /* Format data into single string */
        char data_string[128] = {0};
        for (int i = 0 ; i < message.data_length_code ; i++) {
          char buffer[16];
          sprintf(buffer, "%02X ", data[i]);
          strcat(data_string, buffer);
        }

        ESP_LOGI(TAG, "Data: %s", data_string);
      }
    }
  }

  /* Delete the task if it exits the loop */
  ESP_LOGI(TAG, "Exiting CAN sniffer task");
  vTaskDelete(NULL);
  return;
}

esp_err_t start_can_sniffer_task(int argc, char **argv) {
  /* Configure TWAI acceptance mask */
  f_config.acceptance_mask = TWAI_ACCEPTANCE_MASK;
  f_config.acceptance_code = TWAI_ACCEPTANCE_CODE;
  f_config.single_filter = true;

  /* Install TWAI driver */
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    ESP_LOGI(TAG, "Driver installed");
  } else {
    ESP_LOGE(TAG, "Failed to install driver");
    return -1;
  }

  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    ESP_LOGI(TAG, "Driver started");
  } else {
    ESP_LOGE(TAG, "Failed to start driver");
    return -1;
  }

  /* Create FreeRTOS task for CAN sniffer loop */
  xTaskCreatePinnedToCore(
      can_sniffer_task,    // Task function
      "can_sniffer_task",  // Task name
      4096,                // Stack size
      NULL,                // Task parameters
      5,                   // Priority
      NULL,                // Task handle
      tskNO_AFFINITY       // Core affinity
  );
  
  return 0;
}
