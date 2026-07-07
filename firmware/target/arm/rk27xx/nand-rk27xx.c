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

/* NAND interface wait-state timing (AHB0_NANDC+0x04). 0x1041 is the shipped
 * default: FMWAIT paces EVERY AHB<->NAND transfer -- the hwecc PAGE_BUF staging,
 * the FLCTL BCH loads, and every FTL/COW read, not just the raw byte-PIO path --
 * so tightening it from the conservative 0x1081 to 0x1041 speeds the whole write
 * and read pipeline. Measured on-device: the 2-plane build at 0x1041 sustained
 * 4 MB/s on real file copies (md5-verified); a mistaken "cleanup" to 0x1081
 * halved that to ~2 MB/s -- 0x1041 is the real-world default, not just a raw-path
 * micro-opt. Verified bad=0 in the FMWAIT sweep; tighter values plateaued.
 * Overridable for re-sweeps via -DC4PRO_NAND_FMWAIT_VALUE. */
#ifndef C4PRO_NAND_FMWAIT_VALUE
#define C4PRO_NAND_FMWAIT_VALUE 0x1041u
#endif
uint32_t c4pro_nand_fmwait = C4PRO_NAND_FMWAIT_VALUE;

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

#ifdef C4PRO_WDT_CRASH_CAPTURE
/* Fine-grained NAND-op stage marker for the WDT crash capture (rec[4]). The
 * coarse phase marker (c4pro_hb_phase=5) spans the WHOLE c4own_write_page call
 * region including the COW-fill reads between programs, so it cannot tell which
 * access actually wedged the NANDC slave port. This marker pinpoints the
 * instruction region: (region << 8) | sector, where region is
 *   1 = staging copy into PAGE_BUF/SPARE_BUF   (program path)
 *   2 = FL_RDY wait after firing the sector transfer (program path)
 *   3 = program-confirm 0x10 issued, tPROG completion poll
 *   4 = FL_RDY wait after firing a read sector transfer (hwecc read path)
 *   5 = drain copy out of PAGE_BUF               (hwecc read path)
 *   6 = write-op entry (FL_RST / select / ready wait)
 *   7 = write-op cmd/addr issue (0x80 + row)
 *   8 = deselect (per-op FMCTL write, both paths)
 *   9 = read-op cmd/addr issue (0x00 + row + 0x30)
 *   A = read-op tR ready wait
 *   0 = idle / op completed
 * Plain DRAM stores only -- the tick ISR must NEVER read NANDC registers to
 * capture this (a wedged slave port would stall the capturer itself). */
volatile uint32_t c4pro_nand_stage;
#define NAND_STAGE(x) (c4pro_nand_stage = (x))
#else
#define NAND_STAGE(x) do { } while (0)
#endif

/* Per-sector BCH control value, written FRESH (no read-modify-write) under
 * C4PRO_NAND_BCH_NO_RMW. The default RMW pattern (BCHCTL = (BCHCTL & ~0x1C03) |
 * ...) READS BCHCTL while the BCH engine may be mid-encode/decode -- an access
 * type stock provably never performs (its trigger routine at 0x60081214 writes a
 * fresh-computed BCHCTL, and never reads it). A CPU read of a busy engine's
 * control register is the prime remaining suspect for the residual phase-5 AHB
 * slave-port stall, and this is the last "we do, stock doesn't" divergence in
 * the pinpointed region. The fresh value is fully determined: entry code sets
 * BCHCTL = 1 (strength-8 preset, bit13=0), so nothing outside bits 0,1,10,11
 * ever needs preserving. */
#ifdef C4PRO_NAND_BCH_NO_RMW
#define C4PRO_BCHCTL_WR(bank)  (BCHCTL = 3u | (((bank) & 3u) << 10))
#define C4PRO_BCHCTL_RD(bank)  (BCHCTL = 1u | (((bank) & 3u) << 10))
#else
#define C4PRO_BCHCTL_WR(bank) \
    (BCHCTL = (BCHCTL & ~0x1C03u) | 1u | 2u | (((bank) & 3u) << 10))
#define C4PRO_BCHCTL_RD(bank) \
    (BCHCTL = (BCHCTL & ~0x1C03u) | 1u | (((bank) & 3u) << 10))
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

#ifdef C4PRO_NAND_FMCTL_NO_RMW
/* Cached FMCTL non-CE bits (bit4 FM_PROTECT is the only writable one; bit5
 * FM_RDY is read-only status), snapshotted once at nand_device_init() while the
 * controller is quiescent. Lets select/deselect write FMCTL fresh instead of
 * read-modify-write: the st=0x000/phase=5 freeze localization puts the residual
 * AHB wedge at the op BOUNDARY, exactly where these two per-op FMCTL READS live
 * -- the same CPU-reads-NANDC-register-at-engine-transition pattern whose
 * elimination on BCHCTL (C4PRO_NAND_BCH_NO_RMW) already dropped the wedge
 * frequency (host format went from always-freezing to passing). Default covers
 * the pre-init window (WP# high = writes permitted), overwritten at init. */
static unsigned long c4pro_nand_fmctl_base = FM_PROTECT;
#endif

static void nand_select(uint32_t bank)
{
#ifndef C4PRO_NAND_FMCTL_NO_RMW
    unsigned long ctl = FMCTL;
#endif

#ifdef COLORFLY_C4_PRO
    /* Re-assert the NAND CS2/CS3 pin-mux on every select. These IOMUXA bits
     * (14/16) are SHARED with the codec-I2C routing, and a codec transaction
     * (i2c_iomux) transiently clears them; re-asserting here guarantees the
     * pins carry the NAND-CS function for this access regardless of I2C timing,
     * so a volume-change I2C write can never corrupt a concurrent die-2/3 read.
     * Mirrors the setup in nand_device_init().
     * C4PRO_NAND_IOMUX_DIE0_SKIP: die 0's CE#0 is a dedicated NAND pin that
     * needs no mux, so for bank-0 ops the reassert is pure overhead -- and it is
     * a per-op SCU read-modify-write sitting exactly in the op-boundary window
     * the residual freeze localizes to. Skipping it for bank 0 removes the last
     * boundary register READ for die-0 traffic (which is where mkfs/dd land)
     * while keeping the corruption guard for dies 1-3. */
#ifdef C4PRO_NAND_IOMUX_DIE0_SKIP
    if (bank != 0)
#endif
    {
        SCU_IOMUXA_CON = (SCU_IOMUXA_CON & ~((3u << 14) | (3u << 16))) |
                         IOMUX_NAND_CS1 | IOMUX_NAND_CS2 | IOMUX_NAND_CS3;
    }
#endif

#ifdef C4PRO_NAND_FMCTL_NO_RMW
    FMCTL = c4pro_nand_fmctl_base | (1u << bank);
#else
    ctl &= 0xf0;
    ctl |= 1u << bank;
    FMCTL = ctl;
#endif
}

static void nand_deselect(void)
{
#ifdef C4PRO_NAND_FMCTL_NO_RMW
    FMCTL = c4pro_nand_fmctl_base;
#else
    FMCTL &= 0xf0;
#endif
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
#ifdef C4PRO_USB_RX_TIMING
/* Per-page write cost split: LD = the 8 per-sector BCH FLCTL loads (0xA26 +
 * nand_wait_fl_ready), TP = the 0x10 program-confirm + tPROG completion wait.
 * Tells us which half of the ~1.37ms/page to attack. */
volatile uint32_t c4pro_wp_load_sum, c4pro_wp_tprog_sum, c4pro_wp_cnt;
volatile uint32_t c4pro_wp_start_sum;   /* entry nand_wait_ready = prev tPROG */
#endif

#ifdef C4PRO_NAND_CACHE_PROGRAM
/* When set, c4pro_nand_write_page_hwecc confirms with 0x11 (cache program)
 * instead of 0x10: the die commits the page to the array in the BACKGROUND and
 * frees its page/cache register immediately, so the NEXT page's data load (LD)
 * overlaps the previous page's array program (SW/tPROG) -- SW+LD collapses to
 * max(SW,LD). The caller must program the LAST page of a run with the flag
 * CLEAR (plain 0x10 + full tPROG wait) to guarantee the final page commits
 * before the block is considered durable. */
volatile int c4pro_nand_cache_prog;
#endif

uint32_t c4pro_nand_write_page_hwecc(uint32_t bank, uint32_t page,
                                     const void* databuffer, uint32_t nsec)
{
    unsigned s, i;
    const unsigned char *data = databuffer;
    unsigned status;
#ifdef C4PRO_USB_RX_TIMING
    uint32_t _wp_t0 = 0, _wp_t1 = 0, _wp_ts = 0;
#endif

    if (bank > 3 || !data || nsec == 0 || nsec > 8)
        return 1;
#ifdef C4PRO_USB_RX_TIMING
    _wp_ts = TMR2CVR;
#endif

    NAND_STAGE(0x600u);   /* write-op entry: FL_RST/select/ready wait */
    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;

    nand_select(bank);
    nand_prog_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        return 1;
    }

#ifdef C4PRO_USB_RX_TIMING
    c4pro_wp_start_sum += (_wp_ts - TMR2CVR) & 0xFFFFFFFFu;
#endif
    BCHCTL = 1;   /* strength-8 preset leaves bit13=0 */

    NAND_STAGE(0x700u);   /* write-op cmd/addr issue */
    /* serial data input: 0x80 + col=0 + 3 row bytes */
    FLASH_CMD(bank) = 0x80;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;

#ifdef C4PRO_USB_RX_TIMING
    _wp_t0 = TMR2CVR;
#endif
    for (s = 0; s < nsec; s++)
    {
        volatile unsigned char *pb =
            (volatile unsigned char *)&PAGE_BUF + ((s & 3) << 9);
        volatile unsigned char *sb =
            (volatile unsigned char *)&SPARE_BUF + ((s & 3) << 4);

        NAND_STAGE(0x100u | s);
        /* 32-bit word copy into on-chip PAGE_BUF (was a 512-iter byte loop =
         * 4096 MMIO stores/page). PAGE_BUF slot is 512-aligned and c4own_page_data
         * is word-aligned, so this is safe and ~4x fewer AHB stores -- shaving the
         * staging portion of LD. Mirrors c4pro_nand_prog_pages_hwecc_multi. */
        {
            volatile uint32_t *pbw = (volatile uint32_t *)pb;
            const uint32_t *dw = (const uint32_t *)(data + s * 512);
            for (i = 0; i < 512u / 4u; i++)
                pbw[i] = dw[i];
        }
        {
            volatile uint32_t *sbw = (volatile uint32_t *)sb;
            for (i = 0; i < 16u / 4u; i++)
                sbw[i] = 0xffffffffu;
        }

        FLCTL = FL_RST;
        C4PRO_BCHCTL_WR(bank);
        /* FL_WR|FL_START|(1 sector)|FL_XFER_EN|FL_COR_EN + start-sector */
        NAND_STAGE(0x200u | s);
        FLCTL = 0xA26u | ((s & 3u) << 3);
        if (!nand_wait_fl_ready())
        {
            nand_deselect();
            return 2;
        }
    }

#ifdef C4PRO_USB_RX_TIMING
    _wp_t1 = TMR2CVR;
    c4pro_wp_load_sum += (_wp_t0 - _wp_t1) & 0xFFFFFFFFu;
    c4pro_wp_cnt++;
#endif
    NAND_STAGE(0x300u);
#ifdef C4PRO_NAND_CACHE_PROGRAM
    /* 0x11 = cache program (pipeline tPROG under the next page's load); plain
     * 0x10 for the flag-clear last page so the block fully commits. */
    FLASH_CMD(bank) = c4pro_nand_cache_prog ? 0x11u : 0x10u;
#else
    FLASH_CMD(bank) = 0x10;   /* program confirm */
#endif

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
#ifdef C4PRO_USB_RX_TIMING
    c4pro_wp_tprog_sum += (_wp_t1 - TMR2CVR) & 0xFFFFFFFFu;
#endif
    NAND_STAGE(0x800u);   /* deselect (per-op FMCTL write) */
    nand_deselect();
    NAND_STAGE(0);
    last_activity = current_tick;

    return (status & 0x01) ? 4 : 0;   /* bit0 = program FAIL */
}

#if defined(C4PRO_NAND_2PLANE) || defined(C4PRO_NAND_2PLANE_TEST) || \
    defined(C4PRO_NAND_2PLANE_PCYCLE_TEST)
/* Stage one plane's payload into the FMC via the SAME proven per-sector BCH path
 * as c4pro_nand_write_page_hwecc (PAGE_BUF slots + FLCTL=0xA26|((s&3)<<3)). The
 * 0x80/0x81 serial-input command must already be open for this plane.
 * HARDWARE-BCH inline 512+16 layout -- the layout every stable production build
 * uses (all S-series/DAILY builds define C4PRO_NAND_HWECC), so pages read back
 * via c4pro_nand_read_page_hwecc byte-identically to single-plane writes. */
static bool c4pro_2plane_stage(uint32_t bank, const unsigned char *data,
                               unsigned nsec)
{
    unsigned s, i;
    for (s = 0; s < nsec; s++)
    {
        volatile unsigned char *pb =
            (volatile unsigned char *)&PAGE_BUF + ((s & 3) << 9);
        volatile unsigned char *sb =
            (volatile unsigned char *)&SPARE_BUF + ((s & 3) << 4);
        volatile uint32_t *pbw = (volatile uint32_t *)pb;
        const uint32_t *dw = (const uint32_t *)(data + s * 512);
        volatile uint32_t *sbw = (volatile uint32_t *)sb;

        for (i = 0; i < 512u / 4u; i++)
            pbw[i] = dw[i];
        for (i = 0; i < 16u / 4u; i++)
            sbw[i] = 0xffffffffu;

        FLCTL = FL_RST;
        C4PRO_BCHCTL_WR(bank);
        FLCTL = 0xA26u | ((s & 3u) << 3);
        if (!nand_wait_fl_ready())
            return false;
    }
    return true;
}

/* Two-plane completion wait, matching the stock discipline (rkboot_s3 program
 * fn 0x60705b68): wait the controller-ready bit FIRST, THEN poll the NAND's own
 * READ-STATUS byte (0x70) until bit6 (R/B ready) is set, and only then return
 * the status byte so the caller samples the per-plane P/F bits after the shared
 * two-plane tPROG has truly completed. Our old single nand_wait_ready() sampled
 * status a hair early on a slow die. Returns the final status byte, or -1 on
 * timeout. */
static int c4pro_2plane_wait_status(uint32_t bank)
{
    unsigned guard, status;

    if (!nand_wait_ready())
        return -1;

    guard = 400000u;
    do
    {
        FLASH_CMD(bank) = 0x70;
        status = FLASH_DATA(bank);
        if (status & 0x40u)             /* bit6 = R/B ready */
            return (int)status;
    } while (--guard);

    return -1;
}

/*
 * Two-plane page program -- the stock ~6.7 MB/s lever (2 pages per single tPROG).
 *
 * Programs plane 0 at row0 and plane 1 at row1 in ONE array-program using the
 * Micron two-plane sequence (verified from rkboot_s3.bin program fn 0x60707afc,
 * agent disassembly 2026-07-03; stride + round-trip + 2x speed validated
 * on-device via C4PRO_NAND_2PLANE_TEST: s0 st0 m0,0, 8533 vs 4266 KB/s):
 *     0x80 - addr(row0) - data0 - 0x11   (plane-0 serial input + dummy confirm)
 *     0x81 - addr(row1) - data1 - 0x10   (plane-1 serial input + final commit)
 *     0x70  status                        (bit0 = unified two-plane P/F)
 * Payload staging per plane reuses the proven single-plane hwecc BCH path
 * (c4pro_2plane_stage); only the command bytes and the two-phase wrapper differ,
 * so pages are byte-identical on read via c4pro_nand_read_page_hwecc.
 *
 * row1 MUST be row0 with ONLY the plane block-address bit toggled (bit 0 of the
 * block address = +128 in row units -- the on-device-validated stride): same die,
 * same page-in-block, same column.
 *
 * DANGER (validated the hard way): emitting 0x11 WITHOUT the matching
 * 0x81..0x10 second phase corrupts silently -- never split the two phases.
 */
uint32_t c4pro_nand_write_page_2plane(uint32_t bank, uint32_t row0, uint32_t row1,
                                      const void *buf0, const void *buf1,
                                      uint32_t nsec)
{
    if (bank > 3 || !buf0 || !buf1 || nsec == 0 || nsec > 8)
        return 1;

    NAND_STAGE(0x2100u);   /* 2plane entry: FL_RST/select/ready wait */
    FMWAIT = c4pro_nand_fmwait;
    FLCTL = FL_RST;
    nand_select(bank);
    nand_prog_delay();
    if (!nand_wait_ready())
    {
        nand_deselect();
        NAND_STAGE(0);
        return 1;
    }
    BCHCTL = 1;

    /* ---- PHASE 0: plane 0 serial input, then dummy program confirm 0x11 ---- */
    NAND_STAGE(0x2200u);   /* plane-0 cmd/addr + staging */
    FLASH_CMD(bank) = 0x80;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = row0 & 0xff;
    FLASH_ADDR(bank) = (row0 >> 8) & 0xff;
    FLASH_ADDR(bank) = (row0 >> 16) & 0xff;
    if (!c4pro_2plane_stage(bank, buf0, nsec))
    {
        /* No confirm was issued yet: the open 0x80 serial-input is cleanly
         * superseded by the next op's command, but reset anyway for symmetry. */
        goto abort_reset;
    }
    NAND_STAGE(0x2300u);   /* 0x11 dummy confirm wait */
    FLASH_CMD(bank) = 0x11;   /* plane-0 dummy confirm (no array program yet) */
    /* Inter-plane wait via the NAND's own status bit6 (stock reads the multi-plane
     * read-status here), not just the controller FM_RDY: plane-1 serial input must
     * not begin until plane 0's dummy-busy (tDBSY) has cleared, else plane 0 is
     * latched marginal. */
    if (c4pro_2plane_wait_status(bank) < 0)
        goto abort_reset;

    /* ---- PHASE 1: plane 1 serial input, then final two-plane commit 0x10 ---- */
    NAND_STAGE(0x2400u);   /* plane-1 cmd/addr + staging */
    FLASH_CMD(bank) = 0x81;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = row1 & 0xff;
    FLASH_ADDR(bank) = (row1 >> 8) & 0xff;
    FLASH_ADDR(bank) = (row1 >> 16) & 0xff;
    if (!c4pro_2plane_stage(bank, buf1, nsec))
        goto abort_reset;
    NAND_STAGE(0x2500u);   /* 0x10 final commit: shared tPROG for both planes */
    FLASH_CMD(bank) = 0x10;   /* final commit -> ONE tPROG programs both planes */
    {
        int st = c4pro_2plane_wait_status(bank);
        if (st < 0)
            goto abort_reset;

        nand_deselect();
        NAND_STAGE(0);
        last_activity = current_tick;

        /* Two-plane program P/F is PER-PLANE on this part: plane 0 in bit0,
         * plane 1 in bit1 (stock samples bit1 for the multi-plane case,
         * rkboot_s3 0x60705c40: lsr status,#1; tst #1). Testing only bit0 -- as
         * we did -- MISSED a failed/marginal plane-1 program: BCH still corrected
         * the fresh read so it looked fine while powered, but the pair was
         * committed with a plane-1 half that decayed across a power cycle. Test
         * BOTH planes' fail bits so a marginal plane retires the pair (the FTL
         * then retries on a fresh pair / falls back to single-plane). */
        return (st & 0x03) ? 4 : 0;
    }

abort_reset:
    /* Never leave the die parked mid two-plane sequence: once 0x11 has been
     * issued, ANY next command other than the 0x81..0x10 continuation is
     * out-of-spec and corrupts silently (the retry path programs this same die
     * single-plane). 0xFF device RESET aborts the pending multi-plane state;
     * wait out tRST before releasing the die. Also issued for pre-0x11 bails --
     * harmless and keeps one recovery path. */
    NAND_STAGE(0x2600u);   /* abort: 0xFF device reset wait */
    FLASH_CMD(bank) = 0xff;
    nand_wait_ready();
    nand_deselect();
    NAND_STAGE(0);
    return 3;
}
#endif /* C4PRO_NAND_2PLANE || C4PRO_NAND_2PLANE_TEST || C4PRO_NAND_2PLANE_PCYCLE_TEST */

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

    NAND_STAGE(0x900u);   /* read-op cmd/addr issue */
    FLASH_CMD(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = 0x00;
    FLASH_ADDR(bank) = page & 0xff;
    FLASH_ADDR(bank) = (page >> 8) & 0xff;
    FLASH_ADDR(bank) = (page >> 16) & 0xff;
    FLASH_CMD(bank) = 0x30;

    NAND_STAGE(0xA00u);   /* read-op tR wait after 0x30 */
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

        C4PRO_BCHCTL_RD(bank);
        NAND_STAGE(0x400u | s);
        FLCTL = 0xA24u | ((s & 3u) << 3);
        if (!nand_wait_fl_ready())
        {
            nand_deselect();
            return 2;
        }
        /* BCHST read happens with the engine already FL_RDY-idle, unlike the
         * (optional) BCHCTL RMW above which reads mid-run -- keep it. */
        c4pro_nand_last_bchst |= BCHST;   /* OR of every sector's BCH status */
        NAND_STAGE(0x500u | s);
        for (i = 0; i < 512; i++)
            data[s * 512 + i] = pb[i];
    }

    NAND_STAGE(0x800u);   /* deselect (per-op FMCTL write) */
    nand_deselect();
    NAND_STAGE(0);
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

    NAND_STAGE(0xB00u);   /* erase entry/issue */
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

    NAND_STAGE(0xC00u);   /* tERASE wait */
    if (!nand_wait_ready_erase())
    {
        nand_deselect();
        return 1;
    }

    FLASH_CMD(bank) = 0x70;
    status = FLASH_DATA(bank);
    NAND_STAGE(0x800u);   /* deselect */
    nand_deselect();
    NAND_STAGE(0);
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
#ifdef C4PRO_USB_RX_TIMING
volatile uint32_t c4pro_ml_load_sum, c4pro_ml_load_cnt, c4pro_ml_prog_sum;
static uint32_t c4pro_ml_p1_save;
#endif

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

#ifdef C4PRO_USB_RX_TIMING
    uint32_t _ml_p0 = TMR2CVR;
#endif
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
            C4PRO_BCHCTL_WR(bank);
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

#ifdef C4PRO_USB_RX_TIMING
    /* Split the multi-die program: LOAD = phase-1 serial staging+FLCTL transfer
     * of all n dies (does NOT overlap across dies); PROG = phase-2 tPROG wait
     * (DOES overlap). If LOAD >> PROG, die-interleave can't help -- the data-load
     * is the wall, and the fix is that path, not more dies. */
    {
        extern volatile uint32_t c4pro_ml_load_sum, c4pro_ml_load_cnt;
        extern volatile uint32_t c4pro_ml_prog_sum;
        uint32_t _ml_p1 = TMR2CVR;
        c4pro_ml_load_sum += (_ml_p0 - _ml_p1) & 0xFFFFFFFFu;
        c4pro_ml_load_cnt++;
        c4pro_ml_p1_save = _ml_p1;
    }
#endif
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

#ifdef C4PRO_USB_RX_TIMING
    {
        extern volatile uint32_t c4pro_ml_prog_sum;
        c4pro_ml_prog_sum += (c4pro_ml_p1_save - TMR2CVR) & 0xFFFFFFFFu;
    }
#endif
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

#ifdef C4PRO_NAND_FMCTL_NO_RMW
    /* One-time snapshot of FMCTL's non-CE bits while the controller is
     * quiescent (clock just ungated, no op in flight) so per-op select/deselect
     * never has to READ FMCTL again. Force FM_PROTECT (WP# high) into the base:
     * if the boot path happened to leave WP asserted at snapshot time, every
     * later program/erase would silently fail (a possible cause of dead-looking
     * cold boots). All writes legitimately require WP# high anyway. */
    c4pro_nand_fmctl_base = (FMCTL & 0xf0u) | FM_PROTECT;
#endif

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
