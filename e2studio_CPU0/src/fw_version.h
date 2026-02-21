/**
 * @file fw_version.h
 * @brief Mimamori-Sense firmware version definitions
 * @details
 * Firmware version macros shared across the project.
 * Used by the startup banner (ntshell_thread_entry.c) and
 * the info/version commands (usrcmd.c).
 */

#ifndef FW_VERSION_H
#define FW_VERSION_H

/** Firmware version - major */
#define FW_VERSION_MAJOR    (0)

/** Firmware version - minor */
#define FW_VERSION_MINOR    (1)

/** Firmware version - patch */
#define FW_VERSION_PATCH    (0)

/** Project name */
#define FW_PROJECT_NAME     "Mimamori-Sense"

/** Target board name */
#define FW_BOARD_NAME       "EK-RA8P1"

#endif /* FW_VERSION_H */
