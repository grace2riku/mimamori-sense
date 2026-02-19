#include "new_thread0.h"
#include <stdio.h>

volatile bool g_uart_transfer_finished = false;

void uart_callback(uart_callback_args_t *p_args)
{
    if (UART_EVENT_TX_COMPLETE == p_args->event)
    {
        g_uart_transfer_finished = true;
    }
}

static void __putc(char c)
{
    fsp_err_t err;

    err = R_SCI_B_UART_Write(&g_uart0_ctrl, (uint8_t *)&c, 1);
    assert(FSP_SUCCESS == err);

    while(false == g_uart_transfer_finished)
    {
        ;
    }
    g_uart_transfer_finished = false;
}

static char __getc(void)
{
    return '\0'; //NULL character
}

static int sample_flush(FILE *file)
{
    /* This function doesn't need to do anything */
    (void) file;        /* Not used in this function */
    return 0;
}

static int sample_putc(char c, FILE *file)
{
    (void) file;        /* Not used in this function */
    __putc(c);     /* Defined by underlying system */
    return c;
}

static int sample_getc(FILE *file)
{
    unsigned char c;
    (void) file;        /* Not used in this function */
    c = __getc();  /* Defined by underlying system */
    return c;
}


static FILE __stdio = FDEV_SETUP_STREAM(sample_putc,
                    sample_getc,
                    sample_flush,
                    _FDEV_SETUP_RW);

FILE *const stdin = &__stdio; __strong_reference(stdin, stdout); __strong_reference(stdin, stderr);
