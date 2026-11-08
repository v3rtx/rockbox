/*
 * ILI9341 LCD driver for the Colorfly Pocket HiFi C4 Pro.
 *
 * Init sequence reverse-engineered from the stock BASE.RKW image
 * (load address 0x60000000, init function at VMA 0x600665F4). Panel is
 * configured as 320x240 landscape with MADCTL=0xA8 (MY|MV|BGR), 16-bit
 * pixel format (COLMOD=0x55). See docs/rockbox/lcd_init_sequence.txt.
 *
 * The LCD reset GPIO is not yet identified; we rely on the SoC power-on
 * reset leaving the panel in a clean state and on SLEEP_OUT (0x11) to
 * bring it up.
 */

#include "config.h"
#include "kernel.h"
#include "lcd.h"
#include "system.h"
#include "cpu.h"
#include "lcdif-rk27xx.h"
#include "lcd-target.h"

static bool display_on = false;

static void lcd_write_cmd_data(unsigned int cmd, const unsigned char *data, int n)
{
    lcd_cmd(cmd);
    while (n-- > 0)
        lcd_data(*data++);
}

void lcd_display_init(void)
{
    /* Sleep out — wait 120 ms per ILI9341 datasheet before issuing any
     * command that touches the booster / charge pump.
     */
    lcd_cmd(0x11);
    udelay(120000);

    static const unsigned char d_EF[] = { 0x03, 0x80, 0x02 };
    lcd_write_cmd_data(0xEF, d_EF, sizeof(d_EF));

    static const unsigned char d_CF[] = { 0x00, 0xAA, 0xB0 };
    lcd_write_cmd_data(0xCF, d_CF, sizeof(d_CF));

    static const unsigned char d_ED[] = { 0x67, 0x03, 0x12, 0x81 };
    lcd_write_cmd_data(0xED, d_ED, sizeof(d_ED));

    static const unsigned char d_CB[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
    lcd_write_cmd_data(0xCB, d_CB, sizeof(d_CB));

    static const unsigned char d_EA[] = { 0x00, 0x00 };
    lcd_write_cmd_data(0xEA, d_EA, sizeof(d_EA));

    static const unsigned char d_E8[] = { 0x85, 0x0A, 0x78 };
    lcd_write_cmd_data(0xE8, d_E8, sizeof(d_E8));

    lcd_write_reg(0xC0, 0x18); /* power control 1 */
    lcd_write_reg(0xC1, 0x13); /* power control 2 */

    static const unsigned char d_C5[] = { 0x31, 0x3C };
    lcd_write_cmd_data(0xC5, d_C5, sizeof(d_C5));

    lcd_write_reg(0xC7, 0xA8); /* VCOM control 2 */
    lcd_write_reg(0x3A, 0x55); /* COLMOD: 16bpp DBI */
    lcd_write_reg(0x36, 0xA8); /* MADCTL: MY|MV|BGR — landscape */

    static const unsigned char d_B1[] = { 0x00, 0x18 };
    lcd_write_cmd_data(0xB1, d_B1, sizeof(d_B1));

    static const unsigned char d_B6[] = { 0x0A, 0xA2, 0x27 };
    lcd_write_cmd_data(0xB6, d_B6, sizeof(d_B6));

    lcd_write_reg(0xF2, 0x00); /* 3-gamma disable */
    lcd_write_reg(0xF7, 0x20); /* pump ratio */
    lcd_write_reg(0x26, 0x01); /* gamma curve 1 */

    static const unsigned char d_E0[] = {
        0x1F, 0x25, 0x25, 0x0C, 0x11, 0x0A, 0x4E, 0xCB,
        0x37, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    lcd_write_cmd_data(0xE0, d_E0, sizeof(d_E0));

    static const unsigned char d_E1[] = {
        0x00, 0x1A, 0x1C, 0x02, 0x0E, 0x06, 0x31, 0x36,
        0x48, 0x0C, 0x1F, 0x1F, 0x3F, 0x3F, 0x1F
    };
    lcd_write_cmd_data(0xE1, d_E1, sizeof(d_E1));

    /* partial area (unused but matches OF init order) */
    static const unsigned char d_30[] = { 0x00, 0x00, 0x01, 0x3F };
    lcd_write_cmd_data(0x30, d_30, sizeof(d_30));

    lcd_cmd(0x13); /* normal display mode on */

    /* CASET 0..319, PASET 0..239 */
    static const unsigned char d_2A[] = { 0x00, 0x00, 0x01, 0x3F };
    lcd_write_cmd_data(0x2A, d_2A, sizeof(d_2A));
    static const unsigned char d_2B[] = { 0x00, 0x00, 0x00, 0xEF };
    lcd_write_cmd_data(0x2B, d_2B, sizeof(d_2B));

    lcd_cmd(0x29); /* DISPON */
    lcd_cmd(0x2C); /* RAMWR — leave bus in data-write state for buffered DMA */

    display_on = true;
}

void lcd_enable(bool on)
{
    if (on == display_on)
        return;

    lcdctrl_bypass(1);
    LCDC_CTRL |= RGB24B;

    if (on)
    {
        lcd_cmd(0x11); /* sleep out */
        udelay(120000);
        lcd_cmd(0x29); /* display on */
        lcd_cmd(0x2C); /* RAMWR */
    }
    else
    {
        lcd_cmd(0x28); /* display off */
        lcd_cmd(0x10); /* sleep in */
        udelay(120000);
    }

    display_on = on;
    LCDC_CTRL &= ~RGB24B;
}

void lcd_set_gram_area(int x_start, int y_start, int x_end, int y_end)
{
    lcdctrl_bypass(1);
    LCDC_CTRL |= RGB24B;

    lcd_cmd(0x2A); /* CASET */
    lcd_data(x_start >> 8);
    lcd_data(x_start & 0xFF);
    lcd_data(x_end   >> 8);
    lcd_data(x_end   & 0xFF);

    lcd_cmd(0x2B); /* PASET */
    lcd_data(y_start >> 8);
    lcd_data(y_start & 0xFF);
    lcd_data(y_end   >> 8);
    lcd_data(y_end   & 0xFF);

    lcd_cmd(0x2C); /* RAMWR */
    LCDC_CTRL &= ~RGB24B;
}

bool lcd_active(void)
{
    return display_on;
}
