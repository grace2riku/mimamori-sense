#include "../../../e2studio_CPU0/src/rtc_boot_diag.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int run_tests(void)
{
    rtc_boot_snapshot_t out = {99,99,99,99,99};
    /* Simulate old completed records surviving a warm reset. */
    for (unsigned int i = 0; i < RTC_BOOT_STAGE_COUNT; ++i) {
        s_rtc_boot[i].complete = RTC_BOOT_COMPLETE;
        s_rtc_boot[i].value.rcr2 = 0xAA;
    }
    mock_rtc.RCR1 = 0; mock_rtc.RCR2 = 0x41; mock_rtc.RCR4 = 0;
    mock_system.SOSCCR = 0; mock_system.SOMCR = 1;
    rtc_boot_diag_begin();
    CHECK(rtc_boot_diag_get(RTC_BOOT_RESET, &out));
    CHECK(out.rcr2 == 0x41 && out.somcr == 1);
    for (unsigned int i = 1; i < RTC_BOOT_STAGE_COUNT; ++i) {
        CHECK(!rtc_boot_diag_get((rtc_boot_stage_t)i, &out));
        CHECK(out.rcr2 == 0x41);
    }
    for (unsigned int i = 1; i < RTC_BOOT_STAGE_COUNT; ++i) {
        mock_rtc.RCR2 = (uint8_t)i;
        rtc_boot_diag_capture((rtc_boot_stage_t)i);
        CHECK(rtc_boot_diag_get((rtc_boot_stage_t)i, &out));
        CHECK(out.rcr2 == i && out.somcr == 1);
        CHECK(mock_rtc.RCR2 == i && mock_system.SOMCR == 1);
        mock_rtc.RCR2 = 0xFF;
        rtc_boot_diag_capture((rtc_boot_stage_t)i);
        CHECK(rtc_boot_diag_get((rtc_boot_stage_t)i, &out));
        CHECK(out.rcr2 == i); /* first capture remains immutable */
    }
    rtc_boot_diag_capture((rtc_boot_stage_t)-1);
    rtc_boot_diag_capture(RTC_BOOT_STAGE_COUNT);
    CHECK(!rtc_boot_diag_get((rtc_boot_stage_t)-1, &out));
    CHECK(!rtc_boot_diag_get(RTC_BOOT_STAGE_COUNT, &out));
    CHECK(!rtc_boot_diag_get(RTC_BOOT_RESET, NULL));
    s_rtc_boot[RTC_BOOT_POST_C].complete = 1; /* interrupted/invalid capture */
    CHECK(!rtc_boot_diag_get(RTC_BOOT_POST_C, &out));
    mock_rtc.RCR2 = 0x40;
    rtc_boot_diag_begin();
    CHECK(rtc_boot_diag_get(RTC_BOOT_RESET, &out) && out.rcr2 == 0x40);
    CHECK(!rtc_boot_diag_get(RTC_BOOT_BEFORE_DECISION, &out));
    return 0;
}
