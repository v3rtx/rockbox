/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2006 by Thom Johansen
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
#include <stdio.h>
#include "lcd.h"
#include "font.h"
#include "gcc_extensions.h"

#include <get_sp.h>
#include <backtrace.h>

static const char* const uiename[] = {
    "Undefined instruction",
    "Prefetch abort",
    "Data abort",
    "Divide by zero",
    "SWI"
};

void __attribute__((weak,naked)) data_abort_handler(void)
{
    asm volatile(
        "sub    r0, lr, #8  \n"
        "mov    r1, #2      \n"
        "b      UIE         \n"
        );
}

void __attribute__((weak,naked)) software_int_handler(void)
{
    asm volatile(
        "sub    r0, lr, #4  \n"
        "mov    r1, #4      \n"
        "b      UIE         \n"
        );
}

void __attribute__((weak,naked)) reserved_handler(void)
{
    asm volatile(
        "sub    r0, lr, #4  \n"
        "mov    r1, #4      \n"
        "b      UIE         \n"
        );
}

void __attribute__((weak,naked)) prefetch_abort_handler(void)
{
    asm volatile(
        "sub    r0, lr, #4  \n"
        "mov    r1, #1      \n"
        "b      UIE         \n"
        );
}

void __attribute__((weak,naked)) undef_instr_handler(void)
{
    asm volatile(
        "sub    r0, lr, #4    \n"
#ifdef USE_THUMB
        "mrs    r1, spsr      \n"
        "tst    r1, #(1 << 5) \n" // T bit set ?
        "subne  r0, lr, #2    \n" // if yes, offset to THUMB instruction
#endif
        "mov    r1, #0        \n"
        "b      UIE           \n"
        );
}

/* Unexpected Interrupt or Exception handler. Currently only deals with
   exceptions, but will deal with interrupts later.
 */
void NORETURN_ATTR UIE(unsigned int pc, unsigned int num)
{
    /* safe guard variable - we call backtrace() only on first
     * UIE call. This prevent endless loop if backtrace() touches
     * memory regions which cause abort
     */
    static bool triggered = false;

#if LCD_DEPTH > 1
    lcd_set_backdrop(NULL);
    lcd_set_drawinfo(DRMODE_SOLID, LCD_BLACK, LCD_WHITE);
#endif
    unsigned line = 0;

    lcd_setfont(FONT_SYSFIXED);
    lcd_set_viewport(NULL);
    lcd_clear_display();
    lcd_putsf(0, line++, "%s at %08x" IF_COP(" (%d)"), uiename[num], pc IF_COP(, CURRENT_CORE));

#if defined(C4PRO_FAULT_VISIBLE) && (CONFIG_CPU == RK27XX)
    /* Make a CPU fault VISIBLE on the C4 Pro. During a sustained USB copy a hang
     * leaves a black/frozen screen that is ambiguous between "CPU fault -> UIE"
     * and "masked-IRQ busy-spin". Here we FORCE the backlight/PWM on, paint a
     * high-contrast fault frame (exception name + faulting PC) and LOOP
     * REDRAWING it forever. We do NOT call system_exception_wait() (it spins on
     * a button that is invisible here) and we do NOT read CP15 FSR/FAR (the
     * RK27XX ARM7 core has no CP15 coprocessor -> that access would itself fault
     * inside the fault handler). Interrupts stay disabled (fault state), but the
     * LCD path is pure polling (lcdif_wait_*), so lcd_update works here.
     *
     * Outcome after the next hang:
     *   - This frame appears  -> it WAS a CPU fault; map PC with:
     *        arm-none-eabi-addr2line -f -e build-c4pro/rockbox.elf <PC>
     *   - Screen keeps the frozen heartbeat (no fault frame) -> UIE was never
     *     reached; the hang is a masked-IRQ busy-spin, not a fault. */
    {
        extern void backlight_hw_on(void);
        extern void backlight_hw_brightness(int brightness);
        extern volatile uint32_t c4pro_hb_phase;      /* last write-path stage */
        /* Persisted fault record. Written BEFORE any call that might itself
         * fault, so the values survive even if the paint below double-faults. */
        extern volatile uint32_t c4pro_fault_count;
        extern volatile uint32_t c4pro_fault_pc;
        extern volatile uint32_t c4pro_fault_num;
        extern volatile uint32_t c4pro_fault_phase;
        static int reentry;

        disable_interrupt(IRQ_FIQ_STATUS);

        /* Record FIRST (no function calls, no dereferences that can re-fault). */
        c4pro_fault_pc    = pc;
        c4pro_fault_num   = num;
        c4pro_fault_phase = c4pro_hb_phase;
        c4pro_fault_count++;

        /* Re-entry guard: if painting the fault frame itself faults (e.g.
         * lcd_update touches memory that also aborts), we land back here in
         * abort mode. On the 2nd+ entry, skip the (faulting) LCD path and just
         * hold the backlight on + spin, so we don't recurse the abort forever
         * (which looks identical to a masked-IRQ lockup). */
        if (++reentry >= 2)
        {
            for (;;)
                backlight_hw_on();   /* pure register writes; keep screen lit */
        }

        /* Format PC/PH into STATIC buffers with a MINIMAL hex routine - NO
         * lcd_putsf()/vsnprintf(), because the RK27XX abort mode shares the tiny
         * 0x400 (1 KB) IRQ stack (crt0.S), and vsnprintf's large format buffer
         * overflowed it -> the paint itself data-aborted (the "double fault"
         * that left only the frozen heartbeat). Static buffers + lcd_puts keep
         * abort-context stack use tiny so the frame actually paints. */
        {
            static const char hx[] = "0123456789abcdef";
            static char pcbuf[16];   /* "PC=xxxxxxxx" */
            static char phbuf[16];   /* "PH=xx CNT=xx" */
            int i;

            pcbuf[0] = 'P'; pcbuf[1] = 'C'; pcbuf[2] = '=';
            for (i = 0; i < 8; i++)
                pcbuf[3 + i] = hx[(pc >> ((7 - i) * 4)) & 0xf];
            pcbuf[11] = 0;

            phbuf[0] = 'P'; phbuf[1] = 'H'; phbuf[2] = '=';
            phbuf[3] = hx[(c4pro_fault_phase >> 4) & 0xf];
            phbuf[4] = hx[c4pro_fault_phase & 0xf];
            phbuf[5] = ' '; phbuf[6] = 'N'; phbuf[7] = '='; phbuf[8] = 'r';
            /* N=r reminds: read c4pro_fault_num for the exception type */
            phbuf[9] = 0;

            for (;;)
            {
                backlight_hw_on();
                backlight_hw_brightness(MAX_BRIGHTNESS_SETTING);
#if LCD_DEPTH > 1
                lcd_set_backdrop(NULL);
                lcd_set_drawinfo(DRMODE_SOLID, LCD_BLACK, LCD_WHITE);
#endif
                lcd_setfont(FONT_SYSFIXED);
                lcd_set_viewport(NULL);
                lcd_clear_display();
                lcd_puts(0, 0, "*** CPU FAULT ***");
                lcd_puts(0, 2, uiename[num]);   /* literal string, no format */
                lcd_puts(0, 3, pcbuf);
                lcd_puts(0, 5, phbuf);
                lcd_puts(0, 6, pcbuf);          /* repeated for legibility */
                lcd_update();
                udelay(200000);   /* ~0.2s; IRQs off, cannot sleep() */
            }
        }
    }
#endif /* C4PRO_FAULT_VISIBLE && RK27XX */

#if !defined(CPU_ARM7TDMI) && (CONFIG_CPU != RK27XX) /* arm7tdmi has no MPU/MMU */
    if(num == 1 || num == 2) /* prefetch / data abort */
    {
        register unsigned status;

#if ARM_ARCH >= 6
        /* ARMv6 has 2 different registers for prefetch & data aborts */
        if(num == 1)    /* instruction prefetch abort */
            asm volatile( "mrc p15, 0, %0, c5, c0, 1\n" : "=r"(status));
        else
#endif
            asm volatile( "mrc p15, 0, %0, c5, c0, 0\n" : "=r"(status));

        lcd_putsf(0, line++, "FSR 0x%x", status);

        unsigned int domain = (status >> 4) & 0xf;
        unsigned int fault = status & 0xf;
#if ARM_ARCH >= 6
        fault |= (status & (1<<10)) >> 6; /* fault is 5 bits on armv6 */
#endif
        lcd_putsf(0, line++, "(domain %d, fault %d)", domain, fault);

        if(num == 2) /* data abort */
        {
            register unsigned address;
            /* read FAR (fault address register) */
            asm volatile( "mrc p15, 0, %0, c6, c0\n" : "=r"(address));
            lcd_putsf(0, line++, "address 0x%8x", address);
#if ARM_ARCH >= 6
            lcd_putsf(0, line++, (status & (1<<11)) ? "(write)" : "(read)");
#endif
        }
    }   /* num == 1 || num == 2 // prefetch/data abort */
#endif /* !defined(CPU_ARM7TDMI */

#ifdef HAVE_RB_BACKTRACE
    if (!triggered)
    {
        triggered = true;
        rb_backtrace(pc, __get_sp(), &line);
    }
#else
    (void)triggered;
#endif

    lcd_update();

    disable_interrupt(IRQ_FIQ_STATUS);

    system_exception_wait(); /* If this returns, try to reboot */
    system_reboot();
    while (1);       /* halt */
}

/* Needs to be here or gcc won't find it */
void __attribute__((naked)) __div0(void)
{
    asm volatile (
        "ldr    r0, [sp]    \r\n"
        "sub    r0, r0, #4  \r\n"
        "mov    r1, #3      \r\n"
        "b      UIE         \r\n"
    );
}
