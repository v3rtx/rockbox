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

static unsigned char page_data[4096];
static unsigned char page_spare[128];
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

#define C4PRO_FTL_MODE_RAW 0
#define C4PRO_FTL_MODE_SECDEC 1
#define C4PRO_FTL_MODE_PAGEDEC 2

#ifdef C4PRO_FTL_HASH_ORACLE
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

#ifdef C4PRO_FTL_HASH_ORACLE
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
    const unsigned char *data = page_data + sector_in_page * 512;

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

static uint32_t c4pro_ftl_lba_to_psec(uint32_t lba)
{
    uint32_t psec = c4pro_ftl_runtime_map[lba];
    int inferred_psec;

    if (psec != C4PRO_FTL_PSEC_INVALID)
        return psec;

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
    if (c4pro_ftl_runtime_map[lba] != C4PRO_FTL_PSEC_INVALID)
        return c4pro_ftl_runtime_mode[lba];

    return C4PRO_FTL_MODE_RAW;
}

static void c4pro_ftl_build_hash_map(void)
{
    uint32_t start = C4PRO_FTL_HASH_SCAN_START;
    uint32_t end = start + C4PRO_FTL_HASH_SCAN_SECTORS;
    uint32_t cached_page = 0xffffffff;

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

    for (uint32_t psec = start; psec < end; psec++)
    {
        uint32_t page = psec / C4PRO_SECTORS_PER_PAGE;
        uint32_t sector_in_page = psec & (C4PRO_SECTORS_PER_PAGE - 1);
        int lba;

        if (page != cached_page)
        {
            if (nand_read_page(0, page, page_data, page_spare, 1, 0))
                continue;
            cached_page = page;
        }

        lba = c4pro_ftl_hash_lookup(
            c4pro_ftl_hash_sector(page_data + sector_in_page * 512));
        c4pro_ftl_record_hash_match(psec, lba, C4PRO_FTL_MODE_RAW);
        c4pro_ftl_record_signature_match(psec, sector_in_page,
                                         C4PRO_FTL_MODE_RAW);

#ifdef C4PRO_FTL_HASH_HAS_RK27_KEYS
        lba = c4pro_ftl_hash_lookup(
            c4pro_ftl_hash_sector_xor(page_data + sector_in_page * 512,
                                      c4pro_rk27_sector_key));
        c4pro_ftl_record_hash_match(psec, lba, C4PRO_FTL_MODE_SECDEC);
        c4pro_ftl_record_signature_match(psec, sector_in_page,
                                         C4PRO_FTL_MODE_SECDEC);

        lba = c4pro_ftl_hash_lookup(
            c4pro_ftl_hash_sector_xor(
                page_data + sector_in_page * 512,
                c4pro_rk27_page_key + sector_in_page * 512));
        c4pro_ftl_record_hash_match(psec, lba, C4PRO_FTL_MODE_PAGEDEC);
        c4pro_ftl_record_signature_match(psec, sector_in_page,
                                         C4PRO_FTL_MODE_PAGEDEC);
#endif
    }
}

#undef C4PRO_FTL_LBA_TO_PSEC
#define C4PRO_FTL_LBA_TO_PSEC(lba) c4pro_ftl_lba_to_psec(lba)
#endif
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
#ifdef C4PRO_FTL_HASH_ORACLE
    c4pro_ftl_build_hash_map();
#endif
#endif
    return 0;
}

uint32_t ftl_total_sectors(void)
{
    return C4PRO_STORAGE_SECTORS;
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
        uint32_t psec = C4PRO_FTL_LBA_TO_PSEC(sector);
        uint32_t page;
        uint32_t sector_in_page;
        unsigned char mode = C4PRO_FTL_MODE_RAW;
#ifdef C4PRO_FTL_SYNTH_SECTOR_COUNT
        int synth_offset;

        synth_offset = c4pro_ftl_synth_lookup(sector);
        if (synth_offset >= 0)
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

        if (page != cached_page)
        {
            if (nand_read_page(0, page, page_data, page_spare, 1, 0))
                return 1;

            cached_page = page;
        }

#ifdef C4PRO_FTL_HASH_ORACLE
        mode = c4pro_ftl_lba_mode(sector);
#ifdef C4PRO_FTL_HASH_HAS_RK27_KEYS
        if (mode == C4PRO_FTL_MODE_SECDEC)
        {
            for (int i = 0; i < 512; i++)
                out[i] = page_data[sector_in_page * 512 + i] ^
                         c4pro_rk27_sector_key[i];
        }
        else if (mode == C4PRO_FTL_MODE_PAGEDEC)
        {
            const unsigned char *key =
                c4pro_rk27_page_key + sector_in_page * 512;
            for (int i = 0; i < 512; i++)
                out[i] = page_data[sector_in_page * 512 + i] ^ key[i];
        }
        else
#endif
#endif
        {
            (void)mode;
            memcpy(out, page_data + sector_in_page * 512, 512);
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
    (void)sector;
    (void)count;
    (void)buffer;
    return 1;
}

uint32_t ftl_sync(void)
{
    return 0;
}
