#include "ntshell_thread.h"
#include "jlink_console.h"

/* NT-Shell Thread entry function */
/* pvParameters contains TaskHandle_t */
void ntshell_thread_entry(void *pvParameters) {
	FSP_PARAMETER_NOT_USED(pvParameters);

    /* Initialise user level serial console support using SEGGER serial driver DEBUG1 */
    jlink_console_init ();

    print_to_console("Hello ntshell_thread_entry!\r\n");

	/* TODO: add your own code here */
	while (1) {
		vTaskDelay(1);
	}
}
