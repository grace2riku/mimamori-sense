#include "new_thread0.h"
#include "ospi_flash.h"

#define READ_SFDP_COMMAND          (0x5AU)
#define WRITE_ENABLE_COMMAND       (0x06U)
#define READ_STATUS_COMMAND        (0x05U)
#define WRITE_ENABLE_MASK      (0x02U)
#define READ_SFDP_DUMMY_CYCLES (8U)
#define SFDP_SIGNATURE         (0x50444653U)

#define MX25LW51245G_DEFAULT_DUMMY_CYCLES (20U)
#define MX25LW51245G_MIN_DUMMY_CYCLES     (6U)

static void write_en(void);
static void transition_to_dopi(void);
void oclk_change(bsp_clocks_octaclk_div_t divider);

static const uint32_t g_autocalibration_data [4]BSP_PLACE_IN_SECTION(".ospi0_cs1") =
{
		0xFFFF0000U,
		0x000800FFU,
		0x00FFF700U,
		0xF700F708U
};


static spi_flash_cfg_t           g_ram_ospi_cfg;
static ospi_b_extended_cfg_t     g_ram_ospi_extended_cfg;


static void write_en(void)
{
    fsp_err_t err;

    spi_flash_direct_transfer_t tfr =
    {
        .command = WRITE_ENABLE_COMMAND,
        .command_length =  1U,
        .address_length = 0U,
        .data_length = 0U,
        .dummy_cycles = 0U
    };

    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    assert(FSP_SUCCESS == err);

    tfr = (spi_flash_direct_transfer_t) {
        .command = READ_STATUS_COMMAND,
        .command_length =  1U,
        .address_length =  0U,    // Address is always sent for any kind of read in DOPI
        .data_length = 1U,
        .dummy_cycles = 0
    };

    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    assert(FSP_SUCCESS == err);

    assert((tfr.data & WRITE_ENABLE_MASK) == WRITE_ENABLE_MASK);

}

void oclk_change(bsp_clocks_octaclk_div_t divider)
{
    /* Now update the octaclk divider. */
    bsp_octaclk_settings_t octaclk_settings;
    octaclk_settings.source_clock = BSP_CFG_OCTACLK_SOURCE;
    octaclk_settings.divider      = divider;
    R_BSP_OctaclkUpdate(&octaclk_settings);
}

void ospi_reset(spi_flash_ctrl_t * const p_ctrl)
{

    ospi_b_instance_ctrl_t * p_instance_ctrl = (ospi_b_instance_ctrl_t *) p_ctrl;

    p_instance_ctrl->p_reg->LIOCTL_b.RSTCS0 = 1;
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
    p_instance_ctrl->p_reg->LIOCTL_b.RSTCS0 = 0;
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
    p_instance_ctrl->p_reg->LIOCTL_b.RSTCS0 = 1;
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
}

static void transition_to_dopi(void)
{
    fsp_err_t err = FSP_SUCCESS;
    //uint8_t dopi_dummy_cycles = 10;
    uint8_t spi_dummy_cycles  = 0;
    uint8_t cr2_300;
    uint8_t cr2_000;

#define READ_CR2_COMMAND  0x71
#define WRITE_CR2_COMMAND 0x72

#define CR2_REGISTER_ADDRESS_300 0x300
#define CR2_REGISTER_ADDRESS_000 0x000

    spi_flash_direct_transfer_t tfr =
    {
        .command = READ_CR2_COMMAND,
        .command_length = 1U,
        .address_length = 4U,
        .data_length = 1U,
        .dummy_cycles = spi_dummy_cycles
    };

    /*Read CR2 0x300 for dummy cycles*/
    tfr.address = CR2_REGISTER_ADDRESS_300;
    tfr.data = 0x0;
    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    assert(FSP_SUCCESS == err);
    cr2_300 = (0x000000FF & tfr.data);

    /*Read CR2 0x000 for SPI mode*/
    tfr.address = CR2_REGISTER_ADDRESS_000;
    tfr.data = 0x0;
    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    assert(FSP_SUCCESS == err);
    cr2_000 = (0x000000FF & tfr.data);

    write_en();

    ospi_b_extended_cfg_t * p_ospi_flash_extended_cfg;
    ospi_b_xspi_command_set_t * p_table;

    p_ospi_flash_extended_cfg = (ospi_b_extended_cfg_t *)g_ospi_flash_cfg.p_extend;
    p_table = (ospi_b_xspi_command_set_t *)p_ospi_flash_extended_cfg->p_xspi_command_set->p_table;
    uint8_t table_length = p_ospi_flash_extended_cfg->p_xspi_command_set->length;
    uint8_t i;
    uint8_t hs_read_dummy_cycles = 0xff;

    for (i = 0 ; i < table_length; i++)
    {
        if (SPI_FLASH_PROTOCOL_8D_8D_8D == p_table[i].protocol)
        {
            hs_read_dummy_cycles = p_table[i].read_dummy_cycles;
        }
    }

    if (0xff == hs_read_dummy_cycles)
    {
        assert(0); //No 8D-8D-8D protcol table found
    }

    if ( (MX25LW51245G_DEFAULT_DUMMY_CYCLES < hs_read_dummy_cycles) || (MX25LW51245G_MIN_DUMMY_CYCLES > hs_read_dummy_cycles))
    {
        assert(0); //invalid dummy cycle setting
    }

    /* Set the new dummy cycles in cr2_300 */
    tfr.address = CR2_REGISTER_ADDRESS_300;
    tfr.command = WRITE_CR2_COMMAND;
    cr2_300 = (cr2_300 & ~0x7) | ((MX25LW51245G_DEFAULT_DUMMY_CYCLES - hs_read_dummy_cycles)/2);
    tfr.data = cr2_300;

    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    assert(FSP_SUCCESS == err);

    /*Read CR2 0x300 for dummy cycles*/
    cr2_300 = 0;
    tfr.address = CR2_REGISTER_ADDRESS_300;
    tfr.command = READ_CR2_COMMAND;
    tfr.data = 0x0;
    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    assert(FSP_SUCCESS == err);
    cr2_300 = (0x000000FF & tfr.data);

    write_en();

    /* Change to DOPI */
    cr2_000 = cr2_000 | 0x2;
    tfr.address = CR2_REGISTER_ADDRESS_000;
    tfr.command = WRITE_CR2_COMMAND;
    tfr.data = cr2_000;

    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    assert(FSP_SUCCESS == err);

    /* Change the protocol mode of the driver. */
    /* DS will auto-calibrate in this call. */
    err = R_OSPI_B_SpiProtocolSet(&g_ospi_flash_ctrl, SPI_FLASH_PROTOCOL_8D_8D_8D);
    assert(FSP_SUCCESS == err);

    /* Change clock speed. */
    oclk_change(BSP_CLOCKS_OCTACLK_DIV_1);

    /* auto-calibrate after changing the speed of OM_SCLK */
    err = R_OSPI_B_AutoCalibrate(&g_ospi_flash_ctrl);
    assert(FSP_SUCCESS == err);
}

void init_opsi_flash(void)
{
    fsp_err_t err = FSP_SUCCESS;

    memcpy(&g_ram_ospi_cfg, &g_ospi_flash_cfg, sizeof(spi_flash_cfg_t));
    memcpy(&g_ram_ospi_extended_cfg, g_ospi_flash_cfg.p_extend, sizeof(ospi_b_extended_cfg_t));

    /* Set the erase commands for High Speed mode for 8D-8D-8D */
    g_ram_ospi_cfg.p_extend = &g_ram_ospi_extended_cfg;


    g_ram_ospi_extended_cfg.p_autocalibration_preamble_pattern_addr = (uint8_t*)&g_autocalibration_data[0];

    err = R_OSPI_B_Open(&g_ospi_flash_ctrl, &g_ram_ospi_cfg);
    assert(FSP_SUCCESS == err);

    ospi_reset(&g_ospi_flash_ctrl);

    spi_flash_direct_transfer_t test_tfr =
    {
         .command = READ_SFDP_COMMAND,
         .command_length = 1,
         .address = 0,
         .address_length = 3,
         .data = 0,
         .data_length = 4,
         .dummy_cycles = READ_SFDP_DUMMY_CYCLES
    };

    err = R_OSPI_B_DirectTransfer(&g_ospi_flash_ctrl, &test_tfr, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    assert(FSP_SUCCESS == err);

    assert(SFDP_SIGNATURE == test_tfr.data);

    transition_to_dopi();
}
