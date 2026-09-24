/* Test-only placement: Unicorn maps the ELF's zeroed data into emulated RAM. */
#define SDRAM_SECTION_NOINIT
#define BSP_CFG_DCACHE_ENABLED 0
