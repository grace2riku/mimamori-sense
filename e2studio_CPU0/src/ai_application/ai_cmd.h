/**
 * @file ai_cmd.h
 * @brief NT-Shell "ai" command for AI inference diagnostics (F-003-5)
 * @details
 * Provides the "ai" command handler for NT-Shell, exposing AI model
 * configuration information and diagnostics via subcommands.
 *
 * Subcommands:
 *   ai model  - Display model information (input size, output size, arena sizes)
 *   ai config - Display AI application configuration constants
 */

#ifndef AI_CMD_H
#define AI_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NT-Shell "ai" command handler
 *
 * @param argc Argument count (including command name)
 * @param argv Argument array
 * @return 0 on success
 */
int usrcmd_ai(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* AI_CMD_H */
