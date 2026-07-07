/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by Marcin Bukat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "kernel.h"
#include "system.h"
#include "panic.h"
#include "button.h"
#include "system-target.h"

#define default_interrupt(name) \
  extern __attribute__((weak,alias("UIRQ"))) void name (void)

void irq_handler(void) __attribute__((interrupt ("IRQ"), naked));
void fiq_handler(void) __attribute__((interrupt ("FIQ"), naked, \
                                      weak, alias("fiq_dummy")));

default_interrupt(INT_UART0);
default_interrupt(INT_UART1);
default_interrupt(INT_TIMER0);
default_interrupt(INT_TIMER1);
default_interrupt(INT_TIMER2);
default_interrupt(INT_GPIO0);
default_interrupt(INT_SW_INT0);
default_interrupt(INT_AHB0_MAILBOX);
default_interrupt(INT_RTC);
default_interrupt(INT_SCU);
default_interrupt(INT_SD);
default_interrupt(INT_SPI);
default_interrupt(INT_HDMA);
default_interrupt(INT_A2A_BRIDGE);
default_interrupt(INT_I2C);
default_interrupt(INT_I2S);
default_interrupt(INT_UDC);
default_interrupt(INT_UHC);
default_interrupt(INT_PWM0);
default_interrupt(INT_PWM1);
default_interrupt(INT_PWM2);
default_interrupt(INT_PWM3);
default_interrupt(INT_ADC);
default_interrupt(INT_GPIO1);
default_interrupt(INT_VIP);
default_interrupt(INT_DWDMA);
default_interrupt(INT_NANDC);
default_interrupt(INT_LCDC);
default_interrupt(INT_DSP);
default_interrupt(INT_SW_INT1);
default_interrupt(INT_SW_INT2);
default_interrupt(INT_SW_INT3);

static void (* const irqvector[])(void) USED_ATTR =
{
    INT_UART0,INT_UART1,INT_TIMER0,INT_TIMER1,INT_TIMER2,INT_GPIO0,INT_SW_INT0,INT_AHB0_MAILBOX,
    INT_RTC,INT_SCU,INT_SD,INT_SPI,INT_HDMA,INT_A2A_BRIDGE,INT_I2C,INT_I2S,
    INT_UDC,INT_UHC,INT_PWM0,INT_PWM1,INT_PWM2,INT_PWM3,INT_ADC,INT_GPIO1,
    INT_VIP,INT_DWDMA,INT_NANDC,INT_LCDC,INT_DSP,INT_SW_INT1,INT_SW_INT2,INT_SW_INT3
};

static const char * const irqname[] =
{
    "INT_UART0","INT_UART1","INT_TIMER0","INT_TIMER1","INT_TIMER2","INT_GPIO0","INT_SW_INT0","INT_AHB0_MAILBOX",
    "INT_RTC","INT_SCU","INT_SD","INT_SPI","INT_HDMA","INT_A2A_BRIDGE","INT_I2C","INT_I2S",
    "INT_UDC","INT_UHC","INT_PWM0","INT_PWM1","INT_PWM2","INT_PWM3","INT_ADC","INT_GPIO1",
    "INT_VIP","INT_DWDMA","INT_NANDC","INT_LCDC","INT_DSP","INT_SW_INT1","INT_SW_INT2","INT_SW_INT3"
};

#ifdef C4PRO_BOOT_HEARTBEAT
/* Set by irq_handler to the index it ACTUALLY dispatched (used to index
 * irqvector), captured BEFORE UIRQ's own racy INTC_ISR re-read. Plus per-source
 * UIRQ hit counts so the heartbeat can show the TRUE unhandled source. */
volatile unsigned int  c4pro_irq_dispatch_src;
volatile unsigned int  c4pro_uirq_last_src;
volatile unsigned long c4pro_uirq_total;
volatile unsigned long c4pro_uirq_count[32];
#endif

#ifdef C4PRO_FAULT_VISIBLE
/* Persisted CPU-fault record, written at UIE entry (system-arm-classic.c) BEFORE
 * any call that could re-fault. c4pro_fault_count>0 means a CPU abort/undef DID
 * happen (so a "silent" freeze is a fault, not a pure busy-spin); c4pro_fault_pc
 * is the faulting PC, c4pro_fault_phase the write-stage it faulted in. */
volatile uint32_t c4pro_fault_count;
volatile uint32_t c4pro_fault_pc;
volatile uint32_t c4pro_fault_num;
volatile uint32_t c4pro_fault_phase;
#endif

static void UIRQ(void)
{
#ifdef C4PRO_BOOT_HEARTBEAT
    /* Instrumented build: DO NOT panic and DO NOT touch INT2FLAG (clearing it
     * stole genuine UDC OUT/EP completions from the real INT_UDC handler ->
     * usb-thread wedge). Record the TRUE dispatched source (from irq_handler,
     * not the racy re-read) and per-source counts, then return so the copy
     * survives and the heartbeat reveals which source is really firing. The
     * INTC latch for the dispatched source is cleared by irq_handler on return;
     * any genuine peripheral bits stay pending for their real handler. */
    unsigned int truesrc = c4pro_irq_dispatch_src & 0x1f;

    c4pro_uirq_last_src = truesrc;
    c4pro_uirq_count[truesrc]++;
    c4pro_uirq_total++;
    return;
#else
    unsigned int offset = INTC_ISR & 0x1f;

    /* The RK27xx interrupt controller reports source 0x1f (INT_SW_INT3, which
     * nothing in this port enables) when the interrupt that asserted the IRQ
     * line has already deasserted by the time the handler reads INTC_ISR. This
     * spurious read happens under heavy concurrent IRQ load - e.g. sustained
     * USB mass-storage writes alongside audio (HDMA) and timer interrupts - and
     * is harmless. Ignore it instead of panicking; the dispatcher clears the
     * (already inactive) source on return. Genuine unhandled sources at other
     * indices still panic so real bugs stay visible. */
    if (offset == 0x1f)
        return;

#if defined(C4PRO_USB_UDC_IRQ_FIX) && defined(C4PRO_USB_MSC_ENABLE)
    /* CRASH FIX (default ON for the C4 MSC build). A stray INT_UDC (source 16)
     * reaching UIRQ used to panic ("Unhandled IRQ 10: INT_UDC") mid-copy. Just
     * RETURN - crucially WITHOUT touching INT2FLAG: clearing it here (as an
     * earlier version did) write-1-cleared genuine OUT/EP completion bits out
     * from under the real INT_UDC handler, wedging the usb thread. Leaving them
     * pending lets the real INT_UDC handler service them on the next dispatch.
     * The INTC latch is cleared by irq_handler on return. */
    if (offset == 16)   /* INT_UDC (IRQ_ARM_UDC = 1<<16) */
        return;
#endif

    panicf("Unhandled IRQ %02X: %s", offset, irqname[offset]);
#endif /* C4PRO_BOOT_HEARTBEAT */
}

#ifdef C4PRO_BOOT_HEARTBEAT
/* Count of TMR0CON timer-drain fires in irq_handler (below), so a heartbeat
 * build can show whether the drain is actually firing under sustained load. */
volatile unsigned long c4pro_hb_drain;
#endif

#ifdef C4PRO_WDT_CRASH_CAPTURE
/* lr_irq captured on every IRQ entry = interrupted PC + 4. INT_TIMER0
 * (kernel-rk27xx.c) stashes it each tick into a DRAM record that survives a
 * watchdog reset (2026-07-02: DRAM confirmed to survive a WDT-triggered
 * reset, unlike the slow human CE#-short replug cycle), so the boot after a
 * WDT-caught freeze can report the stuck code's address without JTAG. This
 * is how the sustained-write freeze was root-caused -- see
 * docs/c4-usb-msc-write-freeze.md s7. */
volatile unsigned long c4pro_irq_interrupted_pc;
#define C4PRO_IRQ_PC_STASH \
                 "ldr   r0, =c4pro_irq_interrupted_pc \n" \
                 "str   lr, [r0]             \n"
#else
#define C4PRO_IRQ_PC_STASH
#endif

void irq_handler(void)
{
#ifdef C4PRO_USB_TIMER_PRIO
    /* Boot-safe "keep the kernel tick alive" fix (default OFF), REPLACING the
     * INTC_SCR priority write (which mirrors stock but breaks Rockbox boot).
     *
     * Root cause: current_tick only advances in INT_TIMER0. irq_handler reads
     * INTC_ISR&0x1f = the highest-PRIORITY pending source and dispatches only
     * that one. Under a sustained transfer INT_UDC (source 16) is continuously
     * pending; if the INTC keeps reporting UDC as the active source, INT_TIMER0
     * (source 2) is never dispatched, the tick freezes, and the whole
     * cooperative scheduler stalls -> dead device. (Stock resolves the SAME ISR
     * register @0x104 and clears via ICCR @0x118 - verified from
     * SYSTEM_BASE.RKW @0x600ebce4/0x60007608 - so the dispatch register is NOT
     * the difference; only INTC_SCR priorities are, and writing those from
     * Rockbox hangs boot.)
     *
     * Fix without touching INTC config: the timer's OWN overflow/interrupt flag
     * (TMR0CON bit 2 @0x18000008) is set by the timer hardware independently of
     * the INTC, so it is visible even when the INTC never delivers/arbitrates
     * TIMER0. On EVERY IRQ entry, if that flag is set, service the tick directly
     * via INT_TIMER0() (which clears TMR0CON bit 2 AND runs call_tick_tasks ->
     * current_tick++). Thus a continuous INT_UDC storm can never starve the
     * kernel tick, regardless of INTC arbitration. Then do the normal ISR
     * dispatch. (On-device C4PRO_INTC_PROBE proved INTC_IPR@0x108 reads 0 here -
     * it is NOT a raw-pending bitmask - and INTC_ISR&0x1f is a source INDEX, so
     * "TIMER0 pending" is undetectable from the INTC; the timer flag is.)
     *
     * We deliberately do NOT clear the INTC TIMER0 latch here: if the timer was
     * NOT INTC-delivered this entry there is no latch to clear, and if it WAS
     * (ISR reports source 2) the normal dispatch below clears it. INT_TIMER0's
     * TMR0CON &= ~0x04 is idempotent, so a same-entry double-service (drain +
     * normal dispatch) is at most one extra harmless current_tick++ and cannot
     * loop (the flag is cleared). */
    asm volatile("stmfd sp!, {r0-r5, ip, lr} \n" /* store context */
                 C4PRO_IRQ_PC_STASH               /* interrupted PC readout */
                 "ldr   r0, =0x18000008      \n" /* TMR0CON */
                 "ldr   r1, [r0]             \n"
                 "tst   r1, #4               \n" /* timer overflow flag (bit 2)? */
                 "beq   1f                   \n"
#ifdef C4PRO_BOOT_HEARTBEAT
                 "ldr   r2, =c4pro_hb_drain  \n" /* D: count drain fires */
                 "ldr   r3, [r2]             \n"
                 "add   r3, r3, #1           \n"
                 "str   r3, [r2]             \n"
#endif
                 "bl    INT_TIMER0           \n" /* clears TMR0 flag + current_tick++ */
                 "1:                         \n"
                 "ldr   r4, =0x18080000      \n" /* INTC base */
                 "ldr   r5, [r4, #0x104]     \n" /* INTC_ISR */
                 "and   r5, r5, #0x1f        \n" /* irq_no = INTC_ISR & 0x1f */
#ifdef C4PRO_BOOT_HEARTBEAT
                 "ldr   r2, =c4pro_irq_dispatch_src \n" /* capture TRUE source */
                 "str   r5, [r2]             \n" /* before the racy UIRQ re-read */
#endif
                 "ldr   r3, =irqvector       \n"
                 "ldr   r3,[r3, r5, lsl #2]  \n"
                 "blx   r3                   \n" /* irqvector[irq_no]() */
                 "mov   r3, #1               \n"
                 "lsl   r5, r3, r5           \n" /* clear interrupt */
                 "str   r5, [r4, #0x118]     \n" /* INTC_ICCR = (1<<irq_no) */
                 "ldmfd sp!, {r0-r5, ip, lr} \n" /* restore context */
                 "subs  pc, lr, #4           \n");
#else
    asm volatile("stmfd sp!, {r0-r5, ip, lr} \n" /* store context */
                 C4PRO_IRQ_PC_STASH               /* interrupted PC readout */
                 "ldr   r4, =0x18080000      \n" /* INTC base */
                 "ldr   r5, [r4, #0x104]     \n" /* INTC_ISR */
                 "and   r5, r5, #0x1f        \n" /* irq_no = INTC_ISR & 0x1f */
#ifdef C4PRO_BOOT_HEARTBEAT
                 "ldr   r2, =c4pro_irq_dispatch_src \n" /* capture TRUE source */
                 "str   r5, [r2]             \n" /* before the racy UIRQ re-read */
#endif
                 "ldr   r3, =irqvector       \n"
                 "ldr   r3,[r3, r5, lsl #2]  \n"
                 "blx   r3                   \n" /* irqvector[irq_no]() */
                 "mov   r3, #1               \n"
                 "lsl   r5, r3, r5           \n" /* clear interrupt */
                 "str   r5, [r4, #0x118]     \n" /* INTC_ICCR = (1<<irq_no) */
                 "ldmfd sp!, {r0-r5, ip, lr} \n" /* restore context */
                 "subs  pc, lr, #4           \n");
#endif
}

void fiq_dummy(void)
{
    asm volatile (
        "subs   pc, lr, #4   \r\n"
    );
}


void system_init(void)
{
    /* disable WDT just in case nand loader activated it */
    WDTCON &= ~(1<<3);

#ifndef BOOTLOADER
    /* SDRAM tweaks.  Note this assumes 100MHz AHB+SDRAM clock. */

#if !(defined(HM60X) || defined(HM801))
    MCSDR_MODE = (3<<4)|3;         /* CAS=3, burst=8(2^3) -- Safe but slower */
#else
    MCSDR_MODE = (2<<4)|3;         /* CAS=2, burst=8(2^3) -- Ideal but causes startup issues on (some?) IHIFI devices */
#endif

    MCSDR_T_REF = (125*100) >> 3;  /* 125/8 = 15.625 autorefresh interval */
    MCSDR_T_RFC = (64*100) / 1000; /* autorefresh period */
    MCSDR_T_RP = 1;                /* precharge period */
    MCSDR_T_RCD = 1;               /* active to RD/WR delay */

    /* turn off clock for unused modules */
#ifndef COLORFLY_C4_PRO
    SCU_CLKCFG |= CLKCFG_WDT        |        /* WDT pclk */
                  CLKCFG_RTC        |        /* RTC pclk */
                  CLKCFG_HSADC      |        /* HS_ADC clock */
                  CLKCFG_HCLK_HSADC |        /* HS_ADC HCLK */
                  CLKCFG_SPI        |        /* SPI clock */
                  CLKCFG_UART1      |        /* UART1 clock */
                  CLKCFG_UART0      |        /* UART0 clock */
                  CLKCFG_VIP        |        /* VIP clock */
                  CLKCFG_HCLK_VIP   |        /* VIP HCLK */
                  CLKCFG_LCDC       |        /* LCDC clock */
                  CLKCFG_NAND       |        /* NAND HCLK */
                  CLKCFG_UHC        |        /* USB host HCLK */
                  CLKCFG_DSP        |        /* DSP clock */
                  CLKCFG_OTP;                /* OTP clock (dunno what it is */
#else
    /* C4PRO: do NOT gate any clocks. MaskROM leaves SCU_CLKCFG = 0x00100000
     * (almost everything on) and USB enumerates at high-speed; Rockbox's
     * gating (0xc7bcc003) was starving the USB device PHY's clock domain,
     * corrupting signalling (host error -71, full-speed only). Leaving the
     * inherited clock config intact costs a little idle power but keeps USB
     * working. */
#endif

#ifndef COLORFLY_C4_PRO
    /* turn off DSP pll */
    SCU_PLLCON2 |= (1<<22);

    /* turn off codec pll */
    SCU_PLLCON3 |= (1<<22);
#else
    /* C4PRO: do NOT power down the DSP/codec PLLs. The USB device PHY's
     * reference clock appears to derive from one of these, so powering them
     * down corrupts USB signalling (host error -71, full-speed only) even
     * though MaskROM enumerates high-speed with them left running. */

    /* NOTE: clearing PLLCON3 bit0 (the only inherited-register difference
     * between the USB-working warm state ...f8 and the failing NAND cold boot
     * ...f9) was tried and made NO difference - cold boot still falls back to
     * full-speed/error-71. So the cold-boot HS failure is NOT a static register
     * difference; the mask BootROM's USB bring-up does something to the PHY
     * (its internal HS PLL / analog state) that a NAND cold boot never runs and
     * SOFT_POR cannot reproduce. Fix requires porting that PHY init sequence. */
#endif
#endif
    /* NOTE: C4PRO_USB_TIMER_PRIO's "keep the tick alive" logic now lives in
     * irq_handler (an explicit INT_TIMER0 drain), NOT here. The earlier
     * INTC_SCR priority write - even the stock-faithful "all sources level 1,
     * then TIMER0 level 2" form - hangs boot on this port, so writing INTC
     * priorities from Rockbox is abandoned. The observable INTC_SCR experiment
     * lives behind C4PRO_INTC_PROBE (apps/main.c), run at an LCD-available
     * point so its effect on the tick can be seen. */
}

/* not tested */
void system_reboot(void)
{
    /* use Watchdog to reset */
    SCU_CLKCFG &= ~CLKCFG_WDT;
    WDTLR = 1;
    WDTCON = (1<<4) | (1<<3);

    /* Wait for reboot to kick in */
    while(1);
}

/* NOTE: entering the mask-ROM RKUSB download mode (071b:3201) for a CE#-free
 * reflash is NOT done from the running app. Jumping to the ROM handler while USB
 * is already enumerated as MSC leaves the host with a stale enumeration and it
 * never re-enumerates. The app instead requests it across a watchdog reset (a
 * HANDOFF magic + system_reboot()); the native loader enters RKUSB at boot with
 * a fresh USB. See tools/c4_native_rockbox_loader.c (HANDOFF_RKUSB_MAGIC) and
 * apps/main.c c4pro_usb_msc_serve(). */

#ifdef C4PRO_WDT_CRASH_CAPTURE
/* Arm the WDT for crash capture WITHOUT spinning (unlike system_reboot()):
 * the tick-driven pet in INT_TIMER0 (kernel-rk27xx.c) keeps reloading WDTLR
 * every tick while c4pro_wdt_capture_enable is true, so the countdown only
 * ever completes if ticks stop -- i.e. the freeze itself. Same clock-gate +
 * arm sequence as the proven system_reboot(), except the load value: LR=1
 * (system_reboot()'s value) turned out to expire in under one tick period
 * (2026-07-02 field note -- caused an immediate boot loop, the WDT firing
 * before the first tick could even write a crash record). Matches the
 * larger load value INT_TIMER0's pet now uses, so the window between this
 * initial arm and the first pet is never shorter than every other window. */
volatile bool c4pro_wdt_capture_enable = false;

void c4pro_wdt_capture_start(void)
{
    /* Zero the sampler slots so a freeze report can't show a stale previous
     * session's values (DRAM persists across the WDT reset by design). */
    volatile unsigned long *rec = (volatile unsigned long *)C4PRO_WDT_CAPTURE_ADDR;
    rec[3] = rec[4] = rec[5] = rec[6] = 0;
    rec[7] = rec[8] = rec[9] = rec[10] = 0;   /* stall-kick state slots */

    SCU_CLKCFG &= ~CLKCFG_WDT;
    WDTLR = C4PRO_WDT_CAPTURE_RELOAD;
    WDTCON = (1<<4) | (1<<3);
    c4pro_wdt_capture_enable = true;
}
#endif

void system_exception_wait(void)
{
    /* wait until button release (if a button is pressed) */
    while(button_read_device());
    /* then wait until next button press */
    while(!button_read_device());
}

int system_memory_guard(int newmode)
{
    (void)newmode;
    return 0;
}

/* usecs may be at most 2^32/200 (~21 seconds) for 200MHz max cpu freq */
void udelay(unsigned usecs)
{
    unsigned cycles_per_usec;
    unsigned delay;

    if (cpu_frequency == CPUFREQ_MAX) {
        cycles_per_usec = (CPUFREQ_MAX + 999999) / 1000000;
    } else {
        cycles_per_usec = (CPUFREQ_NORMAL + 999999) / 1000000;
    }

    delay = (usecs * cycles_per_usec) / 5;

    asm volatile(
        "1: subs %0, %0, #1  \n"    /* 1 cycle  */
        "   nop              \n"    /* 1 cycle  */
        "   bne  1b          \n"    /* 3 cycles */
        : : "r"(delay)
    );
}

#ifdef C4PRO_CACHE_SPIN_CAP
/* Iteration-capped wait for a cache op to finish (CACHEOP bits[1:0]). The raw
 * `while (CACHEOP & 0x03)` is an UNCAPPED hardware-poll that runs with IRQs
 * MASKED in two paths: commit_discard_idcache() wraps it in disable_irq_save(),
 * and commit_discard_dcache_range() is reached from usb_core check_for_new_setup
 * -> process_setup_request -> usb_drv_recv_nonblocking while IRQs are disabled.
 * If a bus master (a wedged UDC/NAND DMA after a host link reset) never releases
 * the AHB, CACHEOP never clears and the CPU spins forever with IRQs masked ->
 * the T=D-frozen "busy-spin" hang (F=0, no CPU fault). Bound it: on timeout,
 * give up on the (best-effort) cache op and proceed - a rare cache incoherence
 * is far better than a dead device. Counts timeouts for the heartbeat. */
volatile uint32_t c4pro_cache_spin_timeouts;
#ifndef C4PRO_CACHE_SPIN_ITERS
#define C4PRO_CACHE_SPIN_ITERS 8000000u   /* ~tens of ms ceiling; normal op << */
#endif
#define C4PRO_CACHE_WAIT() do {                         \
        uint32_t _g = C4PRO_CACHE_SPIN_ITERS;           \
        while ((CACHEOP & 0x03) && --_g)                \
            ;                                           \
        if (!_g) c4pro_cache_spin_timeouts++;           \
    } while (0)
#else
#define C4PRO_CACHE_WAIT() do { while (CACHEOP & 0x03) ; } while (0)
#endif

static void cache_invalidate_way(int way)
{
    /* Issue invalidata way command to the cache controler */
    CACHEOP = ((way<<31)|0x2);

    /* wait for invalidate process to complete */
    C4PRO_CACHE_WAIT();
}

void commit_discard_idcache(void)
{
    int old_irq = disable_irq_save();

    cache_invalidate_way(0);

    cache_invalidate_way(1);

    restore_irq(old_irq);
}
void commit_discard_dcache (void) __attribute__((alias("commit_discard_idcache")));

void commit_discard_dcache_range (const void *base, unsigned int size)
{
    /* 0x01 is opcode for cache line commit discard */
    uint32_t end_opcode = (uint32_t)((uintptr_t)base + size) | 0x01;
    uint32_t opcode = (uint32_t)((uintptr_t)base & 0xffffffe0) | 0x01;

#ifdef COLORFLY_C4_PRO
    /* CACHEOP is a SINGLE shared cache-controller register and a cache op is
     * issue-then-poll -- NOT atomic. The rk27xx audio path arms its HDMA from
     * pcm_play_dma_start() via this same range op (pcm-rk27xx.c), reachable from
     * the HDMA ISR's getmore refill. If that fires while this loop is mid-op it
     * clobbers the in-flight CACHEOP -> cache-controller corruption -> a random
     * wild-branch data abort (seen once the microSD was enabled). It never bit
     * with NAND-only playback because NAND is PIO with NO cache ops; the SD driver
     * added a second, thread-context cache-op caller that the audio ISR races.
     * Serialize with IRQs masked, exactly as commit_discard_idcache() does -- the
     * spin is bounded (C4PRO_CACHE_SPIN_CAP) so the IRQ-off window stays short. The
     * USB path already reaches this IRQ-masked; disable_irq_save nests safely. */
    int old_irq = disable_irq_save();
#endif

    while (opcode <= end_opcode)
    {
        C4PRO_CACHE_WAIT();

        CACHEOP = opcode;
        opcode += 32;
    }

    /* The loop waits for the PREVIOUS op at the top of each iteration, so the
     * final opcode issued above is never waited on. Without this, the last
     * cache line's commit-discard can still be in flight when the caller starts
     * a DMA from the just-flushed buffer (the rk27xx UDC IN DMA is a separate
     * bus master). Wait for it here, matching cache_invalidate_way(). */
    C4PRO_CACHE_WAIT();

#ifdef COLORFLY_C4_PRO
    restore_irq(old_irq);
#endif
}

#if !defined(BOOTLOADER) && defined(HAVE_ADJUSTABLE_CPU_FREQ)
static inline void set_sdram_timing(int ahb_freq)
{
#if 1
    if (ahb_freq > 100000000)
        MCSDR_MODE = (3<<4)|3;         /* CAS=3, burst=8(2^3) */
    else
        MCSDR_MODE = (2<<4)|3;         /* CAS=2, burst=8(2^3) */
#endif
    MCSDR_T_REF = (125*ahb_freq/1000000) >> 3;
    MCSDR_T_RFC = (64*ahb_freq/1000000)/1000;
}

void set_cpu_frequency(long frequency)
{
    if (cpu_frequency == frequency)
        return;

    /* Temporarily use more conservative SDRAM settings
       so we don't glitch when changing clocks */
    set_sdram_timing(12000000);

    if (frequency == CPUFREQ_MAX)
    {
        /* PLL set to 200 Mhz
         * PLL:ARM = 1:1
         * ARM:AHB = 2:1
         * AHB:APB = 2:1
         */
        SCU_DIVCON1 = (SCU_DIVCON1 &~ 0x1f) | (1<<3)|1;
        SCU_PLLCON1 = ((1<<24)|(1<<23)|(5<<16)|(49<<4)); /*((24/6)*50)/1*/

        /* wait for PLL lock ~0.3 ms */
        while (!(SCU_STATUS & 1));

        /* leave SLOW mode */
        SCU_DIVCON1 &= ~1;

        set_sdram_timing(CPUFREQ_MAX/2);
    }
    else
    {
        /* PLL set to 100 MHz
         * PLL:ARM = 2:1
         * ARM:AHB = 1:1
         * AHB:APB = 1:1
         */
        SCU_DIVCON1 = (SCU_DIVCON1 & ~0x1f) | (1<<2)|1;
        SCU_PLLCON1 = ((1<<24)|(1<<23)|(5<<16)|(49<<4)|(1<<1)); /*((24/6)*50)/2*/

        /* wait for PLL lock ~0.3 ms */
        while (!(SCU_STATUS & 1));

        /* leave SLOW mode */
        SCU_DIVCON1 &= ~1;

        set_sdram_timing(CPUFREQ_NORMAL);
    }

    cpu_frequency = frequency;
}
#endif
