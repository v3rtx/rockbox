/*
 * This config file is for the Colorfly Pocket HiFi C4 Pro
 *
 * SoC:        Rockchip RK27xx (ARM7EJ-S @ 200 MHz, 16 MiB DRAM @ 0x60000000)
 * LCD:        ILI9341 320x240 landscape (16-bit parallel MCU bus via LCDIF)
 * NAND:       2x Micron 9WA18 BGA (32 GiB total, accessed via rk27xx NAND/FTL)
 * DAC:        Cirrus CS4398 + CS8422 SRC
 */

/* For Rolo and boot loader */
#define MODEL_NUMBER 83

#define MODEL_NAME   "Colorfly Pocket HiFi C4 Pro"

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

/* NAND (built-in, drive 0) + microSD (drive 1). */
#define CONFIG_STORAGE (STORAGE_NAND | STORAGE_SD)

#define CONFIG_NAND NAND_RK27XX
#define HAVE_SW_TONE_CONTROLS

#define HAVE_SW_VOLUME_CONTROL

#define NUM_DRIVES 2
#define HAVE_MULTIDRIVE
#define HAVE_MULTIVOLUME
#define SECTOR_SIZE 512

/* for small(ish) SD cards */
#define HAVE_FAT16SUPPORT

/* LCD dimensions: 320x240 ILI9341 in landscape (MADCTL=0xA8). */
#define LCD_WIDTH  320
#define LCD_HEIGHT 240
#define LCD_DPI    165
#define LCD_DEPTH  16
#define LCD_PIXELFORMAT RGB565

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

/* Battery: SRP903852 ~ 1800 mAh nominal */
#define BATTERY_CAPACITY_DEFAULT 1800
#define BATTERY_CAPACITY_MIN     1500
#define BATTERY_CAPACITY_MAX     2000
#define BATTERY_CAPACITY_INC       50

#define CONFIG_BATTERY_MEASURE VOLTAGE_MEASURE
#define NO_LOW_BATTERY_SHUTDOWN

#define HAVE_STORAGE_FLUSH
#define C4PRO_FTL_MOUNT_CHECKPOINT

/* Hardware controlled charging with monitoring */
#define CONFIG_CHARGING CHARGING_MONITOR

/* define this if the unit can be powered or charged via USB */
#define HAVE_USB_POWER

/* USB On-the-go */
#define CONFIG_USBOTG USBOTG_RK27XX

/* enable these for the experimental usb stack */
#define HAVE_USBSTACK

#define C4PRO_PHASE_MARK

#ifndef C4PRO_USB_NO_IDLE_UDC_GUARD
#define C4PRO_USB_IDLE_UDC_GUARD
#endif

#ifndef C4PRO_USB_NO_RESET_DMA_ABORT
#define C4PRO_USB_RESET_DMA_ABORT
#endif

#ifndef C4PRO_USB_NO_RX_DMA_GUARD
#define C4PRO_USB_RX_DMA_GUARD
#endif

#ifndef C4PRO_USB_NO_SERIALIZE_WRITE
#define C4PRO_USB_SERIALIZE_WRITE
#endif

#define C4PRO_NAND_HWECC
#define C4PRO_NAND_2PLANE
#define C4PRO_NAND_FAST_FORMAT
#define C4PRO_NAND_BCH_NO_RMW
#define C4PRO_NAND_FMCTL_NO_RMW
#define C4PRO_NAND_IOMUX_DIE0_SKIP
#define C4PRO_USB_TX_WEDGE_GUARD
#define C4PRO_USB_DEFER_WRITE
#define C4PRO_USB_RX_STALL_KICK
#define C4PRO_USB_IRQ_DRAIN

/* Run RK27xx I2S as slave; CS8422/CPLD clockgen provides master clock. */
#if !defined(C4PRO_NO_RK_I2S_SLAVE) && !defined(C4PRO_RK_I2S_SLAVE_TEST)
#define C4PRO_RK_I2S_SLAVE_TEST 1
#endif

#ifndef C4PRO_USB_NO_RESET_STALE_GUARD
#define C4PRO_USB_RESET_STALE_GUARD
#endif

#ifndef C4PRO_FTL_NO_EAGER_FLUSH
#define C4PRO_FTL_EAGER_FLUSH
#endif

#ifndef C4PRO_FTL_NO_DEFER_COW
#define C4PRO_FTL_DEFER_COW
#endif

#ifndef USB_WRITE_BUFFER_SIZE
#define USB_WRITE_BUFFER_SIZE (128*1024)
#endif
#ifndef USB_READ_BUFFER_SIZE
#define USB_READ_BUFFER_SIZE (128*1024)
#endif

#if defined(C4PRO_NAND_2PLANE) && !defined(C4PRO_NAND_HWECC)
#error "C4PRO_NAND_2PLANE requires C4PRO_NAND_HWECC"
#endif

#ifndef C4PRO_NO_CACHE_SPIN_CAP
#define C4PRO_CACHE_SPIN_CAP
#endif

#define USB_VENDOR_ID 0x071b
#define USB_PRODUCT_ID 0x3203
#define HAVE_BOOTLOADER_USB_MODE

/* The exact type of CPU */
#define CONFIG_CPU RK27XX

/* I2C interface */
#define CONFIG_I2C I2C_RK27XX

/* Define this to the CPU frequency */
#define CPU_FREQ        200000000

/* Virtual LED (icon) */
#define CONFIG_LED LED_VIRTUAL

#define RKW_FORMAT
#define BOOTFILE_EXT "rkw"
#define BOOTFILE "rockbox." BOOTFILE_EXT
#define BOOTDIR "/.rockbox"
