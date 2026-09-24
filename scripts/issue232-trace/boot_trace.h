#ifndef ISSUE232_BOOT_TRACE_H
#define ISSUE232_BOOT_TRACE_H
#include "hal_data.h"
#include <stdint.h>

enum boot_trace_point {
    BT_INIT_BEGIN, BT_INIT_END, BT_OPEN_BEGIN, BT_OPEN_END, BT_BRANCH,
    BT_PROVISION_BEGIN, BT_PROVISION_END, BT_GET_BEGIN, BT_GET_END,
    BT_SYNC_BEGIN, BT_SYNC_END, BT_DISPLAY_BEGIN, BT_DISPLAY_END,
    BT_RESET_BEGIN, BT_RESET_END, BT_GLCDC_OPEN_BEGIN, BT_GLCDC_OPEN_END,
    BT_GLCDC_START_BEGIN, BT_GLCDC_START_END, BT_INITIAL_BUFFER_BEGIN,
    BT_INITIAL_BUFFER_END, BT_FLUSH_BEGIN, BT_FLUSH_END, BT_BACKLIGHT,
    BT_WAIT_BEGIN, BT_WAIT_END, BT_COUNT
};
void boot_trace_post_c(void);
void boot_trace_mark(enum boot_trace_point point, int32_t result, uint32_t aux);
void boot_trace_branch(bool provisioned);
void boot_trace_ai_dwt_reset(void);
int usrcmd_boottrace(int argc, char **argv);
#endif
