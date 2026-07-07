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

/* Never force a low-battery auto power-off (2026-07-07). The device is normally
 * USB-powered and the battery reading is unreliable/optional (a desoldered or
 * weak cell reads ~0V, and _battery_voltage()'s ADC calibration is a placeholder).
 * Without this, query_force_shutdown() (powermgmt.c) sees "empty battery" and
 * sys_poweroff()s a few seconds into boot -- observed as "splash then goes dark"
 * on both battery and USB (the USB power_input_present() inhibit doesn't apply
 * until USB is enumerated, which is after the long mount). Data safety comes from
 * a clean unmount/flush, not from yanking power on a bad reading. */
#define NO_LOW_BATTERY_SHUTDOWN

/* TEMPORARY DIAGNOSTIC (2026-07-07): run the in-app multi-cycle FTL persistence
 * self-test on boot (ftl-rk27xx.c) instead of serving USB. Write a "file", then
 * 30x { churn a different region -> ftl_sync -> c4own_mount_scan remount ->
 * verify the file byte-exact }, and HALT showing failcyc/badsec/uncorr. Isolates
 * the data-durability drift with no host/USB/power confound. REMOVE after. */
/* #define C4PRO_FTL_ECC_SELFTEST  -- passed (failcyc=-1): FTL logic is correct */
/* #define C4PRO_FTL_PCYCLE_TEST   -- passed (DURA=SURVIVED): internal writes are
 *   cold-durable, so the data loss was the HOST write path never being flushed. */
/* THE durability fix: flush the FTL cache on serve-loop exit (apps/main.c). That
 * flush uses storage_flush(), which needs HAVE_STORAGE_FLUSH defined here. */
#define HAVE_STORAGE_FLUSH

/* Mount CHECKPOINT: ON, ISOLATION TEST (2026-07-07). The earlier "checkpoint
 * corrupts" observation came from a build where 2-plane was ALSO on -- and 2-plane
 * is now the confirmed culprit. This tests the checkpoint ALONE (2-plane OFF) to
 * see if its out-of-order superblock programming (page0, entries, footer@126,
 * marker@127 -- MLC-unsafe in theory) is actually durable on this NAND. If a
 * host-eject + replug cycle stays clean with this, the checkpoint is fine and we
 * keep fast boot; if it corrupts, rewrite it MLC-legal (footer@page1, entries
 * after, dirty marker@N+2 -- one contiguous sequential run). */
#define C4PRO_FTL_MOUNT_CHECKPOINT

/* Hardware controlled charging with monitoring */
#define CONFIG_CHARGING CHARGING_MONITOR

/* define this if the unit can be powered or charged via USB */
#define HAVE_USB_POWER

/* USB On-the-go */
#define CONFIG_USBOTG USBOTG_RK27XX

/* enable these for the experimental usb stack */
#define HAVE_USBSTACK

/* NOTE: enlarging USB_WRITE_BUFFER_SIZE does NOT help write throughput on the C4
 * (tested: 24 KB -> 63 KB gave 1.7 -> 1.8 MB/s, i.e. no change, and 64 KB tripped
 * the 16-bit out_intr count wrap and dropped the bus). The read/write asymmetry
 * (read 4.2, write 1.8) is NOT buffer-driven -- the bulk-OUT RECEIVE path itself
 * caps at ~1.8 MB/s (per-packet DMA arm), independent of NAND and buffer size.
 * Left at the generic defaults; the real lever is the RX path, not buffer size. */

/* Lean bulk-OUT re-arm (OPT-IN: -DC4PRO_USB_RX_LEAN_REARM). Confirmed on-device
 * that RX re-arms once per 512B packet (O ~= bytes/512) at ~314us/packet vs
 * stock's ~78us -> write caps ~1.7 MB/s vs stock ~6.5. Stock disassembly shows
 * its per-packet handler is 4 instructions (read RX1STAT length, advance ptr,
 * write DMAOUTLMADDR, write DMAOUTCTL) and touches nothing else -- no RXCON
 * rewrite, no per-packet MMIO. (Multi-packet-per-DMA is impossible: the OUT
 * engine has no transfer-length register.) This flag strips our per-packet path
 * toward stock's: no RXCON NAK-clear on bulk re-arm, no per-packet RXCON MMIO
 * read in diag. Freeze-sensitive path -> validate with crash-capture + multiple
 * runs. */

/* C4PRO_USB_RX_STALL_KICK (OPT-IN): serve-loop watchdog for the residual
 * sustained-write freeze -- the armed bulk-OUT receive (phase 13) silently never
 * completing, which the host punishes ~30s later with a bus reset that wedges
 * the AHB. On "phase 13 + zero RX completions for 2s" the main thread kicks the
 * receive engine (abort+RXCLR+re-arm at the same position; safe because nothing
 * was ACKed). See c4pro_usb_rx_stall_kick() in usb-drv-rk27xx.c. */

/* Phase markers must be live whenever the WDT crash capture is, not only with
 * the LCD heartbeat: a captured freeze with the USB phases (1,2,10-13) compiled
 * out reports a STALE FTL phase and misdirects the investigation (this exact
 * mistake pointed the residual-freeze hunt at the NAND boundary while the
 * thread was actually hanging in the USB CSW/receive path). */
/* PHASE_MARK: USB/FTL phase markers (c4pro_hb_phase). The RX stall-kick freeze
 * recovery reads them, so it is UNCONDITIONALLY on -- not tied to the WDT crash
 * capture (which we keep off to avoid reboots). Was previously gated on
 * CRASH_CAPTURE/HEARTBEAT; decoupling it is what lets the stall-kick run with no
 * watchdog. */
#define C4PRO_PHASE_MARK

/* Residual phase-5 wedge, stage-1 instruments/fix (see nand-rk27xx.c):
 * - C4PRO_NAND_BCH_NO_RMW (OPT-IN): write BCHCTL fresh per sector instead of the
 *   read-modify-write. The RMW READS the BCH engine's control register while it
 *   may be mid-encode -- an access stock provably never makes (its trigger
 *   routine writes a fresh-computed BCHCTL and never reads it), and the last
 *   "we do, stock doesn't" divergence left in the wedge region after
 *   serialize/spaced-poll/status-poll were exhausted.
 * - c4pro_nand_stage marker (automatic with C4PRO_WDT_CRASH_CAPTURE):
 *   fine-grained (region<<8)|sector stage in the WDT crash record (rec[4],
 *   "st=" on the FREEZE screen), discriminating staging-copy / program-engine
 *   wait / tPROG poll / read-engine wait -- plus FTL phase 7 for the COW-fill
 *   reads that previously masqueraded as phase 5.
 * - C4PRO_NAND_WRITE_STRESS (OPT-IN, diagnostic build): standalone FTL write
 *   hammer with USB never attached, to discriminate a pure-NANDC wedge from a
 *   UDC-presence-dependent one. */

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

/* Sustained-write freeze guards -- default-on daily-driver config. These were
 * previously only passed as build flags, so a trimmed boot-speed build dropped
 * them and a large USB copy wedged and tore a write (2026-07-05). Baked in here
 * so they can never be lost by trimming -Dflags. SERIALIZE_WRITE (usb_storage.c)
 * serializes the bulk-OUT DMA against the NAND program -- the collision that
 * wedges the AHB bus; WDT_AUTOREBOOT (main.c) auto-recovers if a wedge slips
 * through. Define the C4PRO_*_NO_* twin to A/B either off. */
#ifndef C4PRO_USB_NO_SERIALIZE_WRITE
#define C4PRO_USB_SERIALIZE_WRITE
#endif
/* CRASH_CAPTURE is what actually ARMS + pets the freeze-recovery watchdog
 * (c4pro_wdt_capture_start + the tick pet in kernel-rk27xx.c). WDT_AUTOREBOOT
 * alone only sets the post-reset behaviour -- without CRASH_CAPTURE the WDT is
 * never armed, so a residual NAND-program freeze hangs dead instead of
 * self-rebooting (observed 2026-07-05). Both are needed for the daily's
 * auto-recover-on-freeze. The ~1s WDT reload only trips when the tick stops. */
/* ==========================================================================
 * C4 Pro production write path -- ALL default-on here, NO build flags needed.
 *
 * HARD-WON LESSON (2026-07-06): -D flags passed via C4_NATIVE_RUNTIME_EXTRA_DEFINES
 * (make EXTRA_DEFINES) DO NOT REACH firmware .c files (nand-rk27xx.c,
 * usb-drv-rk27xx.c) -- only config-header #defines do. A full day was lost
 * passing the write path's flags on the command line where they silently
 * evaporated: software-BCH instead of HWECC (volume corruption), and the RX
 * stall-kick freeze-recovery compiled out (hard hangs seconds into a copy).
 * EVERY firmware flag the write path needs lives HERE, unconditionally. Never
 * move them back to a recipe. A production build passes NO write-path -D flags.
 *
 *  NAND: HWECC (on-NAND layout, inline 512+16) + 2PLANE (~4MB/s, needs HWECC)
 *        + FAST_FORMAT + BCH_NO_RMW + FMCTL_NO_RMW + IOMUX_DIE0_SKIP
 *  USB freeze recovery: RESET_DMA_ABORT (default above) + RX_STALL_KICK, which
 *        reads the PHASE_MARK markers (made unconditional above) -- this trio is
 *        THE fix for the residual sustained-write freeze; drop any one and a
 *        copy hangs within seconds. Plus IRQ_DRAIN + TX_WEDGE_GUARD +
 *        DEFER_WRITE + SERIALIZE_WRITE (default above).
 *  FAULT_VISIBLE: show a fault on-screen instead of a silent wedge.
 *
 * WDT crash-capture/auto-reboot is deliberately OFF: with the stall-kick live
 * the freeze is RECOVERED in place (copy resumes), so the device never reboots
 * under the user. Opt in only for debugging with -DC4PRO_WDT_CRASH_CAPTURE. */
#define C4PRO_NAND_HWECC
/* C4PRO_NAND_2PLANE: OFF (2026-07-07). Isolated: with the host-flush bug fixed,
 * single-plane is durable but re-enabling 2-plane corrupts across a power cycle
 * EVEN AFTER a proper host eject -- so the original finding was right: the
 * two-plane array commit (c4pro_nand_write_page_2plane, nand-rk27xx.c: 0x11
 * plane-0 dummy -> 0x81/0x10 final) reads back correctly WHILE POWERED but is not
 * durably fixed into the array (shared-tPROG wait / FMWAIT too short, or plane-1
 * serial input not committed). It was masked by the host-flush bug, not a red
 * herring. Re-enable only after the 2-plane program's tPROG/commit is fixed +
 * verified across a real power cycle. Single-plane hwecc is durable. */
/* #define C4PRO_NAND_2PLANE */
#define C4PRO_NAND_FAST_FORMAT
#define C4PRO_NAND_BCH_NO_RMW
#define C4PRO_NAND_FMCTL_NO_RMW
#define C4PRO_NAND_IOMUX_DIE0_SKIP
#define C4PRO_USB_TX_WEDGE_GUARD
#define C4PRO_USB_DEFER_WRITE
#define C4PRO_USB_RX_STALL_KICK
#define C4PRO_USB_IRQ_DRAIN
#define C4PRO_FAULT_VISIBLE

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

/* Deferred copy-on-write (default ON). The owned-RW write cache used to pre-read
 * the ENTIRE old 504 KB block on every write-miss to a mapped logical block, so
 * rewriting an existing volume paid ~504 KB of NAND reads per block that a fresh
 * (unmapped) volume did not -- the measured "4 MB/s fresh vs 2 MB/s rewrite" gap
 * (~24 s of a 64 s / 128 MB dd spent in old-block reads). This tracks which
 * payload sectors the host actually wrote and backfills only the UNWRITTEN gaps
 * at flush time, so a full-block overwrite (dd, large file copies) does ZERO
 * old-block reads and rewrites run at fresh-volume speed (measured 2.0 -> 4.4
 * MB/s, permanently). Partial writes still COW the gaps, and both FTL read paths
 * serve unwritten cached sectors from the old block, so data is unchanged. Opt
 * out with C4PRO_FTL_NO_DEFER_COW (reverts to the unconditional pre-read). */
#ifndef C4PRO_FTL_NO_DEFER_COW
#define C4PRO_FTL_DEFER_COW
#endif

/* Larger USB bulk transfer buffers (default 128 KB, up from the generic 24 KB).
 * The sustained-write bottleneck is partly the per-buffer storage-handoff gap;
 * bigger buffers mean proportionally fewer gaps. The RX DMA is armed per 512-byte
 * packet, so buffer size carries NO DMA limit -- it is only a larger staging
 * area (2x its size in DRAM, well within budget). Override with
 * -DUSB_WRITE_BUFFER_SIZE / -DUSB_READ_BUFFER_SIZE. */
#ifndef USB_WRITE_BUFFER_SIZE
#define USB_WRITE_BUFFER_SIZE (128*1024)
#endif
#ifndef USB_READ_BUFFER_SIZE
#define USB_READ_BUFFER_SIZE (128*1024)
#endif

/* Die-interleaved write path (OPT-IN, enable with -DC4PRO_NAND_DIE_INTERLEAVE).
 * The default flush programs one logical block on one die at a time, so every
 * page pays the full MLC tPROG serially and writes cap at ~1.7 MB/s. This batches
 * up to 4 dirty cache blocks onto 4 distinct dies and programs them in lockstep
 * (c4pro_nand_prog_pages_hwecc_multi fires 0x10 on all dies before waiting on
 * any), overlapping their tPROG windows to approach the stock ~6.5 MB/s. It is a
 * pure scheduling change over the same per-page hwecc format -- no reformat, and
 * the persisted logical->physical map records which die each block lives on, so
 * existing volumes keep mounting. Requires C4PRO_NAND_HWECC (shares its inline
 * 512+16 layout). Add -DC4PRO_NAND_DIE_INTERLEAVE_VERIFY to read back the first
 * and last payload page of every block and fail the write loudly on any mismatch
 * (catches the addressing/die failure modes at ~2 reads/block, so the measured
 * throughput stays meaningful); add -DC4PRO_NAND_DIE_INTERLEAVE_VERIFY_FULL on
 * top to check every page (paranoid, much slower). */
#if defined(C4PRO_NAND_DIE_INTERLEAVE) && !defined(C4PRO_NAND_HWECC)
#error "C4PRO_NAND_DIE_INTERLEAVE requires C4PRO_NAND_HWECC"
#endif

/* Two-plane write path (OPT-IN, enable with -DC4PRO_NAND_2PLANE). The stock
 * ~6.7 MB/s lever: this Micron MLC part programs TWO adjacent-block pages (plane 0
 * = block P, plane 1 = block P+1, differing only in block bit 0) in a SINGLE array
 * tPROG via the 0x80/0x11..0x81/0x10 sequence. The flush pairs two dirty cache
 * blocks onto a fresh plane-pair, halving the per-page tPROG cost (sequence,
 * block-bit-0 stride, round-trip and 2x speed all validated on-device). Each block
 * is written in the SAME hwecc inline 512+16 layout as the single-plane writer
 * (c4pro_nand_write_page_hwecc), so it is byte-identical on NAND to a serial write
 * and the reader, mount scan and checkpoint are UNCHANGED -- existing volumes keep
 * mounting and it is a pure write-side scheduling change (no reformat). Requires
 * C4PRO_NAND_HWECC (every production build defines it -- a build without it uses
 * the incompatible software-BCH layout and corrupts: this exact mismatch caused
 * the 2026-07-03 bad-md5/vanished-disk regression). Mutually exclusive with
 * C4PRO_NAND_DIE_INTERLEAVE (both replace the serial flush). */
#if defined(C4PRO_NAND_2PLANE) && !defined(C4PRO_NAND_HWECC)
#error "C4PRO_NAND_2PLANE requires C4PRO_NAND_HWECC (production layout is hwecc inline 512+16)"
#endif
#if defined(C4PRO_NAND_2PLANE) && defined(C4PRO_NAND_DIE_INTERLEAVE)
#error "C4PRO_NAND_2PLANE and C4PRO_NAND_DIE_INTERLEAVE are mutually exclusive"
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
