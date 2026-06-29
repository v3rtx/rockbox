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
#include "cpu.h"
#include "string.h"
#include "usb.h"
#include "usb_drv.h"
#include "usb_core.h"
#include "system.h"
#include "system-target.h"

int usb_status = USB_EXTRACTED;

void usb_init_device(void)
{
    SCU_CLKCFG &= ~CLKCFG_UDC;
#if !defined(COLORFLY_C4_PRO) || defined(C4PRO_USB_STOCK_PATH)
    /* Hard-reset the UDC controller. NOT done on C4PRO: the BootROM already
     * brought the UDC/PHY up to a working (high-speed-capable) state, and
     * pulsing RSTCFG_UDC wipes that analog/PHY setup, leaving corrupted
     * signaling (host "error -71", full-speed only). The stock RK27xx loader
     * likewise never re-pulses RSTCFG_UDC (its udc_reset routine is dead code),
     * relying on the BootROM init - so match that and only ungate the clock. */
    SCU_RSTCFG |= RSTCFG_UDC;
    udelay(10000);
    SCU_RSTCFG &= ~RSTCFG_UDC;
    udelay(10000);
#endif

    /* enable UDC interrupt */
    INTC_IMR |= (1<<16);
    INTC_IECR |= (1<<16);

    EN_INT = EN_SUSP_INTR   |  /* Enable Suspend Interrupt */
             EN_RESUME_INTR |  /* Enable Resume Interrupt */
             EN_USBRST_INTR |  /* Enable USB Reset Interrupt */
             EN_OUT0_INTR   |  /* Enable OUT Token receive Interrupt EP0 */
             EN_IN0_INTR    |  /* Enable IN Token transmits Interrupt EP0 */
             EN_SETUP_INTR;    /* Enable SETUP Packet Receive Interrupt */

    /* configure INTCON */
    INTCON = UDC_INTHIGH_ACT |  /* interrupt high active */
             UDC_INTEN;         /* enable EP0 interrupts */

    usb_drv_startup();
}

void usb_attach(void)
{
    usb_enable(true);
}

void usb_enable(bool on)
{
    if(on)
        usb_core_init();
    else
        usb_core_exit();
}
