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
#include "config.h"
#include "system.h"
#include "kernel.h"

/*  rockchip rk27xx driver for the kernel timer */

#ifdef C4PRO_BOOT_HEARTBEAT
/* Liveness heartbeat: bumped every timer IRQ. If this stops advancing while the
 * UI-loop counter keeps painting (see apps/main.c), the kernel tick died
 * (INT_TIMER0 starved by the INT_UDC storm) - the root-cause hypothesis. */
volatile unsigned long c4pro_hb_tick;
#endif

/* sys timer ISR */
void INT_TIMER0(void)
{
    /* clear interrupt */
    TMR0CON &= ~0x04;

#ifdef C4PRO_BOOT_HEARTBEAT
    c4pro_hb_tick++;
#endif

#ifdef C4PRO_WDT_CRASH_CAPTURE
    /* JTAG-free crash capture (docs/c4-usb-msc-write-freeze.md s6 option 2).
     * The sustained-write freeze is a confirmed CPU-level lockup (T=D frozen
     * across reads, 2026-07-02 clean test); a hardware WDT is independent of
     * the CPU/tick and fires even when they're dead. Every tick, while
     * armed: (1) stash the interrupted PC (already captured on every IRQ
     * entry by irq_handler, system-rk27xx.c) + a magic value into a fixed
     * DRAM record -- confirmed 2026-07-02 to survive a WDT-triggered reset
     * (unlike the slow human CE#-short replug, which does NOT preserve
     * DRAM); (2) pet the WDT by reloading WDTLR. The instant the tick stops
     * (the freeze itself), petting stops, the already-armed countdown
     * expires on its own, and the chip resets with the last-written PC
     * intact in DRAM for the next boot to read out.
     *
     * WDTLR reload-on-write is assumed (SP805-style: writing the load
     * register also reloads the counter) -- unverified for RK27xx
     * specifically (no datasheet).
     *
     * FIELD-CORRECTED 2026-07-02: LR=1 (the exact value system_reboot() uses
     * to fire) turned out to expire in UNDER one tick period -- using it for
     * the ongoing pet caused an immediate boot loop (WDT fired before even
     * the first tick could write the magic, so no crash record was ever
     * captured, and every boot just re-armed and died again). Fixed to a
     * much larger load value, based on the one documented clock reference in
     * this codebase for a sibling APB0 peripheral (tick_start()'s TMR0LR =
     * 50000 * ms, i.e. a 50 MHz base) -- if the WDT shares that clock, this
     * is roughly 1 second per pet, comfortably longer than a ~10ms tick. */
    {
        extern volatile bool c4pro_wdt_capture_enable;
        extern volatile unsigned long c4pro_irq_interrupted_pc;
        extern volatile uint32_t c4pro_hb_phase;

        if (c4pro_wdt_capture_enable)
        {
            volatile unsigned long *rec =
                (volatile unsigned long *)C4PRO_WDT_CAPTURE_ADDR;

            rec[0] = C4PRO_WDT_CAPTURE_MAGIC;
            rec[1] = c4pro_irq_interrupted_pc;
            rec[2] = (unsigned long)current_tick;
            /* Last write-path stage the usb thread reached (see the phase legend
             * in ftl-rk27xx.c). On a silent AHB-stall freeze the PC only ever
             * samples innocent scheduler code, so this phase is what actually
             * localizes the wedged stage. */
            rec[3] = (unsigned long)c4pro_hb_phase;

            WDTLR = C4PRO_WDT_CAPTURE_RELOAD;
        }
    }
#endif

    call_tick_tasks();  /* Run through the list of tick tasks */
}

/* this assumes 50MHz APB bus frequency */
void tick_start(unsigned int interval_in_ms)
{
    unsigned int cycles = 50000 * interval_in_ms;
    
    /* enable timer clock */
    SCU_CLKCFG &= ~CLKCFG_TIMER;
    
    /* configure timer0 */
    TMR0LR = cycles;
    TMR0CON = (1<<8) | (1<<7) | (1<<1); /* periodic, 1/1, interrupt enable */

    /* unmask timer0 interrupt */
    INTC_IMR |= IRQ_ARM_TIMER0;

    /* enable timer0 interrupt */
    INTC_IECR |= IRQ_ARM_TIMER0;
}

