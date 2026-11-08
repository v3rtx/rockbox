/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2009 by Michael Sparmann
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

#ifndef __FTL_TARGET_H__
#define __FTL_TARGET_H__

#include "config.h"
#include "inttypes.h"
#include <stdbool.h>

/* The drives the NAND presents: USER, and SYS only when the target exposes
 * it - see HAVE_RK27XX_NAND_SYS in the target config. */
#ifdef HAVE_RK27XX_NAND_SYS
#define FTL_DRIVE_SYS   0
#define FTL_DRIVE_USER  1
#define FTL_NUM_DRIVES  2
#else
#define FTL_DRIVE_USER  0
#define FTL_NUM_DRIVES  1
#endif

#if !defined(COLORFLY_C4_PRO) && !defined(HAVE_RK27XX_NAND_SYS) && !defined(BOOTLOADER) \
    && defined(USB_ENABLE_STORAGE)
/* Without SYS as a drive of its own, the debug menu can ask for it to be
 * shown to a USB host in place of USER, for one connection: the next one,
 * or the current one if none has started. Kept in RAM only. */
#define FTL_SYS_ON_USB
void ftl_set_sys_on_usb(bool on);
bool ftl_get_sys_on_usb(void);
#endif

#ifdef COLORFLY_C4_PRO
/* Pointer to an info structure regarding the flash type used */
extern const struct nand_device_info_type* ftl_nand_type;
/* Number of banks we detected a chip on */
extern uint32_t ftl_banks;

uint32_t ftl_init(void);
uint32_t ftl_read(uint32_t sector, uint32_t count, void* buffer);
uint32_t ftl_write(uint32_t sector, uint32_t count, const void* buffer);
uint32_t ftl_sync(void);
uint32_t ftl_total_sectors(void);
#else
uint32_t ftl_init(void);
uint32_t ftl_read(int drive, uint32_t sector, uint32_t count, void* buffer);
uint32_t ftl_write(int drive, uint32_t sector, uint32_t count,
                   const void* buffer);
uint32_t ftl_sync(void);

/* Usable sectors on a logical disk, 0 if not mounted. Comes from the FTL's
 * own tables, not from raw block geometry - see ftl-rk27xx.c. */
uint32_t ftl_get_sectors(int drive);

/* Whether a drive shows the SYS volume right now */
bool ftl_drive_is_sys(int drive);
#endif

#endif
