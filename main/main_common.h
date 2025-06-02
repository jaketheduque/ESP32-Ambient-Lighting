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

#define DEFAULT_SEQUENTIAL_STEPS 2
#define DEFAULT_SEQUENTIAL_DELAY_MS 20

#define DISPLAY_CAN_ID 0x3B3
#define DISPLAY_STATUS_BYTE_INDEX 2
#define DISPLAY_POWER_MASK 0x05

#define LIGHTS_CAN_ID 0x3F5
#define TS_BYTE_INDEX 0
#define LEFT_TS_MASK 0x02
#define RIGHT_TS_MASK 0x08
#define AMBIENT_LIGHT_BYTE_INDEX 1

#define TWAI_ACCEPTANCE_MASK 0x08DFFFFF
#define TWAI_ACCEPTANCE_CODE 0x7EA00000

#define LIGHT_CONTROLLER_TASK_PRIORITY 5
#define WEB_SERVER_TASK_PRIORITY 10

#define RISING_CHANGE(current, previous, bit_mask) (((current) & (bit_mask)) && !((previous) & (bit_mask)))
#define FALLING_CHANGE(current, previous, bit_mask) (!((current) & (bit_mask)) && ((previous) & (bit_mask)))

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
} rgb_t;

typedef struct {
  uint8_t num_steps;
  uint32_t delay_ms;
  bool reverse;
} sequential_step_t;

typedef struct command_t {
  CommandType type;
  union {
    rgb_t color;
    sequential_step_t step;
  } data;
  QueueHandle_t chained_command_queue;
  struct command_t* chained_command;
} command_t;

typedef struct {
  led_strip_config_t strip_config;
  led_strip_rmt_config_t rmt_config;
  QueueHandle_t command_queue;
  rgb_t current_color;
} ambient_light_t;

/* Shared Members */
extern ambient_light_t lights[NUM_LIGHTS];

/* Functions */
esp_err_t start_can_sniffer_task();
esp_err_t start_http_server_task();
esp_err_t init_ambient_light(ambient_light_t *light, const int gpio_num, const int max_leds);

#endif // MAIN_COMMON_H