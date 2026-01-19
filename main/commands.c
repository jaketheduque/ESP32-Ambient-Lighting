#include "commands.h"

command_t* create_default_fade_to_command(rgb_t color_value) {
  command_t *cmd = calloc(1, sizeof(command_t));
  if (!cmd) {
    return NULL;
  }
  cmd->type = COMMAND_FADE_TO;
  cmd->data.color = color_value;
  cmd->data.step.num_steps = DEFAULT_FADE_STEPS;
  cmd->data.step.delay_ms = DEFAULT_FADE_DELAY_MS;
  return cmd;
}

command_t* create_default_sequential_command(rgb_t color, bool reverse) {
  command_t *cmd = calloc(1, sizeof(command_t));
  if (!cmd) {
    return NULL;
  }
  cmd->type = COMMAND_SEQUENTIAL;
  cmd->data.color = color;
  cmd->data.step.num_steps = DEFAULT_SEQUENTIAL_STEPS;
  cmd->data.step.delay_ms = DEFAULT_SEQUENTIAL_DELAY_MS;
  cmd->data.step.reverse = reverse;
  return cmd;
}

command_t* create_set_color_command(rgb_t color) {
  command_t *cmd = calloc(1, sizeof(command_t));
  if (!cmd) {
    return NULL;
  }
  cmd->type = COMMAND_SET_COLOR;
  cmd->data.color = color;
  return cmd;
}