/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2005 by Björn Stenberg
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
#include <stdlib.h>
#include <file.h>
#include <dir.h>
#include <string.h>
#include <kernel.h>
#include <lcd.h>
#include <debug.h>
#include <font.h>
#include <limits.h>
#include "bookmark.h"
#include "tree.h"
#include "core_alloc.h"
#include "settings.h"
#include "filetypes.h"
#include "talk.h"
#include "playlist.h"
#include "lang.h"
#include "language.h"
#include "screens.h"
#include "plugin.h"
#include "rolo.h"
#include "splash.h"
#include "cuesheet.h"
#include "filetree.h"
#include "misc.h"
#include "strnatcmp.h"
#include "keyboard.h"
#include "button.h"
#if defined(COLORFLY_C4_PRO) && \
    (defined(C4PRO_FILETREE_PROBES) || defined(C4PRO_DIR_PROBE))
#include "nand-target.h"
#include "storage.h"
#endif
#if defined(SIMULATOR) && defined(COLORFLY_C4_PRO) && \
    (defined(C4PRO_FILETREE_PROBES) || defined(C4PRO_DIR_PROBE))
#include <stdint.h>
/* C4-specific NAND access isn't available in hosted simulator builds.
 * Provide a stub for filetree compilation; raw NAND probing is intentionally
 * unavailable in this environment.
 */
uint32_t c4pro_nand_read_sector(uint32_t bank, uint32_t page,
                                uint32_t sector, void* databuffer,
                                void* sparebuffer)
{
    (void)bank;
    (void)page;
    (void)sector;
    (void)databuffer;
    (void)sparebuffer;
    return 1;
}
#endif

#ifdef HAVE_MULTIVOLUME
#include "mv.h"
#endif

#if CONFIG_TUNER
#include "radio.h"
#endif
#include "wps.h"

static struct compare_data
{
    int sort_dir; /* qsort key for sorting directories */
    int sort_file; /*       ...for sorting files       */
    int(*_compar)(const char*, const char*, size_t);
} cmp_data;

/* dummmy functions to allow compatibility with strncmp & strncasecmp */
static int strnatcmp_n(const char *a, const char *b, size_t n)
{
    (void)n;
     return strnatcmp(a, b);
}
static int strnatcasecmp_n(const char *a, const char *b, size_t n)
{
    (void)n;
     return strnatcasecmp(a, b);
}

#if defined(COLORFLY_C4_PRO) && \
    (defined(C4PRO_FILETREE_PROBES) || defined(C4PRO_DIR_PROBE))
#define C4PRO_SYSTEM_PSEC_BASE 20480
#define C4PRO_SECTORS_PER_PAGE 8
#define C4PRO_ROOT_LBA 85
#define C4PRO_ROOT_EXPECTED_COUNT 7
#define C4PRO_PHYS_SCAN_PAGES 8192
#define C4PRO_DIR_MIN_SCORE 16
#define C4PRO_BAD_PSEC (-1)

struct c4pro_phys_scan
{
    int root_psec;
    int boot_psec;
    int fat1_psec;
    int fat1_dec_psec;
    int root_dec_psec;
    int fat1_page_dec_psec;
    int root_page_dec_psec;
    int dir_psec;
    int dir_score;
    unsigned dir_sig;
    int dir_attr;
    unsigned lba1_word;
    unsigned lba43_word;
    unsigned lba85_sig;
    int lba85_attr;
    unsigned boot_meta;
    unsigned fat1_meta;
    unsigned lba1_meta;
    unsigned lba43_meta;
    unsigned expect_root_meta;
    unsigned dir_meta;
    unsigned page0_word[C4PRO_SECTORS_PER_PAGE];
    unsigned page0_dec_word[C4PRO_SECTORS_PER_PAGE];
    unsigned page0_meta[C4PRO_SECTORS_PER_PAGE];
};

#ifdef C4PRO_DIR_PROBE
struct c4pro_mapping_probe
{
    const char *label;
    uint32_t base;
    uint32_t spp;
    bool page_base;
};
#endif

#if !defined(SIMULATOR) || defined(C4PRO_DIR_PROBE)
static bool c4pro_read_mapped_sector(uint32_t base, uint32_t spp,
                                    bool page_base, uint32_t lba,
                                    unsigned char *out)
{
    uint32_t psec = (page_base ? base * spp : base) + lba;
    if (!spp)
        return false;

    uint32_t page = psec / spp;
    uint32_t sec = psec % spp;

    return c4pro_nand_read_sector(0, page, sec, out, NULL) == 0;
}
#endif

#if !defined(SIMULATOR)
static const char c4pro_root_expected_sfn[C4PRO_ROOT_EXPECTED_COUNT][11] =
{
    "APPDATA    ",
    "RESOURCE   ",
    "RK27BOOTBIN",
    "SYSTEM     ",
    "SYSTEM00   ",
    "VERSION BIN",
    "RK28IMG CHK",
};

static const char * const c4pro_root_expected_name[C4PRO_ROOT_EXPECTED_COUNT] =
{
    "APPDATA",
    "RESOURCE",
    "RK27BOOT.BIN",
    "SYSTEM",
    "SYSTEM00",
    "VERSION.BIN",
    "RK28IMG.CHK",
};

static const uint8_t c4pro_root_expected_attr[C4PRO_ROOT_EXPECTED_COUNT] =
{
    ATTR_DIRECTORY,
    ATTR_DIRECTORY,
    ATTR_ARCHIVE,
    ATTR_DIRECTORY,
    ATTR_DIRECTORY,
    ATTR_ARCHIVE,
    ATTR_ARCHIVE,
};
#endif

#ifdef C4PRO_DIR_PROBE
struct c4pro_boot_info
{
    bool sig;
    bool jump_ok;
    bool media_ok;
    uint32_t bps;
    uint8_t media;
};

static bool c4pro_read_boot_sector_info(uint32_t base, uint32_t spp,
                                       bool page_base,
                                       struct c4pro_boot_info *out)
{
    unsigned char sector[SECTOR_SIZE];
    if (!out || !c4pro_read_mapped_sector(base, spp, page_base, 0, sector))
        return false;

    out->sig = sector[510] == 0x55 && sector[511] == 0xaa;
    out->jump_ok = (sector[0] == 0xEB) || (sector[0] == 0xE9);
    out->bps = sector[11] | ((uint32_t)sector[12] << 8);
    out->media = sector[21];
    out->media_ok = (out->media == 0xF0) || (out->media == 0xF8) ||
                    (out->media == 0xF9);
    return true;
}

static bool c4pro_is_short_name(const unsigned char *p)
{
    for (int i = 0; i < 11; i++)
    {
        unsigned char ch = p[i];
        if (ch == 0x00 || ch == 0x05 || ch == 0x20)
            return false;
        if (ch >= 0x7f || ch < 0x20)
            return false;
    }

    return true;
}

static bool c4pro_ascii_sfn(const unsigned char *p)
{
    for (int i = 0; i < 11; i++)
    {
        if (p[i] == 0x00 || p[i] == 0x20)
            return false;
        if (p[i] < 0x20 || p[i] > 0x7e)
            return false;
    }

    return true;
}
#endif

#if !defined(SIMULATOR) || defined(C4PRO_DIR_PROBE)
#define C4PRO_PROBE_WAIT_MASK (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | \
                               BUTTON_RIGHT | BUTTON_SELECT | BUTTON_NEXT | \
                               BUTTON_PREV | BUTTON_MENU | BUTTON_BACK | \
                               BUTTON_SRC | BUTTON_EQ)

static void c4pro_wait_probe_keypress(void)
{
    while (true)
    {
        long btn = button_get_w_tmo(TIMEOUT_BLOCK);
        btn &= ~BUTTON_REPEAT;

        if (!btn || (btn & BUTTON_REL) ||
            ((btn & C4PRO_PROBE_WAIT_MASK) == 0))
            continue;

        break;
    }
}
#endif

#if !defined(SIMULATOR)
static int c4pro_check_root_sector(const unsigned char *sector)
{
    for (int i = 0; i < C4PRO_ROOT_EXPECTED_COUNT; i++)
    {
        const unsigned char *entry = sector + i * 32;
        if (memcmp(entry, c4pro_root_expected_sfn[i], 11) ||
            entry[11] != c4pro_root_expected_attr[i])
            return i;
    }

    return -1;
}

static bool c4pro_sector_is_boot(const unsigned char *sector)
{
    uint32_t bps = sector[11] | ((uint32_t)sector[12] << 8);
    uint16_t sig = sector[510] | ((uint16_t)sector[511] << 8);

    return sig == 0xaa55 && bps == 512 &&
           ((sector[0] == 0xeb && sector[2] == 0x90) || sector[0] == 0xe9) &&
           !memcmp(sector + 3, "MSDOS5.0", 8);
}

static bool c4pro_sector_is_fat1(const unsigned char *sector)
{
    static const unsigned char fat1_sig[16] =
    {
        0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0x04, 0x00,
        0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00,
    };

    return !memcmp(sector, fat1_sig, sizeof(fat1_sig));
}

static unsigned c4pro_pack4(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static unsigned c4pro_spare_meta(const unsigned char *spare, int sec)
{
    const unsigned char *s = spare + sec * 16;
    return ((unsigned)s[0] << 16) | ((unsigned)s[1] << 8) | (unsigned)s[2];
}

static unsigned c4pro_word16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void c4pro_descramble(const unsigned char *in, unsigned char *out,
                             int size)
{
    static const unsigned char key[16] =
    {
        0x7c, 0x4e, 0x03, 0x04, 0x55, 0x05, 0x09, 0x07,
        0x2d, 0x2c, 0x7b, 0x38, 0x17, 0x0d, 0x17, 0x11
    };
    unsigned char key1[0x100];
    unsigned char key2[0x100];
    int i3 = 0;
    int idx = 0;

    for (int i = 0; i < 0x100; i++)
    {
        key1[i] = i;
        key2[i] = key[i & 0xf];
    }

    for (int i = 0; i < 0x100; i++)
    {
        int x = key1[i];
        i3 = (key1[i] + i3 + key2[i]) & 0xff;
        key1[i] = key1[i3];
        key1[i3] = x;
    }

    for (int i = 0; i < size; i++)
    {
        int x = key1[(i + 1) & 0xff];
        idx = (x + idx) & 0xff;
        key1[(i + 1) & 0xff] = key1[idx];
        key1[idx] = x & 0xff;
        out[i] = key1[(key1[(i + 1) & 0xff] + x) & 0xff] ^ in[i];
    }
}

static void c4pro_descramble_sector(const unsigned char *in, unsigned char *out)
{
    c4pro_descramble(in, out, SECTOR_SIZE);
}

static bool c4pro_sfn_byte_ok(unsigned char ch, int pos)
{
    if (ch == 0x20)
        return pos > 0;

    if (ch >= 'A' && ch <= 'Z')
        return true;
    if (ch >= '0' && ch <= '9')
        return true;

    switch (ch)
    {
        case '$':
        case '%':
        case '\'':
        case '-':
        case '_':
        case '@':
        case '~':
        case '`':
        case '!':
        case '(':
        case ')':
        case '{':
        case '}':
        case '^':
        case '#':
        case '&':
            return true;
        default:
            return false;
    }
}

static bool c4pro_sfn_name_ok(const unsigned char *entry)
{
    int nonspace = 0;
    int alpha = 0;
    unsigned char first_nonspace = 0;
    bool all_same = true;

    for (int i = 0; i < 11; i++)
    {
        unsigned char ch = entry[i];

        if (!c4pro_sfn_byte_ok(ch, i))
            return false;

        if (ch == 0x20)
            continue;

        if (nonspace == 0)
            first_nonspace = ch;
        else if (ch != first_nonspace)
            all_same = false;

        if (ch >= 'A' && ch <= 'Z')
            alpha++;

        nonspace++;
    }

    return nonspace >= 3 && alpha > 0 && !all_same;
}

static bool c4pro_attr_type_ok(unsigned char attr)
{
    unsigned char type = attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE);

    if (attr == 0 || (attr & 0xc0) || attr == 0x0f)
        return false;

    return type == ATTR_DIRECTORY || type == ATTR_ARCHIVE ||
           type == ATTR_VOLUME_ID;
}

static int c4pro_dir_sector_score(const unsigned char *sector,
                                  unsigned *sig, int *matched_attr)
{
    int score = 0;
    bool first_match = true;

    for (int i = 0; i < 16; i++)
    {
        const unsigned char *entry = sector + i * 32;
        unsigned char first = entry[0];
        unsigned char entry_attr = entry[11];

        if (first == 0x00)
            break;
        if (first == 0xe5 || first == 0xff)
            continue;
        if (!c4pro_attr_type_ok(entry_attr))
            continue;
        if (!c4pro_sfn_name_ok(entry))
            continue;

        if (first_match)
        {
            *sig = c4pro_pack4(entry);
            *matched_attr = entry_attr;
            first_match = false;
        }

        score += 4;
        if ((entry_attr & (ATTR_DIRECTORY | ATTR_ARCHIVE)) == ATTR_DIRECTORY)
            score += 2;
        if (entry_attr == ATTR_DIRECTORY || entry_attr == ATTR_ARCHIVE)
            score += 2;
    }

    return score;
}

static void c4pro_scan_phys(struct c4pro_phys_scan *out)
{
    static unsigned char page_data[C4PRO_SECTORS_PER_PAGE * SECTOR_SIZE];
    static unsigned char page_spare[C4PRO_SECTORS_PER_PAGE * 16];

    out->root_psec = C4PRO_BAD_PSEC;
    out->boot_psec = C4PRO_BAD_PSEC;
    out->fat1_psec = C4PRO_BAD_PSEC;
    out->fat1_dec_psec = C4PRO_BAD_PSEC;
    out->root_dec_psec = C4PRO_BAD_PSEC;
    out->fat1_page_dec_psec = C4PRO_BAD_PSEC;
    out->root_page_dec_psec = C4PRO_BAD_PSEC;
    out->dir_psec = C4PRO_BAD_PSEC;
    out->dir_score = 0;
    out->dir_sig = 0;
    out->dir_attr = -1;
    out->lba1_word = 0xffff;
    out->lba43_word = 0xffff;
    out->lba85_sig = 0;
    out->lba85_attr = -1;
    out->boot_meta = 0xffffff;
    out->fat1_meta = 0xffffff;
    out->lba1_meta = 0xffffff;
    out->lba43_meta = 0xffffff;
    out->expect_root_meta = 0xffffff;
    out->dir_meta = 0xffffff;
    for (int i = 0; i < C4PRO_SECTORS_PER_PAGE; i++)
    {
        out->page0_word[i] = 0xffff;
        out->page0_dec_word[i] = 0xffff;
        out->page0_meta[i] = 0xffffff;
    }

    for (int page = 0; page < C4PRO_PHYS_SCAN_PAGES; page++)
    {
        static unsigned char page_dec[C4PRO_SECTORS_PER_PAGE * SECTOR_SIZE];

        if (nand_read_page(0, page, page_data, page_spare, 1, 0))
            continue;

        c4pro_descramble(page_data, page_dec, sizeof(page_dec));

        for (int sec = 0; sec < C4PRO_SECTORS_PER_PAGE; sec++)
        {
            unsigned char *sector = page_data + sec * SECTOR_SIZE;
            unsigned char *page_dec_sector = page_dec + sec * SECTOR_SIZE;
            int psec = page * C4PRO_SECTORS_PER_PAGE + sec;
            int dir_score;
            unsigned dir_sig = 0;
            int dir_attr = -1;
            unsigned char decoded[SECTOR_SIZE];

            c4pro_descramble_sector(sector, decoded);

            if (psec >= C4PRO_SYSTEM_PSEC_BASE &&
                psec < C4PRO_SYSTEM_PSEC_BASE + C4PRO_SECTORS_PER_PAGE)
            {
                int i = psec - C4PRO_SYSTEM_PSEC_BASE;
                out->page0_word[i] = c4pro_word16(sector);
                out->page0_dec_word[i] = c4pro_word16(decoded);
                out->page0_meta[i] = c4pro_spare_meta(page_spare, sec);
            }

            if (psec == C4PRO_SYSTEM_PSEC_BASE + 1)
            {
                out->lba1_word = c4pro_word16(sector);
                out->lba1_meta = c4pro_spare_meta(page_spare, sec);
            }
            if (psec == C4PRO_SYSTEM_PSEC_BASE + 43)
            {
                out->lba43_word = c4pro_word16(sector);
                out->lba43_meta = c4pro_spare_meta(page_spare, sec);
            }
            if (psec == C4PRO_SYSTEM_PSEC_BASE + C4PRO_ROOT_LBA)
            {
                out->lba85_sig = c4pro_pack4(sector);
                out->lba85_attr = sector[11];
                out->expect_root_meta = c4pro_spare_meta(page_spare, sec);
            }

            if (out->root_psec < 0 && c4pro_check_root_sector(sector) < 0)
                out->root_psec = psec;

            if (out->boot_psec < 0 && c4pro_sector_is_boot(sector))
            {
                out->boot_psec = psec;
                out->boot_meta = c4pro_spare_meta(page_spare, sec);
            }

            if (out->fat1_psec < 0 && c4pro_sector_is_fat1(sector))
            {
                out->fat1_psec = psec;
                out->fat1_meta = c4pro_spare_meta(page_spare, sec);
            }

            if (out->fat1_dec_psec < 0 && c4pro_sector_is_fat1(decoded))
                out->fat1_dec_psec = psec;

            if (out->root_dec_psec < 0 && c4pro_check_root_sector(decoded) < 0)
                out->root_dec_psec = psec;

            if (out->fat1_page_dec_psec < 0 &&
                c4pro_sector_is_fat1(page_dec_sector))
                out->fat1_page_dec_psec = psec;

            if (out->root_page_dec_psec < 0 &&
                c4pro_check_root_sector(page_dec_sector) < 0)
                out->root_page_dec_psec = psec;

            dir_score = c4pro_dir_sector_score(sector, &dir_sig, &dir_attr);
            if (dir_score >= C4PRO_DIR_MIN_SCORE &&
                dir_score > out->dir_score)
            {
                out->dir_psec = psec;
                out->dir_score = dir_score;
                out->dir_sig = dir_sig;
                out->dir_attr = dir_attr;
                out->dir_meta = c4pro_spare_meta(page_spare, sec);
            }
        }
    }
}

static void c4pro_show_root_selfcheck(int direct_ok, int storage_ok,
                                      int fat_ok, int count, int entry,
                                      unsigned direct_sig,
                                      unsigned storage_sig,
                                      unsigned fat_sig,
                                      int direct_attr,
                                      int storage_attr,
                                      int fat_len,
                                      const struct c4pro_phys_scan *scan)
{
    static bool shown;

    (void)direct_sig;
    (void)storage_sig;
    (void)fat_sig;
    (void)direct_attr;
    (void)storage_attr;
    (void)fat_len;

    if (shown)
        return;
    shown = true;

    lcd_setfont(FONT_UI);
    lcd_clear_display();
    lcd_putsf(0, 0, "R D%d S%d F%d N%d E%d",
              direct_ok, storage_ok, fat_ok, count, entry);
    lcd_putsf(0, 1, "pR%d b%d pB%d", scan->root_psec,
              scan->root_psec >= 0 ? scan->root_psec - C4PRO_ROOT_LBA : -1,
              scan->boot_psec);
    lcd_putsf(0, 2, "x1%04x x43%04x",
              scan->lba1_word, scan->lba43_word);
    lcd_putsf(0, 3, "x85%08x a%02x",
              scan->lba85_sig, scan->lba85_attr);
    lcd_putsf(0, 4, "mB%06x m1%06x",
              scan->boot_meta, scan->lba1_meta);
    lcd_putsf(0, 5, "m43%06x mR%06x",
              scan->lba43_meta, scan->expect_root_meta);
    lcd_putsf(0, 6, "pF%d mF%06x",
              scan->fat1_psec, scan->fat1_meta);
    lcd_putsf(0, 7, "dF%d dR%d",
              scan->fat1_dec_psec, scan->root_dec_psec);
    lcd_update();
    button_clear_queue();
    c4pro_wait_probe_keypress();

    lcd_clear_display();
    lcd_putsf(0, 0, "P0 words");
    lcd_putsf(0, 1, "%04x %04x %04x %04x",
              scan->page0_word[0], scan->page0_word[1],
              scan->page0_word[2], scan->page0_word[3]);
    lcd_putsf(0, 2, "%04x %04x %04x %04x",
              scan->page0_word[4], scan->page0_word[5],
              scan->page0_word[6], scan->page0_word[7]);
    lcd_putsf(0, 3, "P0 meta");
    lcd_putsf(0, 4, "%06x %06x",
              scan->page0_meta[0], scan->page0_meta[1]);
    lcd_putsf(0, 5, "%06x %06x",
              scan->page0_meta[2], scan->page0_meta[3]);
    lcd_putsf(0, 6, "%06x %06x",
              scan->page0_meta[4], scan->page0_meta[5]);
    lcd_putsf(0, 7, "%06x %06x",
              scan->page0_meta[6], scan->page0_meta[7]);
    lcd_update();
    button_clear_queue();
    c4pro_wait_probe_keypress();

    lcd_clear_display();
    lcd_putsf(0, 0, "Decode scan");
    lcd_putsf(0, 1, "dF%d dR%d",
              scan->fat1_dec_psec, scan->root_dec_psec);
    lcd_putsf(0, 2, "cF%d cR%d",
              scan->fat1_page_dec_psec, scan->root_page_dec_psec);
    lcd_putsf(0, 3, "gP%d q%d",
              scan->dir_psec, scan->dir_score);
    lcd_update();
    button_clear_queue();
    c4pro_wait_probe_keypress();
    lcd_setfont(FONT_UI);
}
#endif

#ifdef C4PRO_DIR_PROBE
static void c4pro_show_dir_probe(struct tree_context *c)
{
    (void)c;
    unsigned char sector[SECTOR_SIZE];
    static const struct c4pro_mapping_probe probes[] =
    {
        {"p", 20480, 8, false}, /* confirmed working logical-sector base */
    };
    int scan_lba;
    int cand_lba;
    bool bootok, fatok, rawok;
    unsigned mask;
    static bool shown;

    if (shown)
        return;
    shown = true;

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();

    for (uint32_t i = 0; i < ARRAYLEN(probes); i++)
    {
        struct c4pro_boot_info boot_info = { false };
        bool boot_info_ok;
        scan_lba = -1;
        cand_lba = -1;
        boot_info_ok = c4pro_read_boot_sector_info(probes[i].base, probes[i].spp,
                                                  probes[i].page_base, &boot_info);
        bootok = boot_info_ok &&
            boot_info.sig && boot_info.bps == 512;
        fatok = boot_info_ok &&
            boot_info.jump_ok && boot_info.media_ok;
        rawok = c4pro_read_mapped_sector(probes[i].base, probes[i].spp,
                                       probes[i].page_base, 85, sector) == 0 &&
            (sector[11] == ATTR_DIRECTORY);

        for (int lba = 0; lba < 128; lba++)
        {
            if (c4pro_read_mapped_sector(probes[i].base, probes[i].spp,
                                       probes[i].page_base, lba, sector))
                continue;

            for (int off = 0; off < SECTOR_SIZE; off += 32)
            {
                if (scan_lba < 0 &&
                    c4pro_is_short_name(sector + off) &&
                    memcmp(sector + off + 32, "           ", 11) != 0 &&
                    sector[off + 11] == ATTR_DIRECTORY)
                    scan_lba = lba;

                if (cand_lba < 0 && c4pro_ascii_sfn(sector + off) &&
                    (sector[off + 11] == ATTR_DIRECTORY ||
                     sector[off + 11] == ATTR_ARCHIVE))
                {
                    cand_lba = lba;
                }
            }
        }

        mask = (bootok ? 1 : 0) | (fatok ? 2 : 0) |
            (rawok ? 4 : 0) | ((cand_lba >= 0) ? 8 : 0);

        lcd_putsf(0, i, "D%s b%u s%u m%x j%x mm%02X b%u",
                  probes[i].label, probes[i].base, probes[i].spp,
                  mask, boot_info.media, boot_info.bps);
    }

    lcd_update();
    button_clear_queue();
    c4pro_wait_probe_keypress();
    lcd_setfont(FONT_UI);
}
#endif
#endif

int ft_build_playlist(struct tree_context* c, int start_index)
{
    int i;
    int start=start_index;
    int res;
    struct playlist_info *playlist = playlist_get_current();

    tree_lock_cache(c);
    struct entry *entries = tree_get_entries(c);
    bool exceeds_pl = false;
    if (c->filesindir > playlist->max_playlist_size)
    {
        exceeds_pl = true;
        start_index = 0;
    }
    struct playlist_insert_context pl_context;

    res = playlist_insert_context_create(playlist, &pl_context,
                                        PLAYLIST_REPLACE, false, false);
    if (res >= 0)
    {
        cpu_boost(true);
        for(i = 0;i < c->filesindir;i++)
        {
            int item = i;
            if (exceeds_pl)
                item = (i + start) % c->filesindir;
#if 0 /*only needed if displaying progress */
            /* user abort */
            if (action_userabort(TIMEOUT_NOBLOCK))
            {
                break;
            }
#endif
            if((entries[item].attr & FILE_ATTR_MASK) == FILE_ATTR_AUDIO)
            {
                res = playlist_insert_context_add(&pl_context, entries[item].name);
                if (res < 0)
                    break;
            }
            else if (!exceeds_pl)
            {
                /* Adjust the start index when se skip non-MP3 entries */
                if(i < start)
                    start_index--;
            }
        }
        cpu_boost(false);
    }

    playlist_insert_context_release(&pl_context);

    tree_unlock_cache(c);
    return start_index;
}

/* Start playback of a playlist, checking for bookmark autoload, modified
 * playlists, etc., as required. Returns false if playback wasn't started,
 * or started via bookmark autoload, true otherwise.
 *
 * Pointers to both the full pathname and the separated parts needed to
 * avoid allocating yet another path buffer on the stack (and save some
 * code; the caller typically needs to create the full pathname anyway)...
 */
bool ft_play_playlist(char* pathname, char* dirname, char* filename)
{
    if (global_settings.party_mode && audio_status())
    {
        splash(HZ, ID2P(LANG_PARTY_MODE));
        return false;
    }

    int res =  bookmark_autoload(pathname);
    if (res == BOOKMARK_CANCEL || res == BOOKMARK_DO_RESUME || !warn_on_pl_erase())
        return false;

    splash(0, ID2P(LANG_WAIT));

    if (playlist_create(dirname, filename) != -1)
    {
        if (global_settings.playlist_shuffle)
            playlist_shuffle(current_tick, -1);

        playlist_start(0, 0, 0);
        return true;
    }

    return false;
}

/* walk a directory and check all entries if a .talk file exists */
static void check_file_thumbnails(struct tree_context* c)
{
    int i;
    struct dirent *entry;
    struct entry* entries;
    DIR *dir;

    dir = opendir(c->currdir);
    if(!dir)
        return;
    /* mark all files as non talking, except the .talk ones */
    tree_lock_cache(c);
    entries = tree_get_entries(c);

    for (i=0; i < c->filesindir; i++)
    {
        if (entries[i].attr & ATTR_DIRECTORY)
            continue; /* we're not touching directories */

        if (strcasecmp(file_thumbnail_ext,
            &entries[i].name[strlen(entries[i].name)
                              - strlen(file_thumbnail_ext)]))
        {   /* no .talk file */
            entries[i].attr &= ~FILE_ATTR_THUMBNAIL; /* clear */
        }
        else
        {   /* .talk file, we later let them speak themselves */
            entries[i].attr |= FILE_ATTR_THUMBNAIL; /* set */
        }
    }

    while((entry = readdir(dir)) != 0) /* walk directory */
    {
        int ext_pos;
        struct dirinfo info = dir_get_info(dir, entry);
        ext_pos = strlen((char *)entry->d_name) - strlen(file_thumbnail_ext);
        if (ext_pos <= 0 /* too short to carry ".talk" */
            || (info.attribute & ATTR_DIRECTORY) /* no file */
            || strcasecmp((char *)&entry->d_name[ext_pos], file_thumbnail_ext))
        {   /* or doesn't end with ".talk", no candidate */
            continue;
        }

        /* terminate the (disposable) name in dir buffer,
           this truncates off the ".talk" without needing an extra buffer */
        entry->d_name[ext_pos] = '\0';

        /* search corresponding file in dir cache */
        for (i=0; i < c->filesindir; i++)
        {
            if (!strcasecmp(entries[i].name, (char *)entry->d_name))
            {   /* match */
                entries[i].attr |= FILE_ATTR_THUMBNAIL; /* set the flag */
                break; /* exit search loop, because we found it */
            }
        }
    }
    tree_unlock_cache(c);
    closedir(dir);
}

/* support function for qsort() */
static int compare(const void* p1, const void* p2)
{
    struct entry* e1 = (struct entry*)p1;
    struct entry* e2 = (struct entry*)p2;
    int criteria;

    if (cmp_data.sort_dir == SORT_AS_FILE)
    {   /* treat as two files */
        criteria = cmp_data.sort_file;
    }
    else if (e1->attr & ATTR_DIRECTORY && e2->attr & ATTR_DIRECTORY)
    {   /* two directories */
        criteria = cmp_data.sort_dir;

#ifdef HAVE_MULTIVOLUME
        if (e1->attr & ATTR_VOLUME || e2->attr & ATTR_VOLUME)
        {   /* a volume identifier is involved */
            if (e1->attr & ATTR_VOLUME && e2->attr & ATTR_VOLUME)
                criteria = SORT_ALPHA; /* two volumes: sort alphabetically */
            else /* only one is a volume: volume first */
                return (e2->attr & ATTR_VOLUME) - (e1->attr & ATTR_VOLUME);
        }
#endif

    }
    else if (!(e1->attr & ATTR_DIRECTORY) && !(e2->attr & ATTR_DIRECTORY))
    {   /* two files */
        criteria = cmp_data.sort_file;
    }
    else /* dir and file, dir goes first */
        return (e2->attr & ATTR_DIRECTORY) - (e1->attr & ATTR_DIRECTORY);

    switch(criteria)
    {
        case SORT_TYPE:
        case SORT_TYPE_REVERSED:
        {
            int t1 = e1->attr & FILE_ATTR_MASK;
            int t2 = e2->attr & FILE_ATTR_MASK;

            if (!t1) /* unknown type */
                t1 = INT_MAX; /* gets a high number, to sort after known */
            if (!t2) /* unknown type */
                t2 = INT_MAX; /* gets a high number, to sort after known */

            if (t1 != t2) /* if different */
                return (t1 - t2) * (criteria == SORT_TYPE_REVERSED ? -1 : 1);
            /* else alphabetical sorting */
            return cmp_data._compar(e1->name, e2->name, MAX_PATH);
        }

        case SORT_DATE:
        case SORT_DATE_REVERSED:
        {
            if (e1->time_write != e2->time_write)
                return (e1->time_write - e2->time_write)
                       * (criteria == SORT_DATE_REVERSED ? -1 : 1);
            /* else fall through to alphabetical sorting */
        }
        case SORT_ALPHA:
        case SORT_ALPHA_REVERSED:
        {
            return cmp_data._compar(e1->name, e2->name, MAX_PATH) *
                (criteria == SORT_ALPHA_REVERSED ? -1 : 1);
        }

    }
    return 0; /* never reached */
}

/* load and sort directory into the tree's cache. returns NULL on failure. */
int ft_load(struct tree_context* c, const char* tempdir)
{
    if (c->out_of_tree > 0) /* something else is loaded */
        return 0;

    int files_in_dir = 0;
    int name_buffer_used = 0;
    struct dirent *entry;
    bool (*callback_show_item)(char *, int, struct tree_context *) = NULL;
    DIR *dir;
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_FILETREE_PROBES)
    bool c4_root_check = false;
    bool c4_root_error = false;
    int c4_root_count = 0;
    int c4_root_bad_entry = -1;
    int c4_root_direct_ok = 1;
    int c4_root_storage_ok = 1;
    int c4_root_fat_ok = 1;
    int c4_root_error_entry = -1;
    unsigned c4_root_direct_sig = 0;
    unsigned c4_root_storage_sig = 0;
    unsigned c4_root_fat_sig = 0;
    int c4_root_direct_attr = -1;
    int c4_root_storage_attr = -1;
    int c4_root_fat_len = -1;
    struct c4pro_phys_scan c4_scan;
#endif

    if (!c->is_browsing)
        c->browse = NULL;

    if (tempdir)
        dir = opendir(tempdir);
    else
    {
        dir = opendir(c->currdir);
        callback_show_item = c->browse? c->browse->callback_show_item: NULL;
    }
    if(!dir)
        return -1; /* not a directory */

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_FILETREE_PROBES)
    c4_root_check = !tempdir && !strcmp(c->currdir, "/");
#endif

    c->dirsindir = 0;
    c->dirfull = false;

    tree_lock_cache(c);
    while ((entry = readdir(dir))) {
        int len;
        struct dirinfo info;
        struct entry* dptr = tree_get_entry_at(c, files_in_dir);
        if (!dptr)
        {
            c->dirfull = true;
            break;
        }

        info = dir_get_info(dir, entry);
        len = strlen((char *)entry->d_name);

        /* Skip FAT volume ID */
        if (info.attribute & ATTR_VOLUME_ID) {
            continue;
        }

        dptr->attr = info.attribute;
        int dir_attr = (dptr->attr & ATTR_DIRECTORY);
        /* skip directories . and .. */
        if (dir_attr && is_dotdir_name(entry->d_name))
            continue;

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_FILETREE_PROBES)
        if (c4_root_check && c4_root_count < C4PRO_ROOT_EXPECTED_COUNT)
        {
            if (c4_root_count == 0)
            {
                c4_root_fat_sig =
                    c4pro_pack4((const unsigned char *)entry->d_name);
                c4_root_fat_len = len;
            }

            if (strcmp((char *)entry->d_name,
                       c4pro_root_expected_name[c4_root_count]) &&
                c4_root_bad_entry < 0)
            {
                c4_root_bad_entry = c4_root_count;
            }

            c4_root_count++;
        }
#endif

        /* filter out dotfiles and hidden files */
        if (*c->dirfilter != SHOW_ALL &&
            ((entry->d_name[0]=='.') ||
            (info.attribute & ATTR_HIDDEN))) {
            continue;
        }

        if (*c->dirfilter == SHOW_PLUGINS && (dptr->attr & ATTR_DIRECTORY) &&
            (dptr->attr &
            (ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_VOLUME)) != 0) {
            continue; /* skip non plugin folders */
        }

        /* check for known file types */
        if ( !(dir_attr) )
            dptr->attr |= filetype_get_attr((char *)entry->d_name);

        int file_attr = (dptr->attr & FILE_ATTR_MASK);

#define CHK_FT(show,attr) (*c->dirfilter == (show) && file_attr != (attr))
        /* filter out non-visible files */
        if ((!(dir_attr) && (CHK_FT(SHOW_PLAYLIST, FILE_ATTR_M3U) ||
            (CHK_FT(SHOW_MUSIC, FILE_ATTR_AUDIO) && file_attr != FILE_ATTR_M3U) ||
            (*c->dirfilter == SHOW_SUPPORTED && !filetype_supported(dptr->attr)))) ||
            CHK_FT(SHOW_WPS,  FILE_ATTR_WPS)  ||
            CHK_FT(SHOW_FONT, FILE_ATTR_FONT) ||
            CHK_FT(SHOW_SBS,  FILE_ATTR_SBS)  ||
#if CONFIG_TUNER
            CHK_FT(SHOW_FMS, FILE_ATTR_FMS) ||
            CHK_FT(SHOW_FMR, FILE_ATTR_FMR) ||
#endif
#ifdef HAVE_REMOTE_LCD
            CHK_FT(SHOW_RWPS, FILE_ATTR_RWPS) ||
            CHK_FT(SHOW_RSBS, FILE_ATTR_RSBS) ||
#if CONFIG_TUNER
            CHK_FT(SHOW_RFMS, FILE_ATTR_RFMS) ||
#endif
#endif
            CHK_FT(SHOW_M3U, FILE_ATTR_M3U) ||
            CHK_FT(SHOW_CFG, FILE_ATTR_CFG) ||
            CHK_FT(SHOW_LNG, FILE_ATTR_LNG) ||
            CHK_FT(SHOW_MOD, FILE_ATTR_MOD) ||
           /* show first level directories */
           ((!(dir_attr) || c->dirlevel > 0)       &&
            CHK_FT(SHOW_PLUGINS, FILE_ATTR_ROCK)   &&
                       file_attr != FILE_ATTR_LUA  &&
                       file_attr != FILE_ATTR_OPX) ||
            (callback_show_item && !callback_show_item(entry->d_name, dptr->attr, c)))
        {
            continue;
        }
#undef CHK_FT

        if (len > c->cache.name_buffer_size - name_buffer_used - 1) {
            /* Tell the world that we ran out of buffer space */
            c->dirfull = true;
            break;
        }

        ++files_in_dir;

        dptr->name = core_get_data(c->cache.name_buffer_handle)+name_buffer_used;
        dptr->time_write = info.mtime;
        strcpy(dptr->name, (char *)entry->d_name);
        name_buffer_used += len + 1;

        if (dir_attr) /* count the remaining dirs */
            c->dirsindir++;
    }
    c->filesindir = files_in_dir;
    c->dirlength = files_in_dir;
    closedir(dir);

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_FILETREE_PROBES)
    if (c4_root_check)
    {
        unsigned char direct_sector[SECTOR_SIZE];
        unsigned char storage_sector[SECTOR_SIZE];
        int direct_bad = -1;
        int storage_bad = -1;

        if (!c4pro_read_mapped_sector(C4PRO_SYSTEM_PSEC_BASE,
                                      C4PRO_SECTORS_PER_PAGE, false,
                                      C4PRO_ROOT_LBA, direct_sector))
        {
            c4_root_direct_ok = 0;
        }
        else
        {
            c4_root_direct_sig = c4pro_pack4(direct_sector);
            c4_root_direct_attr = direct_sector[11];
            direct_bad = c4pro_check_root_sector(direct_sector);
            c4_root_direct_ok = direct_bad < 0;
        }

        if (storage_read_sectors(IF_MD(0,) C4PRO_ROOT_LBA, 1,
                                 storage_sector) != 0)
        {
            c4_root_storage_ok = 0;
        }
        else
        {
            c4_root_storage_sig = c4pro_pack4(storage_sector);
            c4_root_storage_attr = storage_sector[11];
            storage_bad = c4pro_check_root_sector(storage_sector);
            c4_root_storage_ok = storage_bad < 0;
        }

        c4_root_fat_ok = c4_root_bad_entry < 0 &&
                         c4_root_count >= C4PRO_ROOT_EXPECTED_COUNT;

        if (!c4_root_direct_ok)
            c4_root_error_entry = direct_bad;
        else if (!c4_root_storage_ok)
            c4_root_error_entry = storage_bad;
        else if (!c4_root_fat_ok)
            c4_root_error_entry = c4_root_bad_entry >= 0 ?
                                  c4_root_bad_entry : c4_root_count;

        c4_root_error = !c4_root_direct_ok || !c4_root_storage_ok ||
                        !c4_root_fat_ok;

        if (c4_root_error)
            c4pro_scan_phys(&c4_scan);
    }
#endif

    /* allow directories to be sorted into file list */
    cmp_data.sort_dir = (*c->dirfilter == SHOW_PLUGINS) ? SORT_AS_FILE : c->sort_dir;

    /* playlist catalog uses sorting independent from file browser */
    cmp_data.sort_file = (*c->dirfilter == SHOW_M3U) ?
                         global_settings.sort_playlists : global_settings.sort_file;

    if (global_settings.sort_case)
    {
        if (global_settings.interpret_numbers == SORT_INTERPRET_AS_NUMBER)
            cmp_data._compar = strnatcmp_n;
        else
            cmp_data._compar = strncmp;
    }
    else
    {
        if (global_settings.interpret_numbers == SORT_INTERPRET_AS_NUMBER)
            cmp_data._compar = strnatcasecmp_n;
        else
            cmp_data._compar = strncasecmp;
    }

    qsort(tree_get_entries(c), files_in_dir, sizeof(struct entry), compare);

#if defined(COLORFLY_C4_PRO) && defined(C4PRO_DIR_PROBE)
    c4pro_show_dir_probe(c);
#endif

    /* If thumbnail talking is enabled, make an extra run to mark files with
       associated thumbnails, so we don't do unsuccessful spinups later. */
    if (global_settings.talk_file_clip)
        check_file_thumbnails(c); /* map .talk to ours */

    tree_unlock_cache(c);

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_FILETREE_PROBES)
    if (c4_root_error)
        c4pro_show_root_selfcheck(c4_root_direct_ok, c4_root_storage_ok,
                                  c4_root_fat_ok, c4_root_count,
                                  c4_root_error_entry,
                                  c4_root_direct_sig,
                                  c4_root_storage_sig,
                                  c4_root_fat_sig,
                                  c4_root_direct_attr,
                                  c4_root_storage_attr,
                                  c4_root_fat_len,
                                  &c4_scan);
#endif

    if (global_settings.keep_directory && get_current_activity() == ACTIVITY_FILEBROWSER)
    {
        path_append(global_status.browse_last_folder, c->currdir, PA_SEP_HARD,
                    sizeof(global_status.browse_last_folder));
    }

    return 0;
}
static void ft_load_font(char *file)
{
    int current_font_id;
    enum screen_type screen = SCREEN_MAIN;
#if NB_SCREENS > 1
    MENUITEM_STRINGLIST(menu, ID2P(LANG_CUSTOM_FONT), NULL,
                        ID2P(LANG_MAIN_SCREEN), ID2P(LANG_REMOTE_SCREEN))
    switch (do_menu(&menu, NULL, NULL, false))
    {
        case 0: /* main lcd */
            screen = SCREEN_MAIN;
            set_file(file, (char *)global_settings.font_file);
            break;
        case 1: /* remote */
            screen = SCREEN_REMOTE;
            set_file(file, (char *)global_settings.remote_font_file);
            break;
    }
#else
    set_file(file, (char *)global_settings.font_file);
#endif
    splash(0, ID2P(LANG_WAIT));
    current_font_id = screens[screen].getuifont();
    if (current_font_id >= 0)
        font_unload(current_font_id);
    screens[screen].setuifont(
        font_load_ex(file,0,global_settings.glyphs_to_cache));
    viewportmanager_theme_changed(THEME_UI_VIEWPORT);
}

static void ft_apply_skin_file(char *buf, char *file)
{
    splash(0, ID2P(LANG_WAIT));
    set_file(buf, file);
    settings_apply_skins();
}

static const char *strip_slash(const char *path, const char *def)
{
    if (path)
    {
        while (*path == PATH_SEPCH)
            path++; /* we don't want this treated as an absolute path */
        return path;
    }
    return def;
}

int ft_assemble_path(char *buf, size_t bufsz, const char* currdir, const char* filename)
{
    size_t len;
    const char *cd = strip_slash(currdir, "");
    filename = strip_slash(filename, "");
    /* remove slashes and NULL strings to make logic below simpler */

#ifdef HAVE_MULTIVOLUME
    /* Multi-volume device drives might be enumerated in root so everything
       should be an absolute qualified path with <drive>/ prepended */
    if (*cd != '\0') /* Not in / */
    {
        if (*cd == VOL_START_TOK)
        {
          /* use currdir, here we want the slash as it already contains the <drive> */
            len = path_append(buf, currdir, filename, bufsz);
        } /* buf => /currdir/filename */
        else
        {
            len = path_append(buf, root_realpath(), cd, bufsz); /* /<drive>/currdir */
            if(len < bufsz)
                len += path_append(buf + len, PA_SEP_HARD, filename, bufsz - len);
        } /* buf => /<drive>/currdir/filename */
    }
    else /* In / */
    {
        if (*filename == VOL_START_TOK)
        {
            len = path_append(buf, PATH_SEPSTR, filename, bufsz);
        } /* buf => /filename */
        else
        {
            len = path_append(buf, root_realpath(), filename, bufsz);
        } /* buf => /<drive>/filename */
    }
#else
    /* Other devices might need a specific drive/dir prepended but its usually '/' */
    if (*cd != '\0') /* Not in / */
    {
        len = path_append(buf, root_realpath(), cd, bufsz);/* /currdir */
        if(len < bufsz)
            len += path_append(buf + len, PA_SEP_HARD, filename, bufsz - len);
    } /* buf => /currdir/filename */
    else /* In / */
    {
        len = path_append(buf, root_realpath(), filename, bufsz);
    }  /* buf => /filename */
#endif

    if (len > bufsz)
        splash(HZ, ID2P(LANG_PLAYLIST_DIRECTORY_ACCESS_ERROR));
    return (int)len;
}

int ft_enter(struct tree_context* c)
{
    int rc = GO_TO_PREVIOUS;
    char buf[MAX_PATH];

    struct entry* file = tree_get_entry_at(c, c->selected_item);
    if (!file)
    {
        splashf(HZ, ID2P(LANG_READ_FAILED), str(LANG_UNKNOWN));
        return rc;
    }

    int file_attr = file->attr;
    ft_assemble_path(buf, sizeof(buf), c->currdir, file->name);
    if (file_attr & ATTR_DIRECTORY) {
        memcpy(c->currdir, buf, sizeof(c->currdir));
        if ( c->dirlevel < MAX_DIR_LEVELS )
            c->selected_item_history[c->dirlevel] = c->selected_item;
        c->dirlevel++;
        c->selected_item=0;
    }
    else {
        int seed = current_tick;
        bool play = false;
        int start_index=0;

        switch ( file_attr & FILE_ATTR_MASK ) {
            case FILE_ATTR_M3U:
                play = ft_play_playlist(buf, c->currdir, file->name);

                if (play)
                {
                    start_index = 0;
                }

                break;

            case FILE_ATTR_AUDIO:
            {
                int res = bookmark_autoload(c->currdir);
                if (res == BOOKMARK_CANCEL || res == BOOKMARK_DO_RESUME)
                    break;

                splash(0, ID2P(LANG_WAIT));

                /* about to create a new current playlist...
                   allow user to cancel the operation */
                if (!warn_on_pl_erase())
                    break;

                if (global_settings.party_mode && audio_status())
                {
                    playlist_insert_track(NULL, buf,
                                          PLAYLIST_INSERT_LAST, true, true);
                    splash(HZ, ID2P(LANG_QUEUE_LAST));
                }
                else
                {
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_SELECTED_ONLY_PLAYBACK_TEST)
                    if (playlist_create(c->currdir, NULL) != -1 &&
                        playlist_insert_track(NULL, buf,
                                              PLAYLIST_INSERT_LAST,
                                              false, true) >= 0)
                    {
                        struct playlist_info *playlist = playlist_get_current();
                        playlist->index = 0;
                        playlist->first_index = 0;
                        if (playlist->amount > 0)
                            playlist->indices[0] &= ~0x10000000u;
                        playlist_start(0, 0, 0);
                        play = true;
                    }
#else
                    /* use the assembled path sans filename */
                    char * fp = strrchr(buf, PATH_SEPCH);
                    if (fp)
                        *fp = '\0';
                    if (playlist_create(buf, NULL) != -1)
                    {
                        start_index = ft_build_playlist(c, c->selected_item);
                        if (global_settings.playlist_shuffle)
                        {
                            start_index = playlist_shuffle(seed, start_index);
                            /* when shuffling dir.: play all files
                               even if the file selected by user is
                               not the first one */
                            if (!global_settings.play_selected)
                                start_index = 0;
                        }
                        playlist_start(start_index, 0, 0);
                        play = true;
                    }
#endif
                }
                break;
            }
#if CONFIG_TUNER
                /* fmr preset file */
            case FILE_ATTR_FMR:
                radio_load_presets(buf);
                rc = GO_TO_FM;
                break;
            case FILE_ATTR_FMS:
                ft_apply_skin_file(buf, global_settings.fms_file);
                break;
#ifdef HAVE_REMOTE_LCD
            case FILE_ATTR_RFMS:
                ft_apply_skin_file(buf, global_settings.rfms_file);
                break;
#endif
#endif
            case FILE_ATTR_SBS:
                ft_apply_skin_file(buf, global_settings.sbs_file);
                break;
#ifdef HAVE_REMOTE_LCD
            case FILE_ATTR_RSBS:
                ft_apply_skin_file(buf, global_settings.rsbs_file);
                break;
#endif
                /* wps config file */
            case FILE_ATTR_WPS:
                ft_apply_skin_file(buf, global_settings.wps_file);
                break;
#if defined(HAVE_REMOTE_LCD) && (NB_SCREENS > 1)
                /* remote-wps config file */
            case FILE_ATTR_RWPS:
                ft_apply_skin_file(buf, global_settings.rwps_file);
                break;
#endif
            case FILE_ATTR_CFG:
                splash(0, ID2P(LANG_WAIT));
                if (!settings_load_config(buf,true))
                    break;
                splash(HZ, ID2P(LANG_SETTINGS_LOADED));
                break;

            case FILE_ATTR_BMARK:
                splash(0, ID2P(LANG_WAIT));
                bookmark_load(buf, false);
                rc = GO_TO_FILEBROWSER;
                break;

            case FILE_ATTR_LNG:
                splash(0, ID2P(LANG_WAIT));
                if (lang_core_load(buf))
                {
                    splash(HZ, ID2P(LANG_FAILED));
                    break;
                }
                set_file(buf, (char *)global_settings.lang_file);
                talk_init(); /* use voice of same language */
                viewportmanager_theme_changed(THEME_LANGUAGE);
                settings_apply_skins();
                splash(HZ, ID2P(LANG_LANGUAGE_LOADED));
                break;

            case FILE_ATTR_FONT:
                ft_load_font(buf);
                break;

            case FILE_ATTR_KBD:
                splash(0, ID2P(LANG_WAIT));
                if (!load_kbd(buf))
                    splash(HZ, ID2P(LANG_KEYBOARD_LOADED));
                set_file(buf, (char *)global_settings.kbd_file);
                break;

#if defined(HAVE_ROLO)
                /* firmware file */
            case FILE_ATTR_MOD:
                splash(0, ID2P(LANG_WAIT));
                audio_hard_stop();
                rolo_load(buf);
                break;
#endif
            case FILE_ATTR_CUE:
                display_cuesheet_content(buf);
                break;

                /* plugin file */
            case FILE_ATTR_ROCK:
            {
                char *plugin = buf, *argument = NULL;
                if (global_settings.party_mode && audio_status()) {
                    splash(HZ, ID2P(LANG_PARTY_MODE));
                    break;
                }

#ifdef PLUGINS_RUN_IN_BROWSER /* Stay in the filetree to run a plugin */
                switch (plugin_load(plugin, argument))
                {
                    case PLUGIN_GOTO_WPS:
                        play = true;
                        break;
                    case PLUGIN_GOTO_PLUGIN:
                        rc = GO_TO_PLUGIN;
                        break;
                    case PLUGIN_USB_CONNECTED:
                        if(*c->dirfilter > NUM_FILTER_MODES)
                            /* leave sub-browsers after usb, doing
                               otherwise might be confusing to the user */
                            rc = GO_TO_ROOT;
                        else
                            rc = GO_TO_FILEBROWSER;
                        break;
                    case PLUGIN_GOTO_ROOT:
                        rc = GO_TO_ROOT;
                        break;
                    /*
                    case PLUGIN_ERROR:
                    case PLUGIN_OK:
                    */
                    default:
                        break;
                }
#else /* Exit the filetree to run a plugin */
                plugin_open(plugin, argument);
                rc = GO_TO_PLUGIN;
#endif
                break;
            }

            default:
            {
                const char* plugin;
                char plugin_path[MAX_PATH];
                const char *argument = buf;
                if (global_settings.party_mode && audio_status()) {
                    splash(HZ, ID2P(LANG_PARTY_MODE));
                    break;
                }

                file = tree_get_entry_at(c, c->selected_item);
                if (!file)
                {
                    splashf(HZ, ID2P(LANG_READ_FAILED), str(LANG_UNKNOWN));
                    return rc;
                }

                plugin = filetype_get_plugin(file->attr, plugin_path, sizeof(plugin_path));
                if (plugin)
                {
#ifdef PLUGINS_RUN_IN_BROWSER /* Stay in the filetree to run a plugin */
                    switch (plugin_load(plugin, argument))
                    {
                        case PLUGIN_USB_CONNECTED:
                            rc = GO_TO_FILEBROWSER;
                            break;
                        case PLUGIN_GOTO_PLUGIN:
                            rc = GO_TO_PLUGIN;
                            break;
                        case PLUGIN_GOTO_WPS:
                            rc = GO_TO_WPS;
                            break;
                        /*
                        case PLUGIN_OK:
                        case PLUGIN_ERROR:
                        */
                        default:
                            break;
                    }
#else /* Exit the filetree to run a plugin */
                    plugin_open(plugin, argument);
                    rc = GO_TO_PLUGIN;
#endif
                }
                break;
            }
        }

        if ( play ) {
            /* the resume_index must always be the index in the
               shuffled list in case shuffle is enabled */
            global_status.resume_index = start_index;
            global_status.resume_crc32 =
                playlist_get_filename_crc32(NULL, start_index);
            global_status.resume_elapsed = 0;
            global_status.resume_offset = 0;
            status_save(false);
            rc = GO_TO_WPS;
        }
        else {
            if (*c->dirfilter > NUM_FILTER_MODES &&
                *c->dirfilter != SHOW_CFG &&
                *c->dirfilter != SHOW_FONT &&
                *c->dirfilter != SHOW_PLUGINS)
            {
                rc = GO_TO_ROOT;
            }
        }
    }

    return rc;
}

int ft_exit(struct tree_context* c)
{
    extern char lastfile[]; /* from tree.c */
    char buf[MAX_PATH];
    int rc = 0;
    bool exit_func = false;
    int i = strlen(c->currdir);

    /* strip trailing slashes */
    while (c->currdir[i-1] == PATH_SEPCH)
        i--;

    if (i>1) {
        while (c->currdir[i-1]!=PATH_SEPCH)
            i--;
        strcpy(buf,&c->currdir[i]);
        if (i==1)
            c->currdir[i]='\0';
        else
            c->currdir[i-1]='\0';

#ifdef HAVE_MULTIVOLUME /* un-redirect the realpath */
        if ((unsigned)c->dirlevel<=2) /* only expect redirect two levels max */
        {
            char *currdir = c->currdir;
            const char *root = root_realpath();
            int len = i-1;
            /* compare to the root path bail if they don't match except single '/' */
            for (; len > 0 && *root != '\0' && *root == *currdir; len--)
            {
                root++;
                currdir++;
            }
            if (*root == PATH_SEPCH) /* root may have trailing slash */
                root++;
            if (*root == '\0' &&
                (len == 0 || (len == 1 && *currdir == PATH_SEPCH)))
            {
                strcpy(c->currdir, PATH_ROOTSTR);
                c->dirlevel=1;
            }
        }
#endif

        if (*c->dirfilter > NUM_FILTER_MODES && c->dirlevel < 1)
            exit_func = true;

        c->dirlevel--;
        if ( c->dirlevel < MAX_DIR_LEVELS )
            c->selected_item=c->selected_item_history[c->dirlevel];
        else
            c->selected_item=0;

        /* if undefined position */
        if (c->selected_item == -1)
            strcpy(lastfile, buf);
    }
    else
    {
        if (*c->dirfilter > NUM_FILTER_MODES && c->dirlevel < 1)
            exit_func = true;
    }

    if (exit_func)
        rc = 3;

    c->out_of_tree = 0;

    return rc;
}
