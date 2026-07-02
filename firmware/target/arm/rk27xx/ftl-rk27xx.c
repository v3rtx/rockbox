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
#include "c4pro/c4pro_bch.h"
#if defined(C4PRO_FTL_ECC_SELFTEST) || defined(C4PRO_NAND_DMA_SELFTEST) || \
    defined(C4PRO_NAND_DMA_RWTEST) || defined(C4PRO_NAND_DMA_SPEEDTEST) || \
    defined(C4PRO_NAND_FMWAIT_SWEEP) || defined(C4PRO_NAND_CLKPROBE) || \
    defined(C4PRO_NAND_DELAY_SWEEP) || defined(C4PRO_NAND_HWECC_TEST) || \
    defined(C4PRO_NAND_HWECC_ERASED) || defined(C4PRO_NAND_WDELAY_SWEEP) || \
    defined(C4PRO_NAND_HWECC_MULTIDIE)
#include "lcd.h"
#include "backlight.h"
#endif
#include <stdbool.h>

#ifdef COLORFLY_C4_PRO
struct c4pro_ftl_hash_entry
{
    uint32_t hash;
    uint32_t lba;
};

struct c4pro_ftl_synth_entry
{
    uint32_t lba;
    uint32_t offset;
};

struct c4pro_ftl_range_entry
{
    uint32_t lba;
    uint32_t psec;
    uint32_t count;
    unsigned char mode;
};

#ifdef C4PRO_FTL_GENERATED_LOCAL
#include "c4pro-ftl-generated.h"
#elif defined(C4PRO_FTL_GENERATED_INCLUDE)
#include C4PRO_FTL_GENERATED_INCLUDE
#endif

#ifdef C4PRO_FTL_GENERATED_HEADER
#ifndef C4PRO_FTL_GENERATED_H
#error "C4PRO_FTL_GENERATED_HEADER is set but no generated FTL header was included"
#endif
#endif

/* Plan-B storage mode for C4 bring-up: serve sectors from a generated
 * Rockbox-owned image/extent table and never fall back to guessed vendor FTL
 * placement for missing sectors. This is deliberately still read-only on NAND;
 * C4PRO_FTL_RAM_WRITE_TEST may be paired with it to absorb volatile FAT writes.
 */
#ifdef C4PRO_FTL_ROCKBOX_OWNED
#ifndef C4PRO_FTL_GENERATED_HEADER
#error "C4PRO_FTL_ROCKBOX_OWNED requires a generated C4 FTL header"
#endif
#ifndef C4PRO_FTL_SKIP_RUNTIME_SCAN
#define C4PRO_FTL_SKIP_RUNTIME_SCAN 1
#endif
#ifndef C4PRO_FTL_STRICT_READ_TEST
#define C4PRO_FTL_STRICT_READ_TEST 1
#endif
#endif

/* Saved raw/logical dumps from the C4 show the system image boot sector at
 * physical sector 20480. The full RK27xx FTL layout is still unknown, so this
 * remains a read-only contiguous-window prototype until host-side raw dump
 * analysis proves the real logical-to-physical map.
 */
#ifndef C4PRO_SYSTEM_PSEC_BASE
#define C4PRO_SYSTEM_PSEC_BASE 20480
#endif
#ifndef C4PRO_SYSTEM_SECTORS
#define C4PRO_SYSTEM_SECTORS   43008
#endif
#ifndef C4PRO_STORAGE_SECTORS
#define C4PRO_STORAGE_SECTORS C4PRO_SYSTEM_SECTORS
#endif
#ifndef C4PRO_SECTORS_PER_PAGE
#define C4PRO_SECTORS_PER_PAGE 8
#endif

#ifndef C4PRO_FTL_GENERATED_HEADER
#define C4PRO_FTL_GENERATED_MODE_RAW 1
#endif

#ifndef C4PRO_FTL_GENERATED_MODE_RAW
#error "C4 generated FTL header selected a non-raw mapping; this prototype only copies raw sectors"
#endif

#ifdef C4PRO_FTL_HASH_ORACLE
#ifndef C4PRO_FTL_HASH_SCAN_START
#define C4PRO_FTL_HASH_SCAN_START 0
#endif
#ifndef C4PRO_FTL_HASH_SCAN_SECTORS
#define C4PRO_FTL_HASH_SCAN_SECTORS 65536
#endif
static uint32_t c4pro_ftl_runtime_map[C4PRO_SYSTEM_SECTORS];
static unsigned char c4pro_ftl_runtime_mode[C4PRO_SYSTEM_SECTORS];
static uint16_t c4pro_ftl_delta_counts
    [C4PRO_FTL_HASH_SCAN_SECTORS + C4PRO_SYSTEM_SECTORS + 1];
#endif

#ifndef C4PRO_FTL_LBA_TO_PSEC
#define C4PRO_FTL_LBA_TO_PSEC(lba) (C4PRO_SYSTEM_PSEC_BASE + (lba))
#endif

#ifndef C4PRO_FTL_PSEC_INVALID
#define C4PRO_FTL_PSEC_INVALID 0xffffffffu
#endif

#if defined(C4PRO_FTL_RAM_WRITE_TEST)
#ifndef C4PRO_FTL_RAM_WRITE_SECTORS
#define C4PRO_FTL_RAM_WRITE_SECTORS 512
#endif
static uint32_t c4pro_ftl_ram_write_lba[C4PRO_FTL_RAM_WRITE_SECTORS];
static unsigned char c4pro_ftl_ram_write_valid[C4PRO_FTL_RAM_WRITE_SECTORS];
static unsigned char c4pro_ftl_ram_write_data[C4PRO_FTL_RAM_WRITE_SECTORS][512];

static int c4pro_ftl_ram_write_lookup(uint32_t sector)
{
    for (int i = 0; i < C4PRO_FTL_RAM_WRITE_SECTORS; i++)
    {
        if (c4pro_ftl_ram_write_valid[i] &&
            c4pro_ftl_ram_write_lba[i] == sector)
            return i;
    }

    return -1;
}

static int c4pro_ftl_ram_write_slot(uint32_t sector)
{
    int slot = c4pro_ftl_ram_write_lookup(sector);
    if (slot >= 0)
        return slot;

    for (int i = 0; i < C4PRO_FTL_RAM_WRITE_SECTORS; i++)
    {
        if (!c4pro_ftl_ram_write_valid[i])
        {
            c4pro_ftl_ram_write_valid[i] = 1;
            c4pro_ftl_ram_write_lba[i] = sector;
            return i;
        }
    }

    return -1;
}
#endif

#ifndef C4PRO_FTL_ROCKBOX_OWNED_RW
static unsigned char page_data[4096];
static unsigned char page_spare[128];
#endif
int c4pro_ftl_diag_hash_mapped;
int c4pro_ftl_diag_hash_dupes;
int c4pro_ftl_diag_hash_raw;
int c4pro_ftl_diag_hash_secdec;
int c4pro_ftl_diag_hash_pagedec;
int c4pro_ftl_diag_hash_boot_psec = -1;
int c4pro_ftl_diag_hash_fat_psec = -1;
int c4pro_ftl_diag_hash_root_psec = -1;
int c4pro_ftl_diag_hash_bootbin_psec = -1;
int c4pro_ftl_diag_hash_version_psec = -1;
int c4pro_ftl_diag_hash_rk28_psec = -1;
int c4pro_ftl_diag_hash_boot_mode = -1;
int c4pro_ftl_diag_hash_fat_mode = -1;
int c4pro_ftl_diag_hash_root_mode = -1;
int c4pro_ftl_diag_hash_bootbin_mode = -1;
int c4pro_ftl_diag_hash_version_mode = -1;
int c4pro_ftl_diag_hash_rk28_mode = -1;
int c4pro_ftl_diag_hash_best_delta = 0;
int c4pro_ftl_diag_hash_best_delta_count = 0;
int c4pro_ftl_diag_sig_fat_psec = -1;
int c4pro_ftl_diag_sig_root_psec = -1;
int c4pro_ftl_diag_sig_fat_mode = -1;
int c4pro_ftl_diag_sig_root_mode = -1;
int c4pro_ftl_diag_synth_sectors;

#ifdef C4PRO_FTL_SYNTH_AUDIT
#ifndef C4PRO_FTL_AUDIT_START_LBA
#define C4PRO_FTL_AUDIT_START_LBA 47488u
#endif
#ifndef C4PRO_FTL_AUDIT_SECTORS
#define C4PRO_FTL_AUDIT_SECTORS 256u
#endif
int c4pro_ftl_audit_total;
int c4pro_ftl_audit_ok;
int c4pro_ftl_audit_missing;
int c4pro_ftl_audit_bad;
int c4pro_ftl_audit_first_lba = -1;
int c4pro_ftl_audit_first_psec = -1;
int c4pro_ftl_audit_first_mode = -1;
int c4pro_ftl_audit_first_expected = -1;
int c4pro_ftl_audit_first_actual = -1;
#endif

#ifdef C4PRO_FTL_LBA_DIAG
#ifndef C4PRO_FTL_LBA_DIAG0
#define C4PRO_FTL_LBA_DIAG0 47488u
#endif
#ifndef C4PRO_FTL_LBA_DIAG1
#define C4PRO_FTL_LBA_DIAG1 47489u
#endif
#ifndef C4PRO_FTL_LBA_DIAG2
#define C4PRO_FTL_LBA_DIAG2 47490u
#endif
#ifndef C4PRO_FTL_LBA_DIAG3
#define C4PRO_FTL_LBA_DIAG3 104352u
#endif
int c4pro_ftl_diag_probe_lba[4];
int c4pro_ftl_diag_probe_psec[4];
int c4pro_ftl_diag_probe_inferred[4];
int c4pro_ftl_diag_probe_mode[4];
int c4pro_ftl_diag_probe_synth[4];
#endif

#define C4PRO_FTL_MODE_RAW 0
#define C4PRO_FTL_MODE_SECDEC 1
#define C4PRO_FTL_MODE_PAGEDEC 2

#ifdef C4PRO_FTL_DIAG_EXPORT
#ifndef C4PRO_FTL_DIAG_EXPORT_LBA
#define C4PRO_FTL_DIAG_EXPORT_LBA (C4PRO_SYSTEM_SECTORS - 1)
#endif
#ifndef C4PRO_FTL_DIAG_EXPORT_PSEC
#define C4PRO_FTL_DIAG_EXPORT_PSEC 297128u
#endif

static void c4pro_ftl_put32le(unsigned char *buf, int off, uint32_t value)
{
    buf[off + 0] = (unsigned char)value;
    buf[off + 1] = (unsigned char)(value >> 8);
    buf[off + 2] = (unsigned char)(value >> 16);
    buf[off + 3] = (unsigned char)(value >> 24);
}

static uint32_t c4pro_ftl_read_diag_export(unsigned char *out)
{
    uint32_t page = C4PRO_FTL_DIAG_EXPORT_PSEC / C4PRO_SECTORS_PER_PAGE;
    int off = 16;

    memset(out, 0, 512);
    out[0] = 'C';
    out[1] = '4';
    out[2] = 'D';
    out[3] = 'G';
    c4pro_ftl_put32le(out, 4, C4PRO_FTL_DIAG_EXPORT_PSEC);
    c4pro_ftl_put32le(out, 8, page);
    c4pro_ftl_put32le(out, 12, C4PRO_SECTORS_PER_PAGE);

    if (nand_read_page(0, page, page_data, page_spare, 1, 0))
    {
        out[3] = 'E';
        return 1;
    }

    for (int sec = 0; sec < C4PRO_SECTORS_PER_PAGE; sec++)
    {
        const unsigned char *data = page_data + sec * 512;
        const unsigned char *spare = page_spare + sec * 16;
        uint32_t sig = ((uint32_t)data[0] << 24) |
                       ((uint32_t)data[1] << 16) |
                       ((uint32_t)data[2] << 8) |
                       (uint32_t)data[3];

        out[off++] = (unsigned char)sec;
        out[off++] = 0;
        out[off++] = 0;
        out[off++] = 0;
        c4pro_ftl_put32le(out, off, sig);
        off += 4;
        memcpy(out + off, spare, 16);
        off += 16;
    }

    return 0;
}
#endif

#ifdef C4PRO_FTL_HASH_ORACLE
#ifndef C4PRO_FTL_SKIP_RUNTIME_SCAN
static const unsigned char c4pro_root_sfn[7][11] =
{
    "APPDATA    ",
    "RESOURCE   ",
    "RK27BOOTBIN",
    "SYSTEM     ",
    "SYSTEM00   ",
    "VERSION BIN",
    "RK28IMG CHK",
};

static const unsigned char c4pro_root_attr[7] =
{
    0x10, 0x10, 0x20, 0x10, 0x10, 0x20, 0x20,
};
#endif

#ifndef C4PRO_FTL_SKIP_RUNTIME_SCAN
static uint32_t c4pro_ftl_hash_sector(const unsigned char *data)
{
    uint32_t hash = 2166136261u;

    for (int i = 0; i < 512; i++)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}

static uint32_t c4pro_ftl_hash_sector_xor(const unsigned char *data,
                                          const unsigned char *key)
{
    uint32_t hash = 2166136261u;

    for (int i = 0; i < 512; i++)
    {
        hash ^= data[i] ^ key[i];
        hash *= 16777619u;
    }

    return hash;
}
#endif

#if !defined(C4PRO_FTL_SKIP_RUNTIME_SCAN) || defined(C4PRO_FTL_SYNTH_AUDIT)
static unsigned char c4pro_ftl_sector_byte(const unsigned char *data,
                                           uint32_t sector_in_page,
                                           int mode, int offset)
{
#ifdef C4PRO_FTL_HASH_HAS_RK27_KEYS
    if (mode == C4PRO_FTL_MODE_SECDEC)
        return data[offset] ^ c4pro_rk27_sector_key[offset];
    if (mode == C4PRO_FTL_MODE_PAGEDEC)
        return data[offset] ^
               c4pro_rk27_page_key[sector_in_page * 512 + offset];
#else
    (void)sector_in_page;
#endif
    return data[offset];
}
#endif

#ifndef C4PRO_FTL_SKIP_RUNTIME_SCAN
static int c4pro_ftl_sector_starts_with(const unsigned char *data,
                                        uint32_t sector_in_page,
                                        int mode,
                                        const unsigned char *expected,
                                        int len)
{
    for (int i = 0; i < len; i++)
    {
        if (c4pro_ftl_sector_byte(data, sector_in_page, mode, i) != expected[i])
            return 0;
    }

    return 1;
}

static int c4pro_ftl_sector_is_fat1(const unsigned char *data,
                                    uint32_t sector_in_page, int mode)
{
    static const unsigned char sig[16] =
    {
        0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0x04, 0x00,
        0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00,
    };

    return c4pro_ftl_sector_starts_with(data, sector_in_page, mode, sig,
                                        sizeof(sig));
}

static int c4pro_ftl_sector_is_root(const unsigned char *data,
                                    uint32_t sector_in_page, int mode)
{
    for (int entry = 0; entry < 7; entry++)
    {
        int off = entry * 32;

        for (int i = 0; i < 11; i++)
        {
            if (c4pro_ftl_sector_byte(data, sector_in_page, mode, off + i) !=
                c4pro_root_sfn[entry][i])
                return 0;
        }

        if (c4pro_ftl_sector_byte(data, sector_in_page, mode, off + 11) !=
            c4pro_root_attr[entry])
            return 0;
    }

    return 1;
}
#endif

#if !defined(C4PRO_FTL_SKIP_RUNTIME_SCAN) || \
    defined(C4PRO_FTL_HIT_PAGE_PROBE) || defined(C4PRO_FTL_NEAR_HASH_PROBE) || \
    defined(C4PRO_FTL_LBA_SLOT_PROBE)
static int c4pro_ftl_hash_lookup(uint32_t hash)
{
    int lo = 0;
    int hi = C4PRO_FTL_HASH_ENTRY_COUNT - 1;

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        uint32_t mid_hash = c4pro_ftl_hash_entries[mid].hash;

        if (hash < mid_hash)
            hi = mid - 1;
        else if (hash > mid_hash)
            lo = mid + 1;
        else
            return (int)c4pro_ftl_hash_entries[mid].lba;
    }

    return -1;
}
#endif

#if defined(C4PRO_FTL_HIT_PAGE_PROBE) || defined(C4PRO_FTL_NEAR_HASH_PROBE) || \
    defined(C4PRO_FTL_LBA_SLOT_PROBE)
int c4pro_ftl_diag_hash_lookup(uint32_t hash)
{
    return c4pro_ftl_hash_lookup(hash);
}
#endif

static int c4pro_ftl_synth_lookup(uint32_t lba)
{
#ifdef C4PRO_FTL_SYNTH_SECTOR_COUNT
    int lo = 0;
    int hi = C4PRO_FTL_SYNTH_SECTOR_COUNT - 1;

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        uint32_t mid_lba = c4pro_ftl_synth_entries[mid].lba;

        if (lba < mid_lba)
            hi = mid - 1;
        else if (lba > mid_lba)
            lo = mid + 1;
        else
            return (int)c4pro_ftl_synth_entries[mid].offset;
    }
#else
    (void)lba;
#endif

    return -1;
}

#ifdef C4PRO_FTL_PROC_WAV_TEST
#ifndef C4PRO_FTL_PROC_WAV_LBA
#define C4PRO_FTL_PROC_WAV_LBA 47488u
#endif
#ifndef C4PRO_FTL_PROC_WAV_BYTES
#define C4PRO_FTL_PROC_WAV_BYTES 1764044u
#endif
#ifndef C4PRO_FTL_PROC_WAV_RATE
#define C4PRO_FTL_PROC_WAV_RATE 44100u
#endif
#ifndef C4PRO_FTL_PROC_WAV_FREQ
#define C4PRO_FTL_PROC_WAV_FREQ 440u
#endif
#ifndef C4PRO_FTL_PROC_WAV_AMP
#define C4PRO_FTL_PROC_WAV_AMP 18000
#endif

static void c4pro_ftl_proc_wav_sector(uint32_t lba, unsigned char *out)
{
    uint32_t sector_off = (lba - C4PRO_FTL_PROC_WAV_LBA) * 512u;
    uint32_t data_bytes = C4PRO_FTL_PROC_WAV_BYTES - 44u;

    memset(out, 0, 512);

    if (sector_off == 0)
    {
        static const unsigned char hdr_prefix[44] =
        {
            'R', 'I', 'F', 'F',
            0, 0, 0, 0,
            'W', 'A', 'V', 'E',
            'f', 'm', 't', ' ',
            16, 0, 0, 0,
            1, 0,
            2, 0,
            0x44, 0xac, 0x00, 0x00,
            0x10, 0xb1, 0x02, 0x00,
            4, 0,
            16, 0,
            'd', 'a', 't', 'a',
            0, 0, 0, 0,
        };

        memcpy(out, hdr_prefix, sizeof(hdr_prefix));
        uint32_t riff_size = C4PRO_FTL_PROC_WAV_BYTES - 8u;
        out[4] = riff_size;
        out[5] = riff_size >> 8;
        out[6] = riff_size >> 16;
        out[7] = riff_size >> 24;
        out[40] = data_bytes;
        out[41] = data_bytes >> 8;
        out[42] = data_bytes >> 16;
        out[43] = data_bytes >> 24;
    }

    uint32_t first_byte = sector_off > 44u ? sector_off - 44u : 0u;
    uint32_t sample = (first_byte + 3u) / 4u;

    while (true)
    {
        uint32_t file_byte = 44u + sample * 4u;
        if (file_byte >= sector_off + 512u ||
            file_byte >= C4PRO_FTL_PROC_WAV_BYTES)
            break;

        uint32_t off = file_byte - sector_off;
        uint32_t phase = (sample * C4PRO_FTL_PROC_WAV_FREQ) %
                         C4PRO_FTL_PROC_WAV_RATE;
        int16_t value = phase < (C4PRO_FTL_PROC_WAV_RATE / 2u) ?
                        C4PRO_FTL_PROC_WAV_AMP : -C4PRO_FTL_PROC_WAV_AMP;
        unsigned char lo = value & 0xff;
        unsigned char hi = (uint16_t)value >> 8;

        if (off + 3u < 512u)
        {
            out[off + 0u] = lo;
            out[off + 1u] = hi;
            out[off + 2u] = lo;
            out[off + 3u] = hi;
        }

        sample++;
    }
}

static bool c4pro_ftl_proc_wav_lookup(uint32_t lba)
{
    uint32_t count = (C4PRO_FTL_PROC_WAV_BYTES + 511u) / 512u;
    return lba >= C4PRO_FTL_PROC_WAV_LBA &&
           lba < C4PRO_FTL_PROC_WAV_LBA + count;
}
#endif

static int c4pro_ftl_explicit_lookup(uint32_t lba, uint32_t *psec,
                                     unsigned char *mode)
{
#ifdef C4PRO_FTL_EXPLICIT_RANGE_COUNT
    int lo = 0;
    int hi = C4PRO_FTL_EXPLICIT_RANGE_COUNT - 1;

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        const struct c4pro_ftl_range_entry *entry =
            &c4pro_ftl_explicit_ranges[mid];

        if (lba < entry->lba)
        {
            hi = mid - 1;
        }
        else if (lba >= entry->lba + entry->count)
        {
            lo = mid + 1;
        }
        else
        {
            uint32_t offset = lba - entry->lba;
            *psec = entry->psec + offset;
            *mode = entry->mode;
            return 1;
        }
    }
#else
    (void)lba;
#endif
    (void)psec;
    (void)mode;
    return 0;
}

#ifndef C4PRO_FTL_SKIP_RUNTIME_SCAN
static void c4pro_ftl_record_hash_match(uint32_t psec, int lba, int mode)
{
    int delta;
    int delta_index;

    if (lba < 0 || (uint32_t)lba >= C4PRO_SYSTEM_SECTORS)
        return;

    if (c4pro_ftl_runtime_map[lba] != C4PRO_FTL_PSEC_INVALID)
    {
        c4pro_ftl_diag_hash_dupes++;
        return;
    }

    c4pro_ftl_runtime_map[lba] = psec;
    c4pro_ftl_runtime_mode[lba] = mode;
    c4pro_ftl_diag_hash_mapped++;

    delta = (int)psec - lba;
    delta_index = delta + C4PRO_SYSTEM_SECTORS;
    if (delta_index >= 0 &&
        delta_index < (int)(sizeof(c4pro_ftl_delta_counts) /
                            sizeof(c4pro_ftl_delta_counts[0])))
    {
        uint16_t count = ++c4pro_ftl_delta_counts[delta_index];
        if (count > c4pro_ftl_diag_hash_best_delta_count)
        {
            c4pro_ftl_diag_hash_best_delta_count = count;
            c4pro_ftl_diag_hash_best_delta = delta;
        }
    }

    if (mode == C4PRO_FTL_MODE_RAW)
        c4pro_ftl_diag_hash_raw++;
    else if (mode == C4PRO_FTL_MODE_SECDEC)
        c4pro_ftl_diag_hash_secdec++;
    else if (mode == C4PRO_FTL_MODE_PAGEDEC)
        c4pro_ftl_diag_hash_pagedec++;

    if (lba == 0)
    {
        c4pro_ftl_diag_hash_boot_psec = psec;
        c4pro_ftl_diag_hash_boot_mode = mode;
    }
    else if (lba == 1)
    {
        c4pro_ftl_diag_hash_fat_psec = psec;
        c4pro_ftl_diag_hash_fat_mode = mode;
    }
    else if (lba == 85)
    {
        c4pro_ftl_diag_hash_root_psec = psec;
        c4pro_ftl_diag_hash_root_mode = mode;
    }
    else if (lba == 26985)
    {
        c4pro_ftl_diag_hash_bootbin_psec = psec;
        c4pro_ftl_diag_hash_bootbin_mode = mode;
    }
    else if (lba == 37117)
    {
        c4pro_ftl_diag_hash_version_psec = psec;
        c4pro_ftl_diag_hash_version_mode = mode;
    }
    else if (lba == 37121)
    {
        c4pro_ftl_diag_hash_rk28_psec = psec;
        c4pro_ftl_diag_hash_rk28_mode = mode;
    }
}

static void c4pro_ftl_record_signature_match(uint32_t psec,
                                             uint32_t sector_in_page,
                                             int mode)
{
    const unsigned char *data = page_data;

    if (c4pro_ftl_runtime_map[1] == C4PRO_FTL_PSEC_INVALID &&
        c4pro_ftl_sector_is_fat1(data, sector_in_page, mode))
    {
        c4pro_ftl_runtime_map[1] = psec;
        c4pro_ftl_runtime_mode[1] = mode;
        c4pro_ftl_diag_sig_fat_psec = psec;
        c4pro_ftl_diag_sig_fat_mode = mode;
        if (c4pro_ftl_diag_hash_fat_psec < 0)
        {
            c4pro_ftl_diag_hash_fat_psec = psec;
            c4pro_ftl_diag_hash_fat_mode = mode;
        }
    }

    if (c4pro_ftl_runtime_map[85] == C4PRO_FTL_PSEC_INVALID &&
        c4pro_ftl_sector_is_root(data, sector_in_page, mode))
    {
        c4pro_ftl_runtime_map[85] = psec;
        c4pro_ftl_runtime_mode[85] = mode;
        c4pro_ftl_diag_sig_root_psec = psec;
        c4pro_ftl_diag_sig_root_mode = mode;
        if (c4pro_ftl_diag_hash_root_psec < 0)
        {
            c4pro_ftl_diag_hash_root_psec = psec;
            c4pro_ftl_diag_hash_root_mode = mode;
        }
    }
}
#endif

static uint32_t c4pro_ftl_lba_to_psec(uint32_t lba)
{
    unsigned char explicit_mode;
    uint32_t psec;
    int inferred_psec;

    if (c4pro_ftl_explicit_lookup(lba, &psec, &explicit_mode))
        return psec;

    psec = c4pro_ftl_runtime_map[lba];
    if (psec != C4PRO_FTL_PSEC_INVALID)
        return psec;

#ifdef C4PRO_FTL_STRICT_READ_TEST
    return C4PRO_FTL_PSEC_INVALID;
#endif

    inferred_psec = (int)lba + c4pro_ftl_diag_hash_best_delta;
    if (c4pro_ftl_diag_hash_best_delta_count >= 16 &&
        inferred_psec >= (int)C4PRO_FTL_HASH_SCAN_START &&
        inferred_psec < (int)(C4PRO_FTL_HASH_SCAN_START +
                              C4PRO_FTL_HASH_SCAN_SECTORS))
        return (uint32_t)inferred_psec;

    return C4PRO_SYSTEM_PSEC_BASE + lba;
}

static unsigned char c4pro_ftl_lba_mode(uint32_t lba)
{
    unsigned char mode;
    uint32_t psec;

    if (c4pro_ftl_explicit_lookup(lba, &psec, &mode))
        return mode;

    if (c4pro_ftl_runtime_map[lba] != C4PRO_FTL_PSEC_INVALID)
        return c4pro_ftl_runtime_mode[lba];

    return C4PRO_FTL_MODE_RAW;
}

#ifdef C4PRO_FTL_SYNTH_AUDIT
static uint32_t c4pro_ftl_read32be(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

void c4pro_ftl_synth_audit(void)
{
    c4pro_ftl_audit_total = 0;
    c4pro_ftl_audit_ok = 0;
    c4pro_ftl_audit_missing = 0;
    c4pro_ftl_audit_bad = 0;
    c4pro_ftl_audit_first_lba = -1;
    c4pro_ftl_audit_first_psec = -1;
    c4pro_ftl_audit_first_mode = -1;
    c4pro_ftl_audit_first_expected = -1;
    c4pro_ftl_audit_first_actual = -1;

#ifdef C4PRO_FTL_SYNTH_SECTOR_COUNT
    for (int i = 0; i < C4PRO_FTL_SYNTH_SECTOR_COUNT; i++)
    {
        uint32_t lba = c4pro_ftl_synth_entries[i].lba;
        const unsigned char *expected;
        uint32_t psec;
        uint32_t page;
        uint32_t sector_in_page;
        unsigned char mode;
        unsigned char actual[512];
        int offset;
        int same = 1;

        if (lba < C4PRO_FTL_AUDIT_START_LBA ||
            lba >= C4PRO_FTL_AUDIT_START_LBA + C4PRO_FTL_AUDIT_SECTORS)
            continue;

        c4pro_ftl_audit_total++;
        offset = c4pro_ftl_synth_lookup(lba);
        if (offset < 0)
            continue;
        expected = c4pro_ftl_synth_data + offset;
        psec = c4pro_ftl_lba_to_psec(lba);

        if (psec == C4PRO_FTL_PSEC_INVALID)
        {
            c4pro_ftl_audit_missing++;
            if (c4pro_ftl_audit_first_lba < 0)
            {
                c4pro_ftl_audit_first_lba = (int)lba;
                c4pro_ftl_audit_first_expected =
                    (int)c4pro_ftl_read32be(expected);
            }
            continue;
        }

        page = psec / C4PRO_SECTORS_PER_PAGE;
        sector_in_page = psec & (C4PRO_SECTORS_PER_PAGE - 1);
        if (c4pro_nand_read_sector(0, page, sector_in_page,
                                   page_data, page_spare))
        {
            c4pro_ftl_audit_missing++;
            if (c4pro_ftl_audit_first_lba < 0)
            {
                c4pro_ftl_audit_first_lba = (int)lba;
                c4pro_ftl_audit_first_psec = (int)psec;
                c4pro_ftl_audit_first_expected =
                    (int)c4pro_ftl_read32be(expected);
            }
            continue;
        }

        mode = c4pro_ftl_lba_mode(lba);
        for (int byte = 0; byte < 512; byte++)
            actual[byte] = c4pro_ftl_sector_byte(
                page_data, sector_in_page, mode, byte);

        for (int byte = 0; byte < 512; byte++)
        {
            if (actual[byte] != expected[byte])
            {
                same = 0;
                break;
            }
        }

        if (same)
        {
            c4pro_ftl_audit_ok++;
        }
        else
        {
            c4pro_ftl_audit_bad++;
            if (c4pro_ftl_audit_first_lba < 0)
            {
                c4pro_ftl_audit_first_lba = (int)lba;
                c4pro_ftl_audit_first_psec = (int)psec;
                c4pro_ftl_audit_first_mode = mode;
                c4pro_ftl_audit_first_expected =
                    (int)c4pro_ftl_read32be(expected);
                c4pro_ftl_audit_first_actual =
                    (int)c4pro_ftl_read32be(actual);
            }
        }
    }
#endif
}
#endif

#ifdef C4PRO_FTL_LBA_DIAG
static void c4pro_ftl_update_lba_diag(void)
{
    static const uint32_t lbas[4] =
    {
        C4PRO_FTL_LBA_DIAG0,
        C4PRO_FTL_LBA_DIAG1,
        C4PRO_FTL_LBA_DIAG2,
        C4PRO_FTL_LBA_DIAG3,
    };

    for (int i = 0; i < 4; i++)
    {
        uint32_t lba = lbas[i];
        uint32_t psec = C4PRO_FTL_PSEC_INVALID;
        int inferred = -1;

        c4pro_ftl_diag_probe_lba[i] = (int)lba;
        c4pro_ftl_diag_probe_psec[i] = -1;
        c4pro_ftl_diag_probe_inferred[i] = -1;
        c4pro_ftl_diag_probe_mode[i] = -1;
        c4pro_ftl_diag_probe_synth[i] = 0;

        if (lba >= C4PRO_SYSTEM_SECTORS)
            continue;

        psec = c4pro_ftl_runtime_map[lba];
        if (psec != C4PRO_FTL_PSEC_INVALID)
        {
            c4pro_ftl_diag_probe_psec[i] = (int)psec;
            c4pro_ftl_diag_probe_mode[i] = c4pro_ftl_runtime_mode[lba];
        }

#ifdef C4PRO_FTL_SYNTH_SECTOR_COUNT
        c4pro_ftl_diag_probe_synth[i] = c4pro_ftl_synth_lookup(lba) >= 0;
#endif
        inferred = (int)lba + c4pro_ftl_diag_hash_best_delta;
        if (c4pro_ftl_diag_hash_best_delta_count >= 16 &&
            inferred >= (int)C4PRO_FTL_HASH_SCAN_START &&
            inferred < (int)(C4PRO_FTL_HASH_SCAN_START +
                             C4PRO_FTL_HASH_SCAN_SECTORS))
            c4pro_ftl_diag_probe_inferred[i] = inferred;
    }
}
#endif

static void c4pro_ftl_build_hash_map(void)
{
    uint32_t start = C4PRO_FTL_HASH_SCAN_START;
    uint32_t end = start + C4PRO_FTL_HASH_SCAN_SECTORS;

    c4pro_ftl_diag_hash_mapped = 0;
    c4pro_ftl_diag_hash_dupes = 0;
    c4pro_ftl_diag_hash_raw = 0;
    c4pro_ftl_diag_hash_secdec = 0;
    c4pro_ftl_diag_hash_pagedec = 0;
    c4pro_ftl_diag_hash_boot_psec = -1;
    c4pro_ftl_diag_hash_fat_psec = -1;
    c4pro_ftl_diag_hash_root_psec = -1;
    c4pro_ftl_diag_hash_bootbin_psec = -1;
    c4pro_ftl_diag_hash_version_psec = -1;
    c4pro_ftl_diag_hash_rk28_psec = -1;
    c4pro_ftl_diag_hash_boot_mode = -1;
    c4pro_ftl_diag_hash_fat_mode = -1;
    c4pro_ftl_diag_hash_root_mode = -1;
    c4pro_ftl_diag_hash_bootbin_mode = -1;
    c4pro_ftl_diag_hash_version_mode = -1;
    c4pro_ftl_diag_hash_rk28_mode = -1;
    c4pro_ftl_diag_hash_best_delta = 0;
    c4pro_ftl_diag_hash_best_delta_count = 0;
    c4pro_ftl_diag_sig_fat_psec = -1;
    c4pro_ftl_diag_sig_root_psec = -1;
    c4pro_ftl_diag_sig_fat_mode = -1;
    c4pro_ftl_diag_sig_root_mode = -1;
#ifdef C4PRO_FTL_SYNTH_SECTOR_COUNT
    c4pro_ftl_diag_synth_sectors = C4PRO_FTL_SYNTH_SECTOR_COUNT;
#else
    c4pro_ftl_diag_synth_sectors = 0;
#endif

    for (uint32_t lba = 0; lba < C4PRO_SYSTEM_SECTORS; lba++)
    {
        c4pro_ftl_runtime_map[lba] = C4PRO_FTL_PSEC_INVALID;
        c4pro_ftl_runtime_mode[lba] = C4PRO_FTL_MODE_RAW;
    }
    memset(c4pro_ftl_delta_counts, 0, sizeof(c4pro_ftl_delta_counts));

#ifndef C4PRO_FTL_SKIP_RUNTIME_SCAN
    for (uint32_t psec = start; psec < end; psec++)
    {
        uint32_t page = psec / C4PRO_SECTORS_PER_PAGE;
        uint32_t sector_in_page = psec & (C4PRO_SECTORS_PER_PAGE - 1);
        int lba;

        if (c4pro_nand_read_sector(0, page, sector_in_page,
                                   page_data, page_spare))
            continue;

        lba = c4pro_ftl_hash_lookup(c4pro_ftl_hash_sector(page_data));
        c4pro_ftl_record_hash_match(psec, lba, C4PRO_FTL_MODE_RAW);
        c4pro_ftl_record_signature_match(psec, sector_in_page,
                                         C4PRO_FTL_MODE_RAW);

#ifdef C4PRO_FTL_HASH_HAS_RK27_KEYS
        lba = c4pro_ftl_hash_lookup(
            c4pro_ftl_hash_sector_xor(page_data, c4pro_rk27_sector_key));
        c4pro_ftl_record_hash_match(psec, lba, C4PRO_FTL_MODE_SECDEC);
        c4pro_ftl_record_signature_match(psec, sector_in_page,
                                         C4PRO_FTL_MODE_SECDEC);

        lba = c4pro_ftl_hash_lookup(
            c4pro_ftl_hash_sector_xor(
                page_data, c4pro_rk27_page_key + sector_in_page * 512));
        c4pro_ftl_record_hash_match(psec, lba, C4PRO_FTL_MODE_PAGEDEC);
        c4pro_ftl_record_signature_match(psec, sector_in_page,
                                         C4PRO_FTL_MODE_PAGEDEC);
#endif
    }
#else
    (void)start;
    (void)end;
#endif

#ifdef C4PRO_FTL_LBA_DIAG
    c4pro_ftl_update_lba_diag();
#endif
}

#undef C4PRO_FTL_LBA_TO_PSEC
#define C4PRO_FTL_LBA_TO_PSEC(lba) c4pro_ftl_lba_to_psec(lba)
#endif
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
static unsigned char c4own_page_spare[128];
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
int c4own_diag_format_count;
int c4own_diag_write_count;
int c4own_diag_read_count;
int c4own_diag_last_write_sector = -1;
int c4own_diag_last_write_count;
int c4own_diag_last_read_sector = -1;
int c4own_diag_map0 = -1;
int c4own_diag_map2 = -1;
int c4own_diag_lba0_sig = -1;
int c4own_diag_lba2048_sig = -1;
int c4own_diag_last_write_result;
int c4own_diag_last_writeback_sig = -1;
int c4own_diag_cache_flush_count;
int c4own_diag_cache_logical = -1;

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

static unsigned char c4own_read_spare[128];

/* Diagnostics: count BCH outcomes across the corrected read path so a self-test
 * can tell uncorrectable bit-rot (rc<0) apart from a logical/map bug. */
int c4own_diag_uncorr;     /* sectors BCH reported uncorrectable (rc < 0) */
int c4own_diag_corrected;  /* total bits BCH corrected (sum of rc >= 0) */

/* Read a full 4096-byte page via the LINEAR path (which matches the linear PIO
 * write layout) plus its 128-byte spare, then software-BCH-correct each of the
 * 8 sectors in place. The hardware buffered PAGE_BUF engine uses a different
 * on-NAND layout that cannot read our PIO-written pages, so every owned-FTL
 * read goes through this. Correction is best-effort: an uncorrectable sector is
 * left as read (c4pro_bch_correct_sector returns -1 and does not alter it). */
static int c4own_read_page_corrected(uint32_t bank, uint32_t page,
                                     unsigned char *out4096)
{
#ifdef C4PRO_NAND_HWECC
    /* Hardware BCH: the controller corrects each sector on read (inline 512+16
     * layout) and returns the 4096 data bytes directly -- no software BCH decode
     * (which was the ~355us/sector wall capping FTL reads at ~1.16 MB/s). */
    return c4pro_nand_read_page_hwecc(bank, page, out4096,
                                      C4OWN_SECTORS_PER_PAGE);
#else
    unsigned s;

#ifdef C4PRO_NAND_DMA
    /* Fast path: buffered PAGE_BUF engine (ECC off) for the 4096 data bytes +
     * a short PIO spare read, drop-in for the per-byte PIO loop. Same linear
     * layout, so the software BCH decode below is unchanged. */
    if (c4pro_nand_read_linear_page_spare_dma(bank, page, out4096,
                                              c4own_read_spare))
        return 1;
#else
    if (c4pro_nand_read_linear_page_spare(bank, page, out4096,
                                          c4own_read_spare))
        return 1;
#endif

    for (s = 0; s < C4OWN_SECTORS_PER_PAGE; s++)
    {
        int rc = c4pro_bch_correct_sector(out4096 + s * 512u,
                                          c4own_read_spare + s * 16u);
        if (rc < 0)
            c4own_diag_uncorr++;
        else
            c4own_diag_corrected += rc;
    }
    return 0;
#endif /* C4PRO_NAND_HWECC */
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

#if defined(C4PRO_BOOT_HEARTBEAT) || defined(C4PRO_FAULT_VISIBLE)
/* Write-path phase marker for the heartbeat freeze localizer (also read by the
 * visible-fault handler to tag WHICH stage faulted). Set at entry of each
 * sustained-write stage (all run in usb-thread context, so no IRQ race). On a
 * silent freeze the frozen value = the innermost stage the usb thread reached:
 *   1  USB (usb_storage, between/after a write)   2  usb_storage write dispatch
 *   3  c4own_write_logical_block                  4  superblock/checkpoint flush
 *   5  c4own_write_page (NAND hwecc program)      6  c4own_erase_pblk (erase)
 *   10 send_csw (about to send CSW)               11 bulk-IN sync sender (CSW DMA)
 *   12 CSW sent, arming next-command receive     13 armed, waiting for host
 *                                                    bulk-OUT WRITE data (the
 *                                                    exact state usbmon
 *                                                    caught wedged 2026-07-02,
 *                                                    see freeze doc s5.1/5.2) */
volatile uint32_t c4pro_hb_phase;
#endif

#ifdef C4PRO_BOOT_HEARTBEAT
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
#ifdef C4PRO_NAND_HWECC
    /* Controller generates BCH-8 parity into the inline spare on program; no
     * software encode. Reads via c4pro_nand_read_page_hwecc correct in hardware. */
    return c4pro_nand_write_page_hwecc(c4own_pblk_bank(pblk),
                                       c4own_pblk_page(pblk) + page_in_block,
                                       c4own_page_data, C4OWN_SECTORS_PER_PAGE);
#else
    /* Generate BCH parity for every sector so the hardware engine can correct
     * bit errors on read; the 0xA24 decoder reads this layout back. */
    c4pro_bch_fill_page_spare(c4own_page_data, c4own_page_spare);
#ifdef C4PRO_NAND_DMA_WRITE
    /* Experimental engine-assisted program (default off). tPROG-bound, so the
     * gain is modest; validated only after the read path is byte-for-byte
     * proven. Same linear layout, so reads (DMA or PIO) are unaffected. */
    return c4pro_nand_write_page_dma(c4own_pblk_bank(pblk),
                                     c4own_pblk_page(pblk) + page_in_block,
                                     c4own_page_data, c4own_page_spare);
#else
    return c4pro_nand_write_page(c4own_pblk_bank(pblk),
                                 c4own_pblk_page(pblk) + page_in_block,
                                 c4own_page_data, c4own_page_spare);
#endif
#endif /* C4PRO_NAND_HWECC */
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
    c4own_diag_cache_logical = -1;
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
}

/* Serialize the live map into a fresh checkpoint on the inactive superblock
 * block. Best-effort: on any failure the old checkpoint (already dirty-marked,
 * because map_dirty implies a prior commit) keeps mount on the scan path. */
static int c4own_ckpt_write(void)
{
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
     * and we fall through to the authoritative scan. */
    if (c4own_ckpt_load())
        return 0;

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

    c4own_diag_cache_flush_count++;
    if (c4own_write_logical_block(c4own_cache_logical[slot], 0,
                                  C4OWN_PAYLOAD_SECTORS_PER_BLOCK,
                                  c4own_cache_data[slot]))
    {
        c4own_diag_last_write_result = 3;
        return 1;
    }

    c4own_cache_dirty[slot] = false;
    c4own_diag_map0 = c4own_map[0] == 0xffffu ? -1 : (int)c4own_map[0];
    c4own_diag_map2 = c4own_map[2] == 0xffffu ? -1 : (int)c4own_map[2];
    return 0;
}

static int c4own_flush_cache(void)
{
    for (uint32_t slot = 0; slot < C4OWN_CACHE_SLOTS; slot++)
    {
        if (c4own_flush_cache_slot(slot))
            return 1;
    }

    return 0;
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
    for (uint32_t payload = 0; payload < C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
         payload++)
    {
        unsigned char *dst = c4own_cache_data[victim] + payload * 512u;

        if (old_pblk != 0xffffu)
        {
            if (c4own_read_phys_sector(old_pblk,
                                       C4OWN_SECTORS_PER_PAGE + payload,
                                       dst))
                c4own_blank_sector(dst);
        }
        else
            c4own_blank_sector(dst);
    }

    c4own_cache_logical[victim] = logical;
    c4own_cache_age[victim] = ++c4own_cache_tick;
    c4own_cache_valid[victim] = true;
    c4own_cache_dirty[victim] = false;
    c4own_diag_cache_logical = (int)logical;
    return (int)victim;
}

static int c4own_read_sector_internal(uint32_t sector, void *buffer)
{
    uint32_t logical = sector / C4OWN_PAYLOAD_SECTORS_PER_BLOCK;
    uint32_t offset = sector % C4OWN_PAYLOAD_SECTORS_PER_BLOCK;

    if (sector >= C4OWN_VOLUME_SECTORS)
        return 1;

    int slot = c4own_find_cache_slot(logical);

    if (slot >= 0)
    {
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
            memcpy(out, c4own_cache_data[slot] + offset * 512u, n * 512u);
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
    uint32_t first_sector = sector;
    uint32_t first_count = count;
    unsigned char verify[512];

    c4own_diag_write_count++;
    c4own_diag_last_write_sector = (int)sector;
    c4own_diag_last_write_count = (int)count;
    c4own_diag_last_write_result = -1;
    c4own_diag_last_writeback_sig = -1;

    if (sector > C4OWN_VOLUME_SECTORS ||
        count > C4OWN_VOLUME_SECTORS - sector)
    {
        c4own_diag_last_write_result = 1;
        return 1;
    }

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
            {
                c4own_diag_last_write_result = 2;
                return 1;
            }
        }

        memcpy(c4own_cache_data[slot] + offset * 512u, in, n * 512u);
        c4own_cache_dirty[slot] = true;
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
    }
#endif

    if (first_count > 0 &&
        c4own_read_sector_internal(first_sector, verify) == 0)
        c4own_diag_last_writeback_sig = (int)c4own_get32(verify);

    c4own_diag_last_write_result = 0;
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

    c4own_diag_format_count++;
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

#ifdef C4PRO_INTC_PROBE
/* Set at ftl_init entry so the INTC probe (apps/main.c) can show whether boot
 * reached storage init (ftl=5A) or not (ftl=00). */
volatile unsigned long c4pro_intc_ftl_mark;
#endif

uint32_t ftl_init(void)
{
#ifdef C4PRO_INTC_PROBE
    c4pro_intc_ftl_mark = 0x5au;
#endif
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
#ifdef COLORFLY_C4_PRO
    /* Build the libbch decoder tables once before any owned-volume read; reads
     * software-BCH-correct each sector from its spare on the linear layout. */
    c4pro_bch_init();
#if defined(C4PRO_NAND_DMA) && defined(C4PRO_NAND_DMA_SELFTEST)
    /* DMA-read bring-up gate (DRAM RAM-run friendly): prove the buffered
     * PAGE_BUF engine reader returns byte-for-byte identical data to the proven
     * PIO reader BEFORE the live FTL read path is trusted. Runs read-only on the
     * live volume and HALTS, so the live c4own_read_page_corrected DMA path is
     * never reached here.
     *
     * It can't rely on finding pre-written data in the linear (4096+128) layout
     * (the payload is BootROM-formatted, owned blocks are mostly 0xFF after a
     * format), so it MANUFACTURES the data: erase a scratch block well outside
     * the firmware image (loader pblk 0, payload <1536, owned 1536..~1672) and
     * program all 128 pages with a varied per-page pattern via the proven PIO
     * writer, then compare PIO-read vs engine-read on every page. Done on die 0
     * AND die 1 so the engine read is proven on real, varied data on two dies.
     * c4pro_nand_dma_selftest_page() returns 0xffffffff on match, a byte offset
     * (0..4095 data, 4096..4223 spare) on first mismatch, or the
     * 0xfffffffe/0xfffffffd PIO/engine read-fail sentinels. */
    {
#ifndef C4PRO_NAND_DMA_SELFTEST_PBLK
#define C4PRO_NAND_DMA_SELFTEST_PBLK 8000u  /* scratch block, mid die, unused */
#endif
        static unsigned char dma_st_data[4096];
        static unsigned char dma_st_spare[128];
        static unsigned char pioA[4096], pioA_sp[128];
        static unsigned char pioB[4096], pioB_sp[128];
        const uint32_t PBASE = C4PRO_NAND_DMA_SELFTEST_PBLK *
                               C4OWN_PAGES_PER_BLOCK;
        uint32_t res[2] = { 0xffffffffu, 0xffffffffu };       /* first-bad ret */
        uint32_t bad_pg[2] = { 0xffffffffu, 0xffffffffu };
        int dma_pass[2] = { 0, 0 };   /* engine read == PIO read (raw, no ECC) */
        int ctrl_pass[2] = { 0, 0 };  /* PIO read == PIO read (cell-noise ctrl) */
        int wr_fail[2] = { 0, 0 };
        int bank, p, i;

        for (bank = 0; bank < 2; bank++)
        {
            if (c4pro_nand_erase_block((uint32_t)bank, PBASE))
            {
                wr_fail[bank] = -1;          /* erase failed */
                continue;
            }
            for (p = 0; p < (int)C4OWN_PAGES_PER_BLOCK; p++)
            {
                for (i = 0; i < 4096; i++)
                    dma_st_data[i] = (unsigned char)(p * 7 + i * 13 + 0x5a);
                for (i = 0; i < 128; i++)
                    dma_st_spare[i] = (unsigned char)(p * 3 + i * 11 + 0x1d);
                if (c4pro_nand_write_page((uint32_t)bank,
                        PBASE + (uint32_t)p, dma_st_data, dma_st_spare))
                {
                    wr_fail[bank]++;
                    continue;
                }
                {
                    uint32_t r = c4pro_nand_dma_selftest_page((uint32_t)bank,
                            PBASE + (uint32_t)p);
                    if (r == 0xffffffffu)
                        dma_pass[bank]++;
                    else if (bad_pg[bank] == 0xffffffffu)
                    {
                        bad_pg[bank] = PBASE + (uint32_t)p;
                        res[bank] = r;
                    }
                }
                /* control: two independent PIO reads of the SAME page. If these
                 * disagree, the raw cells are noisy (no ECC applied here) and an
                 * engine-vs-PIO miss is cell instability, not an engine fault. */
                if (!c4pro_nand_read_linear_page_spare((uint32_t)bank,
                        PBASE + (uint32_t)p, pioA, pioA_sp) &&
                    !c4pro_nand_read_linear_page_spare((uint32_t)bank,
                        PBASE + (uint32_t)p, pioB, pioB_sp) &&
                    !memcmp(pioA, pioB, 4096) && !memcmp(pioA_sp, pioB_sp, 128))
                    ctrl_pass[bank]++;
            }
        }

        lcd_clear_display();
        lcd_puts(0, 0, "NAND DMA SELFTEST");
        for (bank = 0; bank < 2; bank++)
        {
            int row = 1 + bank * 2;
            lcd_putsf(0, row, "d%d dma%d pio%d wf%d", bank,
                      dma_pass[bank], ctrl_pass[bank], wr_fail[bank]);
            if (bad_pg[bank] == 0xffffffffu)
                lcd_putsf(0, row + 1, "d%d OK (all match)", bank);
            else
                lcd_putsf(0, row + 1, "d%d 1stbad pg%lu r%08lx", bank,
                          (unsigned long)bad_pg[bank],
                          (unsigned long)res[bank]);
        }
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();   /* keep the result screen lit past the timeout */
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_WDELAY_SWEEP
    /* WRITE-delay sweep (DRAM RAM-run): find the fastest program/erase settle
     * (c4pro_nand_prog_delay_iters) that still writes CORRECTLY. Per candidate:
     * erase at a safe delay, write the scratch block at the candidate delay,
     * then read back at a SAFE delay and compare. bad = pages that mis-wrote.
     * wr = write throughput at that delay. (Caveat: this proves write DATA
     * integrity vs delay; it cannot reproduce the USB/many-small-files pattern.)
     * If bad=0 down to 50, writes are correct at 50 and the small-files drop is
     * USB/pattern, not the delay. */
    {
        static unsigned char wbuf[4096], wsp[128], rbuf[4096], rsp[128];
        const uint32_t PBASE = 8000u * C4OWN_PAGES_PER_BLOCK;
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;
        const unsigned long bytes = (unsigned long)NP * 4096u;
        static const uint32_t cand[] = {
            10000, 2000, 1000, 500, 200, 100, 50, 20, 10, 2
        };
        const int NC = (int)(sizeof(cand) / sizeof(cand[0]));
        unsigned long kbs[16];
        int bad[16];
        int ci, p, i;

        lcd_clear_display();
        lcd_puts(0, 0, "WRITE-DELAY SWEEP");
        lcd_puts(0, 1, "...");
        lcd_update();

        c4pro_nand_fmwait = 0x1081;
        c4pro_nand_delay_iters = 10000;   /* safe reads for verify */

        for (ci = 0; ci < NC; ci++)
        {
            long t0, dt;
            int b = 0;

            lcd_putsf(0, 1, "test %lu ...     ", (unsigned long)cand[ci]);
            lcd_update();

            c4pro_nand_prog_delay_iters = 10000;   /* safe erase */
            c4pro_nand_erase_block(0, PBASE);

            c4pro_nand_prog_delay_iters = cand[ci];   /* write at candidate */
            t0 = current_tick;
            for (p = 0; p < NP; p++)
            {
                for (i = 0; i < 4096; i++)
                    wbuf[i] = (unsigned char)(p * 7 + i * 13 + 0x5a);
                for (i = 0; i < 128; i++)
                    wsp[i] = (unsigned char)(p * 3 + i * 11 + 0x1d);
                if (c4pro_nand_write_page(0, PBASE + (uint32_t)p, wbuf, wsp))
                    b++;
            }
            dt = current_tick - t0;
            kbs[ci] = dt > 0 ? (bytes * (unsigned long)HZ) /
                      ((unsigned long)dt * 1024u) : 0;

            for (p = 0; p < NP; p++)   /* read back at safe delay, verify data */
            {
                if (c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p,
                        rbuf, rsp))
                { b++; continue; }
                for (i = 0; i < 4096; i++)
                    if (rbuf[i] != (unsigned char)(p * 7 + i * 13 + 0x5a))
                    { b++; break; }
            }
            bad[ci] = b;
        }
        c4pro_nand_prog_delay_iters = 10000;

        lcd_clear_display();
        lcd_puts(0, 0, "WDELAY  wkb/s  bad");
        for (ci = 0; ci < NC; ci++)
            lcd_putsf(0, 1 + ci, "%5lu  %lu  b%d",
                      (unsigned long)cand[ci], kbs[ci], bad[ci]);
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_HWECC_ERASED
    /* Erased-page hwecc read probe (DRAM RAM-run): the flashed hwecc build hung
     * under real host format/copy; suspect the hwecc read (0xA24) of ERASED /
     * non-hwecc pages that the FTL touches during mount-scan and copy-on-write.
     * Read an all-erased block via hwecc and see if it hangs, faults, or returns
     * cleanly -- and capture BCHST for erased vs written so the uncorrectable
     * bit is known (that's how the fix will detect erased pages). The "reading
     * erased..." line is painted BEFORE the loop so a hang is visible. */
    {
        static unsigned char rbuf[4096], wbuf[4096];
        const uint32_t PBASE = 8000u * C4OWN_PAGES_PER_BLOCK;
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;
        uint32_t bchst_erased = 0xffffffffu, bchst_written = 0xffffffffu;
        int er_ok = 0, er_err = 0, er_nonff = 0, p, i;
        uint32_t rc;

        lcd_clear_display();
        lcd_puts(0, 0, "ERASED HWECC");
        lcd_puts(0, 1, "erase...");
        lcd_update();

        c4pro_nand_delay_iters = 50;
        c4pro_nand_erase_block(0, PBASE);

        lcd_puts(0, 1, "read erased pg1..");
        lcd_update();
        /* single erased page first (pages 1..NP-1 stay erased; page 0 written) */
        rc = c4pro_nand_read_page_hwecc(0, PBASE + 1u, rbuf, 8);
        bchst_erased = c4pro_nand_last_bchst;

        lcd_puts(0, 1, "write+read pg0.. ");
        lcd_update();
        for (i = 0; i < 4096; i++)
            wbuf[i] = (unsigned char)(i * 13 + 0x5a);
        c4pro_nand_write_page_hwecc(0, PBASE, wbuf, 8);
        c4pro_nand_read_page_hwecc(0, PBASE, rbuf, 8);
        bchst_written = c4pro_nand_last_bchst;

        lcd_puts(0, 1, "sweep erased...  ");
        lcd_update();
        for (p = 1; p < NP; p++)   /* all still-erased pages */
        {
            if (c4pro_nand_read_page_hwecc(0, PBASE + (uint32_t)p, rbuf, 8))
                er_err++;
            else
            {
                er_ok++;
                for (i = 0; i < 4096; i++)
                    if (rbuf[i] != 0xff) { er_nonff++; break; }
            }
        }

        lcd_clear_display();
        lcd_puts(0, 0, "ERASED HWECC");
        lcd_putsf(0, 1, "rc1=%lu", (unsigned long)rc);
        lcd_putsf(0, 2, "bchstE %08lx", (unsigned long)bchst_erased);
        lcd_putsf(0, 3, "bchstW %08lx", (unsigned long)bchst_written);
        lcd_putsf(0, 4, "erOK%d er%d", er_ok, er_err);
        lcd_putsf(0, 5, "nonFF%d", er_nonff);
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_HWECC_MULTIDIE
    /* Multi-die hardware-ECC round-trip (DRAM RAM-run): the hwecc primitives put
     * the die index into BCHCTL bits 10-11 as ((bank&3)<<10) and select the die
     * via nand_select(bank) (FMCTL low nibble = 1<<bank). This was only ever
     * proven on die 0 (bits=0). Before scaling the owned volume across all four
     * dies we MUST prove that hardware BCH generate (write) and correct (read)
     * work with the die bits set to 1/2/3 -- and that a write/read to die N does
     * not silently alias onto die 0.
     *
     * Method: write a *die-distinguishing* pattern to one scratch block on EACH
     * die (the pattern byte folds in the bank index), reading nothing yet; THEN
     * read every die back and verify byte-exact against that die's own pattern.
     * Writing all dies before reading any means a stuck die-select (all accesses
     * landing on die 0) shows up as a mismatch on dies 1-3, because die 0's block
     * would hold whichever pattern was written last, not die N's. Per die we
     * report: write failures (wf), read/compare mismatches (mism), and the OR of
     * the hardware BCH status word (st) so an uncorrectable/large-correction flag
     * is visible even when the bytes happen to match. PASS requires wf==0 &&
     * mism==0 on every detected die.
     *
     * Scratch block is high in each die (SB) and destructive; run this on a
     * throwaway/pre-format device, exactly like the die-0 HWECC harness. */
    {
        static unsigned char wbuf[4096];
        static unsigned char rbuf[4096];
        const uint32_t SB = 16000u;                 /* in-die scratch block */
        const uint32_t PBASE = SB * C4OWN_PAGES_PER_BLOCK;
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;
        const uint32_t NDIE = (ftl_banks < 4u) ? ftl_banks : 4u;
        int wf[4] = {0, 0, 0, 0};
        int mism[4] = {0, 0, 0, 0};
        int badpage[4] = {-1, -1, -1, -1};
        uint32_t st[4] = {0, 0, 0, 0};
        uint32_t d;
        int p, i;

        lcd_clear_display();
        lcd_puts(0, 0, "HWECC MULTIDIE");
        lcd_puts(0, 1, "write all dies...");
        lcd_update();

        c4pro_nand_fmwait = 0x1081;
        c4pro_nand_delay_iters = 50;

        /* Phase 1: erase + hwecc-write a bank-tagged pattern on every die. */
        for (d = 0; d < NDIE; d++)
        {
            c4pro_nand_erase_block(d, PBASE);
            for (p = 0; p < NP; p++)
            {
                for (i = 0; i < 4096; i++)
                    wbuf[i] = (unsigned char)(d * 0x51u + p * 7 + i * 13 + 0x5a);
                if (c4pro_nand_write_page_hwecc(d, PBASE + (uint32_t)p, wbuf, 8))
                    wf[d]++;
            }
        }

        lcd_puts(0, 1, "read+verify...   ");
        lcd_update();

        /* Phase 2: hwecc-read every die back, verify against ITS OWN pattern. */
        for (d = 0; d < NDIE; d++)
        {
            for (p = 0; p < NP; p++)
            {
                if (c4pro_nand_read_page_hwecc(d, PBASE + (uint32_t)p, rbuf, 8))
                {
                    mism[d]++;
                    if (badpage[d] < 0) badpage[d] = p;
                    st[d] |= c4pro_nand_last_bchst;
                    continue;
                }
                st[d] |= c4pro_nand_last_bchst;
                for (i = 0; i < 4096; i++)
                    if (rbuf[i] !=
                        (unsigned char)(d * 0x51u + p * 7 + i * 13 + 0x5a))
                    {
                        mism[d]++;
                        if (badpage[d] < 0) badpage[d] = p;
                        break;
                    }
            }
        }

        lcd_clear_display();
        lcd_putsf(0, 0, "HWECC MDIE nb%lu", (unsigned long)NDIE);
        for (d = 0; d < 4u; d++)
            lcd_putsf(0, 1 + (int)d, "d%lu w%d m%d s%08lx",
                      (unsigned long)d, wf[d], mism[d], (unsigned long)st[d]);
        {
            int all_ok = 1;
            for (d = 0; d < NDIE; d++)
                if (wf[d] || mism[d]) all_ok = 0;
            lcd_putsf(0, 5, all_ok ? "MULTIDIE OK" : "MULTIDIE FAIL");
        }
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_HWECC_TEST
    /* Hardware-ECC round-trip (DRAM RAM-run): prove the controller BCH-8 write
     * (c4pro_nand_write_page_hwecc) + read (nand_read_page, 0xA24) is byte-exact
     * with ZERO software BCH -- the primitive for the layout rewrite. Writes a
     * scratch block with hw ECC, reads back hardware-corrected, verifies, and
     * times the read (should be ~6.7 MB/s, unlike the software-BCH 1.16). */
    {
        static unsigned char wbuf[4096];
        static unsigned char rbuf[4096], rsp[128];
        const uint32_t PBASE = 8000u * C4OWN_PAGES_PER_BLOCK;
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;
        const int PASSES = 6;
        const unsigned long bytes = (unsigned long)PASSES * NP * 4096u;
        int wr_fail = 0, mism = 0, badpage = -1, p, pass, i;
        int page0_raw_off = -1, page0_ecc_off = -1, page0_spare0 = -1;
        long t0, dt;
        unsigned long rd_kbs = 0;

        lcd_clear_display();
        lcd_puts(0, 0, "HW-ECC ROUNDTRIP");
        lcd_puts(0, 1, "erase+write...");
        lcd_update();

        c4pro_nand_fmwait = 0x1081;
        c4pro_nand_delay_iters = 50;
        c4pro_nand_erase_block(0, PBASE);
        for (p = 0; p < NP; p++)
        {
            for (i = 0; i < 4096; i++)
                wbuf[i] = (unsigned char)(p * 7 + i * 13 + 0x5a);
            if (c4pro_nand_write_page_hwecc(0, PBASE + (uint32_t)p, wbuf, 8))
                wr_fail++;
        }

        lcd_puts(0, 1, "read+verify...  ");
        lcd_update();

        /* page-0 layout probe: raw (no-ECC) read tells us where the DATA bytes
         * landed (offset 512 => inline 528/sector; 0/match => linear/garbage);
         * ECC read (nand_read_page) tells us if hardware correction reproduces
         * the pattern. rawsp[0..15] = first sector's on-NAND spare (parity?). */
        {
            static unsigned char rawbuf[4096], rawsp[128];
            page0_raw_off = -1;
            page0_ecc_off = -1;
            page0_spare0 = -1;
            if (c4pro_nand_read_linear_page_spare(0, PBASE, rawbuf, rawsp))
                page0_raw_off = -2;
            else
            {
                page0_spare0 = rawsp[0];
                for (i = 0; i < 4096; i++)
                    if (rawbuf[i] != (unsigned char)(i * 13 + 0x5a))
                    { page0_raw_off = i; break; }
            }
            if (c4pro_nand_read_page_hwecc(0, PBASE, rbuf, 8))
                page0_ecc_off = -2;
            else
                for (i = 0; i < 4096; i++)
                    if (rbuf[i] != (unsigned char)(i * 13 + 0x5a))
                    { page0_ecc_off = i; break; }
        }

        for (p = 0; p < NP; p++)
        {
            if (c4pro_nand_read_page_hwecc(0, PBASE + (uint32_t)p, rbuf, 8))
            { mism++; if (badpage < 0) badpage = p; continue; }
            for (i = 0; i < 4096; i++)
                if (rbuf[i] != (unsigned char)(p * 7 + i * 13 + 0x5a))
                { mism++; if (badpage < 0) badpage = p; break; }
        }

        t0 = current_tick;
        for (pass = 0; pass < PASSES; pass++)
            for (p = 0; p < NP; p++)
                c4pro_nand_read_page_hwecc(0, PBASE + (uint32_t)p, rbuf, 8);
        dt = current_tick - t0;
        rd_kbs = dt > 0 ? (bytes * (unsigned long)HZ) /
                 ((unsigned long)dt * 1024u) : 0;
        c4pro_nand_delay_iters = 50;

        lcd_clear_display();
        lcd_puts(0, 0, "HW-ECC ROUNDTRIP");
        lcd_putsf(0, 1, "wf%d mism%d", wr_fail, mism);
        lcd_putsf(0, 2, "raw1stdiff %d", page0_raw_off);
        lcd_putsf(0, 3, "ecc1stdiff %d", page0_ecc_off);
        lcd_putsf(0, 4, "spare0 %d rd%lu", page0_spare0, rd_kbs);
        lcd_putsf(0, 5, (mism == 0 && wr_fail == 0) ? "HW-ECC OK" : "HW-ECC FAIL");
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_DELAY_SWEEP
    /* nand_delay sweep (DRAM RAM-run): the clock probe showed dropping the
     * post-command busy-wait from 10000 to 50 iters took reads 1462->5206 KB/s.
     * Find the fastest value that still reads CORRECTLY (bad=0). Scratch is
     * written at the safe 10000; only the READ delay is swept. bad = pages that
     * errored or mis-compared. Pick the lowest-bad0 delay for the read path. */
    {
        static unsigned char wbuf[4096], wsp[128], rbuf[4096], rsp[128];
        const uint32_t PBASE = 8000u * C4OWN_PAGES_PER_BLOCK;
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;
        const int PASSES = 6;
        const unsigned long bytes = (unsigned long)PASSES * NP * 4096u;
        static const uint32_t cand[] = {
            10000, 1000, 200, 100, 50, 20, 10, 5, 2, 0
        };
        const int NC = (int)(sizeof(cand) / sizeof(cand[0]));
        unsigned long kbs[16];
        int bad[16];
        int ci, p, pass, i;

        lcd_clear_display();
        lcd_puts(0, 0, "DELAY SWEEP");
        lcd_puts(0, 1, "prep...");
        lcd_update();

        c4pro_nand_fmwait = 0x1081;
        c4pro_nand_delay_iters = 10000;
        c4pro_nand_erase_block(0, PBASE);
        for (p = 0; p < NP; p++)
        {
            for (i = 0; i < 4096; i++)
                wbuf[i] = (unsigned char)(p * 7 + i * 13 + 0x5a);
            for (i = 0; i < 128; i++)
                wsp[i] = (unsigned char)(p * 3 + i * 11 + 0x1d);
            c4pro_nand_write_page(0, PBASE + (uint32_t)p, wbuf, wsp);
        }

        for (ci = 0; ci < NC; ci++)
        {
            long t0, dt;
            int b = 0;

            lcd_putsf(0, 1, "test %lu ...     ", (unsigned long)cand[ci]);
            lcd_update();
            c4pro_nand_delay_iters = cand[ci];

            t0 = current_tick;
            for (pass = 0; pass < PASSES; pass++)
                for (p = 0; p < NP; p++)
                    c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p,
                                                      rbuf, rsp);
            dt = current_tick - t0;
            kbs[ci] = dt > 0 ? (bytes * (unsigned long)HZ) /
                      ((unsigned long)dt * 1024u) : 0;

            for (p = 0; p < NP; p++)
            {
                if (c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p,
                        rbuf, rsp))
                { b++; continue; }
                for (i = 0; i < 4096; i++)
                    if (rbuf[i] != (unsigned char)(p * 7 + i * 13 + 0x5a))
                    { b++; break; }
            }
            bad[ci] = b;
        }
        c4pro_nand_delay_iters = 10000;

        lcd_clear_display();
        lcd_puts(0, 0, "DELAY  kb/s  bad");
        for (ci = 0; ci < NC; ci++)
            lcd_putsf(0, 1 + ci, "%5lu  %lu  b%d",
                      (unsigned long)cand[ci], kbs[ci], bad[ci]);
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_CLKPROBE
    /* Decisive clock probe (DRAM RAM-run): is the ~1.4 MB/s read floor measured
     * at stock's 100 MHz AHB HCLK (PLL up) or a slow pre-PLL clock? Print the
     * live PLL/divider/clkcfg, and measure read speed at the full nand_delay vs
     * a tiny one to size the nand_delay overhead. At CPUFREQ_MAX stock sets
     * DIVCON1 bits[4:3]=01 (=0x08..0x18 in low byte) and PLLCON1=0x01a80531-ish;
     * if our DIVCON1 low byte shows that, we're already at stock's clock and the
     * gap is structural, not clock. */
    {
        static unsigned char wbuf[4096], wsp[128], rbuf[4096], rsp[128];
        const uint32_t PBASE = 8000u * C4OWN_PAGES_PER_BLOCK;
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;
        const int PASSES = 6;
        const unsigned long bytes = (unsigned long)PASSES * NP * 4096u;
        uint32_t clk[4];
        long t0, dt;
        unsigned long kbs_slow = 0, kbs_fast = 0;
        int p, pass, i;

        c4pro_nand_clock_regs(clk);

        lcd_clear_display();
        lcd_puts(0, 0, "CLK PROBE");
        lcd_puts(0, 1, "prep...");
        lcd_update();

        c4pro_nand_fmwait = 0x1081;
        c4pro_nand_delay_iters = 10000;
        c4pro_nand_erase_block(0, PBASE);
        for (p = 0; p < NP; p++)
        {
            for (i = 0; i < 4096; i++)
                wbuf[i] = (unsigned char)(p * 7 + i * 13 + 0x5a);
            for (i = 0; i < 128; i++)
                wsp[i] = (unsigned char)(p * 3 + i * 11 + 0x1d);
            c4pro_nand_write_page(0, PBASE + (uint32_t)p, wbuf, wsp);
        }

        c4pro_nand_delay_iters = 10000;
        t0 = current_tick;
        for (pass = 0; pass < PASSES; pass++)
            for (p = 0; p < NP; p++)
                c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p,
                                                  rbuf, rsp);
        dt = current_tick - t0;
        kbs_slow = dt > 0 ? (bytes * (unsigned long)HZ) /
                   ((unsigned long)dt * 1024u) : 0;

        c4pro_nand_delay_iters = 50;
        t0 = current_tick;
        for (pass = 0; pass < PASSES; pass++)
            for (p = 0; p < NP; p++)
                c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p,
                                                  rbuf, rsp);
        dt = current_tick - t0;
        kbs_fast = dt > 0 ? (bytes * (unsigned long)HZ) /
                   ((unsigned long)dt * 1024u) : 0;
        c4pro_nand_delay_iters = 10000;

        lcd_clear_display();
        lcd_puts(0, 0, "CLK PROBE");
        lcd_putsf(0, 1, "PLL %08lx", (unsigned long)clk[0]);
        lcd_putsf(0, 2, "DIV %08lx", (unsigned long)clk[1]);
        lcd_putsf(0, 3, "CLK %08lx", (unsigned long)clk[2]);
        lcd_putsf(0, 4, "rd d10k %lu", kbs_slow);
        lcd_putsf(0, 5, "rd d50  %lu", kbs_fast);
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_FMWAIT_SWEEP
    /* Empirical NAND timing sweep (DRAM RAM-run): all read methods cluster at
     * ~1.4 MB/s while stock does ~6.7, so the bottleneck is the shared FMWAIT
     * wait-state timing, not the transfer method. Write a scratch block at the
     * safe 0x1081, then read it back via PIO at progressively tighter FMWAIT
     * values, recording throughput AND correctness (bad = pages that errored or
     * mis-compared). The fastest value with bad=0 is the new safe timing. */
    {
        static unsigned char wbuf[4096], wsp[128];
        static unsigned char rbuf[4096], rsp[128];
        const uint32_t PBASE = 8000u * C4OWN_PAGES_PER_BLOCK;
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;
        const int PASSES = 6;
        const unsigned long bytes = (unsigned long)PASSES * NP * 4096u;
        static const uint32_t cand[] = {
            0x1081, 0x1041, 0x1021, 0x1011, 0x0821, 0x0421,
            0x0221, 0x0121, 0x0061, 0x0031, 0x0021, 0x0011
        };
        const int NC = (int)(sizeof(cand) / sizeof(cand[0]));
        unsigned long kbs[16];
        int bad[16];
        int ci, p, pass, i;

        lcd_clear_display();
        lcd_puts(0, 0, "FMWAIT SWEEP");
        lcd_puts(0, 1, "prep...");
        lcd_update();

        c4pro_nand_fmwait = 0x1081;
        c4pro_nand_erase_block(0, PBASE);
        for (p = 0; p < NP; p++)
        {
            for (i = 0; i < 4096; i++)
                wbuf[i] = (unsigned char)(p * 7 + i * 13 + 0x5a);
            for (i = 0; i < 128; i++)
                wsp[i] = (unsigned char)(p * 3 + i * 11 + 0x1d);
            c4pro_nand_write_page(0, PBASE + (uint32_t)p, wbuf, wsp);
        }

        for (ci = 0; ci < NC; ci++)
        {
            long t0, dt;
            int b = 0;

            lcd_putsf(0, 1, "test %04lx ...   ", (unsigned long)cand[ci]);
            lcd_update();
            c4pro_nand_fmwait = cand[ci];

            t0 = current_tick;
            for (pass = 0; pass < PASSES; pass++)
                for (p = 0; p < NP; p++)
                    c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p,
                                                      rbuf, rsp);
            dt = current_tick - t0;
            kbs[ci] = dt > 0 ? (bytes * (unsigned long)HZ) /
                      ((unsigned long)dt * 1024u) : 0;

            for (p = 0; p < NP; p++)
            {
                if (c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p,
                        rbuf, rsp))
                { b++; continue; }
                for (i = 0; i < 4096; i++)
                    if (rbuf[i] != (unsigned char)(p * 7 + i * 13 + 0x5a))
                    { b++; break; }
            }
            bad[ci] = b;
        }
        c4pro_nand_fmwait = 0x1081;

        lcd_clear_display();
        lcd_puts(0, 0, "FMWAIT  kb/s  bad");
        for (ci = 0; ci < NC; ci++)
            lcd_putsf(0, 1 + ci, "%04lx  %lu  b%d",
                      (unsigned long)cand[ci], kbs[ci], bad[ci]);
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_NAND_DMA_RWTEST
    /* Focused, fast end-to-end gate with live progress (read backend = whatever
     * the FTL is built with: PIO, DMA engine, or hardware ECC) (so a long run
     * can never be confused with a white-screen hang). Writes a known pattern
     * through the real FTL write path (software-BCH-encoded), syncs, REMOUNTS to
     * drop the page cache and force fresh NAND reads, then reads every sector
     * back through c4own_read_sector_internal -- which under C4PRO_NAND_DMA is
     * the buffered-engine reader + software BCH correction. This proves the
     * engine read is correct in the way that matters (BCH absorbs the raw cell
     * noise the raw selftest exposed). Reports written/read/mismatch counts,
     * first bad sector, and the FTL uncorrectable counter. Halts. */
    {
        static unsigned char bw[512];
        static unsigned char br[512];
        const uint32_t PN = 1024u;          /* pattern = sectors 0..PN-1 (512KB) */
        int fmt_rc, mism = 0, rd_err = 0, badsec = -1, i;
        uint32_t s;

        lcd_clear_display();
        lcd_puts(0, 0, "DMA RW TEST");
        lcd_puts(0, 1, "format + write...");
        lcd_update();

        fmt_rc = (int)c4own_format();
        c4own_mount_scan();

        for (s = 0; s < PN; s++)
        {
            for (i = 0; i < 512; i++)
                bw[i] = (unsigned char)(s * 7u + (unsigned)i * 13u + 0x5au);
            c4own_write_internal(s, 1u, bw);
        }
        ftl_sync();

        lcd_puts(0, 1, "remount + read...   ");
        lcd_update();

        c4own_mount_scan();                 /* drop cache -> reads hit NAND */
        c4own_diag_uncorr = 0;

        for (s = 0; s < PN; s++)
        {
            if (c4own_read_sector_internal(s, br))
            {
                rd_err++;
                if (badsec < 0) badsec = (int)s;
                continue;
            }
            for (i = 0; i < 512; i++)
                bw[i] = (unsigned char)(s * 7u + (unsigned)i * 13u + 0x5au);
            if (memcmp(br, bw, 512))
            {
                mism++;
                if (badsec < 0) badsec = (int)s;
            }
        }

        lcd_clear_display();
        lcd_puts(0, 0, "DMA RW TEST");
        lcd_putsf(0, 1, "fmt=%d wrote=%lu", fmt_rc, (unsigned long)PN);
        lcd_putsf(0, 2, "mism=%d rderr=%d", mism, rd_err);
        lcd_putsf(0, 3, "badsec=%d", badsec);
        lcd_putsf(0, 4, "uncorr=%d", c4own_diag_uncorr);
        lcd_putsf(0, 5, (mism == 0 && rd_err == 0) ? "DMA READ OK" : "DMA READ FAIL");
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#if defined(C4PRO_NAND_DMA) && defined(C4PRO_NAND_DMA_SPEEDTEST)
    /* Throughput comparison (DRAM RAM-run): time reading the same data three
     * ways so the buffered-engine win is quantified, not assumed.
     *  PIO  = c4pro_nand_read_linear_page_spare  (byte-serial FLASH_DATA loop)
     *  ENG  = c4pro_nand_read_linear_page_spare_dma (PAGE_BUF burst + word copy)
     *  FTL  = c4own_read_sector_internal (ENG + per-sector software BCH decode)
     * The FTL number is what the user actually gets; BCH decode is CPU-heavy and
     * may itself cap throughput, so it is measured separately from raw transfer.
     * Reads are uncached (raw funcs hit NAND every call). Halts. */
    {
        static unsigned char sd[4096], ss[128];
        const uint32_t PBASE = 8000u * C4OWN_PAGES_PER_BLOCK; /* scratch block */
        const int NP = (int)C4OWN_PAGES_PER_BLOCK;            /* 128 pages */
        const int PASSES = 8;
        unsigned long bytes = (unsigned long)PASSES * NP * 4096u;
        long t0, dt_pio, dt_eng, dt_nat, dt_ftl;
        unsigned long pio_kbs, eng_kbs, nat_kbs, ftl_kbs;
        int pass, p;
        uint32_t s;

        lcd_clear_display();
        lcd_puts(0, 0, "NAND SPEED");
        lcd_puts(0, 1, "prep...");
        lcd_update();

        /* scratch data for the raw (non-FTL) reads */
        c4pro_nand_erase_block(0, PBASE);
        for (p = 0; p < NP; p++)
        {
            for (s = 0; s < 4096; s++)
                sd[s] = (unsigned char)(p * 7u + s * 13u + 0x5au);
            for (s = 0; s < 128; s++)
                ss[s] = (unsigned char)(p * 3u + s * 11u + 0x1du);
            c4pro_nand_write_page(0, PBASE + (uint32_t)p, sd, ss);
        }

        lcd_puts(0, 1, "PIO read...     ");
        lcd_update();
        t0 = current_tick;
        for (pass = 0; pass < PASSES; pass++)
            for (p = 0; p < NP; p++)
                c4pro_nand_read_linear_page_spare(0, PBASE + (uint32_t)p, sd, ss);
        dt_pio = current_tick - t0;

        lcd_puts(0, 1, "ENG read...     ");
        lcd_update();
        t0 = current_tick;
        for (pass = 0; pass < PASSES; pass++)
            for (p = 0; p < NP; p++)
                c4pro_nand_read_linear_page_spare_dma(0, PBASE + (uint32_t)p,
                                                      sd, ss);
        dt_eng = current_tick - t0;

        /* native-streaming ceiling (wrong data, timing only): what the engine
         * would do on a stock 528-byte inline layout, no per-sector reposition */
        lcd_puts(0, 1, "NAT read...     ");
        lcd_update();
        t0 = current_tick;
        for (pass = 0; pass < PASSES; pass++)
            for (p = 0; p < NP; p++)
                c4pro_nand_read_native_stream_dma(0, PBASE + (uint32_t)p, sd);
        dt_nat = current_tick - t0;

        /* FTL end-to-end (engine + BCH): write a region, remount, time reads */
        lcd_puts(0, 1, "FTL prep...     ");
        lcd_update();
        c4own_format();
        c4own_mount_scan();
        for (s = 0; s < (uint32_t)NP * 8u; s++)   /* NP pages worth of sectors */
        {
            for (p = 0; p < 512; p++)
                sd[p] = (unsigned char)(s * 7u + (unsigned)p * 13u + 0x5au);
            c4own_write_internal(s, 1u, sd);
        }
        ftl_sync();
        c4own_mount_scan();
        lcd_puts(0, 1, "FTL read...     ");
        lcd_update();
        t0 = current_tick;
        for (pass = 0; pass < PASSES; pass++)
            for (s = 0; s < (uint32_t)NP * 8u; s++)
                c4own_read_sector_internal(s, sd);
        dt_ftl = current_tick - t0;

        pio_kbs = dt_pio > 0 ? (bytes * (unsigned long)HZ) /
                  ((unsigned long)dt_pio * 1024u) : 0;
        eng_kbs = dt_eng > 0 ? (bytes * (unsigned long)HZ) /
                  ((unsigned long)dt_eng * 1024u) : 0;
        nat_kbs = dt_nat > 0 ? (bytes * (unsigned long)HZ) /
                  ((unsigned long)dt_nat * 1024u) : 0;
        ftl_kbs = dt_ftl > 0 ? (bytes * (unsigned long)HZ) /
                  ((unsigned long)dt_ftl * 1024u) : 0;

        lcd_clear_display();
        lcd_puts(0, 0, "NAND SPEED (KB/s)");
        lcd_putsf(0, 1, "PIO %lu", pio_kbs);
        lcd_putsf(0, 2, "ENG(crc) %lu", eng_kbs);
        lcd_putsf(0, 3, "NAT(528) %lu", nat_kbs);
        lcd_putsf(0, 4, "FTL %lu", ftl_kbs);
        lcd_putsf(0, 5, "NAT x%lu.%02lu vs PIO",
                  pio_kbs ? nat_kbs / pio_kbs : 0,
                  pio_kbs ? (nat_kbs * 100u / pio_kbs) % 100u : 0);
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_FTL_ECC_SELFTEST
    /* Multi-cycle persistence test: single write->sync->remount passes
     * (mism=0), but real use shows data degrading across MULTIPLE cold boots
     * (boot1 md5 ok -> boot3 md5 wrong). This reproduces that in isolation:
     * write a "file" (sectors 0..PN), then loop NCYC times { write a DIFFERENT
     * region (like player/FAT churn) -> ftl_sync (flush+checkpoint) -> remount
     * via c4own_mount_scan -> re-verify the file is byte-exact }. If the file
     * corrupts after some cycle, the FTL checkpoint/COW/generation logic drifts
     * across cycles -> that's the bug. failcyc = first cycle the file broke
     * (-1 = survived all NCYC). Halts. */
    {
        static unsigned char bw[512];
        static unsigned char br[512];
        const uint32_t PN = 3000u;             /* "file" = sectors 0..PN-1 */
        const uint32_t QBASE = 5000u, QN = 500u; /* churn region */
        const int NCYC = 30;
        int fmt_rc, failcyc = -1, badsec = -1, cyc, i;
        uint32_t s;

        fmt_rc = (int)c4own_format();
        c4own_mount_scan();

        for (s = 0; s < PN; s++)
        {
            for (i = 0; i < 512; i++)
                bw[i] = (unsigned char)(s * 7u + (unsigned)i * 13u + 0x5au);
            c4own_write_internal(s, 1u, bw);
        }
        ftl_sync();

        for (cyc = 0; cyc < NCYC && failcyc < 0; cyc++)
        {
            for (s = QBASE; s < QBASE + QN; s++)
            {
                for (i = 0; i < 512; i++)
                    bw[i] = (unsigned char)(s * 3u + (unsigned)i * 11u
                                            + (unsigned)cyc);
                c4own_write_internal(s, 1u, bw);
            }
            ftl_sync();
            c4own_mount_scan();   /* remount via checkpoint */

            for (s = 0; s < PN; s++)
            {
                for (i = 0; i < 512; i++)
                    bw[i] = (unsigned char)(s * 7u + (unsigned)i * 13u + 0x5au);
                if (c4own_read_sector_internal(s, br) ||
                    memcmp(br, bw, 512))
                {
                    failcyc = cyc;
                    badsec = (int)s;
                    break;
                }
            }
        }

        lcd_clear_display();
        lcd_puts(0, 0, "MULTI-CYCLE");
        lcd_putsf(0, 1, "fmt=%d ncyc=%d", fmt_rc, NCYC);
        lcd_putsf(0, 2, "failcyc=%d", failcyc);
        lcd_putsf(0, 3, "badsec=%d", badsec);
        lcd_putsf(0, 4, "uncorr=%d", c4own_diag_uncorr);
        lcd_update();
        while (1) {
#ifdef HAVE_BACKLIGHT
            backlight_on();   /* keep the result screen lit past the timeout */
#endif
            sleep(HZ);
        }
    }
#endif
#ifdef C4PRO_FTL_ROCKBOX_OWNED_RW
    if (c4own_mount_scan())
    {
        if (c4own_format() || c4own_mount_scan())
            return 1;
    }
    c4own_ready = true;
    {
        unsigned char diag_sector[512];

        c4own_diag_map0 = c4own_map[0] == 0xffffu ? -1 : (int)c4own_map[0];
        c4own_diag_map2 = c4own_map[2] == 0xffffu ? -1 : (int)c4own_map[2];
        if (c4own_read_sector_internal(0, diag_sector) == 0)
            c4own_diag_lba0_sig = (int)c4own_get32(diag_sector);
        if (c4own_read_sector_internal(2048u, diag_sector) == 0)
            c4own_diag_lba2048_sig = (int)c4own_get32(diag_sector);
    }
#else
#ifdef C4PRO_FTL_HASH_ORACLE
    c4pro_ftl_build_hash_map();
#endif
#endif
#endif
    return 0;
}

uint32_t ftl_total_sectors(void)
{
#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)
    return C4OWN_VOLUME_SECTORS;
#else
    return C4PRO_STORAGE_SECTORS;
#endif
}

uint32_t ftl_read(uint32_t sector, uint32_t count, void* buffer)
{
#if defined(COLORFLY_C4_PRO) && defined(C4PRO_FTL_ROCKBOX_OWNED_RW)
    unsigned char *out = buffer;
    uint32_t first_sector = sector;

    if (!c4own_ready ||
        sector > C4OWN_VOLUME_SECTORS ||
        count > C4OWN_VOLUME_SECTORS - sector)
        return 1;

    c4own_diag_read_count++;
    c4own_diag_last_read_sector = (int)sector;
    c4own_diag_map0 = c4own_map[0] == 0xffffu ? -1 : (int)c4own_map[0];
    c4own_diag_map2 = c4own_map[2] == 0xffffu ? -1 : (int)c4own_map[2];

    if (c4own_read_internal(sector, count, out))
        return 1;

    if (first_sector == 0)
        c4own_diag_lba0_sig = (int)c4own_get32(out);
    if (first_sector <= 2048u && first_sector + count > 2048u)
        c4own_diag_lba2048_sig =
            (int)c4own_get32(out + (2048u - first_sector) * 512u);

    return 0;
#elif defined(COLORFLY_C4_PRO)
    unsigned char *out = buffer;

    if (sector > C4PRO_SYSTEM_SECTORS ||
        count > C4PRO_SYSTEM_SECTORS - sector)
        return 1;

    while (count > 0)
    {
#ifdef C4PRO_FTL_DIAG_EXPORT
        if (sector == C4PRO_FTL_DIAG_EXPORT_LBA)
        {
            c4pro_ftl_read_diag_export(out);
            out += 512;
            sector++;
            count--;
            continue;
        }
#endif
        uint32_t psec = C4PRO_FTL_LBA_TO_PSEC(sector);
        uint32_t page;
        uint32_t sector_in_page;
        unsigned char mode = C4PRO_FTL_MODE_RAW;
#ifdef C4PRO_FTL_RAM_WRITE_TEST
        int ram_offset;

        ram_offset = c4pro_ftl_ram_write_lookup(sector);
        if (ram_offset >= 0)
        {
            memcpy(out, c4pro_ftl_ram_write_data[ram_offset], 512);
            out += 512;
            sector++;
            count--;
            continue;
        }
#endif
#ifdef C4PRO_FTL_PROC_WAV_TEST
        if (c4pro_ftl_proc_wav_lookup(sector))
        {
            c4pro_ftl_proc_wav_sector(sector, out);
            out += 512;
            sector++;
            count--;
            continue;
        }
#endif
#ifdef C4PRO_FTL_SYNTH_SECTOR_COUNT
        int synth_offset;

        synth_offset = c4pro_ftl_synth_lookup(sector);
        if (synth_offset >= 0
#if defined(C4PRO_FTL_SYNTH_COMPARE_ONLY) && \
    defined(C4PRO_FTL_AUDIT_START_LBA) && defined(C4PRO_FTL_AUDIT_SECTORS)
            && (sector < C4PRO_FTL_AUDIT_START_LBA ||
                sector >= C4PRO_FTL_AUDIT_START_LBA + C4PRO_FTL_AUDIT_SECTORS)
#endif
        )
        {
            memcpy(out, c4pro_ftl_synth_data + synth_offset, 512);
            out += 512;
            sector++;
            count--;
            continue;
        }
#endif

        if (psec == C4PRO_FTL_PSEC_INVALID)
            return 1;

        page = psec / C4PRO_SECTORS_PER_PAGE;
        sector_in_page = psec & (C4PRO_SECTORS_PER_PAGE - 1);

        if (c4pro_nand_read_sector(0, page, sector_in_page,
                                   page_data, page_spare))
            return 1;

#ifdef C4PRO_FTL_HASH_ORACLE
        mode = c4pro_ftl_lba_mode(sector);
#ifdef C4PRO_FTL_HASH_HAS_RK27_KEYS
        if (mode == C4PRO_FTL_MODE_SECDEC)
        {
            for (int i = 0; i < 512; i++)
                out[i] = page_data[i] ^ c4pro_rk27_sector_key[i];
        }
        else if (mode == C4PRO_FTL_MODE_PAGEDEC)
        {
            const unsigned char *key =
                c4pro_rk27_page_key + sector_in_page * 512;
            for (int i = 0; i < 512; i++)
                out[i] = page_data[i] ^ key[i];
        }
        else
#endif
#endif
        {
            (void)mode;
            memcpy(out, page_data, 512);
        }
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
    const unsigned char *in = buffer;

    if (sector > C4PRO_SYSTEM_SECTORS ||
        count > C4PRO_SYSTEM_SECTORS - sector)
        return (uint32_t)-1;

    while (count > 0)
    {
        int slot = c4pro_ftl_ram_write_slot(sector);
        if (slot < 0)
            return (uint32_t)-1;

        memcpy(c4pro_ftl_ram_write_data[slot], in, 512);
        in += 512;
        sector++;
        count--;
    }

    return 0;
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
