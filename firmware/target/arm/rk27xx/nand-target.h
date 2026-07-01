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
uint32_t c4pro_nand_read_sector(uint32_t bank, uint32_t page,
                                uint32_t sector, void* databuffer,
                                void* sparebuffer);
uint32_t c4pro_nand_read_linear_sector(uint32_t bank, uint32_t page,
                                       uint32_t sector, void* databuffer);
uint32_t c4pro_nand_read_linear_page(uint32_t bank, uint32_t page,
                                     void* databuffer);
uint32_t c4pro_nand_read_linear_page_spare(uint32_t bank, uint32_t page,
                                           void* databuffer, void* sparebuffer);
uint32_t c4pro_nand_read_page_flctl(uint32_t bank, uint32_t page,
                                    void* databuffer, uint32_t flctl_base);
extern uint32_t c4pro_nand_fmwait;        /* tunable NAND wait-state timing */
extern uint32_t c4pro_nand_delay_iters;        /* read-path settle busy-wait */
extern uint32_t c4pro_nand_prog_delay_iters;   /* program/erase settle busy-wait */
void c4pro_nand_clock_regs(uint32_t out[4]);   /* live PLL/divider/clkcfg snapshot */
extern uint32_t c4pro_nand_last_bchst;         /* BCHST OR from last hwecc read */
#ifdef C4PRO_NAND_DMA
uint32_t c4pro_nand_read_linear_page_spare_dma(uint32_t bank, uint32_t page,
                                               void* databuffer,
                                               void* sparebuffer);
uint32_t c4pro_nand_dma_selftest_page(uint32_t bank, uint32_t page);
uint32_t c4pro_nand_read_native_stream_dma(uint32_t bank, uint32_t page,
                                           void* databuffer);
#endif
#ifdef C4PRO_NAND_DMA_WRITE
uint32_t c4pro_nand_write_page_dma(uint32_t bank, uint32_t page,
                                   const void* databuffer,
                                   const void* sparebuffer);
#endif
uint32_t c4pro_nand_write_page(uint32_t bank, uint32_t page,
                               const void* databuffer,
                               const void* sparebuffer);
uint32_t c4pro_nand_write_page_hwecc(uint32_t bank, uint32_t page,
                                     const void* databuffer, uint32_t nsec);
uint32_t c4pro_nand_read_page_hwecc(uint32_t bank, uint32_t page,
                                    void* databuffer, uint32_t nsec);
uint32_t c4pro_nand_program_bytes(uint32_t bank, uint32_t page,
                                  uint32_t column,
                                  const void* databuffer,
                                  uint32_t count);
uint32_t c4pro_nand_erase_block(uint32_t bank, uint32_t page);
/* Multi-die concurrent block erase: issues the erase command to up to 4 dies
 * (one block each) and only then polls every die's R/B# to completion, so the
 * per-die tERASE windows overlap. banks[i]/pages[i] describe block i; n<=4.
 * Returns a bitmask where bit i is set if block i failed to erase. */
uint32_t c4pro_nand_erase_blocks_multi(const uint32_t *banks,
                                       const uint32_t *pages, uint32_t n);
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
