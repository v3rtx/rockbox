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

/* NOTE: HAVE_LCD_ENABLE intentionally NOT defined. With the backlight subsystem
 * live, backlight_hw_off() would call lcd_enable(false) -> DISPOFF+SLEEP-IN,
 * blanking the panel (white/blank screen) on any fade/timeout. The sibling
 * rk27xx targets don't define it either; backlight-off just kills the PWM. */
/* #define HAVE_LCD_ENABLE */

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

/* Default-ON crash fix (active only in the USB-MSC build): if a stray INT_UDC
 * reaches UIRQ during the FTL sync-flush of a sustained copy, ACK it instead of
 * panicking (see UIRQ in system-rk27xx.c). Define C4PRO_USB_NO_UDC_IRQ_FIX to
 * A/B it off. */
#ifndef C4PRO_USB_NO_UDC_IRQ_FIX
#define C4PRO_USB_UDC_IRQ_FIX
#endif

/* Default-ON crash guard (active only in the USB-MSC build): when a host USB
 * link-reset lands between commands, INT_UDC finishes the re-init and acks
 * WITHOUT processing stale endpoint completions latched in the same interrupt
 * (which would data-abort on reset-cleared state). See INT_UDC in
 * usb-drv-rk27xx.c. Define C4PRO_USB_NO_RESET_GUARD to A/B it off. */
#ifndef C4PRO_USB_NO_RESET_GUARD
#define C4PRO_USB_RESET_GUARD
#endif

/* Default-ON companion to the reset guard: on every host USBRST, ABORT any
 * in-flight UDC DMA engine (clear DMA_START on all TX/RX DMA control registers)
 * and flush the per-endpoint FIFOs (TXCLR/RXCLR) BEFORE re-init. A link reset
 * that lands while a bulk-OUT receive or bulk-IN send DMA is mid-burst leaves
 * DMA_START stuck (the link died, so it never self-clears) -> the UDC DMA
 * engine holds the AHB bus -> the next access/DMA-arm busy-spins with IRQs
 * masked (F=0, T=D frozen, blank screen). The reset guard alone re-inits+acks
 * but never releases the wedged bus master. Pure register writes, bounded and
 * IRQ-safe. See c4pro_usb_reset_dma_abort() in usb-drv-rk27xx.c. Define
 * C4PRO_USB_NO_RESET_DMA_ABORT to A/B it off. */
#ifndef C4PRO_USB_NO_RESET_DMA_ABORT
#define C4PRO_USB_RESET_DMA_ABORT
#endif

/* Default-ON — THE FIX for the sustained-write hard freeze (verified 2026-07-02:
 * 900+ MB copied vs certain death within minutes before; see
 * docs/c4-usb-msc-write-freeze.md s7). Mirrors the STOCK firmware's bulk-OUT
 * arm discipline (disassembled from SYSTEM_BASE.RKW @0x6002ba68/@0x6002b52c):
 * (1) never write RXSTAT on the receive path (stock treats it read-only there;
 * the upstream 'RXSTAT = xfer_size' write has no stock counterpart), and
 * (2) never fire DMA_START into a still-busy OUT engine — bounded-wait for
 * self-clear first, and if it is genuinely stuck, abort + RXCLR-flush it at
 * arm time (the wedged engine otherwise holds the AHB bus and the CPU's next
 * memory access stalls forever: no fault, no IRQs, dead until power cycle).
 * See ep_read() in usb-drv-rk27xx.c. Define C4PRO_USB_NO_RX_DMA_GUARD to A/B
 * it off. */
#ifndef C4PRO_USB_NO_RX_DMA_GUARD
#define C4PRO_USB_RX_DMA_GUARD
#endif

/* C4PRO_WDT_CRASH_CAPTURE (opt-in, not default-on): JTAG-free freeze-forensics
 * tool used to root-cause the fix above (docs/c4-usb-msc-write-freeze.md s7).
 * The record layout/address/magic/reload-value are shared here so the tick
 * writer (kernel-rk27xx.c), the arm sequence (system-rk27xx.c), and the boot
 * reader (apps/main.c) can never silently disagree -- a debug tool whose own
 * three pieces drift out of sync just stops reporting, with no signal that
 * it broke. Chosen address is well past the app image and comfortably within
 * the 16 MiB DRAM window (0x60000000-0x61000000, see MEMORYSIZE above); nets
 * out unused unless C4PRO_WDT_CRASH_CAPTURE is explicitly defined. */
#define C4PRO_WDT_CAPTURE_ADDR   0x60910000u
#define C4PRO_WDT_CAPTURE_MAGIC  0xc4c4c4a5u
/* WDTLR reload value for both the initial arm and the per-tick pet. FIELD-
 * CORRECTED 2026-07-02: LR=1 (the value system_reboot() uses to fire
 * immediately) expires in under one tick period when reused for petting --
 * caused an instant boot loop (WDT fired before the first tick could even
 * write a crash record). This larger value is based on the one documented
 * clock reference in this codebase for a sibling APB0 peripheral
 * (tick_start()'s TMR0LR = 50000 * ms, i.e. a 50 MHz base) -- unverified for
 * the WDT specifically (no datasheet), but empirically confirmed: no boot
 * loop, and a real freeze was caught cleanly on the next repro. */
#define C4PRO_WDT_CAPTURE_RELOAD 50000000u

/* Default-ON (PART 1 - defer heavy re-init out of the IRQ): the upstream rk27xx
 * driver calls the heavy usb_drv_init() from INT_UDC on every USBRST. After the
 * one-time connect (done in thread context by usb_core_init before any reset),
 * that per-reset call only re-runs usb_drv_startup() + semaphore_init() - which
 * is both unnecessary and unsafe to do in IRQ context (re-initialising the
 * endpoint completion semaphores while the usb thread may be waiting on them,
 * and burning IRQ time while the tick is frozen). The minimal EP0 re-arm the
 * USBRST branch already does inline, plus usb_core_bus_reset() (which defers the
 * class-driver teardown to the usb thread - the standard Rockbox pattern), is
 * sufficient. With this flag the reset IRQ does NOT call usb_drv_init(). Define
 * C4PRO_USB_NO_RESET_DEFER to A/B it off (restore the in-IRQ re-init). */
#ifndef C4PRO_USB_NO_RESET_DEFER
#define C4PRO_USB_RESET_DEFER
#endif

/* Default-ON (PART 2 - drop stale data-EP completions around a reset): a host
 * bus reset invalidates every in-flight bulk transfer. A bulk-OUT/IN completion
 * IRQ latched in flight around the reset (observed: X=0x100, a BOUT1 completion
 * right before the freeze) must NOT be processed against endpoint/usb_core state
 * the reset is concurrently tearing down: doing so re-arms a DMA (ep_read) to a
 * stale buffer or posts a completion for a transfer the thread already dropped,
 * wedging the AHB -> masked-IRQ busy-spin (F=0, T=D frozen). While
 * usb_core_bus_reset_pending() is true, in_intr()/out_intr() ACK and skip data
 * endpoints (EP0/control is never gated). Define C4PRO_USB_NO_RESET_STALE_GUARD
 * to A/B it off. */
#ifndef C4PRO_USB_NO_RESET_STALE_GUARD
#define C4PRO_USB_RESET_STALE_GUARD
#endif

/* Default-ON: incremental write-back flush. As a sequential write stream
 * advances past a complete logical block, program that block to NAND right away
 * instead of letting up to C4OWN_CACHE_SLOTS dirty blocks pile up until the
 * end-of-file SCSI SYNCHRONIZE_CACHE. That keeps the per-file sync flush to ~1
 * block (fast), so the host never times out the SYNCHRONIZE_CACHE command and
 * never resets the USB link (the reset was what triggered the IRQ-context
 * crash). Same total NAND writes, just spread across the transfer. Opt out with
 * C4PRO_FTL_NO_EAGER_FLUSH to A/B (reproduce the crash). */
#ifndef C4PRO_FTL_NO_EAGER_FLUSH
#define C4PRO_FTL_EAGER_FLUSH
#endif

/* Default-ON crash fix: iteration-cap the cache-op busy-waits (CACHEOP) in
 * system-rk27xx.c. Those `while (CACHEOP & 0x03)` spins run with IRQs MASKED
 * (commit_discard_idcache wraps them in disable_irq_save; commit_discard_dcache_
 * range is reached from usb_core's disable_irq'd setup path), so if a wedged bus
 * master never releases the AHB the CPU spins forever -> the F=0 / T=D-frozen
 * "busy-spin" hang. Capping breaks out and proceeds. Opt out with
 * C4PRO_NO_CACHE_SPIN_CAP to A/B. */
#ifndef C4PRO_NO_CACHE_SPIN_CAP
#define C4PRO_CACHE_SPIN_CAP
#endif

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
