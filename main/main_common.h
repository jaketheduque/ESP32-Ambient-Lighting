#ifndef MAIN_COMMON_H
#define MAIN_COMMON_H

#include "freertos/queue.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <led_strip.h>

/* Macros */
#define NUM_LIGHTS 3

#define DASHBOARD_INDEX 0
#define CENTER_INDEX 1
#define DOOR_INDEX 2

#define DISPLAY_CAN_ID 0x3B3
#define LIGHTS_CAN_ID 0x3F5

#define TWAI_ACCEPTANCE_MASK 0x08DFFFFF
#define TWAI_ACCEPTANCE_CODE 0x7EA00000

#define LIGHT_CONTROLLER_TASK_PRIORITY 5
#define WEB_SERVER_TASK_PRIORITY 10

/* Enums */
typedef enum {
  COMMAND_TURN_OFF,
  COMMAND_TURN_ON, /* Turns on the lights with the current color set in ambient_light_t, can also be used to "refresh" after color change */
  COMMAND_SET_COLOR, /* Only sets the current color of the ambient_light_t, another command is required to turn on the lights */
  COMMAND_SEQUENTIAL
} CommandType;

/* Structs */
typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} RGB;

typedef struct {
  uint8_t num_steps;
  uint32_t delay_ms;
} SequentialStep;

typedef struct {
  CommandType type;
  union {
    RGB color;
    SequentialStep step;
  } data;
} Command;

typedef struct {
  led_strip_config_t strip_config;
  led_strip_rmt_config_t rmt_config;
  QueueHandle_t command_queue;
  RGB current_color;
} ambient_light_t;

/* Shared Members */
extern ambient_light_t lights[NUM_LIGHTS];

/* Functions */
esp_err_t start_can_sniffer_task();
esp_err_t start_http_server_task();
esp_err_t init_ambient_light(ambient_light_t *light, const int gpio_num, const int max_leds);

#endif // MAIN_COMMON_H