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
#include "system.h"
#include "kernel.h"
#include "string.h"
#include "nand-target.h"

#if 0
/* This is for documentation purpose as FTL has not been reverse engineered yet
 * Raw nand handling functions based on OF disassembly and partially inspired
 * by Rockchip patent
 */

#define MAX_FLASH_NUM 4
#define CMD_READ_STATUS 0x70
#define CMD_RESET       0xFF
#define CMD_READ_ID     0x90
#define READ_PAGE_CMD   0x30

/* this is the struct OF uses */
struct flashspec_t
{
    uint8_t  cache_prog;
    uint8_t  mul_plane;
    uint8_t  interleave;
    uint8_t  large;
    uint8_t  five;
    uint8_t  mlc;
    uint8_t  vendor;
    uint8_t  access_time;
    uint8_t  sec_per_page;
    uint8_t  sec_per_page_raw;
    uint16_t sec_per_block;
    uint16_t sec_per_block_raw;
    uint16_t page_per_block;
    uint16_t page_per_block_raw;

    uint32_t tot_logic_sec;
    uint32_t total_phy_sec;
    uint32_t total_bloks;

    uint32_t cmd;
    uint32_t addr;
    uint32_t data;
};

/* holds nand chips characteristics */
struct flashspec_t flash_spec[MAX_FLASH_NUM];

/* sum of all phy sectors in all chips */
uint32_t  total_phy_sec;

enum vendor_t {
    SAMSUNG,
    TOSHIBA,
    HYNIX,
    INFINEON,
    MICRON,
    RENESAS,
    ST
};

/* taken from OF */
const uint8_t device_code[] = {
    0x76,
    0x79,
    0xf1,
    0xda,
    0xdc,
    0xd3,
    0xd7
};

const uint8_t manufacture_id_tbl[] =
{
    0xec,     /* SAMSUNG  */
    0x98,     /* TOSHIBA  */
    0xad,     /* HYNIX    */
    0xc1,     /* INFINEON */
    0x2c,     /* MICRON   */
    0x07,     /* RENESAS  */
    0x20      /* ST       */
};

/* phisical sectors */
const uint32_t device_info[] =
{
    0x20000,      /*  64M, small page */
    0x40000,      /* 128M, small page */
    0x40000,      /* 128M, large page */
    0x80000,      /* 256M, large page */
    0x100000,     /* 512M, large page */
    0x200000,     /*   1G, large page */
    0x400000,     /*   2G, large page */
    0x800000      /*   4G, large page */
};

static int flash_delay(int n)
{
    volatile int cnt, i, j;

    for (j=0; j<n; j++)
        for (i=0; i<10000; i++)
            cnt++;

    return cnt;
}

static void flash_wait_busy(void)
{
    unsigned int i;

    for (i=0; i<0x2000; i++)
    {
        if (FMCTL & FM_RDY)
             break;

        flash_delay(1);
    }
}

void flash_chip_deselect(void)
{
    uint32_t tmp;
    tmp = FMCTL;
    tmp &= 0xf0;
    FMCTL = tmp;
}

void flash_chip_select(uint8_t chip)
{
    uint32_t tmp;

    /* Maybe we should handle IOMUX here as well?
     * for chip == 0,1 it is not needed
     */
    tmp = FMCTL;
    tmp &= 0xf0;
    tmp |= 1<<chip;
    FMCTL = tmp;
}

void flash_init(void)
{
    uint8_t buff[5]; /* buff for CMD_READ_ID response */
    uint32_t i,j;

    mlc_refresh_row = 0xffffffff;
    total_phy_sec = 0;
    flash_pend_cmd.valid = 0;
    flash_read_status_cmd = CMD_READ_STATUS;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    for (i=0; i< MAX_FLASH_NUM; i++)
    {
        /* Redundat - we will use special macros
         * just for reference what OF does
         */
        flash_spec[i].cmd = 0x180E8200 + (i<<9);
        flash_spec[i].addr = 0x180E204 + (i<<9);
        flash_spec[i].data = 0x180E208 + (i<<9);

        flash_chip_select(i);
        FLASH_CMD(i) = CMD_RESET; /* write cmd to flash chip */
        flash_delay(2);
        flash_wait_busy();
        FLASH_CMD(i) = CMD_READ_ID; /* write cmd to flash chip */
        FLASH_ADDR(i) = 0x00;

        /* read 5 bytes of CMD_READ_ID response */
        for (j=0; j<5; j++)
             buff[j] = FLASH_DATA(i);

        flash_chip_deselect();

        /* Get the vendor of the chip */
        for (j=0; j<sizeof(manufacture_id_tbl); j++)
        {
            /* store Manufacturer index */
            if (ManufactureIDTbl[j] == buff[0])
            {
                flash_spec[i].vendor = j;
            }
        }

        for (j=0; j<sizeof(device_code); j++)
        {
            /* look for matching device code
             * and store total phys sectors
             */
            if (DeviceCode[j] == buff[1])
            {
                flash_spec[i].total_phy_sec = device_info[j];
                break;
             }
        }

        /* div zero is fatal for us (not for OF :P) */
        if (flash_spec[i].total_phy_sec == 0)
            continue;

        /* loc_7e8 */
        flash_spec[i].mlc = 0;
        flash_spec[i].large = 0;
        flash_spec[i].five = 1;
        flash_spec[i].mul_plane = 1;
        flash_spec[i].interleave = 0;

        flash_spec[i].cache_prog = buff[2] & 0x80;

        /* flash access time (ns) */
        switch (buff[3] & 0x88)
        {
            case 0:
                flash_spec[i].access_time = 50;
                break;
            case 0x80:
                flash_spec[i].access_time = 25;
                break;
            case 0x08:
                flash_spec[i].access_time = 20;
                break;
            default:
                flash_spec[i].access_time = 60;
        }

        /* set_large
         * j is index in device_code table
         */
        if (j < 2)
        {
            /* small block */
            flash_spec[i].large = 0;
            flash_spec[i].sec_per_page_raw = 1;
            flash_spec[i].sec_per_block_raw = 32;
        }
        else
        {
            flash_spec[i].large = 1;
            if (j == 2)
                flash_spec[i].five = 0;

            /* cell type */
            flash_spec[i].mlc = (buff[2] >> 2) & 0x03;

            flash_spec[i].sec_per_page_raw = 2;   /* 1KB~8KB */

            /* set_sec_per_page_raw */
            flash_spec[i].sec_per_page_raw <<= (buff[3] & 3);

            flash_spec[i].sec_per_block_raw = 128;      /* 64KB~512KB */

            /* set_sec_per_block_raw */
            flash_spec[i].sec_per_block_raw <<= ((buff[3]>>4) & 3);

            /* simult_prog */
            if (buff[2] & 0x30)
            {
                /* buff4_mulplane */
                flash_spec[i].mul_plane <<= ((buff[4]>>2) & 3);
            }

            /* set_interleave */
            if (flash_spec[i].vendor == TOSHIBA)
            {
                flash_spec[i].mul_plane = 2;
                if (buff[2] & 3)
                    flash_spec[i].interleave = 1;
            }

        } /* large block */

        if (flash_spec[i].mul_plane > 2)
        {
            flash_spec[i].mul_plane = 2;
            flash_spec[i].interleave = 1;
        }

        flash_spec[i].page_per_block_raw = flash_spec[i].sec_per_block_raw/flash_spec[i].sec_per_page_raw;
        flash_spec[i].page_per_block = flash_spec[i].page_per_block_raw * flash_spec[i].mul_plane;
        flash_spec[i].sec_per_block = flash_spec[i].sec_per_block_raw * flash_spec[i].mul_plane;
        flash_spec[i].sec_per_page = flash_spec[i].sec_per_page_raw * flash_spec[i].mul_plane;
        flash_spec[i].total_bloks = flash_spec[i].total_phy_sec / flash_spec[i].sec_per_block;

        total_phy_sec += flash_spec[i].total_phy_sec;
    }

    /* read ID block and propagate SysDiskCapacity and SysResBlocks */
}

/* read single page in unbuffered mode */
void flash_read_page(int page, unsigned char *pgbuff)
{
    unsigned int i;

    flash_chip_select(0);
    flash_delay(2);
    flash_wait_busy();

    /* setup transfer */
    FLASH_CMD(0)  = 0x00;
    FLASH_ADDR(0) = 0x00;                /* column */
    FLASH_ADDR(0) = 0x00;                /* column */
    FLASH_ADDR(0) = page & 0xff;         /* row */
    FLASH_ADDR(0) = (page >> 8) & 0xff;  /* row */
    FLASH_ADDR(0) = (page >> 16) & 0xff; /* row */
    FLASH_CMD(0)  = READ_PAGE_CMD;

    /* wait for operation complete */
    flash_wait_busy();

    /* copy data from page register
     * WARNING flash page size can be different
     * for different chips. This value should be set
     * based on initialization.
     */
    for (i=0; i<(4096+218); i++)
        pgbuff[i] = FLASH_DATA(0);

    flash_chip_deselect();
}

void flash_read_sector(int page, unsigned char *secbuf, int nsec)
{
    int i = 0;
    int j = 0;

    /* WARNING this code assumes only one nand chip
     * it does not handle data split across different nand chips
     */
    flash_chip_select(0);
    flash_delay(2);
    flash_wait_busy();

    FLASH_CMD(0)  = 0x00;
    FLASH_ADDR(0) = 0x00;
    FLASH_ADDR(0) = 0x00;
    FLASH_ADDR(0) = page & 0xff;
    FLASH_ADDR(0) = (page >> 8) & 0xff;
    FLASH_ADDR(0) = (page >> 16) & 0xff;
    FLASH_CMD(0)  = READ_PAGE_CMD;

    flash_delay(1);

    /* wait for operation to complete */
    flash_wait_busy();

    /* enables hw checksum control most probably */
    BCHCTL = 1;

    /* This initializes the transfer from the nand to the buffer
     * There are 4 consecutive hw buffers 512 bytes long for data (PAGE_BUF)
     * and 4 16 bytes long for metadata (BCH code checksum) (SPARE_BUF)
     */
    FLCTL = 0xA24;

    /* This scheme utilizes some overlap in data transfers -
     * data are copied from buffer to the mem and from nand to the buf
     * at the same time.
     */
    while (++j < nsec)
    {
        /* wait for transfer to complete */
        while(! (FLCTL & FL_RDY));

        /* initialize next transfer to the next buffer */
        FLCTL = 0xA24 | (j&3)<<3;

        /* copy data chunk */
        memcpy(secbuf, (((unsigned char *)&PAGE_BUF)+((i&3)<<9)), 0x200);
        secbuf += 0x200;

        /* copy metadata chunk (BCH)
         * in real application this can be discarded
         */
        memcpy(secbuf, (((unsigned char *)&SPARE_BUF)+((i&3)<<4)), 0x10);
        secbuf += 0x10;
        i++;
    }

    /* wait for transfer to complete */
    while(! (FLCTL & FL_RDY));

    /* copy data chunk */
    memcpy(secbuf, (((unsigned char *)&PAGE_BUF)+((i&3)<<9)), 0x200);
    secbuf += 0x200;

    /* copy metadata chunk (BCH)
     * in real application this can be discarded
     */
    memcpy(secbuf, (((unsigned char *)&SPARE_BUF)+((i&3)<<4)), 0x10);
    secbuf += 0x10;

    flash_chip_deselect();
}

#endif
/*
 * Full-capacity geometry: one 8 GiB die per chip-enable, four CE# = 32 GB.
 *
 * The C4 Pro carries four Micron 2C D9 D5 3E 88 dies, each on its own NAND
 * chip-enable (CE#0..3). Each die is 16384 blocks × 128 pages × 4096 B = 8 GiB,
 * addressed by a 3-byte row address within the die; the die is selected by the
 * FMCTL low-nibble chip-enable bit (1 << bank), so the FTL's bank index maps
 * 1:1 to the die index. The upper chip-selects are enabled by the IOMUX pin-mux
 * in nand_device_init(). Four banks × 16384 blocks = the full 32 GB.
 *
 * Verified on-device: with the CS1..3 pin-mux set, all four CE# return the
 * Micron ID and read/write distinct per-die data.
 */
static const struct nand_device_info_type c4pro_micron_2cd9d53e88 =
{
    .id = 0x2cd9d53e,
    .blocks = 16384,
    .userblocks = 16384,
    .pagesperblock = 128,
    .blocksizeexponent = 17,
    .tunk1 = 0,
    .twp = 0,
    .tunk2 = 0,
    .tunk3 = 0,
};

static long last_activity = 0;

/* NAND interface wait-state timing (AHB0_NANDC+0x04). 0x1081 is the original
 * conservative value; every read/write programs it from here so a DRAM sweep
 * can find the fastest value the NAND still reads correctly at (stock runs
 * tighter timing -> ~6.7 MB/s vs our ~1.4). Lower = faster but risks errors. */
uint32_t c4pro_nand_fmwait = 0x1081;

/* Post-command settle busy-wait. The original 10000 iters (~150-250us) ran
 * several times per page ON TOP of the real nand_wait_ready() FM_RDY poll and
 * was the dominant per-page cost: on-device a DRAM sweep read correctly (bad=0)
 * at every value down to 0, and dropping 10000->~2 took reads 1462->5389 KB/s
 * (3.7x, ~80% of stock's 6.7 MB/s). 50 is the conservative default: ~5.2 MB/s
 * with 200x margin over the verified-safe floor for the (unswept) program/erase
 * path. Tunable so it can be retuned per phase. */
uint32_t c4pro_nand_delay_iters = 50;

/* Separate settle for the PROGRAM/ERASE path. On-device write-delay sweep wrote
 * byte-correct (bad=0) down to 2 iters, so writes need no extra settle; 100 is a
 * comfortable margin at negligible cost (writes are tPROG-bound, so the settle
 * is a tiny fraction of program time). The many-small-files copy drop is a USB/
 * copy-on-write pattern issue, NOT this delay. */
uint32_t c4pro_nand_prog_delay_iters = 100;
static void nand_prog_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < c4pro_nand_prog_delay_iters; i++)
        ;
}

/* Last BCH status (BCHST) OR'd across a hardware-ECC read's sectors -- exposes
 * the uncorrectable bit so erased/non-hwecc pages can be detected, not choked. */
uint32_t c4pro_nand_last_bchst = 0;

/* Capture the live clock state so a DRAM probe can tell whether NAND reads run
 * at stock's 100 MHz AHB HCLK (PLL up) or a slow pre-PLL clock. out[0]=PLLCON1,
 * out[1]=DIVCON1 (bits[4:3]=ARM:AHB ratio), out[2]=CLKCFG, out[3]=STATUS. */
void c4pro_nand_clock_regs(uint32_t out[4])
{
    out[0] = SCU_PLLCON1;
    out[1] = SCU_DIVCON1;
    out[2] = SCU_CLKCFG;
    out[3] = SCU_STATUS;
}

static void nand_delay(void)
{
    volatile uint32_t i;

    for (i = 0; i < c4pro_nand_delay_iters; i++)
        ;
}

/* Inter-poll spacing for the NAND ready waits. Our default is a tight register
 * spin (while(!(FMCTL & FM_RDY))); stock instead spaces each status read with a
 * ~20-count delay. Hammering the NANDC register in a tight loop while its engine
 * is mid-AHB-transaction is the most plausible source of the intermittent AHB
 * bus stall (the phase-5 sustained-write freeze). Spacing the reads mirrors stock
 * and stops the CPU from pounding the controller bus during a program/erase.
 * Gated so it can be A/B'd; C4PRO_NAND_POLL_SPACING tunes the gap. */
#ifndef C4PRO_NAND_STATUS_POLL_SPACING
/* Inter-poll delay for the stock-style 0x70 READ-STATUS program-completion poll
 * (C4PRO_NAND_STATUS_POLL). Stock spaces each status read ~20 counts. */
#define C4PRO_NAND_STATUS_POLL_SPACING 20
#endif

#ifdef C4PRO_NAND_SPACED_POLL
#ifndef C4PRO_NAND_POLL_SPACING
#define C4PRO_NAND_POLL_SPACING 24
#endif
static inline void nand_poll_space(void)
{
    volatile int d = C4PRO_NAND_POLL_SPACING;
    while (d--)
        ;
}
#else
static inline void nand_poll_space(void) { }
#endif

static bool nand_wait_ready(void)
{
    int i;

    for (i = 0; i < 100000; i++)
    {
        if (FMCTL & FM_RDY)
            return true;
        nand_poll_space();
    }

    return false;
}

static bool nand_wait_fl_ready(void)
{
    int i;

    for (i = 0; i < 100000; i++)
    {
        if (FLCTL & FL_RDY)
            return true;
        nand_poll_space();
    }

    return false;
}

static void nand_select(uint32_t bank)
{
    unsigned long ctl = FMCTL;

#ifdef COLORFLY_C4_PRO
    /* Re-assert the NAND CS2/CS3 pin-mux on every select. These IOMUXA bits
     * (14/16) are SHARED with the codec-I2C routing, and a codec transaction
     * (i2c_iomux) transiently clears them; re-asserting here guarantees the
     * pins carry the NAND-CS function for this access regardless of I2C timing,
     * so a volume-change I2C write can never corrupt a concurrent die-2/3 read.
     * Mirrors the setup in nand_device_init(). */
    SCU_IOMUXA_CON = (SCU_IOMUXA_CON & ~((3u << 14) | (3u << 16))) |
                     IOMUX_NAND_CS1 | IOMUX_NAND_CS2 | IOMUX_NAND_CS3;
#endif

    ctl &= 0xf0;
    ctl |= 1u << bank;
    FMCTL = ctl;
}

static void nand_deselect(void)
{
    FMCTL &= 0xf0;
}

static bool nand_wait_ready_erase(void)
{
    int i;

    /* MLC block erase can take 3-30 ms; the standard nand_wait_ready() budget
     * of 100000 iterations (~2.5 ms at 200 MHz) is too tight for blocks that
     * carry live data. 400000 (~10 ms ceiling) covers worst-case MLC erase
     * while keeping the stall bounded under sustained multi-die writes. */
    for (i = 0; i < 400000; i++)
    {
        if (FMCTL & FM_RDY)
            return true;
        nand_poll_space();
    }

    return false;
}

static bool nand_read_id(uint32_t bank, unsigned char id[5])
{
    int i;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    FLASH_CMD(bank) = 0xff;
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return false;
    }

    FLASH_CMD(bank) = 0x90;
    FLASH_ADDR(bank) = 0x00;

    for (i = 0; i < 5; i++)
        id[i] = FLASH_DATA(bank);

    nand_deselect();
    return true;
}

const struct nand_device_info_type* nand_get_device_type(uint32_t bank)
{
    unsigned char id[5];

    if (bank > 3)
        return NULL;

    if (!nand_read_id(bank, id))
        return NULL;

    if (id[0] == 0x2c && id[1] == 0xd9 && id[2] == 0xd5 &&
        id[3] == 0x3e && id[4] == 0x88)
        return &c4pro_micron_2cd9d53e88;

    return NULL;
}

uint32_t nand_read_page(uint32_t bank, uint32_t page, void* databuffer,
                        void* sparebuffer, uint32_t doecc,
                        uint32_t checkempty)
{
    unsigned i;
    unsigned sector;
    unsigned half;
    unsigned char *data = databuffer;
    unsigned char *spare = sparebuffer;

    (void)doecc;
    (void)checkempty;

    if (bank > 3 || (!data && !spare))
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    BCHCTL = 1;

#ifdef COLORFLY_C4_PRO
    for (half = 0; half < 2; half++)
#else
    for (half = 0; half < 1; half++)
#endif
    {
        unsigned column = half * 2048;

        FLASH_CMD(bank) = 0x00;
        FLASH_ADDR(bank) = column & 0xff;
        FLASH_ADDR(bank) = (column >> 8) & 0xff;
        FLASH_ADDR(bank) = page & 0xff;
        FLASH_ADDR(bank) = (page >> 8) & 0xff;
        FLASH_ADDR(bank) = (page >> 16) & 0xff;
        FLASH_CMD(bank) = 0x30;

        nand_delay();

        if (!nand_wait_ready())
        {
            nand_deselect();
            return 1;
        }

        for (sector = 0; sector < 4; sector++)
        {
            unsigned out_sector = half * 4 + sector;

            FLCTL = 0xA24 | ((sector & 3) << 3);
            if (!nand_wait_fl_ready())
            {
                nand_deselect();
                return 1;
            }

            if (data)
            {
                for (i = 0; i < 512; i++)
                    data[out_sector * 512 + i] =
                        *((volatile unsigned char *)&PAGE_BUF +
                          ((sector & 3) << 9) + i);
            }

            if (spare)
            {
                for (i = 0; i < 16; i++)
                    spare[out_sector * 16 + i] =
                        *((volatile unsigned char *)&SPARE_BUF +
                          ((sector & 3) << 4) + i);
            }
        }
    }

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

#ifdef COLORFLY_C4_PRO
uint32_t c4pro_nand_read_id(uint32_t bank, unsigned char id[5])
{
    if (bank > 3 || !id)
        return 1;

    return nand_read_id(bank, id) ? 0 : 1;
}

uint32_t c4pro_nand_read_sector(uint32_t bank, uint32_t page,
                                uint32_t sector, void* databuffer,
                                void* sparebuffer)
{
    unsigned i;
    unsigned char *data = databuffer;
    unsigned char *spare = sparebuffer;
    uint32_t column;
    uint32_t spare_column;

    if (bank > 3 || sector >= 8 || (!data && !spare))
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    if (data)
    {
        column = sector * 538;

        FLASH_CMD(bank) = 0x00;
        FLASH_ADDR(bank) = column & 0xff;
        FLASH_ADDR(bank) = (column >> 8) & 0xff;
        FLASH_ADDR(bank) = page & 0xff;
        FLASH_ADDR(bank) = (page >> 8) & 0xff;
        FLASH_ADDR(bank) = (page >> 16) & 0xff;
        FLASH_CMD(bank) = 0x30;

        nand_delay();
        if (!nand_wait_ready())
        {
            nand_deselect();
            return 1;
        }

        for (i = 0; i < 512; i++)
            data[i] = FLASH_DATA(bank);
    }

    if (spare)
    {
        spare_column = 4096 + sector * 16;

        FLASH_CMD(bank) = 0x00;
        FLASH_ADDR(bank) = spare_column & 0xff;
        FLASH_ADDR(bank) = (spare_column >> 8) & 0xff;
        FLASH_ADDR(bank) = page & 0xff;
        FLASH_ADDR(bank) = (page >> 8) & 0xff;
        FLASH_ADDR(bank) = (page >> 16) & 0xff;
        FLASH_CMD(bank) = 0x30;

        nand_delay();
        if (!nand_wait_ready())
        {
            nand_deselect();
            return 1;
        }

        for (i = 0; i < 16; i++)
            spare[i] = FLASH_DATA(bank);
    }

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

uint32_t c4pro_nand_read_linear_sector(uint32_t bank, uint32_t page,
                                       uint32_t sector, void* databuffer)
{
    unsigned i;
    unsigned char *data = databuffer;
    uint32_t column;

    if (bank > 3 || sector >= 8 || !data)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    column = sector * 512;

    FLASH_CMD(bank) = 0x00;
    FLASH_ADDR(bank) = column & 0xff;
    FLASH_ADDR(bank) = (column >> 8) & 0xff;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0x30;

    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    for (i = 0; i < 512; i++)
        data[i] = FLASH_DATA(bank);

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

uint32_t c4pro_nand_read_linear_page(uint32_t bank, uint32_t page,
                                     void* databuffer)
{
    unsigned i;
    unsigned char *data = databuffer;

    if (bank > 3 || !data)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0x30;

    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    for (i = 0; i < 4096; i++)
        data[i] = FLASH_DATA(bank);

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

/* Linear read of the full page: 4096 data bytes + 128 spare bytes, matching the
 * linear write layout in c4pro_nand_write_page (data then spare, contiguous).
 * Used by the owned FTL so software BCH can correct each sector from its spare;
 * the buffered PAGE_BUF engine uses a different layout incompatible with the
 * linear PIO writes, so reads must stay linear. */
uint32_t c4pro_nand_read_linear_page_spare(uint32_t bank, uint32_t page,
                                           void* databuffer, void* sparebuffer)
{
    unsigned i;
    unsigned char *data = databuffer;
    unsigned char *spare = sparebuffer;

    if (bank > 3 || !data || !spare)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0x30;

    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    for (i = 0; i < 4096; i++)
        data[i] = FLASH_DATA(bank);
    for (i = 0; i < 128; i++)
        spare[i] = FLASH_DATA(bank);

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

/* Diagnostic: read a page's 4096 data bytes via the buffered PAGE_BUF engine
 * with a caller-supplied FLCTL base (0xA24 = ECC on, 0x224 = ECC off), so an
 * in-app test can compare the buffered engine against the linear read.
 * Returns 0 ok, 1 = NAND not ready, 2 = FL_RDY timeout (engine never finished).*/
uint32_t c4pro_nand_read_page_flctl(uint32_t bank, uint32_t page,
                                    void* databuffer, uint32_t flctl_base)
{
    unsigned i;
    unsigned sector;
    unsigned half;
    unsigned char *data = databuffer;

    if (bank > 3 || !data)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;
    nand_select(bank);
    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }
    BCHCTL = 1;

    for (half = 0; half < 2; half++)
    {
        unsigned column = half * 2048;

        FLASH_CMD(bank) = 0x00;
        FLASH_ADDR(bank) = column & 0xff;
        FLASH_ADDR(bank) = (column >> 8) & 0xff;
        FLASH_ADDR(bank) = page & 0xff;
        FLASH_ADDR(bank) = (page >> 8) & 0xff;
        FLASH_ADDR(bank) = (page >> 16) & 0xff;
        FLASH_CMD(bank) = 0x30;
        nand_delay();
        if (!nand_wait_ready())
        {
            nand_deselect();
            return 1;
        }

        for (sector = 0; sector < 4; sector++)
        {
            unsigned out_sector = half * 4 + sector;

            FLCTL = flctl_base | ((sector & 3) << 3);
            if (!nand_wait_fl_ready())
            {
                nand_deselect();
                return 2;
            }
            for (i = 0; i < 512; i++)
                data[out_sector * 512 + i] =
                    *((volatile unsigned char *)&PAGE_BUF +
                      ((sector & 3) << 9) + i);
        }
    }

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

#ifdef C4PRO_NAND_DMA
/*
 * Fast owned-FTL page read via the hardware buffered PAGE_BUF engine, replacing
 * the per-byte FLASH_DATA PIO loop in c4pro_nand_read_linear_page_spare().
 *
 * Why this is the chosen fast path (see docs/c4-nand-dma-speed-design.md):
 *  - The throughput bottleneck is the byte-serial FLASH_DATA port: the PIO
 *    reader issues one CPU-gated AHB/NAND access per byte for 4096+128 bytes,
 *    which measures ~1 MB/s (~4 ms/page). The buffered engine sequences the
 *    NAND->on-chip-SRAM transfer in hardware and the CPU only does a word-wide
 *    copy out of PAGE_BUF, removing ~4096 byte-serial round trips per page.
 *  - The RK27xx NANDC has NO DRAM-master DMA of its own; the only "DMA" blocks
 *    on this SoC are the UDC scatter-gather engine (0x180a0000, mutually
 *    exclusive with USB-MSC) and the generic HDMA/DWDMA mem-to-mem engines,
 *    none of which shorten the NAND->buffer leg that dominates here. The
 *    buffered PAGE_BUF engine IS the NAND-dedicated fast path.
 *
 * Layout compatibility: the owned FTL writes a *linear* page (4096 contiguous
 * data bytes at column 0, then 128 contiguous software-BCH spare bytes at
 * column 4096). The buffered engine's hardware-ECC mode (FLCTL=0xA24) assumes
 * an *inline* 512-data+16-spare (528-byte) sector stride and would both
 * mis-sample the data and mis-correct against the wrong spare. We therefore run
 * the engine with hardware correction OFF (C4PRO_NAND_DMA_FLCTL_BASE=0x224): it
 * advances exactly 512 data bytes/sector, reproducing the contiguous data, and
 * the existing software BCH still corrects each sector from the spare. The 128
 * spare bytes are read with a short PIO burst from column 4096 (3% of the page)
 * because the engine's auto-spare offset does not match the linear layout.
 */
#ifndef C4PRO_NAND_DMA_FLCTL_BASE
#define C4PRO_NAND_DMA_FLCTL_BASE 0x224u /* buffered transfer, hardware ECC OFF */
#endif
/* tCCS settle iterations after a change-read-column (0x05/0xE0) before the
 * engine samples the new column. Start small; raise if reads corrupt, lower
 * toward 0 once a safe minimum is found (it is paid 8x per page). */
#ifndef C4PRO_NAND_DMA_CRC_DELAY
#define C4PRO_NAND_DMA_CRC_DELAY 64
#endif

uint32_t c4pro_nand_read_linear_page_spare_dma(uint32_t bank, uint32_t page,
                                               void* databuffer,
                                               void* sparebuffer)
{
    unsigned i;
    unsigned sector;
    unsigned char *data = databuffer;
    unsigned char *spare = sparebuffer;

    if (bank > 3 || !data || !spare)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    BCHCTL = 1;

    /* Data phase: 8 linear 512-byte sectors via the buffered PAGE_BUF engine.
     * The engine advances the NAND output by the stock 528-byte sector stride
     * (512 data + 16 inline ECC) per FLCTL trigger -- even with ECC correction
     * off (0x224 vs 0xA24) -- so it cannot stream our linear (4096 contiguous)
     * layout sequentially. Re-issuing a full read command (with tR latency) per
     * sector fixes the address but is ~4x SLOWER than byte PIO (8 tR/page).
     * Instead: ONE full page read latches the page (single tR), then for each
     * sector use CHANGE READ COLUMN (0x05 + col + 0xE0) to reposition the NAND
     * output pointer to the exact linear offset (sector*512) WITHOUT re-reading
     * the array, and trigger the engine burst. One tR/page + cheap column moves
     * + word-wide PAGE_BUF copy -> the actual speedup. */
    FLASH_CMD(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;            /* column 0 (low) */
    FLASH_ADDR(bank) = 0x00;            /* column 0 (high) */
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0x30;

    nand_delay();
    if (!nand_wait_ready())            /* the single tR for the whole page */
    {
        nand_deselect();
        return 1;
    }

    for (sector = 0; sector < 8; sector++)
    {
        unsigned column = sector * 512;
        const volatile uint32_t *src;
        uint32_t *dst;

        /* change read column to the linear sector offset (no array re-read) */
        FLASH_CMD(bank) = 0x05;
        FLASH_ADDR(bank) = column & 0xff;
        FLASH_ADDR(bank) = (column >> 8) & 0xff;
        FLASH_CMD(bank) = 0xE0;
        /* tCCS: short settle before the engine samples the new column */
        {
            volatile int d;
            for (d = 0; d < C4PRO_NAND_DMA_CRC_DELAY; d++)
                ;
        }

        FLCTL = C4PRO_NAND_DMA_FLCTL_BASE | ((sector & 3) << 3);
        if (!nand_wait_fl_ready())
        {
            nand_deselect();
            return 2;
        }

        src = (const volatile uint32_t *)
              ((volatile unsigned char *)&PAGE_BUF + ((sector & 3) << 9));
        dst = (uint32_t *)(data + sector * 512);
        for (i = 0; i < 512 / 4; i++)
            dst[i] = src[i];
    }

    /* Spare phase: 128 contiguous software-BCH bytes at column 4096, PIO. */
    FLASH_CMD(bank) = 0x00;
    FLASH_ADDR(bank) = 4096 & 0xff;
    FLASH_ADDR(bank) = (4096 >> 8) & 0xff;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0x30;

    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    for (i = 0; i < 128; i++)
        spare[i] = FLASH_DATA(bank);

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

/* SPEED-CEILING PROBE ONLY (data is WRONG on our linear layout): read a page in
 * the engine's NATIVE streaming mode -- one read command per 2048-byte half, 4
 * sequential PAGE_BUF bursts each, NO per-sector change-column. This is exactly
 * how the engine would run on a stock 528-byte inline layout, so its timing is
 * the ceiling a 528-layout rewrite could reach. Returns the same 4096 bytes
 * nand_read_page would, but mis-aligned for our layout -- use for timing, not
 * content. */
uint32_t c4pro_nand_read_native_stream_dma(uint32_t bank, uint32_t page,
                                           void* databuffer)
{
    unsigned i, sector, half;
    unsigned char *data = databuffer;

    if (bank > 3 || !data)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;
    nand_select(bank);
    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }
    BCHCTL = 1;

    for (half = 0; half < 2; half++)
    {
        unsigned column = half * 2048;

        FLASH_CMD(bank) = 0x00;
        FLASH_ADDR(bank) = column & 0xff;
        FLASH_ADDR(bank) = (column >> 8) & 0xff;
        FLASH_ADDR(bank) = page & 0xff;
        FLASH_ADDR(bank) = (page >> 8) & 0xff;
        FLASH_ADDR(bank) = (page >> 16) & 0xff;
        FLASH_CMD(bank) = 0x30;

        nand_delay();
        if (!nand_wait_ready())
        {
            nand_deselect();
            return 1;
        }

        for (sector = 0; sector < 4; sector++)
        {
            unsigned out_sector = half * 4 + sector;
            const volatile uint32_t *src;
            uint32_t *dst;

            FLCTL = C4PRO_NAND_DMA_FLCTL_BASE | ((sector & 3) << 3);
            if (!nand_wait_fl_ready())
            {
                nand_deselect();
                return 2;
            }
            src = (const volatile uint32_t *)
                  ((volatile unsigned char *)&PAGE_BUF + ((sector & 3) << 9));
            dst = (uint32_t *)(data + out_sector * 512);
            for (i = 0; i < 512 / 4; i++)
                dst[i] = src[i];
        }
    }

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

/* Validation helper for the staged on-device bring-up: read one page both ways
 * (PIO reference vs engine) and return the first byte offset where they differ
 * (0..4095 = data, 4096..4223 = spare), or 0xffffffff if identical. Distinct
 * error sentinels: 0xfffffffe = PIO read failed, 0xfffffffd = engine read
 * failed. Lets the human prove the engine read on die 1 before switching the
 * FTL read path over. */
static unsigned char c4pro_dma_test_pio_data[4096];
static unsigned char c4pro_dma_test_pio_spare[128];
static unsigned char c4pro_dma_test_dma_data[4096];
static unsigned char c4pro_dma_test_dma_spare[128];

uint32_t c4pro_nand_dma_selftest_page(uint32_t bank, uint32_t page)
{
    unsigned i;

    if (c4pro_nand_read_linear_page_spare(bank, page,
            c4pro_dma_test_pio_data, c4pro_dma_test_pio_spare))
        return 0xfffffffeu;
    if (c4pro_nand_read_linear_page_spare_dma(bank, page,
            c4pro_dma_test_dma_data, c4pro_dma_test_dma_spare))
        return 0xfffffffdu;

    for (i = 0; i < 4096; i++)
        if (c4pro_dma_test_pio_data[i] != c4pro_dma_test_dma_data[i])
            return i;
    for (i = 0; i < 128; i++)
        if (c4pro_dma_test_pio_spare[i] != c4pro_dma_test_dma_spare[i])
            return 4096u + i;

    return 0xffffffffu;
}
#endif /* C4PRO_NAND_DMA */

#ifdef C4PRO_NAND_DMA_WRITE
/*
 * Experimental engine-assisted page program (default OFF, validate only AFTER
 * the read path is byte-for-byte proven). Streams the 4096 data bytes from
 * PAGE_BUF through the buffered engine (ECC OFF, FL_WR) and PIO-writes the 128
 * contiguous spare bytes, preserving the exact linear data+spare layout that
 * the PIO writer and the software BCH expect.
 *
 * Caveat: MLC tPROG (~1-3 ms/page) dominates program time, so the realistic
 * gain over the proven PIO writer is modest, and a full page program must be a
 * single uninterrupted 0x80..0x10 input stream (it cannot re-address per half
 * like the read path). Whether the engine streams all 8 sectors contiguously
 * with ECC off is the exact thing the read-after-write validation must confirm
 * on a sacrificial block before this is trusted. The PIO writer remains the
 * default; this is only used when C4PRO_NAND_DMA_WRITE is explicitly defined.
 */
uint32_t c4pro_nand_write_page_dma(uint32_t bank, uint32_t page,
                                   const void* databuffer,
                                   const void* sparebuffer)
{
    unsigned i;
    unsigned sector;
    const unsigned char *data = databuffer;
    const unsigned char *spare = sparebuffer;

    if (bank > 3 || !data || !spare)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x80;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;

    BCHCTL = 1;

    for (sector = 0; sector < 8; sector++)
    {
        volatile uint32_t *dstbuf;
        const uint32_t *srcw;

        dstbuf = (volatile uint32_t *)
                 ((volatile unsigned char *)&PAGE_BUF + ((sector & 3) << 9));
        srcw = (const uint32_t *)(data + sector * 512);
        for (i = 0; i < 512 / 4; i++)
            dstbuf[i] = srcw[i];

        FLCTL = C4PRO_NAND_DMA_FLCTL_BASE | FL_WR | ((sector & 3) << 3);
        if (!nand_wait_fl_ready())
        {
            nand_deselect();
            return 2;
        }
    }

    for (i = 0; i < 128; i++)
        FLASH_DATA(bank) = spare[i];

    FLASH_CMD(bank) = 0x10;
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x70;
    i = FLASH_DATA(bank);
    nand_deselect();
    last_activity = current_tick;

    return (i & 1) ? 1 : 0;
}
#endif /* C4PRO_NAND_DMA_WRITE */

uint32_t c4pro_nand_write_page(uint32_t bank, uint32_t page,
                               const void* databuffer,
                               const void* sparebuffer)
{
    unsigned i;
    const unsigned char *data = databuffer;
    const unsigned char *spare = sparebuffer;

    if (bank > 3 || !data || !spare)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_prog_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x80;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;

    for (i = 0; i < 4096; i++)
        FLASH_DATA(bank) = data[i];

    for (i = 0; i < 128; i++)
        FLASH_DATA(bank) = spare[i];

    FLASH_CMD(bank) = 0x10;

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x70;
    i = FLASH_DATA(bank);
    nand_deselect();
    last_activity = current_tick;

    return (i & 1) ? 1 : 0;
}

/* Hardware-ECC page program (mirror of nand_read_page's 0xA24 read). The
 * controller's BCH-8 engine generates 13 parity bytes/sector into the spare
 * SRAM and streams data+parity to the NAND, so NO software BCH encode is needed
 * and reads via nand_read_page() come back hardware-corrected. Layout is inline
 * 512-data + 16-spare per sector. `data` is nsec*512 bytes; the 3 free spare
 * bytes/sector are written 0xFF (parity fills the rest). Deltas vs the read
 * trigger: FLCTL 0xA24->0xA26 (FL_WR), BCHCTL |1 -> |3 (generate direction). */
uint32_t c4pro_nand_write_page_hwecc(uint32_t bank, uint32_t page,
                                     const void* databuffer, uint32_t nsec)
{
    unsigned s, i;
    const unsigned char *data = databuffer;
    unsigned status;

    if (bank > 3 || !data || nsec == 0 || nsec > 8)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_prog_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    BCHCTL = 1;   /* strength-8 preset leaves bit13=0 */

    /* serial data input: 0x80 + col=0 + 3 row bytes */
    FLASH_CMD(bank) = 0x80;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;

    for (s = 0; s < nsec; s++)
    {
        volatile unsigned char *pb =
            (volatile unsigned char *)&PAGE_BUF + ((s & 3) << 9);
        volatile unsigned char *sb =
            (volatile unsigned char *)&SPARE_BUF + ((s & 3) << 4);

        for (i = 0; i < 512; i++)
            pb[i] = data[s * 512 + i];
        for (i = 0; i < 16; i++)
            sb[i] = 0xff;

        FLCTL = FL_RST;
        BCHCTL = (BCHCTL & ~0x1C03u) | 1u | 2u | ((bank & 3u) << 10);
        /* FL_WR|FL_START|(1 sector)|FL_XFER_EN|FL_COR_EN + start-sector */
        FLCTL = 0xA26u | ((s & 3u) << 3);
        if (!nand_wait_fl_ready())
        {
            nand_deselect();
            return 2;
        }
    }

    FLASH_CMD(bank) = 0x10;   /* program confirm */

#ifdef C4PRO_NAND_STATUS_POLL
    /* Stock program-completion discipline (SYSTEM_BASE.RKW @0x60082c50): poll the
     * NAND's own READ-STATUS byte (cmd 0x70, bit6 = R/B ready) through the data
     * port, with an inter-poll delay -- NOT a tight FMCTL FM_RDY controller-
     * register spin. Repeatedly reading the NANDC FMCTL register while the die is
     * programming is the suspected trigger of the intermittent AHB bus wedge (the
     * phase-5 sustained-write freeze); the 0x70 status read is chip-mediated and
     * is exactly what stock does. The status byte we need for the FAIL check
     * (bit0) is the same byte, so no extra read. */
    {
        unsigned guard = 400000;
        do
        {
            FLASH_CMD(bank) = 0x70;
            status = FLASH_DATA(bank);
            if (status & 0x40)          /* bit6 = R/B ready */
                break;
            { volatile int d = C4PRO_NAND_STATUS_POLL_SPACING; while (d--) ; }
        } while (--guard);

        if (!guard)
        {
            nand_deselect();
            return 3;
        }
    }
#else
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 3;
    }

    FLASH_CMD(bank) = 0x70;
    status = FLASH_DATA(bank);
#endif
    nand_deselect();
    last_activity = current_tick;

    return (status & 0x01) ? 4 : 0;   /* bit0 = program FAIL */
}

/* Hardware-ECC page read, matching c4pro_nand_write_page_hwecc's inline
 * 512+16-per-sector layout. Unlike nand_read_page() (which re-seeks half 1 at
 * column 2048 -- wrong for the 528-byte inline stride, breaks sectors 4-7),
 * this issues ONE 0x00/0x30 and lets the controller auto-advance through all
 * nsec sectors, draining each PAGE_BUF slot per trigger. Data comes back
 * hardware-BCH-corrected; no software BCH. */
uint32_t c4pro_nand_read_page_hwecc(uint32_t bank, uint32_t page,
                                    void* databuffer, uint32_t nsec)
{
    unsigned s, i;
    unsigned char *data = databuffer;

    if (bank > 3 || !data || nsec == 0 || nsec > 8)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    BCHCTL = 1;

    FLASH_CMD(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0x30;

    nand_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    c4pro_nand_last_bchst = 0;
    for (s = 0; s < nsec; s++)
    {
        volatile unsigned char *pb =
            (volatile unsigned char *)&PAGE_BUF + ((s & 3) << 9);

        BCHCTL = (BCHCTL & ~0x1C03u) | 1u | ((bank & 3u) << 10);
        FLCTL = 0xA24u | ((s & 3u) << 3);
        if (!nand_wait_fl_ready())
        {
            nand_deselect();
            return 2;
        }
        c4pro_nand_last_bchst |= BCHST;   /* OR of every sector's BCH status */
        for (i = 0; i < 512; i++)
            data[s * 512 + i] = pb[i];
    }

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

uint32_t c4pro_nand_program_bytes(uint32_t bank, uint32_t page,
                                  uint32_t column,
                                  const void* databuffer,
                                  uint32_t count)
{
    unsigned i;
    const unsigned char *data = databuffer;

    if (bank > 3 || !data || count == 0 || column >= 4224 ||
        count > 4224 - column)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x80;
    FLASH_ADDR(bank) = column & 0xff;
    FLASH_ADDR(bank) = (column >> 8) & 0xff;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;

    for (i = 0; i < count; i++)
        FLASH_DATA(bank) = data[i];

    FLASH_CMD(bank) = 0x10;

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x70;
    i = FLASH_DATA(bank);
    nand_deselect();
    last_activity = current_tick;

    return (i & 1) ? 1 : 0;
}

uint32_t c4pro_nand_erase_block(uint32_t bank, uint32_t page)
{
    unsigned status;

    if (bank > 3)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_prog_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x60;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0xd0;

    if (!nand_wait_ready_erase())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x70;
    status = FLASH_DATA(bank);
    nand_deselect();
    last_activity = current_tick;

    return (status & 1) ? 1 : 0;
}

uint32_t c4pro_nand_erase_blocks_multi(const uint32_t *banks,
                                       const uint32_t *pages, uint32_t n)
{
    uint32_t fail = 0;
    uint32_t i;

    if (n == 0 || n > 4 || !banks || !pages)
        return 0xfu;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    /* Phase 1: fire the erase command (0x60 + row + 0xD0) at every die WITHOUT
     * blocking on completion. Deasserting CE# (nand_deselect) does not abort an
     * in-progress internal erase, so each die keeps erasing while we issue to
     * the next one; the tERASE windows overlap across dies. The pre-issue
     * nand_wait_ready() confirms the target die is idle before we command it. */
    for (i = 0; i < n; i++)
    {
        uint32_t bank = banks[i];
        uint32_t page = pages[i];

        if (bank > 3)
        {
            fail |= 1u << i;
            continue;
        }

        nand_select(bank);
        nand_prog_delay();

        if (!nand_wait_ready())
        {
            nand_deselect();
            fail |= 1u << i;
            continue;
        }

        FLASH_CMD(bank) = 0x60;
        FLASH_ADDR(bank) = page & 0xff;
        FLASH_ADDR(bank) = (page >> 8) & 0xff;
        FLASH_ADDR(bank) = (page >> 16) & 0xff;
        FLASH_CMD(bank) = 0xd0;
        nand_deselect();
    }

    /* Phase 2: now wait each die to signal ready and read its erase status. By
     * the time we poll die N, dies 0..N-1 have already been erasing, so total
     * wall time approaches one tERASE plus per-die command overhead rather than
     * n * tERASE. */
    for (i = 0; i < n; i++)
    {
        uint32_t bank = banks[i];
        unsigned status;

        if (fail & (1u << i))
            continue;

        nand_select(bank);

        if (!nand_wait_ready_erase())
        {
            nand_deselect();
            fail |= 1u << i;
            continue;
        }

        FLASH_CMD(bank) = 0x70;
        status = FLASH_DATA(bank);
        nand_deselect();

        if (status & 1)
            fail |= 1u << i;
    }

    last_activity = current_tick;
    return fail;
}

/*
 * Multi-die concurrent hardware-ECC page program. The write path is otherwise
 * tPROG-bound: an MLC array program (~1-3 ms) dwarfs the per-page data transfer,
 * so a single-die serial writer caps at ~1.7 MB/s no matter how fast the data
 * phase is. This mirrors c4pro_nand_erase_blocks_multi's interleave: it streams
 * each die's page image and fires the 0x10 program-confirm on up to 4 dies
 * WITHOUT blocking on tPROG (phase 1), then polls every die to completion
 * (phase 2). Deasserting CE# after 0x10 does not abort an in-progress internal
 * program on this Micron MLC part (the same property the erase interleave already
 * relies on), so the tPROG windows overlap and effective throughput scales with
 * the number of dies in the batch -- approaching the stock ~6.5 MB/s at 4 dies.
 *
 * Each datas[i] is a full 512*nsec-byte linear page image, streamed through the
 * buffered PAGE_BUF engine with hardware ECC on (FLCTL=0xA26, inline 512+16
 * layout) exactly as c4pro_nand_write_page_hwecc does, so pages written here are
 * read back byte-for-byte by nand_read_page()/c4pro_nand_read_page_hwecc(). All
 * n images must be distinct live buffers (they are all consumed in phase 1
 * before any wait). Returns a bitmask: bit i set if die i failed a pre-check,
 * FL transfer, or reported program-fail status.
 */
uint32_t c4pro_nand_prog_pages_hwecc_multi(const uint32_t *banks,
                                           const uint32_t *pages,
                                           const void *const *datas,
                                           uint32_t nsec, uint32_t n)
{
    uint32_t fail = 0;
    uint32_t i;
    unsigned s, k;

    if (n == 0 || n > 4 || nsec == 0 || nsec > 8 ||
        !banks || !pages || !datas)
        return 0xfu;

    FMWAIT = c4pro_nand_fmwait;

    /* Phase 1: stage each die's page and fire 0x10; do NOT wait tPROG so the
     * per-die array-program windows overlap. */
    for (i = 0; i < n; i++)
    {
        uint32_t bank = banks[i];
        uint32_t page = pages[i];
        const unsigned char *data = datas[i];

        if (bank > 3 || !data)
        {
            fail |= 1u << i;
            continue;
        }

        FLCTL = FL_RST;
        nand_select(bank);
        nand_prog_delay();
        if (!nand_wait_ready())
        {
            nand_deselect();
            fail |= 1u << i;
            continue;
        }

        BCHCTL = 1;   /* strength-8 preset leaves bit13=0 */

        FLASH_CMD(bank) = 0x80;
        FLASH_ADDR(bank) = 0x00;
        FLASH_ADDR(bank) = 0x00;
        FLASH_ADDR(bank) = page & 0xff;
        FLASH_ADDR(bank) = (page >> 8) & 0xff;
        FLASH_ADDR(bank) = (page >> 16) & 0xff;

        for (s = 0; s < nsec; s++)
        {
            volatile uint32_t *pbw =
                (volatile uint32_t *)((volatile unsigned char *)&PAGE_BUF +
                                      ((s & 3) << 9));
            volatile uint32_t *sbw =
                (volatile uint32_t *)((volatile unsigned char *)&SPARE_BUF +
                                      ((s & 3) << 4));
            const uint32_t *dw = (const uint32_t *)(data + s * 512);

            /* 32-bit word copy into the on-chip PAGE_BUF (not a byte loop): with
             * dies interleaved this staging copy is serialized on the critical
             * path, so 4x fewer AHB stores shrinks it toward the pure-tPROG
             * ceiling. Both ends are word-aligned (PAGE_BUF slot = 512-aligned;
             * datas[i] is the 4096-byte c4own_il_page[] static, s*512 aligned). */
            for (k = 0; k < 512u / 4u; k++)
                pbw[k] = dw[k];
            for (k = 0; k < 16u / 4u; k++)
                sbw[k] = 0xffffffffu;

            FLCTL = FL_RST;
            BCHCTL = (BCHCTL & ~0x1C03u) | 1u | 2u | ((bank & 3u) << 10);
            /* FL_WR|FL_START|(1 sector)|FL_XFER_EN|FL_COR_EN + start-sector */
            FLCTL = 0xA26u | ((s & 3u) << 3);
            if (!nand_wait_fl_ready())
            {
                fail |= 1u << i;
                break;
            }
        }

        if (fail & (1u << i))
        {
            nand_deselect();
            continue;
        }

        FLASH_CMD(bank) = 0x10;   /* program confirm -- no tPROG wait here */
        nand_deselect();
    }

    /* Phase 2: by the time we poll die i, dies 0..i-1 have been programming
     * since phase 1, so total wall time approaches one tPROG plus per-die
     * transfer overhead rather than n * tPROG. */
    for (i = 0; i < n; i++)
    {
        uint32_t bank = banks[i];
        unsigned status;

        if (fail & (1u << i))
            continue;

        nand_select(bank);
        if (!nand_wait_ready())
        {
            nand_deselect();
            fail |= 1u << i;
            continue;
        }

        FLASH_CMD(bank) = 0x70;
        status = FLASH_DATA(bank);
        nand_deselect();

        if (status & 0x01)   /* bit0 = program FAIL */
            fail |= 1u << i;
    }

    last_activity = current_tick;
    return fail;
}
#endif

uint32_t nand_write_page(uint32_t bank, uint32_t page, void* databuffer,
                         void* sparebuffer, uint32_t doecc)
{
    (void)doecc;
#ifdef COLORFLY_C4_PRO
    return c4pro_nand_write_page(bank, page, databuffer, sparebuffer);
#else
    (void)bank;
    (void)page;
    (void)databuffer;
    (void)sparebuffer;
    return 1;
#endif
}

uint32_t nand_block_erase(uint32_t bank, uint32_t page)
{
#ifdef COLORFLY_C4_PRO
    return c4pro_nand_erase_block(bank, page);
#else
    (void)bank;
    (void)page;
    return 1;
#endif
}

uint32_t nand_reset(uint32_t bank)
{
    if (bank > 3)
        return 1;

    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;
    nand_select(bank);
    FLASH_CMD(bank) = 0xff;
    nand_delay();

    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

    nand_deselect();
    last_activity = current_tick;
    return 0;
}

uint32_t nand_device_init(void)
{
    SCU_CLKCFG &= ~CLKCFG_NAND;

#ifdef COLORFLY_C4_PRO
    /*
     * Mux the upper NAND chip-select pins. The C4 Pro carries four 8 GiB dies,
     * one per chip-enable. Rockbox only ever drove CE#0; CE#1..3 stay floating
     * unless their chip-select pins are switched from GPIO/I2C to NAND function
     * in SCU_IOMUXA_CON. The stock firmware does this in its board init before
     * the first NAND ID scan. Without it dies 1..3 read back 0x00 / garbage and
     * the device caps at 8 GiB. CS1=bit9, CS2=bit14, CS3=bit16 (the CS2/CS3
     * fields are 2 bits wide, shared with GPIO_B6/B7, so clear before setting).
     * Verified on-device: with these bits set all four CE# return the Micron ID
     * and read distinct per-die data.
     */
    SCU_IOMUXA_CON = (SCU_IOMUXA_CON & ~((3u << 14) | (3u << 16))) |
                     IOMUX_NAND_CS1 | IOMUX_NAND_CS2 | IOMUX_NAND_CS3;
#endif

    nand_deselect();

    return nand_get_device_type(0) ? 0 : 1;
}

void nand_power_up(void)
{
    SCU_CLKCFG &= ~CLKCFG_NAND;
}

void nand_power_down(void)
{
}

void nand_set_active(void)
{
    nand_power_up();
    last_activity = current_tick;
}

long nand_last_activity(void)
{
    return last_activity;
}

int nand_spinup_time(void)
{
    return 0;
}
