/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2010 by Bertrik Sikken
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
#include "ftl-target.h"
#include "nand-target.h"
#include "string.h"

#ifdef COLORFLY_C4_PRO
/* Saved raw/logical dumps from the C4 show the system image as a contiguous
 * read-only window: logical LBA N maps to raw physical sector 20480 + N.
 */
#define C4PRO_SYSTEM_PSEC_BASE 20480
#define C4PRO_SYSTEM_SECTORS   43008
#define C4PRO_SECTORS_PER_PAGE 8

static unsigned char page_data[4096];
static unsigned char page_spare[128];
#endif

uint32_t ftl_init(void)
{
    if (nand_device_init())
        return 1;

    ftl_nand_type = nand_get_device_type(0);
    if (!ftl_nand_type)
        return 1;

    ftl_banks = 1;
    return 0;
}

uint32_t ftl_read(uint32_t sector, uint32_t count, void* buffer)
{
#ifdef COLORFLY_C4_PRO
    unsigned char *out = buffer;
    uint32_t cached_page = 0xffffffff;

    if (sector > C4PRO_SYSTEM_SECTORS ||
        count > C4PRO_SYSTEM_SECTORS - sector)
        return 1;

    while (count > 0)
    {
        uint32_t psec = C4PRO_SYSTEM_PSEC_BASE + sector;
        uint32_t page = psec / C4PRO_SECTORS_PER_PAGE;
        uint32_t sector_in_page = psec & (C4PRO_SECTORS_PER_PAGE - 1);

        if (page != cached_page)
        {
            if (nand_read_page(0, page, page_data, page_spare, 1, 0))
                return 1;

            cached_page = page;
        }

        memcpy(out, page_data + sector_in_page * 512, 512);
        out += 512;
        sector++;
        count--;
    }

    return 0;
#else
    (void)sector;
    (void)count;
    (void)buffer;
    return 1;
#endif
}

uint32_t ftl_write(uint32_t sector, uint32_t count, const void* buffer)
{
    (void)sector;
    (void)count;
    (void)buffer;
    return 1;
}

uint32_t ftl_sync(void)
{
    return 0;
}
