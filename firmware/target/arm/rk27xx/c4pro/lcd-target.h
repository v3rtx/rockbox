#ifndef LCD_TARGET_H
#define LCD_TARGET_H

/* Colorfly C4 Pro wires the ILI9341 in 16-bit parallel (MCU) mode.
 * Stock loader writes LCDC_CTRL=0x0EC1, MCU_CTRL=0x3F01 — same MCU mode
 * the HM801 driver uses with LCDIF_16BIT.
 */
#define LCD_DATABUS_WIDTH LCDIF_16BIT

#endif
