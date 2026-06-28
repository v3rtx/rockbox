/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
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

#ifndef __NAND_TARGET_H__
#define __NAND_TARGET_H__

#include "config.h"
#include "inttypes.h"


struct nand_device_info_type
{
    uint32_t id;
    uint16_t blocks;
    uint16_t userblocks;
    uint16_t pagesperblock;
    uint8_t blocksizeexponent;
    uint8_t tunk1;
    uint8_t twp;
    uint8_t tunk2;
    uint8_t tunk3;
} __attribute__((packed));

uint32_t nand_read_page(uint32_t bank, uint32_t page, void* databuffer,
                        void* sparebuffer, uint32_t doecc,
                        uint32_t checkempty);
#ifdef COLORFLY_C4_PRO
uint32_t c4pro_nand_read_id(uint32_t bank, unsigned char id[5]);
uint32_t c4pro_nand_read_id_window(uint32_t bank, uint32_t window,
                                   unsigned char id[5]);
uint32_t c4pro_nand_read_id_mask(uint32_t mask, uint32_t window,
                                 unsigned char id[5]);
uint32_t c4pro_nand_read_sector(uint32_t bank, uint32_t page,
                                uint32_t sector, void* databuffer,
                                void* sparebuffer);
uint32_t c4pro_nand_read_linear_sector(uint32_t bank, uint32_t page,
                                       uint32_t sector, void* databuffer);
uint32_t c4pro_nand_read_linear_page(uint32_t bank, uint32_t page,
                                     void* databuffer);
uint32_t c4pro_nand_read_linear_page_mask(uint32_t mask, uint32_t window,
                                          uint32_t page, void* databuffer);
uint32_t c4pro_nand_write_page(uint32_t bank, uint32_t page,
                               const void* databuffer,
                               const void* sparebuffer);
uint32_t c4pro_nand_program_bytes(uint32_t bank, uint32_t page,
                                  uint32_t column,
                                  const void* databuffer,
                                  uint32_t count);
uint32_t c4pro_nand_erase_block(uint32_t bank, uint32_t page);
#endif
uint32_t nand_write_page(uint32_t bank, uint32_t page, void* databuffer,
                         void* sparebuffer, uint32_t doecc);
uint32_t nand_block_erase(uint32_t bank, uint32_t page);

const struct nand_device_info_type* nand_get_device_type(uint32_t bank);
uint32_t nand_reset(uint32_t bank);
uint32_t nand_device_init(void);
void nand_set_active(void);
long nand_last_activity(void);
void nand_power_up(void);
void nand_power_down(void);


#endif
