/**
 * @file fall_detection_cmd.c
 * @brief NT-Shell "fall" command implementation (F-003-9)
 * @details
 * Implements the "fall" command for NT-Shell, providing subcommands
 * to inspect and control the fall detection state machine.
 *
 * Supported subcommands:
 *   fall status    - Display current fall detection state
 *   fall count     - Display consecutive count and total confirmations
 *   fall threshold - Display current threshold parameters
 *   fall set <param> <value> - Set a threshold parameter at runtime
 *   fall reset     - Reset state machine to NORMAL
 *   fall log       - Display state transition log
 *   fall display   - Display screen overlay status (F-003-10)
 *
 * Reference: Issue #26 (F-003-9), Issue #27 (F-003-10) specification
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>

#include "ntlibc.h"
#include "jlink_console.h"
#include "fall_detection_logic.h"
#include "fall_detection_cmd.h"
#include "ui/fall_detection_screen.h"
#include "cmd_utils.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define FALL_CMD_BUF_SIZE   (128)

/** Maximum log entries to display */
#define FALL_CMD_MAX_LOG    (FALL_DETECT_LOG_SIZE)

/**********************************************************************************************************************
 Private function prototypes
 *********************************************************************************************************************/

static void fall_cmd_status(void);
static void fall_cmd_count(void);
static void fall_cmd_threshold(void);
static void fall_cmd_set(int argc, char **argv);
static void fall_cmd_reset(void);
static void fall_cmd_log(void);
static void fall_cmd_usage(void);

static const char *state_to_string(fall_state_t state);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * NT-Shell "fall" command handler.
 *
 * Dispatches to subcommand handlers based on argv[1].
 *
 * @param argc Argument count
 * @param argv Argument strings
 * @return CMD_OK on success
 */
int usrcmd_fall(int argc, char **argv)
{
    if (argc < 2)
    {
        fall_cmd_usage();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "status") == 0)
    {
        fall_cmd_status();
    }
    else if (ntlibc_strcmp(argv[1], "count") == 0)
    {
        fall_cmd_count();
    }
    else if (ntlibc_strcmp(argv[1], "threshold") == 0)
    {
        fall_cmd_threshold();
    }
    else if (ntlibc_strcmp(argv[1], "set") == 0)
    {
        fall_cmd_set(argc, argv);
    }
    else if (ntlibc_strcmp(argv[1], "reset") == 0)
    {
        fall_cmd_reset();
    }
    else if (ntlibc_strcmp(argv[1], "log") == 0)
    {
        fall_cmd_log();
    }
    else if (ntlibc_strcmp(argv[1], "display") == 0)
    {
        fall_detection_screen_cmd_status();
    }
    else
    {
        fall_cmd_usage();
    }

    return CMD_OK;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Display current fall detection state and key metrics.
 */
static void fall_cmd_status(void)
{
    char buf[FALL_CMD_BUF_SIZE];
    fall_detection_stats_t stats;

    fall_detection_get_stats(&stats);

    print_to_console("=== Fall Detection Status ===\r\n");

    snprintf(buf, sizeof(buf), "  State:             %s\r\n", fall_detection_get_state_name());
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total frames:      %lu\r\n", (unsigned long)stats.total_frames);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Candidate frames:  %lu\r\n", (unsigned long)stats.candidate_frames);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Consecutive count: %lu\r\n", (unsigned long)stats.consecutive_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Confirmed total:   %lu\r\n", (unsigned long)stats.confirmed_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Cooldown remain:   %lu frames\r\n", (unsigned long)stats.cooldown_remaining);
    print_to_console(buf);

    /* Use integer formatting to avoid %f on embedded target */
    int ar_int = (int)stats.last_aspect_ratio;
    int ar_frac = (int)((stats.last_aspect_ratio - (float)ar_int) * 100.0f);
    if (ar_frac < 0) ar_frac = -ar_frac;
    snprintf(buf, sizeof(buf), "  Last aspect ratio: %d.%02d\r\n", ar_int, ar_frac);
    print_to_console(buf);

    int sc_int = (int)(stats.last_score * 100.0f);
    snprintf(buf, sizeof(buf), "  Last score:        0.%02d\r\n", sc_int);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Position hint:     %s\r\n", stats.last_position_hint ? "LOWER" : "-");
    print_to_console(buf);
}

/**
 * Display detection counters.
 */
static void fall_cmd_count(void)
{
    char buf[FALL_CMD_BUF_SIZE];
    fall_detection_stats_t stats;

    fall_detection_get_stats(&stats);

    print_to_console("=== Fall Detection Counters ===\r\n");

    snprintf(buf, sizeof(buf), "  Total frames processed: %lu\r\n", (unsigned long)stats.total_frames);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Candidate frames:       %lu\r\n", (unsigned long)stats.candidate_frames);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Consecutive (current):  %lu / %lu\r\n",
             (unsigned long)stats.consecutive_count,
             (unsigned long)fall_detection_get_params()->consecutive_threshold);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Falls confirmed (total):%lu\r\n", (unsigned long)stats.confirmed_count);
    print_to_console(buf);

    if (stats.total_frames > 0)
    {
        uint32_t candidate_pct = (stats.candidate_frames * 100) / stats.total_frames;
        snprintf(buf, sizeof(buf), "  Candidate rate:         %lu%%\r\n", (unsigned long)candidate_pct);
        print_to_console(buf);
    }
}

/**
 * Display current threshold parameters.
 */
static void fall_cmd_threshold(void)
{
    char buf[FALL_CMD_BUF_SIZE];
    const fall_detection_params_t *params = fall_detection_get_params();

    print_to_console("=== Fall Detection Thresholds ===\r\n");

    int ar_int = (int)params->aspect_ratio_threshold;
    int ar_frac = (int)((params->aspect_ratio_threshold - (float)ar_int) * 100.0f);
    if (ar_frac < 0) ar_frac = -ar_frac;
    snprintf(buf, sizeof(buf), "  aspect_ratio:  %d.%02d  (width/height threshold)\r\n", ar_int, ar_frac);
    print_to_console(buf);

    int sc_int = (int)(params->score_threshold * 100.0f);
    snprintf(buf, sizeof(buf), "  score:         0.%02d  (detection confidence)\r\n", sc_int);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  consecutive:   %lu    (frames to confirm)\r\n",
             (unsigned long)params->consecutive_threshold);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  cooldown:      %lu    (frames after confirm)\r\n",
             (unsigned long)params->cooldown_frames);
    print_to_console(buf);

    int pos_int = (int)(params->lower_position_ratio * 100.0f);
    snprintf(buf, sizeof(buf), "  position:      0.%02d  (lower frame ratio)\r\n", pos_int);
    print_to_console(buf);

    print_to_console("\r\nUse 'fall set <param> <value>' to change.\r\n");
    print_to_console("  Params: aspect_ratio, score, consecutive, cooldown\r\n");
}

/**
 * Set a threshold parameter at runtime.
 *
 * Usage: fall set <param_name> <value>
 *
 * Supported params:
 *   aspect_ratio <float>   - Aspect ratio threshold (e.g., 1.3)
 *   score <float>          - Score threshold (e.g., 0.5)
 *   consecutive <int>      - Consecutive frame count
 *   cooldown <int>         - Cooldown frames
 */
static void fall_cmd_set(int argc, char **argv)
{
    char buf[FALL_CMD_BUF_SIZE];

    if (argc < 4)
    {
        print_to_console("Usage: fall set <param> <value>\r\n");
        print_to_console("  Params: aspect_ratio, score, consecutive, cooldown\r\n");
        return;
    }

    const char *param = argv[2];
    const char *value_str = argv[3];

    if (ntlibc_strcmp(param, "aspect_ratio") == 0)
    {
        /* Parse float value: integer part and optional fractional part */
        int int_part = 0;
        int frac_part = 0;
        int frac_digits = 0;
        const char *p = value_str;
        bool has_dot = false;

        /* Parse integer part */
        while (*p >= '0' && *p <= '9')
        {
            int_part = int_part * 10 + (*p - '0');
            p++;
        }
        if (*p == '.')
        {
            has_dot = true;
            p++;
            while (*p >= '0' && *p <= '9' && frac_digits < 4)
            {
                frac_part = frac_part * 10 + (*p - '0');
                frac_digits++;
                p++;
            }
        }

        float value = (float)int_part;
        if (has_dot && frac_digits > 0)
        {
            float divisor = 1.0f;
            for (int i = 0; i < frac_digits; i++) divisor *= 10.0f;
            value += (float)frac_part / divisor;
        }

        if (value > 0.0f)
        {
            fall_detection_set_aspect_ratio(value);
            int vi = (int)value;
            int vf = (int)((value - (float)vi) * 100.0f);
            if (vf < 0) vf = -vf;
            snprintf(buf, sizeof(buf), "  aspect_ratio set to %d.%02d\r\n", vi, vf);
            print_to_console(buf);
        }
        else
        {
            print_to_console("  ERROR: aspect_ratio must be > 0\r\n");
        }
    }
    else if (ntlibc_strcmp(param, "score") == 0)
    {
        /* Parse score as 0.XX format */
        int int_part = 0;
        int frac_part = 0;
        int frac_digits = 0;
        const char *p = value_str;
        bool has_dot = false;

        while (*p >= '0' && *p <= '9')
        {
            int_part = int_part * 10 + (*p - '0');
            p++;
        }
        if (*p == '.')
        {
            has_dot = true;
            p++;
            while (*p >= '0' && *p <= '9' && frac_digits < 4)
            {
                frac_part = frac_part * 10 + (*p - '0');
                frac_digits++;
                p++;
            }
        }

        float value = (float)int_part;
        if (has_dot && frac_digits > 0)
        {
            float divisor = 1.0f;
            for (int i = 0; i < frac_digits; i++) divisor *= 10.0f;
            value += (float)frac_part / divisor;
        }

        if (value >= 0.0f && value <= 1.0f)
        {
            fall_detection_set_score_threshold(value);
            int sc = (int)(value * 100.0f);
            snprintf(buf, sizeof(buf), "  score set to 0.%02d\r\n", sc);
            print_to_console(buf);
        }
        else
        {
            print_to_console("  ERROR: score must be 0.0 to 1.0\r\n");
        }
    }
    else if (ntlibc_strcmp(param, "consecutive") == 0)
    {
        cmd_parse_result_t parsed = cmd_parse_uint32(value_str);
        if (parsed.valid && parsed.value >= 1)
        {
            fall_detection_set_consecutive_count(parsed.value);
            snprintf(buf, sizeof(buf), "  consecutive set to %lu\r\n", (unsigned long)parsed.value);
            print_to_console(buf);
        }
        else
        {
            print_to_console("  ERROR: consecutive must be >= 1\r\n");
        }
    }
    else if (ntlibc_strcmp(param, "cooldown") == 0)
    {
        cmd_parse_result_t parsed = cmd_parse_uint32(value_str);
        if (parsed.valid)
        {
            fall_detection_set_cooldown_frames(parsed.value);
            snprintf(buf, sizeof(buf), "  cooldown set to %lu\r\n", (unsigned long)parsed.value);
            print_to_console(buf);
        }
        else
        {
            print_to_console("  ERROR: invalid value\r\n");
        }
    }
    else
    {
        snprintf(buf, sizeof(buf), "  Unknown param: %s\r\n", param);
        print_to_console(buf);
        print_to_console("  Valid: aspect_ratio, score, consecutive, cooldown\r\n");
    }
}

/**
 * Reset the fall detection state machine.
 */
static void fall_cmd_reset(void)
{
    fall_detection_reset();
    print_to_console("Fall detection state reset to NORMAL.\r\n");
}

/**
 * Display the state transition log.
 */
static void fall_cmd_log(void)
{
    char buf[FALL_CMD_BUF_SIZE];
    fall_detection_log_entry_t entries[FALL_CMD_MAX_LOG];

    uint32_t count = fall_detection_get_log(entries, FALL_CMD_MAX_LOG);

    if (count == 0)
    {
        print_to_console("No state transitions recorded.\r\n");
        return;
    }

    snprintf(buf, sizeof(buf), "=== Fall Detection Log (%lu entries) ===\r\n", (unsigned long)count);
    print_to_console(buf);

    print_to_console("  Frame    From        To          AR    Score  Consec\r\n");
    print_to_console("  -------  ----------  ----------  ----  -----  ------\r\n");

    for (uint32_t i = 0; i < count; i++)
    {
        const fall_detection_log_entry_t *e = &entries[i];

        int ar_int = (int)e->aspect_ratio;
        int ar_frac = (int)((e->aspect_ratio - (float)ar_int) * 100.0f);
        if (ar_frac < 0) ar_frac = -ar_frac;

        int sc_int = (int)(e->score * 100.0f);

        snprintf(buf, sizeof(buf), "  %-7lu  %-10s  %-10s  %d.%02d  0.%02d   %lu\r\n",
                 (unsigned long)e->frame_number,
                 state_to_string(e->from_state),
                 state_to_string(e->to_state),
                 ar_int, ar_frac,
                 sc_int,
                 (unsigned long)e->consecutive);
        print_to_console(buf);
    }
}

/**
 * Display usage information.
 */
static void fall_cmd_usage(void)
{
    print_to_console("Usage: fall <subcommand>\r\n");
    print_to_console("  status    - Show fall detection state and metrics\r\n");
    print_to_console("  count     - Show detection counters\r\n");
    print_to_console("  threshold - Show current threshold parameters\r\n");
    print_to_console("  set <param> <value> - Set threshold parameter\r\n");
    print_to_console("  reset     - Reset state to NORMAL\r\n");
    print_to_console("  log       - Show state transition log\r\n");
    print_to_console("  display   - Show screen overlay status (F-003-10)\r\n");
}

/**
 * Convert fall_state_t to string.
 */
static const char *state_to_string(fall_state_t state)
{
    switch (state)
    {
        case FALL_STATE_NORMAL:     return "NORMAL";
        case FALL_STATE_SUSPECTED:  return "SUSPECTED";
        case FALL_STATE_CONFIRMED:  return "CONFIRMED";
        case FALL_STATE_COOLDOWN:   return "COOLDOWN";
        default:                    return "UNKNOWN";
    }
}
