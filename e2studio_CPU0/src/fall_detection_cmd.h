/**
 * @file fall_detection_cmd.h
 * @brief NT-Shell "fall" command handler declaration (F-003-9)
 * @details
 * Declares the "fall" command handler for registration in usrcmd.c cmdlist[].
 *
 * Reference: Issue #26 (F-003-9) specification
 */

#ifndef FALL_DETECTION_CMD_H
#define FALL_DETECTION_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NT-Shell "fall" command handler.
 *
 * Subcommands: status, count, threshold, set, reset, log
 *
 * @param argc Argument count
 * @param argv Argument strings
 * @return 0 on success
 */
int usrcmd_fall(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* FALL_DETECTION_CMD_H */
