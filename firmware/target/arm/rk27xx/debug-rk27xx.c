/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright © 2008 Rafaël Carré
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

#include <stdbool.h>
#include "config.h"
#include "system.h"
#include "kernel.h"
#include "button.h"
#include "lcd.h"
#include "adc.h"
#include "font.h"
#include "storage.h"
#include "lcd-target.h"

#if defined(RK27_GENERIC)
#define DEBUG_CANCEL BUTTON_VOL
#elif defined(SAMSUNG_YPCP3)
#define DEBUG_CANCEL BUTTON_BACK
#elif defined(HM60X) || defined(HM801) || (CONFIG_KEYPAD == MA_PAD) || \
    (CONFIG_KEYPAD == IHIFI_PAD)
#define DEBUG_CANCEL BUTTON_LEFT
#elif (CONFIG_KEYPAD == IHIFI_770_PAD) || (CONFIG_KEYPAD == IHIFI_800_PAD)
#define DEBUG_CANCEL BUTTON_POWER
#endif

/*  Skeleton for adding target specific debug info to the debug menu
 */

#define _DEBUG_PRINTF(a, varargs...) lcd_putsf(0, line++, (a), ##varargs);

#if (CONFIG_STORAGE & STORAGE_SD)
extern unsigned long sd_debug_time_rd;
extern unsigned long sd_debug_time_wr;
#endif
extern enum lcd_type_t lcd_type;

bool dbg_hw_info(void)
{
    int line;
    lcd_clear_display();
    lcd_setfont(FONT_SYSFIXED);

    while(1)
    {
        lcd_clear_display();
        line = 0;

        /* _DEBUG_PRINTF statements can be added here to show debug info */
        _DEBUG_PRINTF("SCU_ID:      0x%0x", SCU_ID);
        _DEBUG_PRINTF("SCU_PLLCON1: 0x%0x", SCU_PLLCON1);
        _DEBUG_PRINTF("SCU_PLLCON2: 0x%0x", SCU_PLLCON2);
        _DEBUG_PRINTF("SCU_PLLCON3: 0x%0x", SCU_PLLCON3);
        _DEBUG_PRINTF("SCU_DIVCON1: 0x%0x", SCU_DIVCON1);
        _DEBUG_PRINTF("SCU_CLKCFG:  0x%0x", SCU_CLKCFG);
        _DEBUG_PRINTF("SCU_CHIPCFG: 0x%0x", SCU_CHIPCFG);

#ifdef HM60X
        _DEBUG_PRINTF("LCD type: %s", lcd_type == LCD_V1 ? "V1 (HX8340b)": "V2");
#endif
        line++;
#if (CONFIG_STORAGE & STORAGE_SD)
        _DEBUG_PRINTF("sd_debug_time_rd: %d", sd_debug_time_rd);
        _DEBUG_PRINTF("sd_debug_time_wr: %d", sd_debug_time_wr);
#endif
        for(int i = 0; i < 4; i++)
        {
            unsigned long memmap = *(&MEMMAPA + i);
            unsigned addr = memmap & 0xff000000;
            unsigned size = memmap & 0xff;
            const char *size_name;
            switch(size)
            {
                case 0:
                    size = 0;
                    size_name = "invalid";
                    break;
                case 0xfe:
                    size = 32 * 1024 * 1024;
                    size_name = "32MB";
                    break;
                case 0xfc:
                    size = 64 * 1024 * 1024;
                    size_name = "64MB";
                    break;
                case 0xf8:
                    size = 128 * 1024 * 1024;
                    size_name = "128MB";
                    break;
                default:
                    size = 0;
                    size_name = "unk";
                    break;
            }
            _DEBUG_PRINTF("Uncached %c: [0x%x,0x%x[ (size=0x%x / %s)",
                'A' + i, addr, addr + size, size, size_name);
        }
        lcd_update(); 
        switch(button_get_w_tmo(HZ/20))
        {
            case DEBUG_CANCEL:
            case BUTTON_REL:
                lcd_setfont(FONT_UI);
                return false;
        }
    }

    lcd_setfont(FONT_UI);
    return false;
}

bool dbg_ports(void)
{
    int line;

    lcd_setfont(FONT_SYSFIXED);

    while(1)
    {
        lcd_clear_display();
        line = 0;
        
        _DEBUG_PRINTF("GPIO_PADR:  %02x",(unsigned char)GPIO_PADR);
        _DEBUG_PRINTF("GPIO_PACON: %02x",(unsigned char)GPIO_PACON);
        _DEBUG_PRINTF("GPIO_PBDR:  %02x",(unsigned char)GPIO_PBDR);
        _DEBUG_PRINTF("GPIO_PBCON: %02x",(unsigned char)GPIO_PBCON);
        _DEBUG_PRINTF("GPIO_PCDR:  %02x",(unsigned char)GPIO_PCDR);
        _DEBUG_PRINTF("GPIO_PCCON: %02x",(unsigned char)GPIO_PCCON);
        _DEBUG_PRINTF("GPIO_PDDR:  %02x",(unsigned char)GPIO_PDDR);
        _DEBUG_PRINTF("GPIO_PDCON: %02x",(unsigned char)GPIO_PDCON);
        _DEBUG_PRINTF("GPIO_PEDR:  %02x",(unsigned char)GPIO_PEDR);
        _DEBUG_PRINTF("GPIO_PECON: %02x",(unsigned char)GPIO_PECON);
        _DEBUG_PRINTF("GPIO_PFDR:  %02x",(unsigned char)GPIO_PFDR);
        _DEBUG_PRINTF("GPIO_PFCON: %02x",(unsigned char)GPIO_PFCON);
        _DEBUG_PRINTF("ADC0: %d", adc_read(0));
        _DEBUG_PRINTF("ADC1: %d", adc_read(1));
        _DEBUG_PRINTF("ADC2: %d", adc_read(2));
        _DEBUG_PRINTF("ADC3: %d", adc_read(3));

        lcd_update();
        if (button_get_w_tmo(HZ/10) == (DEBUG_CANCEL|BUTTON_REL))
            break;
    }
    lcd_setfont(FONT_UI);
    return false;
}

#if defined(COLORFLY_C4_PRO)
/* S/PDIF DAC-input probe. Puts the C4 into "external DAC" mode (coax S/PDIF
 * on an RCA jack -> CS8422 receiver -> async SRC -> CS4398 -> analog out) and
 * shows the CS8422 lock/rate/error status live. The physical RX pin the coax
 * jack is wired to is undocumented, so RIGHT/LEFT cycle candidate Receiver
 * Input Control (03h) values -- feed a coax source and watch for PLL/RX lock
 * to identify the wiring. BACK exits and powers the DAC back down. */
extern int  c4pro_cs8422_read(unsigned char reg);
extern void c4pro_spdif_dac_enter(unsigned char rx_input_ctl);
extern void c4pro_spdif_dac_set_rx(unsigned char rx_input_ctl);
extern void c4pro_spdif_dac_exit(void);

bool dbg_spdif_dac(void)
{
    static const struct { unsigned char reg03; const char *name; } cand[] = {
        { 0x80, "SE   RX0  comp" },
        { 0xA0, "SE   RX2  comp" },
        { 0x00, "DIFF RXP0/RXN0" },
        { 0x40, "DIFF RXP1/RXN1" },
        { 0x84, "SE   RX0  dig " },
    };
    static const char * const rate[4] = { "??", "32k", "44.1k", "48k" };
    const int ncand = sizeof(cand) / sizeof(cand[0]);
    int sel = 0;
    int line;

    c4pro_spdif_dac_enter(cand[sel].reg03);
    lcd_setfont(FONT_SYSFIXED);

    while (1)
    {
        int r13 = c4pro_cs8422_read(0x13);   /* Receiver Error   */
        int r15 = c4pro_cs8422_read(0x15);   /* PLL Status       */
        int r16 = c4pro_cs8422_read(0x16);   /* Receiver Status  */

        lcd_clear_display();
        line = 0;
        _DEBUG_PRINTF("S/PDIF DAC input");
        _DEBUG_PRINTF("RX 03h=%02x %s", cand[sel].reg03, cand[sel].name);
        line++;
        _DEBUG_PRINTF("PLL_LOCK : %d", (r15 >> 5) & 1);
        _DEBUG_PRINTF("RX_LOCK  : %d", (r16 >> 4) & 1);
        _DEBUG_PRINTF("RCVR_RATE: %s", rate[(r16 >> 5) & 3]);
        _DEBUG_PRINTF("96k/192k : %d/%d", (r15 >> 4) & 1, (r15 >> 3) & 1);
        _DEBUG_PRINTF("RXerr 13h: %02x", r13 & 0xff);
        line++;
        _DEBUG_PRINTF("RIGHT/LEFT: cycle RX");
        _DEBUG_PRINTF("BACK: exit");
        lcd_update();

        int btn = button_get_w_tmo(HZ/5);
        if (btn & BUTTON_REL)
            continue;                        /* ignore release edges */
        if (btn & BUTTON_BACK)
            break;
        if ((btn & BUTTON_RIGHT) || (btn & BUTTON_NEXT))
        {
            sel = (sel + 1) % ncand;
            c4pro_spdif_dac_set_rx(cand[sel].reg03);
        }
        else if ((btn & BUTTON_LEFT) || (btn & BUTTON_PREV))
        {
            sel = (sel + ncand - 1) % ncand;
            c4pro_spdif_dac_set_rx(cand[sel].reg03);
        }
    }

    c4pro_spdif_dac_exit();
    lcd_setfont(FONT_UI);
    return false;
}
#endif /* COLORFLY_C4_PRO */
