/*
 * This config file is for the Colorfly Pocket HiFi C4 Pro
 *
 * SoC:        Rockchip RK27xx (ARM7EJ-S @ 200 MHz, 16 MiB DRAM @ 0x60000000)
 * LCD:        ILI9341 320x240 landscape (16-bit parallel MCU bus via LCDIF)
 * NAND:       2x Micron 9WA18 BGA (32 GiB total, accessed via rk27xx NAND/FTL)
 * DAC:        Cirrus CS4398 + CS8422 SRC
 * Buttons:    placeholder mapping until on-device probing identifies wiring
 */

/* For Rolo and boot loader */
#define MODEL_NUMBER 83

#define MODEL_NAME   "Colorfly Pocket HiFi C4 Pro"

/* Temporary bring-up mode: stop in the bootloader with a high-contrast
 * LCD/backlight/ADC/GPIO probe instead of booting Rockbox proper.
 */
/* #define C4PRO_BOOTLOADER_PROBE */

/* Temporary bring-up mode: stop in the bootloader and probe the RK27xx raw
 * NAND controller before Rockbox proper runs. Keep this separate from the
 * recovery path and app-side storage diagnostics.
 */
/* #define C4PRO_BOOTLOADER_NAND_PROBE */


/* define the bitmask of hardware sample rates */
#define HW_SAMPR_CAPS   (SAMPR_CAP_96 | SAMPR_CAP_48 | SAMPR_CAP_44 | \
                         SAMPR_CAP_32 | SAMPR_CAP_24 | SAMPR_CAP_22 | \
                         SAMPR_CAP_16 | SAMPR_CAP_12 | SAMPR_CAP_11 | SAMPR_CAP_8)

#define HAVE_CS4398
#define CODEC_SLAVE

/* define this if you have a colour LCD */
#define HAVE_LCD_COLOR

/* define this if you want album art for this target */
#define HAVE_ALBUMART

/* define this to enable bitmap scaling */
#define HAVE_BMP_SCALING

/* define this to enable JPEG decoding */
#define HAVE_JPEG

/* define this if you have access to the quickscreen */
#define HAVE_QUICKSCREEN

/* define this if you would like tagcache to build on this target */
#define HAVE_TAGCACHE

/* define this if you have a flash memory storage */
#define HAVE_FLASH_STORAGE

#define CONFIG_STORAGE STORAGE_NAND

#define CONFIG_NAND NAND_RK27XX
#define HAVE_SW_TONE_CONTROLS

#define HAVE_SW_VOLUME_CONTROL

#define NUM_DRIVES 1
#define SECTOR_SIZE 512

/* for small(ish) SD cards */
#define HAVE_FAT16SUPPORT

/* LCD dimensions: 320x240 ILI9341 in landscape (MADCTL=0xA8). 49x37 mm
 * active area => ~165 DPI. sqrt(320^2 + 240^2)/(sqrt(49^2 + 37^2)/25.4) ~ 207 -- use 165
 */
#define LCD_WIDTH  320
#define LCD_HEIGHT 240
#define LCD_DPI    165
#define LCD_DEPTH  16
#define LCD_PIXELFORMAT RGB565

/* Define this if your LCD can be enabled/disabled */
#define HAVE_LCD_ENABLE

#define CONFIG_KEYPAD C4PRO_PAD

/* Define this to enable morse code input */
#define HAVE_MORSE_INPUT

#define CONFIG_LCD LCD_ILI9341_C4

/* Define this for LCD backlight available */
#define HAVE_BACKLIGHT
#define HAVE_BACKLIGHT_BRIGHTNESS
#define MIN_BRIGHTNESS_SETTING      0
#define MAX_BRIGHTNESS_SETTING      31
#define DEFAULT_BRIGHTNESS_SETTING   20
#define CONFIG_BACKLIGHT_FADING BACKLIGHT_FADING_SW_HW_REG

/* Define this if you have a software controlled poweroff */
#define HAVE_SW_POWEROFF

/* The number of bytes reserved for loadable codecs */
#define CODEC_SIZE 0x100000

/* The number of bytes reserved for loadable plugins */
#define PLUGIN_BUFFER_SIZE 0x80000

/* Battery: SRP903852 ~ 1800 mAh nominal (placeholder calibration) */
#define BATTERY_CAPACITY_DEFAULT 1800
#define BATTERY_CAPACITY_MIN     1500
#define BATTERY_CAPACITY_MAX     2000
#define BATTERY_CAPACITY_INC       50


#define CONFIG_BATTERY_MEASURE VOLTAGE_MEASURE

/* Hardware controlled charging with monitoring */
#define CONFIG_CHARGING CHARGING_MONITOR

/* define this if the unit can be powered or charged via USB */
#define HAVE_USB_POWER

/* USB On-the-go */
#define CONFIG_USBOTG USBOTG_RK27XX

/* enable these for the experimental usb stack */
#define HAVE_USBSTACK

#define USB_VENDOR_ID 0x071b
#define USB_PRODUCT_ID 0x3203
#define HAVE_BOOTLOADER_USB_MODE

/* Use the standard rk27xx USB connect path (SOFT_POR -> PHY16BIT, controller
 * reset pulse, standard CONN handler), exactly as the HM-801 does. This is
 * REQUIRED for high-speed enumeration on the C4 - the earlier C4-specific
 * customisations (no SOFT_POR, force-clear PHY16BIT, skip RSTCFG pulse) broke
 * the HS chirp and the host rejected the link (full-speed only, error -71).
 * Verified high-speed + stable /dev/sda 2026-06-29. */
#define C4PRO_USB_STOCK_PATH

/* The exact type of CPU */
#define CONFIG_CPU RK27XX

/* I2C interface */
#define CONFIG_I2C I2C_RK27XX

/* Define this to the CPU frequency */
#define CPU_FREQ        200000000

/* CPU retuning rewrites SDRAM timing and is unsafe on the C4 RAM-run path. */
/* #define HAVE_ADJUSTABLE_CPU_FREQ */

/* Virtual LED (icon) */
#define CONFIG_LED LED_VIRTUAL

#define RKW_FORMAT
#define BOOTFILE_EXT "rkw"
#define BOOTFILE "rockbox." BOOTFILE_EXT
#define BOOTDIR "/.rockbox"
