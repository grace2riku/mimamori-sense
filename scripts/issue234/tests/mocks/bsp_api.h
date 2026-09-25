#include <stddef.h>
#include <stdint.h>
typedef struct { volatile uint8_t RCR1, RCR2, RCR4; } mock_rtc_t;
typedef struct { volatile uint8_t SOSCCR, SOMCR; } mock_system_t;
static mock_rtc_t mock_rtc;
static mock_system_t mock_system;
#define R_RTC (&mock_rtc)
#define R_SYSTEM (&mock_system)
