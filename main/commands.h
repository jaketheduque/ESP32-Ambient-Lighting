#ifndef COMMANDS_H
#define COMMANDS_H

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
command_t* create_default_fade_to_command(rgb_t color_value);

/**
 * @brief Creates a default sequential command structure.
 *
 * Allocates and initializes a new command_t structure with default values
 * for a sequential command. The number of steps and delay between steps
 * are set to their default values, and the direction (reverse or not)
 * is set according to the input parameter.
 *
 * @param color The RGB color to be used in the sequential command.
 * @param reverse If true, the sequential command will be set to reverse order.
 * @return Pointer to the newly allocated command_t structure, or NULL if allocation fails.
 *
 * @note The allocated memory must be freed by the caller to avoid memory leaks.
 */
command_t* create_default_sequential_command(rgb_t color, bool reverse);

/**
 * @brief Creates a default set color command
 *
 * Allocates and initializes a new command_t structure with given RGB for color.
 *
 * @param color The RGB color to be used in the sequential command.
 * @return Pointer to the newly allocated command_t structure, or NULL if allocation fails.
 *
 * @note The allocated memory must be freed by the caller to avoid memory leaks.
 */
command_t* create_set_color_command(rgb_t color);

#endif