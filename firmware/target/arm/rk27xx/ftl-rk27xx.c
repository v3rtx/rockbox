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
#define C4OWN_VERSION 3u
#define C4OWN_SECTORS_PER_PAGE 8u
#define C4OWN_PAGES_PER_BLOCK 128u
#define C4OWN_SECTORS_PER_BLOCK 1024u
#define C4OWN_PAYLOAD_SECTORS_PER_BLOCK \
    ((C4OWN_PAGES_PER_BLOCK - 2u) * C4OWN_SECTORS_PER_PAGE)
#define C4OWN_VOLUME_SECTORS 131072u
#define C4OWN_LOGICAL_BLOCKS \
    ((C4OWN_VOLUME_SECTORS + C4OWN_PAYLOAD_SECTORS_PER_BLOCK - 1u) / \
     C4OWN_PAYLOAD_SECTORS_PER_BLOCK)
#define C4OWN_FIRST_DATA_PBLK 2u

#ifndef C4PRO_OWNED_PBLK_START
#define C4PRO_OWNED_PBLK_START 288u
#endif

#ifndef C4PRO_OWNED_PBLK_COUNT
#define C4PRO_OWNED_PBLK_COUNT 160u
#endif

#if C4OWN_LOGICAL_BLOCKS + C4OWN_FIRST_DATA_PBLK + 2u > C4PRO_OWNED_PBLK_COUNT
#error "C4PRO_OWNED_PBLK_COUNT is too small for the 64 MiB owned FTL"
#endif

static uint16_t c4own_map[C4OWN_LOGICAL_BLOCKS];
static uint32_t c4own_gen[C4OWN_LOGICAL_BLOCKS];
static unsigned char c4own_used[C4PRO_OWNED_PBLK_COUNT];
static unsigned char c4own_retired[C4PRO_OWNED_PBLK_COUNT];
static unsigned char c4own_page_data[4096];
static unsigned char c4own_page_spare[128];
static unsigned char c4own_read_page_data[4096];
static uint32_t c4own_read_page;
static bool c4own_read_page_valid;
#ifndef C4OWN_CACHE_SLOTS
#define C4OWN_CACHE_SLOTS 4u
#endif

static unsigned char c4own_cache_data[C4OWN_CACHE_SLOTS][C4OWN_PAYLOAD_SECTORS_PER_BLOCK * 512u];
static uint32_t c4own_cache_logical[C4OWN_CACHE_SLOTS];
static uint32_t c4own_cache_age[C4OWN_CACHE_SLOTS];
static uint32_t c4own_cache_tick;
static bool c4own_cache_valid[C4OWN_CACHE_SLOTS];
static bool c4own_cache_dirty[C4OWN_CACHE_SLOTS];
static bool c4own_ready;
static bool c4own_formatting;
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

static uint32_t c4own_crc32(const void *buf, uint32_t len)
{
    const unsigned char *p = buf;
    uint32_t crc = 0xffffffffu;

    while (len--)
    {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
    }

    return crc ^ 0xffffffffu;
}

static uint32_t c4own_pblk_page(uint32_t pblk)
{
    return (C4PRO_OWNED_PBLK_START + pblk) * C4OWN_PAGES_PER_BLOCK;
}

static bool c4own_pblk_valid(uint32_t pblk)
{
    return pblk < C4PRO_OWNED_PBLK_COUNT;
}

static int c4own_read_phys_sector(uint32_t pblk, uint32_t sector, void *buf)
{
    uint32_t page;

    if (!c4own_pblk_valid(pblk) || sector >= C4OWN_SECTORS_PER_BLOCK)
        return 1;

    page = c4own_pblk_page(pblk) + sector / C4OWN_SECTORS_PER_PAGE;
    if (!c4own_read_page_valid || c4own_read_page != page)
    {
        if (c4pro_nand_read_linear_page(0, page, c4own_read_page_data))
            return 1;

        c4own_read_page = page;
        c4own_read_page_valid = true;
    }

    memcpy(buf, c4own_read_page_data +
                (sector & (C4OWN_SECTORS_PER_PAGE - 1u)) * 512u,
           512);
    return 0;
}

static int c4own_write_page(uint32_t pblk, uint32_t page_in_block)
{
    if (!c4own_pblk_valid(pblk) || page_in_block >= C4OWN_PAGES_PER_BLOCK)
        return 1;

    c4own_read_page_valid = false;
    memset(c4own_page_spare, 0xff, sizeof(c4own_page_spare));
    return c4pro_nand_write_page(0, c4own_pblk_page(pblk) + page_in_block,
                                 c4own_page_data, c4own_page_spare);
}

static int c4own_erase_pblk(uint32_t pblk)
{
    if (!c4own_pblk_valid(pblk))
        return 1;

    c4own_read_page_valid = false;
    return c4pro_nand_erase_block(0, c4own_pblk_page(pblk));
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

    if (c4own_read_phys_sector(pblk, 0, sector))
        return false;

    return c4own_get32(sector) == C4OWN_MAGIC_SB &&
           c4own_get32(sector + 4) == C4OWN_VERSION &&
           c4own_get32(sector + 8) == C4OWN_VOLUME_SECTORS &&
           c4own_get32(sector + 12) == C4PRO_OWNED_PBLK_START &&
           c4own_get32(sector + 16) == C4PRO_OWNED_PBLK_COUNT &&
           c4own_get32(sector + 20) == C4OWN_PAYLOAD_SECTORS_PER_BLOCK &&
           c4own_get32(sector + 28) == c4own_crc32(sector, 28);
}

static int c4own_write_superblock(uint32_t pblk)
{
    if (c4own_erase_pblk(pblk))
        return 1;

    memset(c4own_page_data, 0xff, sizeof(c4own_page_data));
    c4own_put32(c4own_page_data + 0, C4OWN_MAGIC_SB);
    c4own_put32(c4own_page_data + 4, C4OWN_VERSION);
    c4own_put32(c4own_page_data + 8, C4OWN_VOLUME_SECTORS);
    c4own_put32(c4own_page_data + 12, C4PRO_OWNED_PBLK_START);
    c4own_put32(c4own_page_data + 16, C4PRO_OWNED_PBLK_COUNT);
    c4own_put32(c4own_page_data + 20, C4OWN_PAYLOAD_SECTORS_PER_BLOCK);
    c4own_put32(c4own_page_data + 24, C4OWN_LOGICAL_BLOCKS);
    c4own_put32(c4own_page_data + 28, c4own_crc32(c4own_page_data, 28));
    return c4own_write_page(pblk, 0);
}

static int c4own_mount_scan(void)
{
    unsigned char header[512];
    unsigned char commit[512];

    c4own_reset_maps();

    if (!c4own_superblock_valid(0) && !c4own_superblock_valid(1))
        return 1;

    for (uint32_t pblk = C4OWN_FIRST_DATA_PBLK;
         pblk < C4PRO_OWNED_PBLK_COUNT; pblk++)
    {
        uint32_t logical;
        uint32_t gen;

        if (c4own_read_phys_sector(pblk, 0, header) ||
            !c4own_parse_header(header, &logical, &gen))
            continue;

        if (c4own_read_phys_sector(pblk, C4OWN_SECTORS_PER_BLOCK - 1u,
                                   commit) ||
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

    return 0;
}

static int c4own_alloc_pblk(void)
{
    for (uint32_t pblk = C4OWN_FIRST_DATA_PBLK;
         pblk < C4PRO_OWNED_PBLK_COUNT; pblk++)
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

    if (slot >= 0 && c4own_cache_dirty[slot])
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

static int c4own_write_logical_block(uint32_t logical, uint32_t first,
                                     uint32_t count,
                                     const unsigned char *data)
{
    int new_pblk = c4own_alloc_pblk();
    uint16_t old_pblk;
    uint32_t gen;

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
    s[450] = 0x06;
    c4own_put32(s + 454, 2048u);
    c4own_put32(s + 458, C4OWN_VOLUME_SECTORS - 2048u);
    s[510] = 0x55;
    s[511] = 0xaa;
}

static void c4own_make_boot(unsigned char *s)
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
    c4own_put32(s + 28, 2048u);
    c4own_put32(s + 32, C4OWN_VOLUME_SECTORS - 2048u);
    s[36] = 0x80;
    s[38] = 0x29;
    c4own_put32(s + 39, 0xc4000001u);
    memcpy(s + 43, "C4 ROCKBOX ", 11);
    memcpy(s + 54, "FAT16   ", 8);
    s[510] = 0x55;
    s[511] = 0xaa;
}

static void c4own_make_fat0(unsigned char *s)
{
    c4own_blank_sector(s);
    s[0] = 0xf8;
    s[1] = 0xff;
    s[2] = 0xff;
    s[3] = 0xff;
}

static int c4own_format(void)
{
    unsigned char sector[512];

    c4own_diag_format_count++;
    c4own_formatting = true;
    c4own_reset_maps();

    for (uint32_t pblk = 0; pblk < C4PRO_OWNED_PBLK_COUNT; pblk++)
    {
        if (c4own_erase_pblk(pblk))
            c4own_retired[pblk] = 1;
    }

    if (c4own_write_superblock(0) || c4own_write_superblock(1))
        return 1;

    c4own_make_mbr(sector);
    if (c4own_write_internal(0, 1, sector))
        return 1;

    c4own_make_boot(sector);
    if (c4own_write_internal(2048u, 1, sector))
        return 1;

    c4own_make_fat0(sector);
    if (c4own_write_internal(2049u, 1, sector) ||
        c4own_write_internal(2177u, 1, sector))
        return 1;

    if (c4own_flush_cache())
        return 1;

    c4own_formatting = false;
    return 0;
}

#endif

uint32_t ftl_init(void)
{
    if (nand_device_init())
        return 1;

    ftl_nand_type = nand_get_device_type(0);
    if (!ftl_nand_type)
        return 1;

    ftl_banks = 1;
#ifdef COLORFLY_C4_PRO
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

    while (count--)
    {
        if (c4own_read_sector_internal(sector++, out))
            return 1;
        if (first_sector == 0)
            c4own_diag_lba0_sig = (int)c4own_get32(out);
        if (first_sector == 2048u)
            c4own_diag_lba2048_sig = (int)c4own_get32(out);
        out += 512;
        first_sector++;
    }

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

    return c4own_write_internal(sector, count, buffer) ?
           (uint32_t)-1 : 0;
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
    return c4own_flush_cache() ? (uint32_t)-1 : 0;
#else
    return 0;
#endif
}
