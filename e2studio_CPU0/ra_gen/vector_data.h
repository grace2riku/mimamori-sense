/* generated vector header file - do not edit */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H
#ifdef __cplusplus
        extern "C" {
        #endif
/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (12)
#endif
/* ISR prototypes */
void sci_b_uart_rxi_isr(void);
void sci_b_uart_txi_isr(void);
void sci_b_uart_tei_isr(void);
void sci_b_uart_eri_isr(void);
void glcdc_line_detect_isr(void);
void glcdc_underflow_1_isr(void);
void drw_int_isr(void);
void iic_master_rxi_isr(void);
void iic_master_txi_isr(void);
void iic_master_tei_isr(void);
void iic_master_eri_isr(void);
void r_icu_isr(void);

/* Vector table allocations */
#define VECTOR_NUMBER_SCI8_RXI ((IRQn_Type) 0) /* SCI8 RXI (Receive data full) */
#define SCI8_RXI_IRQn          ((IRQn_Type) 0) /* SCI8 RXI (Receive data full) */
#define VECTOR_NUMBER_SCI8_TXI ((IRQn_Type) 1) /* SCI8 TXI (Transmit data empty) */
#define SCI8_TXI_IRQn          ((IRQn_Type) 1) /* SCI8 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI8_TEI ((IRQn_Type) 2) /* SCI8 TEI (Transmit end) */
#define SCI8_TEI_IRQn          ((IRQn_Type) 2) /* SCI8 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI8_ERI ((IRQn_Type) 3) /* SCI8 ERI (Receive error) */
#define SCI8_ERI_IRQn          ((IRQn_Type) 3) /* SCI8 ERI (Receive error) */
#define VECTOR_NUMBER_GLCDC_LINE_DETECT ((IRQn_Type) 4) /* GLCDC LINE DETECT (Specified line) */
#define GLCDC_LINE_DETECT_IRQn          ((IRQn_Type) 4) /* GLCDC LINE DETECT (Specified line) */
#define VECTOR_NUMBER_GLCDC_UNDERFLOW_1 ((IRQn_Type) 5) /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
#define GLCDC_UNDERFLOW_1_IRQn          ((IRQn_Type) 5) /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
#define VECTOR_NUMBER_DRW_INT ((IRQn_Type) 6) /* DRW INT (DRW interrupt) */
#define DRW_INT_IRQn          ((IRQn_Type) 6) /* DRW INT (DRW interrupt) */
#define VECTOR_NUMBER_IIC1_RXI ((IRQn_Type) 7) /* IIC1 RXI (Receive data full) */
#define IIC1_RXI_IRQn          ((IRQn_Type) 7) /* IIC1 RXI (Receive data full) */
#define VECTOR_NUMBER_IIC1_TXI ((IRQn_Type) 8) /* IIC1 TXI (Transmit data empty) */
#define IIC1_TXI_IRQn          ((IRQn_Type) 8) /* IIC1 TXI (Transmit data empty) */
#define VECTOR_NUMBER_IIC1_TEI ((IRQn_Type) 9) /* IIC1 TEI (Transmit end) */
#define IIC1_TEI_IRQn          ((IRQn_Type) 9) /* IIC1 TEI (Transmit end) */
#define VECTOR_NUMBER_IIC1_ERI ((IRQn_Type) 10) /* IIC1 ERI (Transfer error) */
#define IIC1_ERI_IRQn          ((IRQn_Type) 10) /* IIC1 ERI (Transfer error) */
#define VECTOR_NUMBER_ICU_IRQ19 ((IRQn_Type) 11) /* ICU IRQ19 (External pin interrupt 19) */
#define ICU_IRQ19_IRQn          ((IRQn_Type) 11) /* ICU IRQ19 (External pin interrupt 19) */
/* The number of entries required for the ICU vector table. */
#define BSP_ICU_VECTOR_NUM_ENTRIES (12)

#ifdef __cplusplus
        }
        #endif
#endif /* VECTOR_DATA_H */
