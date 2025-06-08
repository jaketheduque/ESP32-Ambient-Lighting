#include "main_common.h"

/**
 * @brief Creates and initializes a pointer to a command_t structure for a fade-to-color command.
 *
 * This function allocates memory for a command_t pointer and initializes it as a fade-to-color command
 * using the provided color value. The fade step parameters are set to default values defined by
 * DEFAULT_FADE_STEPS and DEFAULT_FADE_DELAY_MS.
 *
 * @param color_value  The color value to fade to (assigned to .data.color).
 * @return Pointer to the allocated and initialized command_t structure, or NULL on allocation failure.
 *
 * @note The allocated memory must be freed by the caller to avoid memory leaks.
 */
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

/**
 * @brief Creates a default sequential command structure.
 *
 * Allocates and initializes a new command_t structure with default values
 * for a sequential command. The number of steps and delay between steps
 * are set to their default values, and the direction (reverse or not)
 * is set according to the input parameter.
 *
 * @param reverse If true, the sequential command will be set to reverse order.
 * @return Pointer to the newly allocated command_t structure, or NULL if allocation fails.
 *
 * @note The allocated memory must be freed by the caller to avoid memory leaks.
 */
command_t* create_default_sequential_command(bool reverse) {
  command_t *cmd = calloc(1, sizeof(command_t));
  if (!cmd) {
    return NULL;
  }
  cmd->type = COMMAND_SEQUENTIAL;
  cmd->data.color = START_COLOR; // Default color for sequential command TODO change later
  cmd->data.step.num_steps = DEFAULT_SEQUENTIAL_STEPS;
  cmd->data.step.delay_ms = DEFAULT_SEQUENTIAL_DELAY_MS;
  cmd->data.step.reverse = reverse;
  return cmd;
}

/**
 * @brief Creates a new command to set the color to the specified RGB value.
 *
 * Allocates and initializes a command_t structure with the type set to COMMAND_SET_COLOR
 * and the color data set to the provided rgb_t value.
 *
 * @param color_value The RGB color value to set in the command.
 * @return Pointer to the newly allocated command_t structure, or NULL if allocation fails.
 *
 * @note The allocated memory must be freed by the caller to avoid memory leaks.
 */
command_t* create_default_set_color_command(rgb_t color_value) {
  command_t *cmd = calloc(1, sizeof(command_t));
  if (!cmd) {
    return NULL;
  }
  cmd->type = COMMAND_SET_COLOR;
  cmd->data.color = color_value;
  return cmd;
}