/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_  *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2010 by Bertrik Sikken
 * Copyright (C) 2026 by Colorfly C4 Pro contributors
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

#ifdef COLORFLY_C4_PRO
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
#include "kernel.h"
#include "string.h"
#include <stdbool.h>

const struct nand_device_info_type* ftl_nand_type;
uint32_t ftl_banks;

#if defined(C4PRO_PHASE_MARK)
/* Write-path phase marker for the heartbeat freeze localizer (also read by the
 * visible-fault handler to tag WHICH stage faulted). Set at entry of each
 * sustained-write stage (all run in usb-thread context, so no IRQ race). On a
 * silent freeze the frozen value = the innermost stage the usb thread reached:
 *   1  USB (usb_storage, between/after a write)   2  usb_storage write dispatch
 *   3  c4own_write_logical_block                  4  superblock/checkpoint flush
 *   5  c4own_write_page (NAND hwecc program)      6  c4own_erase_pblk (erase)
 *   7  COW-fill / cache-load NAND read (between programs; previously reported
 *      as 5, hiding whether the read or the program engine wedged)
 *   10 send_csw (about to send CSW)               11 bulk-IN sync sender (CSW DMA)
 *   12 CSW sent, arming next-command receive     13 armed, waiting for host
 *                                                    bulk-OUT WRITE data (the
 *                                                    exact state usbmon
 *                                                    caught wedged 2026-07-02,
 *                                                    see freeze doc s5.1/5.2)
 *
 * Defined outside the owned-RW block below because its readers (UIE in
 * system-arm-classic.c, usb.c, usb_storage.c, usb-drv-rk27xx.c) are gated on
 * C4PRO_PHASE_MARK alone, which colorflyc4pro.h enables unconditionally -- a
 * build without C4PRO_FTL_ROCKBOX_OWNED_RW used to fail to link on this. */
volatile uint32_t c4pro_hb_phase;
#endif

#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)

#define C4OWN_MAGIC_SB 0x31465443u /* CTF1 */
#define C4OWN_MAGIC_HDR 0x31484b52u /* RKH1 */
#define C4OWN_MAGIC_COMMIT 0x31434b52u /* RKC1 */
#define C4OWN_MAGIC_CKPT 0x314b4f43u /* COK1 - mount checkpoint footer */
#define C4OWN_MAGIC_DIRTY 0x31524443u /* CDR1 - checkpoint invalidation */
/* v4: owned-volume data path gained BCH ECC (parity on write, hardware correct
 * on read). v3 pages have no valid ECC and are unreadable by the 0xA24 decoder,
 * so the version bump forces a one-time reformat migrating off v3. */
#define C4OWN_VERSION 4u
#define C4OWN_SECTORS_PER_PAGE 8u
#define C4OWN_PAGES_PER_BLOCK 128u
#define C4OWN_SECTORS_PER_BLOCK 1024u
#define C4OWN_PAYLOAD_SECTORS_PER_BLOCK \
    ((C4OWN_PAGES_PER_BLOCK - 2u) * C4OWN_SECTORS_PER_PAGE)
#define C4OWN_FIRST_DATA_PBLK 2u

/* Mount checkpoint: a sparse snapshot of the live logical->physical map, stored
 * in the otherwise-unused spare pages of the two superblock blocks (pblk 0/1),
 * so cold mount can skip the full ~64000-block scan. Page 0 of each superblock
 * block stays the superblock; pages 1..125 hold {logical,pblk,gen} entries;
 * page 126 is the footer; page 127 is the dirty/invalidation marker. The two
 * superblock blocks are written alternately so one complete copy always
 * survives a crash. A checkpoint is trusted on mount only if its footer is
 * valid AND no dirty marker (written before the first commit after it) is
 * present; any failure falls back to the full scan. Checkpoint writes never
 * touch data blocks, so a bug here cannot corrupt file data. */
#define C4OWN_CKPT_FIRST_PAGE 1u
#define C4OWN_CKPT_FOOTER_PAGE 126u
#define C4OWN_CKPT_DIRTY_PAGE 127u
#define C4OWN_CKPT_ENTRY_BYTES 10u
#define C4OWN_CKPT_ENTRIES_PER_PAGE (4096u / C4OWN_CKPT_ENTRY_BYTES)
#define C4OWN_CKPT_MAX_ENTRIES \
    ((C4OWN_CKPT_FOOTER_PAGE - C4OWN_CKPT_FIRST_PAGE) * C4OWN_CKPT_ENTRIES_PER_PAGE)

#ifndef C4PRO_OWNED_PBLK_START
#define C4PRO_OWNED_PBLK_START 288u
#endif

#ifndef C4PRO_OWNED_PBLK_COUNT
#define C4PRO_OWNED_PBLK_COUNT 160u
#endif

#ifndef C4PRO_OWNED_VOLUME_SECTORS_MAX
/* Keep two superblocks plus two spare/rotation blocks outside the exposed
 * logical volume. The native build can override this when a smaller test
 * volume is wanted.
 */
#define C4PRO_OWNED_VOLUME_SECTORS_MAX \
    ((C4PRO_OWNED_PBLK_COUNT - C4OWN_FIRST_DATA_PBLK - 2u) * \
     C4OWN_PAYLOAD_SECTORS_PER_BLOCK)
#endif

#define C4OWN_VOLUME_SECTORS c4own_volume_sectors()
#define C4OWN_RUNTIME_PBLK_COUNT c4own_runtime_pblk_count()
#define C4OWN_LOGICAL_BLOCKS \
    ((C4PRO_OWNED_VOLUME_SECTORS_MAX + C4OWN_PAYLOAD_SECTORS_PER_BLOCK - 1u) / \
     C4OWN_PAYLOAD_SECTORS_PER_BLOCK)
#define C4OWN_PARTITION_START 2048u
#define C4OWN_PARTITION_SECTORS (C4OWN_VOLUME_SECTORS - C4OWN_PARTITION_START)
#define C4OWN_IS_FAT32 (C4PRO_OWNED_VOLUME_SECTORS_MAX > 1048576u)
#define C4OWN_FAT32_SECTORS_PER_CLUSTER 8u
#define C4OWN_FAT32_RESERVED_SECTORS 32u
#define C4OWN_FAT32_FAT_SECTORS \
    (((C4OWN_PARTITION_SECTORS / C4OWN_FAT32_SECTORS_PER_CLUSTER) + 127u) / \
     128u + 16u)

#if C4OWN_LOGICAL_BLOCKS + C4OWN_FIRST_DATA_PBLK + 2u > C4PRO_OWNED_PBLK_COUNT
#error "C4PRO_OWNED_PBLK_COUNT is too small for the requested owned FTL volume"
#endif

static uint16_t c4own_map[C4OWN_LOGICAL_BLOCKS];
static uint32_t c4own_gen[C4OWN_LOGICAL_BLOCKS];
static unsigned char c4own_used[C4PRO_OWNED_PBLK_COUNT];
static unsigned char c4own_retired[C4PRO_OWNED_PBLK_COUNT];
static unsigned char c4own_page_data[4096];
static unsigned char c4own_read_page_data[4096];
static uint32_t c4own_read_page;
static uint32_t c4own_read_bank;
static bool c4own_read_page_valid;
#ifndef C4OWN_CACHE_SLOTS
/* 8 slots x 504 KB = ~4 MB of the 16 MiB DRAM. More resident blocks coalesce
 * the scattered FAT/root/FSInfo writes of a host format into fewer whole-block
 * COW flushes (each flush rewrites all 128 pages), cutting write amplification.
 * The audio buffer absorbs the rest of DRAM, so playback is unaffected. */
#define C4OWN_CACHE_SLOTS 8u
#endif

/* Pipelined incremental flush high-water mark: keep this many most-recently-used
 * dirty blocks buffered before flushing the oldest. >1 gives the USB bulk-OUT
 * receive slack to run ahead of the NAND program (speed) and keeps hot metadata
 * (FAT/dir) resident so it isn't thrash-flushed against the data block; small
 * enough that the end-of-file SYNCHRONIZE_CACHE flush stays short (<= this many
 * blocks) and never trips the host's command timeout -> no USB link reset. Must
 * be < C4OWN_CACHE_SLOTS. Tunable via -DC4PRO_FTL_FLUSH_KEEP=N for A/B. */
#ifndef C4PRO_FTL_FLUSH_KEEP
#define C4PRO_FTL_FLUSH_KEEP 3u
#endif

static unsigned char c4own_cache_data[C4OWN_CACHE_SLOTS][C4OWN_PAYLOAD_SECTORS_PER_BLOCK * 512u];
static uint32_t c4own_cache_logical[C4OWN_CACHE_SLOTS];
static uint32_t c4own_cache_age[C4OWN_CACHE_SLOTS];
static uint32_t c4own_cache_tick;
static bool c4own_cache_valid[C4OWN_CACHE_SLOTS];
static bool c4own_cache_dirty[C4OWN_CACHE_SLOTS];
static bool c4own_ready;
static bool c4own_formatting;

#ifdef C4PRO_FTL_DEFER_COW
/* Deferred copy-on-write: instead of pre-reading the whole old block into the
 * cache on every write-miss (c4own_load_cache), track which payload sectors the
 * host actually wrote (bitmap, 1 bit/sector) and read only the UNWRITTEN sectors
 * from the old block at FLUSH time (c4own_cache_backfill). A full-block overwrite
 * (dd, large file copies) writes every sector -> zero COW reads, so rewriting an
 * existing (mapped) block is as fast as writing a fresh one. This is the fix for
 * the "4 MB/s fresh vs 2 MB/s rewrite" gap: the old pre-read was ~504 KB of NAND
 * reads per rewritten block (measured ~24 s of a 64 s / 128 MB dd). */
#define C4OWN_WRITTEN_BYTES ((C4OWN_PAYLOAD_SECTORS_PER_BLOCK + 7u) / 8u)
static uint8_t c4own_cache_written[C4OWN_CACHE_SLOTS][C4OWN_WRITTEN_BYTES];
static inline void c4own_wr_set(uint32_t slot, uint32_t s)
{ c4own_cache_written[slot][s >> 3] |= (uint8_t)(1u << (s & 7u)); }
static inline int c4own_wr_get(uint32_t slot, uint32_t s)
{ return (c4own_cache_written[slot][s >> 3] >> (s & 7u)) & 1u; }
static void c4own_cache_backfill(uint32_t slot);   /* defined after c4own_load_cache */
#endif

/* Mount-checkpoint state. active_block = superblock block (0/1) holding the
 * current on-NAND checkpoint, or -1 if none loaded/written this boot.
 * seq = its monotonic version. marked_dirty = whether the dirty marker for the
 * active checkpoint has already been written (so we only write it once, before
 * the first commit after a checkpoint). map_dirty = whether the live map has
 * changed since the last checkpoint write (so sync only rewrites when needed). */
static int c4own_ckpt_active_block = -1;
static uint32_t c4own_ckpt_seq;
static bool c4own_ckpt_marked_dirty;
static bool c4own_map_dirty;
static unsigned char c4own_ckpt_buf[4096];

static uint32_t c4own_get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t c4own_runtime_pblk_count(void)
{
    uint32_t max_pblk;

    if (!ftl_nand_type || ftl_banks == 0)
        return 0;

    max_pblk = ftl_banks * ftl_nand_type->blocks;
    if (max_pblk <= C4PRO_OWNED_PBLK_START)
        return 0;

    max_pblk -= C4PRO_OWNED_PBLK_START;
    return max_pblk < C4PRO_OWNED_PBLK_COUNT ?
           max_pblk : C4PRO_OWNED_PBLK_COUNT;
}

static uint32_t c4own_volume_sectors(void)
{
    uint32_t pblk_count = c4own_runtime_pblk_count();

    if (pblk_count <= C4OWN_FIRST_DATA_PBLK + 2u)
        return 0;

    return (pblk_count - C4OWN_FIRST_DATA_PBLK - 2u) *
           C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
}

static void c4own_put16(unsigned char *p, uint32_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}

static void c4own_put32(unsigned char *p, uint32_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

static uint32_t c4own_crc32_cont(uint32_t crc, const void *buf, uint32_t len)
{
    const unsigned char *p = buf;

    while (len--)
    {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
    }

    return crc;
}

static uint32_t c4own_crc32(const void *buf, uint32_t len)
{
    return c4own_crc32_cont(0xffffffffu, buf, len) ^ 0xffffffffu;
}

static uint32_t c4own_get16(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t c4own_abs_pblk(uint32_t pblk)
{
    return C4PRO_OWNED_PBLK_START + pblk;
}

static uint32_t c4own_pblk_bank(uint32_t pblk)
{
    return c4own_abs_pblk(pblk) / ftl_nand_type->blocks;
}

static uint32_t c4own_pblk_page(uint32_t pblk)
{
    return (c4own_abs_pblk(pblk) % ftl_nand_type->blocks) *
           C4OWN_PAGES_PER_BLOCK;
}

static bool c4own_pblk_valid(uint32_t pblk)
{
    return pblk < C4OWN_RUNTIME_PBLK_COUNT &&
           c4own_pblk_bank(pblk) < ftl_banks;
}

static int c4own_read_page_corrected(uint32_t bank, uint32_t page,
                                     unsigned char *out4096)
{
    return c4pro_nand_read_page_hwecc(bank, page, out4096,
                                      C4OWN_SECTORS_PER_PAGE);
}

static int c4own_read_phys_sector(uint32_t pblk, uint32_t sector, void *buf)
{
    uint32_t page;

    if (!c4own_pblk_valid(pblk) || sector >= C4OWN_SECTORS_PER_BLOCK)
        return 1;

    page = c4own_pblk_page(pblk) + sector / C4OWN_SECTORS_PER_PAGE;
    /* Key the page cache on BANK too: c4own_pblk_page() is an in-die page index,
     * so on a multi-die volume two blocks one die apart share a page number and
     * would alias to the wrong die's data (silent corruption during playback's
     * interleaved reads -- the "static" on the 30 GB / 4-die volume). */
    if (!c4own_read_page_valid || c4own_read_page != page ||
        c4own_read_bank != c4own_pblk_bank(pblk))
    {
        if (c4own_read_page_corrected(c4own_pblk_bank(pblk), page,
                                      c4own_read_page_data))
            return 1;

        c4own_read_page = page;
        c4own_read_bank = c4own_pblk_bank(pblk);
        c4own_read_page_valid = true;
    }

    memcpy(buf, c4own_read_page_data +
                (sector & (C4OWN_SECTORS_PER_PAGE - 1u)) * 512u,
           512);
    return 0;
}

static int c4own_read_phys_sector_uncached(uint32_t pblk, uint32_t sector,
                                           void *buf)
{
    uint32_t page;

    if (!c4own_pblk_valid(pblk) || sector >= C4OWN_SECTORS_PER_BLOCK)
        return 1;

    /* Read is page-granular, so read+correct the whole page and copy out the
     * requested sector. The cache is invalidated because this path
     * intentionally does not update its key. */
    page = c4own_pblk_page(pblk) + sector / C4OWN_SECTORS_PER_PAGE;
    if (c4own_read_page_corrected(c4own_pblk_bank(pblk), page,
                                  c4own_read_page_data))
    {
        c4own_read_page_valid = false;
        return 1;
    }
    c4own_read_page_valid = false;

    memcpy(buf, c4own_read_page_data +
                (sector & (C4OWN_SECTORS_PER_PAGE - 1u)) * 512u,
           512);
    return 0;
}

#ifdef C4PRO_PHASE_MARK
#define C4PRO_SET_PHASE(n) do { c4pro_hb_phase = (n); } while (0)
#else
#define C4PRO_SET_PHASE(n) do { } while (0)
#endif

static int c4own_write_page(uint32_t pblk, uint32_t page_in_block)
{
    C4PRO_SET_PHASE(5);
    if (!c4own_pblk_valid(pblk) || page_in_block >= C4OWN_PAGES_PER_BLOCK)
        return 1;

    c4own_read_page_valid = false;
    return c4pro_nand_write_page_hwecc(c4own_pblk_bank(pblk),
                                       c4own_pblk_page(pblk) + page_in_block,
                                       c4own_page_data, C4OWN_SECTORS_PER_PAGE);
}

static int c4own_erase_pblk(uint32_t pblk)
{
    C4PRO_SET_PHASE(6);
    if (!c4own_pblk_valid(pblk))
        return 1;

    c4own_read_page_valid = false;
    return c4pro_nand_erase_block(c4own_pblk_bank(pblk), c4own_pblk_page(pblk));
}

static void c4own_make_header(uint32_t logical, uint32_t gen)
{
    memset(c4own_page_data, 0xff, sizeof(c4own_page_data));
    c4own_put32(c4own_page_data + 0, C4OWN_MAGIC_HDR);
    c4own_put32(c4own_page_data + 4, C4OWN_VERSION);
    c4own_put32(c4own_page_data + 8, logical);
    c4own_put32(c4own_page_data + 12, gen);
    c4own_put32(c4own_page_data + 16,
                c4own_crc32(c4own_page_data, 16));
}

static bool c4own_parse_header(const unsigned char *sector, uint32_t *logical,
                               uint32_t *gen)
{
    if (c4own_get32(sector) != C4OWN_MAGIC_HDR ||
        c4own_get32(sector + 4) != C4OWN_VERSION ||
        c4own_get32(sector + 16) != c4own_crc32(sector, 16))
        return false;

    *logical = c4own_get32(sector + 8);
    *gen = c4own_get32(sector + 12);
    return *logical < C4OWN_LOGICAL_BLOCKS;
}

static void c4own_make_commit(uint32_t logical, uint32_t gen)
{
    unsigned char *commit;

    memset(c4own_page_data, 0xff, sizeof(c4own_page_data));
    commit = c4own_page_data + 7u * 512u;
    c4own_put32(commit + 0, C4OWN_MAGIC_COMMIT);
    c4own_put32(commit + 4, C4OWN_VERSION);
    c4own_put32(commit + 8, logical);
    c4own_put32(commit + 12, gen);
    c4own_put32(commit + 16, c4own_crc32(commit, 16));
}

static bool c4own_parse_commit(const unsigned char *sector, uint32_t logical,
                               uint32_t gen)
{
    return c4own_get32(sector) == C4OWN_MAGIC_COMMIT &&
           c4own_get32(sector + 4) == C4OWN_VERSION &&
           c4own_get32(sector + 8) == logical &&
           c4own_get32(sector + 12) == gen &&
           c4own_get32(sector + 16) == c4own_crc32(sector, 16);
}

static void c4own_reset_maps(void)
{
    for (uint32_t i = 0; i < C4OWN_LOGICAL_BLOCKS; i++)
    {
        c4own_map[i] = 0xffffu;
        c4own_gen[i] = 0;
    }

    memset(c4own_used, 0, sizeof(c4own_used));
    memset(c4own_retired, 0, sizeof(c4own_retired));
    c4own_used[0] = 1;
    c4own_used[1] = 1;
    for (uint32_t slot = 0; slot < C4OWN_CACHE_SLOTS; slot++)
    {
        c4own_cache_valid[slot] = false;
        c4own_cache_dirty[slot] = false;
        c4own_cache_logical[slot] = 0;
        c4own_cache_age[slot] = 0;
    }
    c4own_cache_tick = 0;
    c4own_read_page_valid = false;
}

static bool c4own_superblock_valid(uint32_t pblk)
{
    unsigned char sector[512];

    if (c4own_read_phys_sector_uncached(pblk, 0, sector))
        return false;

    return c4own_get32(sector) == C4OWN_MAGIC_SB &&
           c4own_get32(sector + 4) == C4OWN_VERSION &&
           c4own_get32(sector + 8) == C4OWN_VOLUME_SECTORS &&
           c4own_get32(sector + 12) == C4PRO_OWNED_PBLK_START &&
           c4own_get32(sector + 16) == C4OWN_RUNTIME_PBLK_COUNT &&
           c4own_get32(sector + 20) == C4OWN_PAYLOAD_SECTORS_PER_BLOCK &&
           c4own_get32(sector + 28) == c4own_crc32(sector, 28);
}

static void c4own_fill_superblock_page(void)
{
    C4PRO_SET_PHASE(4);
    memset(c4own_page_data, 0xff, sizeof(c4own_page_data));
    c4own_put32(c4own_page_data + 0, C4OWN_MAGIC_SB);
    c4own_put32(c4own_page_data + 4, C4OWN_VERSION);
    c4own_put32(c4own_page_data + 8, C4OWN_VOLUME_SECTORS);
    c4own_put32(c4own_page_data + 12, C4PRO_OWNED_PBLK_START);
    c4own_put32(c4own_page_data + 16, C4OWN_RUNTIME_PBLK_COUNT);
    c4own_put32(c4own_page_data + 20, C4OWN_PAYLOAD_SECTORS_PER_BLOCK);
    c4own_put32(c4own_page_data + 24, C4OWN_LOGICAL_BLOCKS);
    c4own_put32(c4own_page_data + 28, c4own_crc32(c4own_page_data, 28));
}

static int c4own_write_superblock(uint32_t pblk)
{
    if (c4own_erase_pblk(pblk))
        return 1;

    c4own_fill_superblock_page();
    return c4own_write_page(pblk, 0);
}

/* Read a full 4096-byte page from a superblock block (pblk 0/1) into buf. */
static int c4own_ckpt_read_block_page(uint32_t pblk, uint32_t page_in_block,
                                      unsigned char *buf)
{
    if (!c4own_pblk_valid(pblk) || page_in_block >= C4OWN_PAGES_PER_BLOCK)
        return 1;

    return c4own_read_page_corrected(c4own_pblk_bank(pblk),
                                     c4own_pblk_page(pblk) + page_in_block,
                                     buf);
}

/* Write the dirty/invalidation marker for the active checkpoint. Must be called
 * (and succeed) BEFORE the first data-block commit after a checkpoint, so a
 * crash can never leave a clean checkpoint that omits a committed block. */
static int c4own_ckpt_mark_dirty(void)
{
#ifndef C4PRO_FTL_MOUNT_CHECKPOINT
    /* Checkpoint disabled (see mount): this writes page 127 out of order into the
     * superblock block -- MLC-illegal, corrupts it. No-op; mount always scans. */
    return 0;
#else
    if (c4own_ckpt_active_block < 0)
        return 0;

    memset(c4own_page_data, 0xff, sizeof(c4own_page_data));
    c4own_put32(c4own_page_data + 0, C4OWN_MAGIC_DIRTY);
    c4own_put32(c4own_page_data + 4, c4own_ckpt_seq);
    c4own_put32(c4own_page_data + 8, c4own_crc32(c4own_page_data, 8));
    if (c4own_write_page((uint32_t)c4own_ckpt_active_block,
                         C4OWN_CKPT_DIRTY_PAGE))
        return 1;

    c4own_ckpt_marked_dirty = true;
    return 0;
#endif
}

/* Serialize the live map into a fresh checkpoint on the inactive superblock
 * block. Best-effort: on any failure the old checkpoint (already dirty-marked,
 * because map_dirty implies a prior commit) keeps mount on the scan path. */
static int c4own_ckpt_write(void)
{
#ifndef C4PRO_FTL_MOUNT_CHECKPOINT
    /* Checkpoint disabled (see mount): this ERASES the superblock block and
     * rewrites it with out-of-order pages (page 0, a few entries, footer@126) --
     * MLC-illegal, so it corrupts the very superblock that gates mount validity
     * (observed 2026-07-07 as "FS gone"/reformat). No-op; the superblock is
     * written cleanly (page 0 only) by c4own_write_superblock at format time, and
     * mount is authoritative via the full scan. */
    return 0;
#else
    uint32_t target = (c4own_ckpt_active_block == 0) ? 1u : 0u;
    uint32_t count = 0;
    uint32_t entries_crc = 0xffffffffu;
    uint32_t logical = 0;
    uint32_t written = 0;
    uint32_t page_in_block = C4OWN_CKPT_FIRST_PAGE;

    for (uint32_t l = 0; l < C4OWN_LOGICAL_BLOCKS; l++)
        if (c4own_map[l] != 0xffffu)
            count++;

    if (count > C4OWN_CKPT_MAX_ENTRIES)
        return 1; /* too full to snapshot; leave dirty so mount scans */

    if (c4own_erase_pblk(target))
        return 1;

    c4own_fill_superblock_page();
    if (c4own_write_page(target, 0))
        return 1;

    while (written < count)
    {
        uint32_t in_page = 0;

        memset(c4own_page_data, 0xff, sizeof(c4own_page_data));
        while (in_page < C4OWN_CKPT_ENTRIES_PER_PAGE && written < count)
        {
            unsigned char *ent;

            while (logical < C4OWN_LOGICAL_BLOCKS && c4own_map[logical] == 0xffffu)
                logical++;
            if (logical >= C4OWN_LOGICAL_BLOCKS)
                break;

            ent = c4own_page_data + in_page * C4OWN_CKPT_ENTRY_BYTES;
            c4own_put32(ent + 0, logical);
            c4own_put16(ent + 4, c4own_map[logical]);
            c4own_put32(ent + 6, c4own_gen[logical]);
            logical++;
            in_page++;
            written++;
        }

        entries_crc = c4own_crc32_cont(entries_crc, c4own_page_data,
                                       in_page * C4OWN_CKPT_ENTRY_BYTES);
        if (c4own_write_page(target, page_in_block))
            return 1;
        page_in_block++;
    }
    entries_crc ^= 0xffffffffu;

    memset(c4own_page_data, 0xff, sizeof(c4own_page_data));
    c4own_put32(c4own_page_data + 0, C4OWN_MAGIC_CKPT);
    c4own_put32(c4own_page_data + 4, C4OWN_VERSION);
    c4own_put32(c4own_page_data + 8, C4OWN_VOLUME_SECTORS);
    c4own_put32(c4own_page_data + 12, C4PRO_OWNED_PBLK_START);
    c4own_put32(c4own_page_data + 16, C4OWN_RUNTIME_PBLK_COUNT);
    c4own_put32(c4own_page_data + 20, C4OWN_PAYLOAD_SECTORS_PER_BLOCK);
    c4own_put32(c4own_page_data + 24, c4own_ckpt_seq + 1u);
    c4own_put32(c4own_page_data + 28, count);
    c4own_put32(c4own_page_data + 32, entries_crc);
    c4own_put32(c4own_page_data + 36, c4own_crc32(c4own_page_data, 36));
    if (c4own_write_page(target, C4OWN_CKPT_FOOTER_PAGE))
        return 1;

    c4own_ckpt_seq += 1u;
    c4own_ckpt_active_block = (int)target;
    c4own_ckpt_marked_dirty = false;
    c4own_map_dirty = false;
    c4own_read_page_valid = false;
    return 0;
#endif
}

/* Try to populate the maps from a valid, non-dirty checkpoint. Returns true on
 * success (mount may skip the full scan); false (with maps reset) otherwise. */
static bool c4own_ckpt_load(void)
{
    int max_block = -1;
    uint32_t max_seq = 0;
    uint32_t max_count = 0;
    uint32_t remaining;
    uint32_t page_in_block;
    uint32_t crc;
    uint32_t ecrc;

    /* Find the highest-seq valid checkpoint footer across both superblock
     * blocks. seq is globally monotonic, so the highest seq is the most recent
     * checkpoint. An older (lower-seq) footer is stale BY CONSTRUCTION - it was
     * dirty-marked before the newer one was written - so it must never be
     * loaded; only the single latest footer is ever a load candidate. */
    for (uint32_t b = 0; b < 2u; b++)
    {
        unsigned char *f = c4own_ckpt_buf;
        uint32_t seq;
        uint32_t count;

        if (c4own_ckpt_read_block_page(b, C4OWN_CKPT_FOOTER_PAGE, f))
            continue;
        if (c4own_get32(f + 0) != C4OWN_MAGIC_CKPT ||
            c4own_get32(f + 4) != C4OWN_VERSION ||
            c4own_get32(f + 8) != C4OWN_VOLUME_SECTORS ||
            c4own_get32(f + 12) != C4PRO_OWNED_PBLK_START ||
            c4own_get32(f + 16) != C4OWN_RUNTIME_PBLK_COUNT ||
            c4own_get32(f + 20) != C4OWN_PAYLOAD_SECTORS_PER_BLOCK ||
            c4own_get32(f + 36) != c4own_crc32(f, 36))
            continue;

        seq = c4own_get32(f + 24);
        count = c4own_get32(f + 28);
        if (count > C4OWN_CKPT_MAX_ENTRIES)
            continue;

        if (max_block < 0 || seq > max_seq)
        {
            max_block = (int)b;
            max_seq = seq;
            max_count = count;
        }
    }

    if (max_block < 0)
        return false;

    /* Keep the seq counter monotonic across this boot even if we fall through
     * to a scan: the next checkpoint written must outrank every footer now on
     * NAND, so a stale survivor can never win selection on a later mount. */
    c4own_ckpt_seq = max_seq;

    /* The latest checkpoint is usable only if it is clean. Fail CLOSED: if the
     * dirty-marker page cannot even be read, assume it might be dirty and scan
     * rather than risk loading a checkpoint that omits committed blocks. */
    if (c4own_ckpt_read_block_page((uint32_t)max_block, C4OWN_CKPT_DIRTY_PAGE,
                                   c4own_ckpt_buf))
        return false;
    if (c4own_get32(c4own_ckpt_buf + 0) == C4OWN_MAGIC_DIRTY &&
        c4own_get32(c4own_ckpt_buf + 8) == c4own_crc32(c4own_ckpt_buf, 8) &&
        c4own_get32(c4own_ckpt_buf + 4) >= max_seq)
        return false;

    /* Re-read the footer (the dirty read above clobbered the buffer) and load. */
    if (c4own_ckpt_read_block_page((uint32_t)max_block, C4OWN_CKPT_FOOTER_PAGE,
                                   c4own_ckpt_buf))
        return false;
    if (c4own_get32(c4own_ckpt_buf + 24) != max_seq ||
        c4own_get32(c4own_ckpt_buf + 28) != max_count ||
        c4own_get32(c4own_ckpt_buf + 36) != c4own_crc32(c4own_ckpt_buf, 36))
        return false;
    ecrc = c4own_get32(c4own_ckpt_buf + 32);

    c4own_reset_maps();
    remaining = max_count;
    page_in_block = C4OWN_CKPT_FIRST_PAGE;
    crc = 0xffffffffu;

    while (remaining > 0)
    {
        uint32_t this_page = remaining < C4OWN_CKPT_ENTRIES_PER_PAGE ?
                             remaining : C4OWN_CKPT_ENTRIES_PER_PAGE;

        if (c4own_ckpt_read_block_page((uint32_t)max_block, page_in_block,
                                       c4own_ckpt_buf))
        {
            c4own_reset_maps();
            return false;
        }

        crc = c4own_crc32_cont(crc, c4own_ckpt_buf,
                               this_page * C4OWN_CKPT_ENTRY_BYTES);

        for (uint32_t i = 0; i < this_page; i++)
        {
            unsigned char *ent = c4own_ckpt_buf + i * C4OWN_CKPT_ENTRY_BYTES;
            uint32_t lg = c4own_get32(ent + 0);
            uint32_t pb = c4own_get16(ent + 4);
            uint32_t gn = c4own_get32(ent + 6);

            if (lg >= C4OWN_LOGICAL_BLOCKS || pb >= C4OWN_RUNTIME_PBLK_COUNT ||
                pb < C4OWN_FIRST_DATA_PBLK)
            {
                c4own_reset_maps();
                return false;
            }

            c4own_map[lg] = (uint16_t)pb;
            c4own_gen[lg] = gn;
            c4own_used[pb] = 1;
        }

        remaining -= this_page;
        page_in_block++;
    }
    crc ^= 0xffffffffu;

    if (crc != ecrc)
    {
        c4own_reset_maps();
        return false;
    }

    c4own_ckpt_active_block = max_block;
    c4own_ckpt_marked_dirty = false;
    c4own_map_dirty = false;
    c4own_read_page_valid = false;
    return true;
}

static int c4own_mount_scan(void)
{
    unsigned char header[512];
    unsigned char commit[512];

    c4own_reset_maps();

    if (!c4own_superblock_valid(0) && !c4own_superblock_valid(1))
        return 1;

    /* Fast path: a valid, non-dirty checkpoint reconstructs the map without the
     * full per-block scan below. On any mismatch c4own_ckpt_load resets the maps
     * and we fall through to the authoritative scan.
     *
     * DISABLED by default (2026-07-07): DATA-INTEGRITY FIX. c4own_ckpt_write()
     * programs the superblock block out of order -- page 0, a couple of entry
     * pages, then the footer at page 126, and c4own_ckpt_mark_dirty() the marker
     * at page 127 -- leaving the middle pages erased. That is illegal on this MLC
     * NAND (2x Micron; sequential in-block programming required). It reads back
     * correctly IN-SESSION (from the page buffer) but the footer/dirty-marker are
     * NOT durable across a power/mount cycle, so mount can load a STALE map ->
     * the FAT/directory reads back as garbage (corrupt filenames) and the volume
     * looks invalid -> auto-reformat. Confirmed on device 2026-07-07 (corruption
     * survived a proper HOST eject, which flushes correctly). The full scan below
     * reads only sequential-programmed, MLC-durable data-block headers and is
     * authoritative. Re-enable once c4own_ckpt_write() is made MLC-legal
     * (one contiguous sequential page run: superblock + entries + footer, no
     * page-126 jump, no out-of-order page-127 marker, plus a read-back verify so
     * a non-durable checkpoint fails closed to a scan). Cost of the scan-only
     * mount: the ~cold-scan time the checkpoint was added to skip. */
#ifdef C4PRO_FTL_MOUNT_CHECKPOINT
    if (c4own_ckpt_load())
        return 0;
#endif

    c4own_reset_maps();

    for (uint32_t pblk = C4OWN_FIRST_DATA_PBLK;
         pblk < C4OWN_RUNTIME_PBLK_COUNT; pblk++)
    {
        uint32_t logical;
        uint32_t gen;

        if (c4own_read_phys_sector_uncached(pblk, 0, header) ||
            !c4own_parse_header(header, &logical, &gen))
            continue;

        /* Reject stale headers whose logical block number is outside the
         * current map. A block left over from a LARGER owned-volume geometry
         * (e.g. after shrinking C4PRO_OWNED_PBLK_COUNT) carries logical numbers
         * up to that larger volume's count; without this guard the
         * c4own_map[logical]/c4own_gen[logical] writes below run off the end of
         * the (now smaller) arrays and corrupt adjacent globals/code, which
         * manifested as data-abort / undefined-instruction crashes in the
         * c4own_* map/cache functions during USB reads. */
        if (logical >= C4OWN_LOGICAL_BLOCKS)
            continue;

        if (c4own_read_phys_sector_uncached(
                pblk, C4OWN_SECTORS_PER_BLOCK - 1u, commit) ||
            !c4own_parse_commit(commit, logical, gen))
            continue;

        c4own_used[pblk] = 1;
        if (gen >= c4own_gen[logical])
        {
            if (c4own_map[logical] != 0xffffu)
            {
                c4own_used[c4own_map[logical]] = 0;
                c4own_retired[c4own_map[logical]] = 1;
            }
            c4own_map[logical] = pblk;
            c4own_gen[logical] = gen;
        }
        else
        {
            c4own_used[pblk] = 0;
            c4own_retired[pblk] = 1;
        }
    }

    /* Persist the scan result so the NEXT cold boot mounts via the checkpoint
     * even without any intervening write activity. Best-effort. */
    c4own_ckpt_write();
    return 0;
}

static int c4own_alloc_pblk(void)
{
    for (uint32_t pblk = C4OWN_FIRST_DATA_PBLK;
         pblk < C4OWN_RUNTIME_PBLK_COUNT; pblk++)
    {
        if (!c4own_used[pblk] && !c4own_retired[pblk])
        {
            c4own_used[pblk] = 1;
            if (c4own_erase_pblk(pblk))
            {
                c4own_retired[pblk] = 1;
                continue;
            }
            return pblk;
        }
    }

    for (uint32_t pblk = C4OWN_FIRST_DATA_PBLK;
         pblk < C4OWN_RUNTIME_PBLK_COUNT; pblk++)
    {
        if (!c4own_used[pblk] && c4own_retired[pblk])
        {
            c4own_used[pblk] = 1;
            if (c4own_erase_pblk(pblk))
                continue;
            c4own_retired[pblk] = 0;
            return pblk;
        }
    }

    return -1;
}

static void c4own_blank_sector(unsigned char *sector)
{
    memset(sector, 0, 512);
}

static int c4own_write_logical_block(uint32_t logical, uint32_t first,
                                     uint32_t count,
                                     const unsigned char *data);

static int c4own_flush_cache_slot(uint32_t slot)
{
    if (slot >= C4OWN_CACHE_SLOTS ||
        !c4own_cache_valid[slot] || !c4own_cache_dirty[slot])
        return 0;

#ifdef C4PRO_FTL_DEFER_COW
    c4own_cache_backfill(slot);   /* fill unwritten gaps from the old block */
#endif
    if (c4own_write_logical_block(c4own_cache_logical[slot], 0,
                                  C4OWN_PAYLOAD_SECTORS_PER_BLOCK,
                                  c4own_cache_data[slot]))
        return 1;

    c4own_cache_dirty[slot] = false;
    return 0;
}

#ifdef C4PRO_NAND_2PLANE
static unsigned char c4own_il_page[4][4096] __attribute__((aligned(4)));

static void c4own_build_header(unsigned char *buf, uint32_t logical,
                               uint32_t gen)
{
    memset(buf, 0xff, 4096);
    c4own_put32(buf + 0, C4OWN_MAGIC_HDR);
    c4own_put32(buf + 4, C4OWN_VERSION);
    c4own_put32(buf + 8, logical);
    c4own_put32(buf + 12, gen);
    c4own_put32(buf + 16, c4own_crc32(buf, 16));
}

static void c4own_build_commit(unsigned char *buf, uint32_t logical,
                               uint32_t gen)
{
    unsigned char *commit = buf + 7u * 512u;

    memset(buf, 0xff, 4096);
    c4own_put32(commit + 0, C4OWN_MAGIC_COMMIT);
    c4own_put32(commit + 4, C4OWN_VERSION);
    c4own_put32(commit + 8, logical);
    c4own_put32(commit + 12, gen);
    c4own_put32(commit + 16, c4own_crc32(commit, 16));
}

/*
 * Two-plane paired flush -- the stock ~6.7 MB/s lever (validated 8.3 vs 4.2 MB/s
 * raw). Programs TWO dirty cache slots at once onto an ADJACENT physical
 * plane-pair (blocks P and P+1, so row1 = row0 + C4OWN_PAGES_PER_BLOCK -- the
 * proven block-bit-0 stride), sharing one MLC tPROG per page-round between the two
 * blocks. Each block keeps its own header(page0)/commit(page127)/payload layout
 * and is BYTE-IDENTICAL on NAND to a single-plane write, so c4own_read_phys_sector,
 * c4own_mount_scan and the checkpoint are UNCHANGED and existing volumes keep
 * mounting. Map/gen are committed only after all 128 page-rounds succeed; on any
 * page failure both fresh blocks are retired and no map changes, so a torn write
 * never loses committed data (a block whose commit page never landed is skipped by
 * the mount scan, exactly as in the serial writer).
 */
#define C4OWN_ERASE(pb) c4own_erase_pblk(pb)

/* Allocate an adjacent free plane-pair: an even PHYSICAL block P with P and P+1
 * both free/erasable and on the same die. Erases both. Returns the low pblk
 * (*hi = its partner) or -1. abs_pblk = C4PRO_OWNED_PBLK_START + pblk and
 * blocks/die is even, so an even abs block and its +1 partner are always two
 * adjacent same-die blocks differing only in block bit 0 -- the two-plane pairing
 * constraint. Two passes (fresh, then retired) mirror c4own_alloc_pblk. */
static int c4own_alloc_pblk_pair(int *hi)
{
    for (int pass = 0; pass < 2; pass++)
    {
        for (uint32_t P = C4OWN_FIRST_DATA_PBLK;
             P + 1u < C4OWN_RUNTIME_PBLK_COUNT; P++)
        {
            uint32_t Q = P + 1u;

            if (c4own_abs_pblk(P) & 1u)     /* P must start an even physical block */
                continue;
            if (c4own_used[P] || c4own_used[Q])
                continue;
            if (pass == 0 && (c4own_retired[P] || c4own_retired[Q]))
                continue;
            if (c4own_pblk_bank(P) != c4own_pblk_bank(Q))   /* defensive same-die */
                continue;

            if (C4OWN_ERASE(P))
            {
                c4own_used[P] = 1;
                c4own_retired[P] = 1;
                continue;
            }
            if (C4OWN_ERASE(Q))
            {
                c4own_used[Q] = 1;
                c4own_retired[Q] = 1;
                continue;               /* leave P free for a later single/pair use */
            }
            c4own_used[P] = 1;
            c4own_used[Q] = 1;
            c4own_retired[P] = 0;
            c4own_retired[Q] = 0;
            *hi = (int)Q;
            return (int)P;
        }
    }
    return -1;
}

/* Program two logical blocks in lockstep as a plane pair via two-plane. */
static int c4own_flush_pair(uint32_t slot0, uint32_t slot1,
                            int pblk0, int pblk1, uint32_t bank)
{
    uint32_t base0 = c4own_pblk_page((uint32_t)pblk0);
    uint32_t base1 = c4own_pblk_page((uint32_t)pblk1);
    uint32_t logical0 = c4own_cache_logical[slot0];
    uint32_t logical1 = c4own_cache_logical[slot1];
    uint16_t old0 = c4own_map[logical0];
    uint16_t old1 = c4own_map[logical1];
    uint32_t gen0 = c4own_gen[logical0] + 1u;
    uint32_t gen1 = c4own_gen[logical1] + 1u;
    uint32_t p;

    C4PRO_SET_PHASE(3);

    /* Plane pair must be adjacent (the proven stride). alloc guarantees it; bail
     * to the caller's serial fallback if not. */
    if (base1 != base0 + C4OWN_PAGES_PER_BLOCK)
        return 2;

    if (c4own_ckpt_active_block >= 0 && !c4own_ckpt_marked_dirty)
        if (c4own_ckpt_mark_dirty())
        {
            /* Release the freshly-allocated (and already erased) pair: unlike
             * the program-failure path below there is nothing wrong with these
             * blocks, so free rather than retire them. Without this, every
             * retried flush after a ckpt-marker failure leaked 2 blocks for the
             * session (used=1, unmapped). */
            c4own_used[pblk0] = 0;
            c4own_used[pblk1] = 0;
            return 1;
        }

#ifdef C4PRO_FTL_DEFER_COW
    /* Fill unwritten gaps from each slot's old block BEFORE the map is updated
     * below. Full-block overwrites read nothing here (the win). */
    c4own_cache_backfill(slot0);
    c4own_cache_backfill(slot1);
#endif

    for (p = 0; p < C4OWN_PAGES_PER_BLOCK; p++)
    {
        unsigned char *b0 = c4own_il_page[0];
        unsigned char *b1 = c4own_il_page[1];

        if (p == 0)
        {
            c4own_build_header(b0, logical0, gen0);
            c4own_build_header(b1, logical1, gen1);
        }
        else if (p == C4OWN_PAGES_PER_BLOCK - 1u)
        {
            c4own_build_commit(b0, logical0, gen0);
            c4own_build_commit(b1, logical1, gen1);
        }
        else
        {
            memcpy(b0, c4own_cache_data[slot0] +
                       (p - 1u) * C4OWN_SECTORS_PER_PAGE * 512u,
                   C4OWN_SECTORS_PER_PAGE * 512u);
            memcpy(b1, c4own_cache_data[slot1] +
                       (p - 1u) * C4OWN_SECTORS_PER_PAGE * 512u,
                   C4OWN_SECTORS_PER_PAGE * 512u);
        }

        /* Hardware-BCH inline layout, identical to the single-plane hwecc writer
         * the production (C4PRO_NAND_HWECC) builds use, so reads via
         * c4pro_nand_read_page_hwecc are unchanged. Phase 5 = NAND program in
         * flight (same marker as the serial writer), so a wedge inside the
         * two-plane program is attributed to the NAND op, not the FTL. */
        C4PRO_SET_PHASE(5);
        if (c4pro_nand_write_page_2plane(bank, base0 + p, base1 + p, b0, b1,
                                         C4OWN_SECTORS_PER_PAGE))
        {
            c4own_used[pblk0] = 0; c4own_retired[pblk0] = 1;
            c4own_used[pblk1] = 0; c4own_retired[pblk1] = 1;
            return 1;
        }
    }

    c4own_map[logical0] = (uint16_t)pblk0;
    c4own_gen[logical0] = gen0;
    if (old0 != 0xffffu) { c4own_used[old0] = 0; c4own_retired[old0] = 1; }
    c4own_map[logical1] = (uint16_t)pblk1;
    c4own_gen[logical1] = gen1;
    if (old1 != 0xffffu) { c4own_used[old1] = 0; c4own_retired[old1] = 1; }

    c4own_cache_dirty[slot0] = false;
    c4own_cache_dirty[slot1] = false;
    c4own_map_dirty = true;
    return 0;
}

/*
 * Drain dirty cache slots as two-plane pairs. all=true flushes every dirty slot
 * (sync/unmount); all=false is the eager high-water flush keeping
 * C4PRO_FTL_FLUSH_KEEP dirty. Each round takes the two OLDEST dirty slots and one
 * fresh plane-pair; a lone dirty slot, or a round with no plane-pair free, falls
 * back to the serial single-slot writer so forward progress is guaranteed.
 */
static int c4own_flush_batched_2plane(bool all)
{
    unsigned guard = 2u * C4OWN_CACHE_SLOTS + 2u;
    /* Bad-block retry budget: on a 2-plane program failure the pair is retired
     * and the slots stay dirty; retry with a fresh pair rather than failing the
     * whole flush on the first bad block. Bounded so a broadly-bad region still
     * terminates. Retries do not consume `guard` (see below). */
    unsigned bb_retry = 128u;

    while (guard--)
    {
        int s0 = -1, s1 = -1;
        uint32_t a0 = 0xffffffffu, a1 = 0xffffffffu;
        uint32_t dirty = 0;
        int pblk0, hi = -1;

        for (uint32_t s = 0; s < C4OWN_CACHE_SLOTS; s++)
            if (c4own_cache_valid[s] && c4own_cache_dirty[s])
                dirty++;

        if (all)
        {
            if (dirty == 0u)
                break;
        }
        else
        {
            uint32_t threshold = C4PRO_FTL_FLUSH_KEEP + 2u;
            if (threshold > C4OWN_CACHE_SLOTS)
                threshold = C4OWN_CACHE_SLOTS;
            if (dirty < threshold)
                break;
        }

        for (uint32_t s = 0; s < C4OWN_CACHE_SLOTS; s++)
        {
            if (!c4own_cache_valid[s] || !c4own_cache_dirty[s])
                continue;
            if (c4own_cache_age[s] < a0)
            { a1 = a0; s1 = s0; a0 = c4own_cache_age[s]; s0 = (int)s; }
            else if (c4own_cache_age[s] < a1)
            { a1 = c4own_cache_age[s]; s1 = (int)s; }
        }

        if (s0 < 0)
            break;
        if (s1 < 0)
        {
            if (c4own_flush_cache_slot((uint32_t)s0))
                return 1;
            continue;
        }

        pblk0 = c4own_alloc_pblk_pair(&hi);
        if (pblk0 < 0)
        {
            if (c4own_flush_cache_slot((uint32_t)s0))
                return 1;
            continue;
        }

        {
            int r = c4own_flush_pair((uint32_t)s0, (uint32_t)s1, pblk0, hi,
                                     c4own_pblk_bank((uint32_t)pblk0));
            if (r == 1)
            {
                /* Program failure: c4own_flush_pair already retired the bad pair
                 * and left the slots dirty. Retry with a fresh pair so a write
                 * (notably a format's metadata write) routes around sparse bad
                 * blocks instead of dying on the first one. A retry must NOT
                 * consume the forward-progress guard, only the bad-block budget. */
                if (bb_retry == 0u)
                    return 1;
                bb_retry--;
                guard++;
                continue;
            }
            if (r == 2)
            {
                c4own_used[pblk0] = 0;
                c4own_used[hi] = 0;
                if (c4own_flush_cache_slot((uint32_t)s0))
                    return 1;
            }
        }
    }

    /* Guard/bad-block budget exhausted: if an all-flush still has dirty slots the
     * data was NOT persisted -- report failure rather than a false success. */
    if (all)
        for (uint32_t s = 0; s < C4OWN_CACHE_SLOTS; s++)
            if (c4own_cache_valid[s] && c4own_cache_dirty[s])
                return 1;
    return 0;
}
#endif /* C4PRO_NAND_2PLANE */

static int c4own_flush_cache(void)
{
#ifdef C4PRO_NAND_2PLANE
    return c4own_flush_batched_2plane(true);
#else
    for (uint32_t slot = 0; slot < C4OWN_CACHE_SLOTS; slot++)
    {
        if (c4own_flush_cache_slot(slot))
            return 1;
    }

    return 0;
#endif
}

static int c4own_find_cache_slot(uint32_t logical)
{
    for (uint32_t slot = 0; slot < C4OWN_CACHE_SLOTS; slot++)
    {
        if (c4own_cache_valid[slot] && c4own_cache_logical[slot] == logical)
        {
            c4own_cache_age[slot] = ++c4own_cache_tick;
            return (int)slot;
        }
    }

    return -1;
}

static int c4own_load_cache(uint32_t logical)
{
    uint16_t old_pblk;
    uint32_t victim = 0;

    if (logical >= C4OWN_LOGICAL_BLOCKS)
        return -1;

    for (uint32_t slot = 0; slot < C4OWN_CACHE_SLOTS; slot++)
    {
        if (!c4own_cache_valid[slot])
        {
            victim = slot;
            goto load;
        }

        if (c4own_cache_age[slot] < c4own_cache_age[victim])
            victim = slot;
    }

    if (c4own_flush_cache_slot(victim))
        return -1;

load:
    old_pblk = c4own_map[logical];
#ifdef C4PRO_FTL_DEFER_COW
    /* Do NOT pre-read the old block here. Mark all payload sectors unwritten;
     * c4own_cache_backfill() reads only the ones the host never touches, at
     * flush time. cache_data is left stale -- every byte is either overwritten
     * by the host or backfilled before it reaches NAND. */
    (void)old_pblk;
    memset(c4own_cache_written[victim], 0, C4OWN_WRITTEN_BYTES);
#else
    for (uint32_t payload = 0; payload < C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
         payload++)
    {
        unsigned char *dst = c4own_cache_data[victim] + payload * 512u;

        if (old_pblk != 0xffffu)
        {
            C4PRO_SET_PHASE(7);   /* cache-load fill read, see phase legend */
            if (c4own_read_phys_sector(old_pblk,
                                       C4OWN_SECTORS_PER_PAGE + payload,
                                       dst))
                c4own_blank_sector(dst);
        }
        else
            c4own_blank_sector(dst);
    }
#endif

    c4own_cache_logical[victim] = logical;
    c4own_cache_age[victim] = ++c4own_cache_tick;
    c4own_cache_valid[victim] = true;
    c4own_cache_dirty[victim] = false;
    return (int)victim;
}

#ifdef C4PRO_FTL_DEFER_COW
/* Fill the sectors the host never wrote (per c4own_cache_written) from the old
 * mapped block, so cache_data[slot] is a complete image before it is programmed.
 * Called at the start of every flush path. For a fully-overwritten block this
 * reads nothing (the whole win); for a partial write it reads only the gaps --
 * exactly the COW the old c4own_load_cache pre-read did, but deferred and
 * minimized. Must run BEFORE the flush updates c4own_map[logical]. */
static void c4own_cache_backfill(uint32_t slot)
{
    uint32_t logical = c4own_cache_logical[slot];
    uint16_t old_pblk = c4own_map[logical];
    uint32_t payload;

    for (payload = 0; payload < C4OWN_PAYLOAD_SECTORS_PER_BLOCK; payload++)
    {
        unsigned char *dst;

        if (c4own_wr_get(slot, payload))
            continue;                       /* host wrote it -- keep */

        dst = c4own_cache_data[slot] + payload * 512u;
        if (old_pblk != 0xffffu)
        {
            C4PRO_SET_PHASE(7);   /* deferred COW gap read, see phase legend */
            if (c4own_read_phys_sector(old_pblk,
                                       C4OWN_SECTORS_PER_PAGE + payload, dst))
                c4own_blank_sector(dst);
        }
        else
            c4own_blank_sector(dst);
    }
}
#endif

static int c4own_read_sector_internal(uint32_t sector, void *buffer)
{
    uint32_t logical = sector / C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
    uint32_t offset = sector % C4OWN_PAYLOAD_SECTORS_PER_BLOCK;

    if (sector >= C4OWN_VOLUME_SECTORS)
        return 1;

    int slot = c4own_find_cache_slot(logical);

    if (slot >= 0)
    {
#ifdef C4PRO_FTL_DEFER_COW
        /* Deferred COW: an unwritten sector in a dirty slot is NOT in cache_data
         * yet (no pre-read) -- serve it from the old block, exactly as if the
         * slot weren't cached. Written sectors come from the cache as normal. */
        if (!c4own_wr_get((uint32_t)slot, offset))
        {
            if (c4own_map[logical] == 0xffffu)
            {
                c4own_blank_sector(buffer);
                return 0;
            }
            return c4own_read_phys_sector(c4own_map[logical],
                                          C4OWN_SECTORS_PER_PAGE + offset,
                                          buffer);
        }
#endif
        memcpy(buffer, c4own_cache_data[slot] + offset * 512u, 512);
        return 0;
    }

    if (c4own_map[logical] == 0xffffu)
    {
        c4own_blank_sector(buffer);
        return 0;
    }

    return c4own_read_phys_sector(c4own_map[logical],
                                  C4OWN_SECTORS_PER_PAGE + offset,
                                  buffer);
}

static int c4own_read_internal(uint32_t sector, uint32_t count, void *buffer)
{
    unsigned char *out = buffer;

    if (sector > C4OWN_VOLUME_SECTORS ||
        count > C4OWN_VOLUME_SECTORS - sector)
        return 1;

    while (count > 0)
    {
        uint32_t logical = sector / C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
        uint32_t offset = sector % C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
        uint32_t n = C4OWN_PAYLOAD_SECTORS_PER_BLOCK - offset;
        int slot;

        if (n > count)
            n = count;

        slot = c4own_find_cache_slot(logical);
        if (slot >= 0)
        {
#ifdef C4PRO_FTL_DEFER_COW
            /* Some sectors in this dirty slot may be unwritten (not in cache_data
             * under deferred COW) -- go per-sector so each is served from cache
             * or the old block correctly. Reads are off the write hot path. */
            uint32_t k;
            for (k = 0; k < n; k++)
                if (c4own_read_sector_internal(sector + k, out + k * 512u))
                    return 1;
#else
            memcpy(out, c4own_cache_data[slot] + offset * 512u, n * 512u);
#endif
            out += n * 512u;
            sector += n;
            count -= n;
            continue;
        }

        if (c4own_map[logical] == 0xffffu)
        {
            memset(out, 0, n * 512u);
            out += n * 512u;
            sector += n;
            count -= n;
            continue;
        }

        while (n > 0)
        {
            uint32_t phys_sector = C4OWN_SECTORS_PER_PAGE + offset;
            uint32_t sector_in_page =
                phys_sector & (C4OWN_SECTORS_PER_PAGE - 1u);
            uint32_t page_n = C4OWN_SECTORS_PER_PAGE - sector_in_page;
            uint32_t page;

            if (page_n > n)
                page_n = n;

            if (sector_in_page == 0 && page_n == C4OWN_SECTORS_PER_PAGE)
            {
                page = c4own_pblk_page(c4own_map[logical]) +
                       phys_sector / C4OWN_SECTORS_PER_PAGE;
                if (c4own_read_page_corrected(c4own_pblk_bank(c4own_map[logical]),
                                              page, out))
                    return 1;

                c4own_read_page_valid = false;
            }
            else
            {
                if (c4own_read_phys_sector(c4own_map[logical], phys_sector,
                                           out))
                    return 1;

                if (page_n > 1)
                    memcpy(out + 512u,
                           c4own_read_page_data +
                               (sector_in_page + 1u) * 512u,
                           (page_n - 1u) * 512u);
            }

            out += page_n * 512u;
            sector += page_n;
            offset += page_n;
            count -= page_n;
            n -= page_n;
        }
    }

    return 0;
}

static int c4own_write_logical_block(uint32_t logical, uint32_t first,
                                     uint32_t count,
                                     const unsigned char *data)
{
    int new_pblk;
    uint16_t old_pblk;
    uint32_t gen;

    C4PRO_SET_PHASE(3);

    /* Before the first commit after a checkpoint, invalidate it on NAND so a
     * crash can never leave a clean checkpoint that omits this committed block.
     * If the marker write fails, abort the write to preserve the invariant. */
    if (c4own_ckpt_active_block >= 0 && !c4own_ckpt_marked_dirty)
    {
        if (c4own_ckpt_mark_dirty())
            return 1;
    }

    new_pblk = c4own_alloc_pblk();
    if (new_pblk < 0)
        return 1;

    old_pblk = c4own_map[logical];
    gen = c4own_gen[logical] + 1u;

    c4own_make_header(logical, gen);
    if (c4own_write_page(new_pblk, 0))
        goto fail;

    for (uint32_t page = 1; page < C4OWN_PAGES_PER_BLOCK - 1u; page++)
    {
        for (uint32_t sec = 0; sec < C4OWN_SECTORS_PER_PAGE; sec++)
        {
            uint32_t payload = (page - 1u) * C4OWN_SECTORS_PER_PAGE + sec;
            unsigned char *dst = c4own_page_data + sec * 512u;

            if (payload >= C4OWN_PAYLOAD_SECTORS_PER_BLOCK)
                memset(dst, 0xff, 512);
            else if (payload >= first && payload < first + count)
                memcpy(dst, data + (payload - first) * 512u, 512);
            else if (old_pblk != 0xffffu)
            {
                /* COW fill read. Phase 7 so a freeze here is distinguishable
                 * from the page program (5): without this, the whole
                 * program->fill-read->program alternation reported phase 5 and
                 * the read engine could masquerade as a "program" wedge. */
                C4PRO_SET_PHASE(7);
                if (c4own_read_phys_sector(old_pblk,
                                           C4OWN_SECTORS_PER_PAGE + payload,
                                           dst))
                    c4own_blank_sector(dst);
            }
            else
                c4own_blank_sector(dst);
        }

        if (c4own_write_page(new_pblk, page))
            goto fail;
    }

    c4own_make_commit(logical, gen);
    if (c4own_write_page(new_pblk, C4OWN_PAGES_PER_BLOCK - 1u))
        goto fail;

    c4own_map[logical] = new_pblk;
    c4own_gen[logical] = gen;
    if (old_pblk != 0xffffu)
    {
        c4own_used[old_pblk] = 0;
        c4own_retired[old_pblk] = 1;
    }

    c4own_map_dirty = true;
    return 0;

fail:
    c4own_retired[new_pblk] = 1;
    return 1;
}

static int c4own_write_internal(uint32_t sector, uint32_t count,
                                const void *buffer)
{
    const unsigned char *in = buffer;
    uint32_t first_count = count;

    if (sector > C4OWN_VOLUME_SECTORS ||
        count > C4OWN_VOLUME_SECTORS - sector)
        return 1;

    while (count > 0)
    {
        uint32_t logical = sector / C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
        uint32_t offset = sector % C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
        uint32_t n = C4OWN_PAYLOAD_SECTORS_PER_BLOCK - offset;

        if (n > count)
            n = count;

        int slot = c4own_find_cache_slot(logical);

        if (slot < 0)
        {
            slot = c4own_load_cache(logical);
            if (slot < 0)
                return 1;
        }

        memcpy(c4own_cache_data[slot] + offset * 512u, in, n * 512u);
        c4own_cache_dirty[slot] = true;
#ifdef C4PRO_FTL_DEFER_COW
        /* Record these sectors as host-written so backfill skips them. */
        {
            uint32_t s;
            for (s = offset; s < offset + n; s++)
                c4own_wr_set((uint32_t)slot, s);
        }
#endif
        sector += n;
        count -= n;
        in += n * 512u;
    }

#ifdef C4PRO_FTL_EAGER_FLUSH
    /* Pipelined incremental flush: keep the C4PRO_FTL_FLUSH_KEEP most-recently-
     * used dirty blocks buffered and flush only the OLDEST (LRU) dirty block
     * whenever more than that many are dirty. This spreads the NAND programs
     * across the transfer (so the end-of-file SYNCHRONIZE_CACHE flush is only
     * <= KEEP blocks -> short -> no host timeout/reset) WITHOUT the pathology of
     * the earlier keep-only-the-tail policy: keeping >1 block leaves the USB
     * bulk-OUT receive slack to run ahead of the NAND program (restoring speed)
     * and keeps hot metadata (FAT/dir) resident, so an interleaved FAT update no
     * longer thrash-flushes the data block (that write-amplification was the ~4x
     * slowdown). Uses c4own_cache_age (LRU tick) to pick the oldest dirty slot.
     * Skipped during format (which drives its own write+flush sequence). */
    if (first_count > 0 && !c4own_formatting)
    {
#ifdef C4PRO_NAND_2PLANE
        /* Two-plane eager flush: drain excess dirty blocks in plane-pairs so each
         * MLC tPROG programs two blocks at once (~2x). */
        if (c4own_flush_batched_2plane(false))
            return 1;
#else
        /* Bounded by construction: each successful c4own_flush_cache_slot clears
         * exactly one dirty slot (dirty--), a failed one returns, and ages are
         * unique so the oldest never repeats -> at most C4OWN_CACHE_SLOTS passes.
         * The guard is a belt-and-suspenders cap so this can NEVER spin (the
         * masked-IRQ hang we are hunting), even if a flush ever returned success
         * without clearing dirty; on the cap we leave the blocks for the next
         * flush/sync. */
        unsigned guard = C4OWN_CACHE_SLOTS + 1u;

        while (guard--)
        {
            uint32_t dirty = 0;
            int oldest = -1;
            uint32_t oldest_age = 0xffffffffu;

            for (uint32_t slot = 0; slot < C4OWN_CACHE_SLOTS; slot++)
            {
                if (c4own_cache_valid[slot] && c4own_cache_dirty[slot])
                {
                    dirty++;
                    if (c4own_cache_age[slot] < oldest_age)
                    {
                        oldest_age = c4own_cache_age[slot];
                        oldest = (int)slot;
                    }
                }
            }

            if (dirty <= C4PRO_FTL_FLUSH_KEEP || oldest < 0)
                break;

            if (c4own_flush_cache_slot((uint32_t)oldest))
                return 1;   /* result already set to 3 by the slot flush */
        }
#endif /* C4PRO_NAND_DIE_INTERLEAVE */
    }
#endif

    return 0;
}

static void c4own_make_mbr(unsigned char *s)
{
    c4own_blank_sector(s);
    s[446] = 0x80;
#if C4OWN_IS_FAT32
    s[450] = 0x0c;
#else
    s[450] = 0x06;
#endif
    c4own_put32(s + 454, C4OWN_PARTITION_START);
    c4own_put32(s + 458, C4OWN_PARTITION_SECTORS);
    s[510] = 0x55;
    s[511] = 0xaa;
}

static void c4own_make_fat16_boot(unsigned char *s)
{
    static const unsigned char jmp[3] = { 0xeb, 0x3c, 0x90 };

    c4own_blank_sector(s);
    memcpy(s, jmp, sizeof(jmp));
    memcpy(s + 3, "MSDOS5.0", 8);
    c4own_put16(s + 11, 512);
    s[13] = 4;
    c4own_put16(s + 14, 1);
    s[16] = 2;
    c4own_put16(s + 17, 512);
    c4own_put16(s + 19, 0);
    s[21] = 0xf8;
    c4own_put16(s + 22, 128);
    c4own_put16(s + 24, 63);
    c4own_put16(s + 26, 255);
    c4own_put32(s + 28, C4OWN_PARTITION_START);
    c4own_put32(s + 32, C4OWN_PARTITION_SECTORS);
    s[36] = 0x80;
    s[38] = 0x29;
    c4own_put32(s + 39, 0xc4000001u);
    memcpy(s + 43, "C4 ROCKBOX ", 11);
    memcpy(s + 54, "FAT16   ", 8);
    s[510] = 0x55;
    s[511] = 0xaa;
}

static void c4own_make_fat32_boot(unsigned char *s)
{
    static const unsigned char jmp[3] = { 0xeb, 0x58, 0x90 };

    c4own_blank_sector(s);
    memcpy(s, jmp, sizeof(jmp));
    memcpy(s + 3, "MSDOS5.0", 8);
    c4own_put16(s + 11, 512);
    s[13] = C4OWN_FAT32_SECTORS_PER_CLUSTER;
    c4own_put16(s + 14, C4OWN_FAT32_RESERVED_SECTORS);
    s[16] = 2;
    c4own_put16(s + 17, 0);
    c4own_put16(s + 19, 0);
    s[21] = 0xf8;
    c4own_put16(s + 22, 0);
    c4own_put16(s + 24, 63);
    c4own_put16(s + 26, 255);
    c4own_put32(s + 28, C4OWN_PARTITION_START);
    c4own_put32(s + 32, C4OWN_PARTITION_SECTORS);
    c4own_put32(s + 36, C4OWN_FAT32_FAT_SECTORS);
    c4own_put16(s + 40, 0);
    c4own_put16(s + 42, 0);
    c4own_put32(s + 44, 2);
    c4own_put16(s + 48, 1);
    c4own_put16(s + 50, 6);
    s[64] = 0x80;
    s[66] = 0x29;
    c4own_put32(s + 67, 0xc4000032u);
    memcpy(s + 71, "C4 ROCKBOX ", 11);
    memcpy(s + 82, "FAT32   ", 8);
    s[510] = 0x55;
    s[511] = 0xaa;
}

static void c4own_make_fat32_fsinfo(unsigned char *s)
{
    c4own_blank_sector(s);
    c4own_put32(s + 0, 0x41615252u);
    c4own_put32(s + 484, 0x61417272u);
    c4own_put32(s + 488, 0xffffffffu);
    c4own_put32(s + 492, 0xffffffffu);
    c4own_put32(s + 508, 0xaa550000u);
}

static void c4own_make_fat0(unsigned char *s)
{
    c4own_blank_sector(s);
    s[0] = 0xf8;
    s[1] = 0xff;
    s[2] = 0xff;
    s[3] = 0xff;
#if C4OWN_IS_FAT32
    s[4] = 0xff;
    s[5] = 0xff;
    s[6] = 0xff;
    s[7] = 0x0f;
    s[8] = 0xff;
    s[9] = 0xff;
    s[10] = 0xff;
    s[11] = 0x0f;
#endif
}

static int c4own_format(void)
{
    unsigned char sector[512];

    c4own_formatting = true;
    c4own_reset_maps();

    /* A format wipes any prior checkpoint (the erase loop clears pblk 0/1); make
     * sure write_logical_block does not try to dirty-mark a stale one. */
    c4own_ckpt_active_block = -1;
    c4own_ckpt_marked_dirty = false;
    c4own_map_dirty = false;

#ifdef C4PRO_NAND_FAST_FORMAT
    /* Multi-die concurrent erase. The owned pblk range maps monotonically onto
     * the dies (bank = (START + pblk) / blocks_per_die), so each die owns a
     * contiguous pblk sub-range. We walk those sub-ranges in lockstep: gather
     * one pblk from each still-active die into a batch and erase the whole batch
     * concurrently via c4pro_nand_erase_blocks_multi(). This overlaps up to 4
     * tERASE windows, cutting format time ~= (#active dies)x. It is a pure
     * scheduling change over the same set of erases the serial path performs;
     * the on-disk result is byte-for-byte identical, so there is no
     * data-integrity risk vs. the default full-erase path. */
    {
        uint32_t total = C4OWN_RUNTIME_PBLK_COUNT;
        uint32_t blocks = ftl_nand_type ? ftl_nand_type->blocks : 0u;
        uint32_t nb = ftl_banks < 4u ? ftl_banks : 4u;
        uint32_t cur[4];
        uint32_t end[4];

        if (blocks == 0u || nb == 0u)
        {
            /* Geometry not established; fall back to the proven serial erase. */
            for (uint32_t pblk = 0; pblk < total; pblk++)
                if (c4own_erase_pblk(pblk))
                    c4own_retired[pblk] = 1;
        }
        else
        {
            for (uint32_t b = 0; b < nb; b++)
            {
                /* Signed math so a die whose blocks start below the owned
                 * region's START clamps to the owned window without underflow. */
                int64_t lo = (int64_t)b * (int64_t)blocks -
                             (int64_t)C4PRO_OWNED_PBLK_START;
                int64_t hi = (int64_t)(b + 1u) * (int64_t)blocks -
                             (int64_t)C4PRO_OWNED_PBLK_START;
                if (lo < 0) lo = 0;
                if (hi < 0) hi = 0;
                if (lo > (int64_t)total) lo = total;
                if (hi > (int64_t)total) hi = total;
                cur[b] = (uint32_t)lo;
                end[b] = (uint32_t)hi;
            }

            for (;;)
            {
                uint32_t banks[4];
                uint32_t pages[4];
                uint32_t slot_pblk[4];
                uint32_t n = 0;
                uint32_t fail;

                for (uint32_t b = 0; b < nb; b++)
                {
                    if (cur[b] < end[b])
                    {
                        uint32_t pblk = cur[b]++;
                        slot_pblk[n] = pblk;
                        banks[n] = c4own_pblk_bank(pblk);
                        pages[n] = c4own_pblk_page(pblk);
                        n++;
                    }
                }

                if (n == 0)
                    break;

                c4own_read_page_valid = false;
                fail = c4pro_nand_erase_blocks_multi(banks, pages, n);
                for (uint32_t i = 0; i < n; i++)
                    if (fail & (1u << i))
                        c4own_retired[slot_pblk[i]] = 1;
            }
        }
    }
#else
    for (uint32_t pblk = 0; pblk < C4OWN_RUNTIME_PBLK_COUNT; pblk++)
    {
        if (c4own_erase_pblk(pblk))
            c4own_retired[pblk] = 1;
    }
#endif

    if (c4own_write_superblock(0) || c4own_write_superblock(1))
        return 1;

    c4own_make_mbr(sector);
    if (c4own_write_internal(0, 1, sector))
        return 1;

#if C4OWN_IS_FAT32
    c4own_make_fat32_boot(sector);
    if (c4own_write_internal(C4OWN_PARTITION_START, 1, sector) ||
        c4own_write_internal(C4OWN_PARTITION_START + 6u, 1, sector))
        return 1;

    c4own_make_fat32_fsinfo(sector);
    if (c4own_write_internal(C4OWN_PARTITION_START + 1u, 1, sector) ||
        c4own_write_internal(C4OWN_PARTITION_START + 7u, 1, sector))
        return 1;

    c4own_make_fat0(sector);
    if (c4own_write_internal(C4OWN_PARTITION_START + C4OWN_FAT32_RESERVED_SECTORS,
                             1, sector) ||
        c4own_write_internal(C4OWN_PARTITION_START + C4OWN_FAT32_RESERVED_SECTORS +
                             C4OWN_FAT32_FAT_SECTORS, 1, sector))
        return 1;
#else
    c4own_make_fat16_boot(sector);
    if (c4own_write_internal(C4OWN_PARTITION_START, 1, sector))
        return 1;

    c4own_make_fat0(sector);
    if (c4own_write_internal(C4OWN_PARTITION_START + 1u, 1, sector) ||
        c4own_write_internal(C4OWN_PARTITION_START + 129u, 1, sector))
        return 1;
#endif

    if (c4own_flush_cache())
        return 1;

    c4own_formatting = false;

    /* Snapshot the freshly-formatted state so the very next cold boot mounts via
     * the checkpoint instead of scanning. Best-effort: a failure just means the
     * next mount falls back to the scan. */
    c4own_ckpt_write();
    return 0;
}

#endif

uint32_t ftl_init(void)
{
    if (nand_device_init())
        return 1;

    /* Cold-boot robustness: the four dies (CE#0..3) need a brief power-up
     * settle before they answer the ID read. A transient NULL here would make
     * ftl_banks come out smaller than the true die count, which shrinks the
     * computed geometry (c4own_runtime_pblk_count) and makes the persisted
     * superblock look mismatched -> a needless full reformat on cold plug.
     * Retry each die a few times so detection is identical on cold and warm
     * boots and the owned volume mounts instead of reformatting. */
    ftl_nand_type = NULL;
    for (int retry = 0; retry < 20 && !ftl_nand_type; retry++)
    {
        ftl_nand_type = nand_get_device_type(0);
        if (!ftl_nand_type)
            sleep(HZ / 50);
    }
    if (!ftl_nand_type)
        return 1;

    ftl_banks = 1;
    for (uint32_t bank = 1; bank < 4; bank++)
    {
        const struct nand_device_info_type *bank_type = NULL;

        for (int retry = 0; retry < 20 && bank_type != ftl_nand_type; retry++)
        {
            bank_type = nand_get_device_type(bank);
            if (bank_type != ftl_nand_type)
                sleep(HZ / 50);
        }

        if (bank_type == ftl_nand_type)
            ftl_banks++;
        else
            break;
    }
#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)
    if (c4own_mount_scan())
    {
        if (c4own_format() || c4own_mount_scan())
            return 1;
    }
    c4own_ready = true;
#endif
    return 0;
}

uint32_t ftl_total_sectors(void)
{
#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)
    return C4OWN_VOLUME_SECTORS;
#else
    return 0;
#endif
}

uint32_t ftl_read(uint32_t sector, uint32_t count, void* buffer)
{
#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)
    if (!c4own_ready ||
        sector > C4OWN_VOLUME_SECTORS ||
        count > C4OWN_VOLUME_SECTORS - sector)
        return 1;

    return c4own_read_internal(sector, count, buffer) ? 1 : 0;
#else
    (void)sector;
    (void)count;
    (void)buffer;
    return 1;
#endif
}

uint32_t ftl_write(uint32_t sector, uint32_t count, const void* buffer)
{
#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)
    if (!c4own_ready && !c4own_formatting)
        return (uint32_t)-1;

    if (c4own_write_internal(sector, count, buffer))
        return (uint32_t)-1;

#ifdef C4PRO_FTL_ROCKBOX_OWNED_WRITETHROUGH
    if (!c4own_formatting && c4own_flush_cache())
        return (uint32_t)-1;
#endif

    return 0;
#elif defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_RAM_WRITE_TEST)
#else
    (void)sector;
    (void)count;
    (void)buffer;
    return (uint32_t)-1;
#endif
}

uint32_t ftl_sync(void)
{
#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)
    if (c4own_flush_cache())
        return (uint32_t)-1;

    /* Refresh the mount checkpoint only when the map actually changed since the
     * last one (keeps NAND wear on the two superblock blocks proportional to
     * real writes). Best-effort: a failure leaves the prior dirty-marked
     * checkpoint, so the next mount safely falls back to the full scan. */
    if (c4own_ready && c4own_map_dirty)
        c4own_ckpt_write();

    return 0;
#else
    return 0;
#endif
}

#else
/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2010 by Bertrik Sikken
 * Copyright (C) 2026 by Marcin Bukat
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

/* Rockbox storage on the rk27xx NAND, through the FTL scheme the target's
 * config names in CONFIG_RK27XX_FTL: ftl-scheme-a.c or ftl-scheme-b.c.
 *
 * Writing is opt-in: a build without FTL_ALLOW_WRITE mounts read-only and
 * never writes the flash, not even the repairs a mount can make. */

#include "config.h"
#include "system.h"
#include "ftl-target.h"
#include "nand-target.h"
#include "flash-rk27xx.h"
#ifdef FTL_SYS_ON_USB
#include "usb.h"
#endif

#if CONFIG_RK27XX_FTL == RK27XX_FTL_SCHEME_A
#include "ftl-scheme-a.h"
#elif CONFIG_RK27XX_FTL == RK27XX_FTL_SCHEME_B
#include "ftl-scheme-b.h"
#else
#error "NAND storage needs CONFIG_RK27XX_FTL in the target config"
#endif

/* The boot ROM looks for ID blocks at every 512th raw sector of the boot
 * area, up to 50 positions, by metadata type 0x69. */
#define IDB_STRIDE          512
#define IDB_POSITIONS       50
#define IDB_META_TYPE       2
#define IDB_TYPE            0x69
#define IDB_MAX_BOOT_BLOCKS 64

#define SECTORS_PER_MB      2048

#if CONFIG_RK27XX_FTL == RK27XX_FTL_SCHEME_B
/* Scheme B's original firmware keeps its open exchange blocks to 8, and its
 * mount recovers no more (ftl-scheme-b.h) */
#define SCHEME_B_EXCH_BLOCKS 8
#endif

/* What ID block 1 records */
struct idb_info
{
    uint32_t boot_blocks;       /* raw blocks of the boot area */
    uint32_t sys_sectors;       /* the SYS volume (code disk) */
    uint32_t data_sectors;      /* Scheme B: the system data area after it */
};

static bool ftl_mounted = false;

/* The volumes behind the drives. Both are known to the FTL even when SYS
 * is not a drive. */
#if CONFIG_RK27XX_FTL == RK27XX_FTL_SCHEME_A
#define VOL_SYS   FTL_A_VOL_SYS
#define VOL_USER  FTL_A_VOL_USER
#else
#define VOL_SYS   0
#define VOL_USER  1
#define NUM_VOLS  2

/* Scheme B's logical space holds the volumes back to back: SYS, the system
 * data area, then USER to the end */
static uint32_t vol_base[NUM_VOLS];
static uint32_t vol_size[NUM_VOLS];
#endif

#ifdef FTL_SYS_ON_USB
/* Asked for in the debug menu: show SYS through the USER drive while a USB
 * host has the storage. Decided once per connection, so asking during one
 * waits for the next, and used up when that connection ends. */
static bool sys_requested = false;
static enum { USB_VIEW_NONE, USB_VIEW_USER, USB_VIEW_SYS } usb_view;

static bool sys_on_usb(void)
{
    if (!usb_exclusive_storage())
    {
        if (usb_view == USB_VIEW_SYS)
        {
            sys_requested = false;
        }
        usb_view = USB_VIEW_NONE;
    }
    else if (usb_view == USB_VIEW_NONE)
    {
        usb_view = sys_requested ? USB_VIEW_SYS : USB_VIEW_USER;
    }
    return usb_view == USB_VIEW_SYS;
}

void ftl_set_sys_on_usb(bool on)
{
    sys_requested = on;
}

bool ftl_get_sys_on_usb(void)
{
    return sys_requested;
}
#endif

static int drive_volume(int drive)
{
#ifdef HAVE_RK27XX_NAND_SYS
    return (drive == FTL_DRIVE_SYS) ? VOL_SYS : VOL_USER;
#else
    (void)drive;
#ifdef FTL_SYS_ON_USB
    if (sys_on_usb())
    {
        return VOL_SYS;
    }
#endif
    return VOL_USER;
#endif
}

/* ID block 1, the sector after ID block 0:
 *
 *     +0  uint16_t LE   raw blocks of the boot area
 *     +2  uint16_t LE   SYS volume size, MB
 *     +4  uint16_t LE   system data area size, MB (Scheme B)
 *
 * ID block 0 is scrambled; ID block 1 is plain. A sector marked 0x69 whose
 * ID block 1 gives a sane block count and a SYS volume smaller than the
 * chip is taken; descrambling ID block 0 to check its signature would buy
 * little over that. Returns false if none is found. */
static bool idb_read(struct idb_info *idb)
{
    const struct flash_geometry *geo = flash_get_geometry();
    uint8_t data[FLASH_SECTOR_SIZE], meta[FLASH_META_SIZE];
    bool found = false;
    uint32_t pos;

    for (pos = 0; pos < IDB_POSITIONS && !found; pos++)
    {
        uint32_t raw = pos * IDB_STRIDE;
        uint32_t blocks, mb;

        if (raw + 1 >= geo->total_sectors)
        {
            break;
        }

        if (flash_read_raw(raw, data, meta) != 0
            || meta[IDB_META_TYPE] != IDB_TYPE)
        {
            continue;
        }
        if (flash_read_raw(raw + 1, data, meta) != 0)
        {
            continue;
        }

        blocks = data[0] | (data[1] << 8);
        mb     = data[2] | (data[3] << 8);

        if (blocks > 0 && blocks <= IDB_MAX_BOOT_BLOCKS && mb > 0 &&
            mb * SECTORS_PER_MB < geo->total_sectors)
        {
            idb->boot_blocks  = blocks;
            idb->sys_sectors  = mb * SECTORS_PER_MB;
            idb->data_sectors = (data[4] | (data[5] << 8)) * SECTORS_PER_MB;
            found = true;
        }
    }
    return found;
}

#if CONFIG_RK27XX_FTL == RK27XX_FTL_SCHEME_A
static uint32_t mount_scheme(const struct idb_info *idb)
{
    struct ftl_a_config config;
    uint32_t ret = 0;

    config.sys_sectors = idb->sys_sectors;
#ifdef FTL_ALLOW_WRITE
    config.read_only = false;
    /* the rk2705 NAND bootloader's generation formats with flag 1; its
     * write logic is the same as the standard one's */
    config.alt_format_flag = 1;
    config.alt_format_writable = true;
#else
    config.read_only = true;
    config.alt_format_flag = 1;
    config.alt_format_writable = false;
#endif

    if (ftl_a_mount(&config) != FTL_A_OK)
    {
        ret = 3;
    }
    else if (ftl_a_capacity(FTL_A_VOL_USER) == 0)
    {
        ret = 4;
    }
    return ret;
}
#else
static uint32_t mount_scheme(const struct idb_info *idb)
{
    const struct flash_geometry *geo = flash_get_geometry();
    struct ftl_b_config config;
    uint32_t user_base = idb->sys_sectors + idb->data_sectors;
    uint32_t ret = 0;

    config.first_block = (uint16_t)(idb->boot_blocks / geo->planes);
    config.exch_blocks = SCHEME_B_EXCH_BLOCKS;
#ifdef FTL_ALLOW_WRITE
    config.read_only = false;
#else
    config.read_only = true;
#endif

    if (ftl_b_mount(&config) != FTL_B_OK)
    {
        ret = 3;
    }
    else if (ftl_b_capacity() <= user_base)
    {
        ret = 4;
    }
    else
    {
        vol_base[VOL_SYS] = 0;
        vol_size[VOL_SYS] = idb->sys_sectors;
        vol_base[VOL_USER] = user_base;
        vol_size[VOL_USER] = ftl_b_capacity() - user_base;
    }
    return ret;
}
#endif

uint32_t ftl_init(void)
{
    struct idb_info idb;
    uint32_t ret = 0;

    flash_init();

    if (flash_layer_init() != 0)
    {
        ret = 1;
    }
    else if (!idb_read(&idb))
    {
        ret = 2;        /* without it USER cannot be told from SYS */
    }
    else
    {
        ret = mount_scheme(&idb);
        ftl_mounted = ret == 0;
    }
    return ret;
}

static bool drive_valid(int drive)
{
    return ftl_mounted && drive >= 0 && drive < FTL_NUM_DRIVES;
}

#if CONFIG_RK27XX_FTL == RK27XX_FTL_SCHEME_A
static uint32_t vol_sectors(int vol)
{
    return ftl_a_capacity(vol);
}

static int vol_read(int vol, uint32_t sector, uint32_t count, void *buffer)
{
    return ftl_a_read(vol, sector, buffer, count);
}

#ifdef FTL_ALLOW_WRITE
static int vol_write(int vol, uint32_t sector, uint32_t count,
                     const void *buffer)
{
    return ftl_a_write(vol, sector, buffer, count);
}
#endif

static void vol_sync(void)
{
    ftl_a_sync();
}
#else
static uint32_t vol_sectors(int vol)
{
    return vol_size[vol];
}

static bool in_volume(int vol, uint32_t sector, uint32_t count)
{
    return sector < vol_size[vol] && count <= vol_size[vol] - sector;
}

static int vol_read(int vol, uint32_t sector, uint32_t count, void *buffer)
{
    int ret = 1;

    if (in_volume(vol, sector, count))
    {
        ret = ftl_b_read(vol_base[vol] + sector, buffer, count);
    }
    return ret;
}

#ifdef FTL_ALLOW_WRITE
static int vol_write(int vol, uint32_t sector, uint32_t count,
                     const void *buffer)
{
    int ret = 1;

    if (in_volume(vol, sector, count))
    {
        ret = ftl_b_write(vol_base[vol] + sector, buffer, count);
    }
    return ret;
}
#endif

static void vol_sync(void)
{
    ftl_b_sync();
}
#endif

uint32_t ftl_get_sectors(int drive)
{
    uint32_t sectors = 0;

    if (drive_valid(drive))
    {
        sectors = vol_sectors(drive_volume(drive));
    }
    return sectors;
}

bool ftl_drive_is_sys(int drive)
{
    return drive_valid(drive) && drive_volume(drive) == VOL_SYS;
}

uint32_t ftl_read(int drive, uint32_t sector, uint32_t count, void *buffer)
{
    uint32_t ret = 1;

    if (drive_valid(drive))
    {
        ret = vol_read(drive_volume(drive), sector, count, buffer) ? 2 : 0;
    }
    return ret;
}

uint32_t ftl_write(int drive, uint32_t sector, uint32_t count,
                   const void *buffer)
{
    uint32_t ret = 1;

#ifdef FTL_ALLOW_WRITE
    if (drive_valid(drive))
    {
        ret = vol_write(drive_volume(drive), sector, count, buffer) ? 2 : 0;
    }
#else
    /* refuse rather than pretend: a silent success would let the filesystem
     * believe data was committed */
    (void)drive; (void)sector; (void)count; (void)buffer;
#endif
    return ret;
}

uint32_t ftl_sync(void)
{
    if (ftl_mounted)
    {
        vol_sync();
    }
    flash_sync();
    return 0;
}

#endif
