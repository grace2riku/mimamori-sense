/* generated thread header file - do not edit */
#ifndef NTSHELL_THREAD_H_
#define NTSHELL_THREAD_H_
#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"
#ifdef __cplusplus
                extern "C" void ntshell_thread_entry(void * pvParameters);
                #else
extern void ntshell_thread_entry(void *pvParameters);
#endif
FSP_HEADER
FSP_FOOTER
#endif /* NTSHELL_THREAD_H_ */
