/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
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
#include "lcdif-rk27xx.h"

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

static void UIRQ(void)
{
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

    panicf("Unhandled IRQ %02X: %s", offset, irqname[offset]);
}

void irq_handler(void)
{
    asm volatile("stmfd sp!, {r0-r5, ip, lr} \n" /* store context */
                 "ldr   r4, =0x18080000      \n" /* INTC base */
                 "ldr   r5, [r4, #0x104]     \n" /* INTC_ISR */
                 "and   r5, r5, #0x1f        \n" /* irq_no = INTC_ISR & 0x1f */
                 "ldr   r3, =irqvector       \n"
                 "ldr   r3,[r3, r5, lsl #2]  \n"
                 "blx   r3                   \n" /* irqvector[irq_no]() */
                 "mov   r3, #1               \n"
                 "lsl   r5, r3, r5           \n" /* clear interrupt */
                 "str   r5, [r4, #0x118]     \n" /* INTC_ICCR = (1<<irq_no) */
                 "ldmfd sp!, {r0-r5, ip, lr} \n" /* restore context */
                 "subs  pc, lr, #4           \n");
}

void fiq_dummy(void)
{
    asm volatile (
        "subs   pc, lr, #4   \r\n"
    );
}


static void core_sleep_init(void);

/* The DWDMA's clock is on while one of its users holds it: lcdif-rk27xx.c,
 * for dwdma_init() and each LCD update. */
static int dwdma_users = 0;

void rk27xx_dwdma_clock(bool on)
{
    int old = disable_irq_save();

    if (on)
    {
        if (dwdma_users++ == 0)
            SCU_CLKCFG &= ~CLKCFG_DWDMA;
    }
    else if (--dwdma_users == 0)
    {
        SCU_CLKCFG |= CLKCFG_DWDMA;
    }
    restore_irq(old);
}

void system_init(void)
{
    /* disable WDT just in case nand loader activated it */
    WDTCON &= ~(1<<3);

    core_sleep_init();

#ifndef BOOTLOADER
#ifndef COLORFLY_C4_PRO
    /* turn off clock for unused modules - not the NAND controller's: the
     * FTL uses it, and a register access with its HCLK gated aborts */
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
#endif
#endif
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

void system_exception_wait(void)
{
    /* wait until button release (if a button is pressed) */
    while(button_read_device());
    /* then wait until next button press */
    while(!button_read_device());
}

/* Idle: SCU_CPUPD = 0xdeedbabe gates the core clock, the peripherals keep
 * running. The SCU restarts the clock only when the INTC's IRQ output
 * *rises* while the core is stopped: written while an enabled interrupt is
 * pending - one that came in after the scheduler found nothing to do - the
 * core never wakes again. The core cannot avoid that window itself: it
 * stops a few cycles after the write, before it could unmask anything it
 * masked first. So the INTC is masked, and the A2A DMA unmasks it a few
 * microseconds later, after the core has stopped: anything pending then
 * raises the line and wakes the core. Interrupts that arrive meanwhile are
 * delayed by those microseconds. (Tested with hwstub: 2.4 million sleeps
 * under 25 kHz of interrupts without a hang; without the DMA, a hang within
 * 60000.) The zeros the DMA writes first are the delay: the core must stop
 * before the first unmask lands. Measured in Rockbox during playback, the
 * DMA has written at most 1 word when the core writes SCU_CPUPD, normal or
 * boosted, and every sleep with an interrupt pending was woken by the
 * first unmask after 7 zeros; with 3 zeros, and a write to SDRAM between
 * starting the DMA and SCU_CPUPD, it sometimes took the next one, and with
 * a single unmask playback hung within seconds. The DMA still unmasks
 * twice, masking again in between, as a margin: each unmask is a new edge
 * if anything is pending.
 *
 * A2A channel 0 is used for nothing else (the SD driver uses channel 1),
 * so it is set up once, by core_sleep_init(). The DWDMA cannot reach the
 * INTC; borrowing the HDMA channel playback did not use hung the player as
 * soon as playback started. */
#define A2A_CON_UNMASK      ((1<<5) | /* fixed destination address */ \
                             (1<<3) | /* DMA start */ \
                             (2<<1) | /* word transfer size */ \
                             (1<<0))  /* software mode */

/* written to INTC_IMR one after the other (~1.6 us per word at 50 MHz
 * HCLK): 7 zeros, the mask, zero, the mask */
static unsigned long core_sleep_imr[10] __attribute__((aligned(16)));

static void core_sleep_init(void)
{
    SCU_CLKCFG &= ~CLKCFG_A2A;
    A2A_DOMAIN = 0;                 /* both channels on AHB0 */
    A2A_ISRC0 = (unsigned long)core_sleep_imr;
    A2A_IDST0 = (unsigned long)&INTC_IMR;
    A2A_ICNT0 = sizeof(core_sleep_imr);
    A2A_LCNT0 = 1;
}

void rk27xx_core_sleep(void)
{
    unsigned long imr = INTC_IMR;

    if (imr != 0)                   /* else nothing could wake the core */
    {
        core_sleep_imr[7] = imr;
        core_sleep_imr[9] = imr;
        /* the buffer must be in memory before the DMA reads it */
        asm volatile ("" : : : "memory");

        /* nothing between starting the DMA and SCU_CPUPD: it eats into
         * the delay */
        INTC_IMR = 0;
        A2A_CON0 = A2A_CON_UNMASK;

        /* reading the register back makes sure the write has reached the
         * SCU (ARMv5 has no barrier instruction); the core stops a few
         * cycles later, before it gets past the next instructions */
        SCU_CPUPD = 0xdeedbabe;
        (void)SCU_CPUPD;

        /* the INTC must be unmasked again before IRQs are. The channel
         * reads as idle once it has read its last word; the write of that
         * word to INTC_IMR may still be on its way. */
        while (A2A_DMA_STS & 1);
        for (int n = 1000; INTC_IMR != imr && n > 0; n--);
    }
    enable_irq();
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

    delay = (usecs * cycles_per_usec) / 4;
    if (delay == 0)                 /* the loop below would run 2^32 times */
        return;

    asm volatile(
        "1: subs %0, %0, #1  \n"    /* 1 cycle  */
        "   bne  1b          \n"    /* 3 cycles */
        : "+r"(delay)               /* modified by the loop: in/out */
	:
	: "cc"                      /* flags clobbered */
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
void commit_discard_idcache(void)
{
}
void commit_discard_dcache (void) __attribute__((alias("commit_discard_idcache")));

void commit_discard_dcache_range (const void *base, unsigned int size)
{
    /* 0x01 is opcode for cache line commit discard */
    uint32_t end_opcode = (uint32_t)((uintptr_t)base + size) | 0x01;
    uint32_t opcode = (uint32_t)((uintptr_t)base & 0xffffffe0) | 0x01;

#ifdef COLORFLY_C4_PRO
    /* Batch IRQ-masked cache-line ops into 16-line (512B) windows and wait for
     * CACHEOP completion before unmasking so INT_HDMA is never starved by a
     * 4,800-line full-framebuffer flush nor able to race an in-flight CACHEOP. */
    while (opcode <= end_opcode)
    {
        int old_irq = disable_irq_save();
        uint32_t batch_end;
        if (end_opcode - opcode < (16u * 32u))
            batch_end = end_opcode;
        else
            batch_end = opcode + (15u * 32u);

        while (opcode <= batch_end)
        {
            C4PRO_CACHE_WAIT();
            CACHEOP = opcode;
            opcode += 32;
        }
        C4PRO_CACHE_WAIT();
        restore_irq(old_irq);
    }
#else
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
#endif
}

#if !defined(BOOTLOADER) && defined(HAVE_ADJUSTABLE_CPU_FREQ)
static inline void set_sdram_timing(int ahb_freq)
{
    /* refresh only: the mode register - CAS latency - stays as the boot set
     * it (see system_init()) */
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
        /* the LCD strobes take twice the clocks at twice the clock */
        lcdif_set_bus_timing(true);

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

        lcdif_set_bus_timing(false);
    }

    cpu_frequency = frequency;
}
#endif
