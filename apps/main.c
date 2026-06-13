/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 Björn Stenberg
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

#include "version.h"
#include "gcc_extensions.h"
#include "storage.h"
#include "disk.h"
#include "file_internal.h"
#include "lcd.h"
#include "rtc.h"
#include "debug.h"
#include "led.h"
#include "../kernel-internal.h"
#include "button.h"
#include "core_keymap.h"
#include "tree.h"
#include "filetypes.h"
#include "panic.h"
#include "menu.h"
#include "usb.h"
#include "wifi.h"
#include "powermgmt.h"
#include "adc.h"
#include "i2c.h"
#ifndef DEBUG
#include "serial.h"
#endif
#include "audio.h"
#include "settings.h"
#include "backlight.h"
#include "status.h"
#include "debug_menu.h"
#include "font.h"
#include "language.h"
#include "wps.h"
#include "playlist.h"
#include "core_alloc.h"
#include "rolo.h"
#include "screens.h"
#include "usb_screen.h"
#include "power.h"
#include "talk.h"
#include "plugin.h"
#include "misc.h"
#include "dircache.h"
#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#include "tagtree.h"
#endif
#include "lang.h"
#include "string.h"
#include "splash.h"
#include "eeprom_settings.h"
#include "icon.h"
#include "viewport.h"
#include "skin_engine/skin_engine.h"
#include "statusbar-skinned.h"
#include "bootchart.h"
#include "logdiskf.h"
#include "bootdata.h"
#if defined(HAVE_DEVICEDATA)
#include "devicedata.h"
#endif
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
#include "nand-target.h"
#endif

#if (CONFIG_PLATFORM & PLATFORM_ANDROID)
#include "notification.h"
#endif
#include "shortcuts.h"

#ifdef IPOD_ACCESSORY_PROTOCOL
#include "iap.h"
#endif

#include "audio_thread.h"
#include "playback.h"
#include "tdspeed.h"
#if defined(HAVE_RECORDING) && !defined(SIMULATOR)
#include "pcm_record.h"
#endif

#if defined(SIMULATOR)
#include "file.h"
#include "dir.h"
#include "rbunicode.h"
#include <errno.h>
#endif

#ifdef BUTTON_REC
    #define SETTINGS_RESET BUTTON_REC
#elif (CONFIG_KEYPAD == GIGABEAT_PAD)
    #define SETTINGS_RESET BUTTON_A
#endif

#if CONFIG_TUNER
#include "radio.h"
#endif
#if (CONFIG_STORAGE & STORAGE_MMC)
#include "ata_mmc.h"
#endif

#ifdef HAVE_REMOTE_LCD
#include "lcd-remote.h"
#endif

#if CONFIG_USBOTG == USBOTG_ISP1362
#include "isp1362.h"
#endif

#if CONFIG_USBOTG == USBOTG_M5636
#include "m5636.h"
#endif

#ifdef HAVE_HARDWARE_CLICK
#include "piezo.h"
#endif

#if (CONFIG_PLATFORM & PLATFORM_NATIVE)
#define MAIN_NORETURN_ATTR NORETURN_ATTR
#else
/* gcc adds an implicit 'return 0;' at the end of main(), causing a warning
 * with noreturn attribute */
#define MAIN_NORETURN_ATTR
#endif

#if (CONFIG_PLATFORM & PLATFORM_HOSTED)
#ifdef HAVE_MULTIVOLUME
#include "pathfuncs.h" /* for init_volume_names */
#endif
#endif

#if (CONFIG_PLATFORM & PLATFORM_SDL)
#ifdef SIMULATOR
#include "sim_tasks.h"
#endif
#include "system-sdl.h"
#define HAVE_ARGV_MAIN
/* Don't use SDL_main on windows -> no more stdio redirection */
#if defined(WIN32)
#undef main
#endif
#endif /* SDL */

// #define AUTOROCK /* define this to check for "autostart.rock" on boot */
/* Alternatively, you can define autostart plugin path and its argument: */
// #define AUTOROCK     VIEWERS_DATA_DIR"/imageviewer.rock"
// #define AUTOROCK_ARG "/jpegs/sample.jpg"

static void init(void);

#if defined(SIMULATOR)
static void sim_dump_readdir(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        printf("SIM_DUMP: failed to open %s\n", path);
        return;
    }

    printf("SIM_DUMP: path=%s\n", path);
    bool want_decode = getenv("SIM_DUMP_DECODE") != NULL &&
                       strcmp(getenv("SIM_DUMP_DECODE"), "0");
    bool want_hex = getenv("SIM_DUMP_HEX") != NULL &&
                    strcmp(getenv("SIM_DUMP_HEX"), "0");
    bool want_rc = getenv("SIM_DUMP_RC") != NULL &&
                   strcmp(getenv("SIM_DUMP_RC"), "0");

    struct dirent *result;
    int idx = 0;
    while ((result = readdir(dir)))
    {
        int rc = 1;
        int len = strlen(result->d_name);
        if (want_rc)
            printf("SIM_DUMP[%d] path=%s len=%d rc=%d ", idx, path, len, rc);
        else
            printf("SIM_DUMP[%d] path=%s len=%d ", idx, path, len);
        for (int i = 0; i < len; i++)
            printf("%02X ", (unsigned char)result->d_name[i]);
        if (want_decode && *result->d_name && (result->d_name[0] != '.'))
        {
            unsigned char decoded[FAT_DIRENTRY_NAME_MAX];
            iso_decode((const unsigned char *)result->d_name, decoded, -1, len + 1);
            printf("|%s| decoded=", result->d_name);
            for (int i = 0; i < len; i++)
                printf("%02X ", (unsigned char)decoded[i]);
            printf("|%s|", decoded);
        }
        else
        {
            printf("|%s|", result->d_name);
        }

        if (want_hex)
            printf(" wanthex=1");

        printf("\n");
        idx++;
    }
    printf("SIM_DUMP_DONE count=%d path=%s\n", idx, path);
    closedir(dir);
}
#endif

#ifdef COLORFLY_C4_PRO
extern int c4pro_disk_diag_disk_init;
extern int c4pro_disk_diag_fat0_rc;
extern int c4pro_disk_diag_read_rc;
extern unsigned c4pro_disk_diag_sig;
extern unsigned c4pro_disk_diag_head;
#if !defined(SIMULATOR)
extern int c4pro_ftl_diag_hash_mapped;
extern int c4pro_ftl_diag_hash_dupes;
extern int c4pro_ftl_diag_hash_raw;
extern int c4pro_ftl_diag_hash_secdec;
extern int c4pro_ftl_diag_hash_pagedec;
extern int c4pro_ftl_diag_hash_boot_psec;
extern int c4pro_ftl_diag_hash_fat_psec;
extern int c4pro_ftl_diag_hash_root_psec;
extern int c4pro_ftl_diag_hash_boot_mode;
extern int c4pro_ftl_diag_hash_fat_mode;
extern int c4pro_ftl_diag_hash_root_mode;
extern int c4pro_ftl_diag_hash_best_delta;
extern int c4pro_ftl_diag_hash_best_delta_count;
extern int c4pro_ftl_diag_sig_fat_psec;
extern int c4pro_ftl_diag_sig_root_psec;
extern int c4pro_ftl_diag_sig_fat_mode;
extern int c4pro_ftl_diag_sig_root_mode;
extern int c4pro_ftl_diag_synth_sectors;
#endif

#if !defined(SIMULATOR)
#define C4PRO_ROOT_EXPECTED_COUNT 7
#define C4PRO_FTL_BASE_PAGE 2560
#define C4PRO_FTL_SCAN_PAGES 8192
#define C4PRO_FTL_SECTORS_PER_PAGE 8
#define C4PRO_FTL_PAGE_SIZE 4096
#define C4PRO_FTL_SPARE_SIZE 128

static const unsigned char c4pro_expected_root_sfn
    [C4PRO_ROOT_EXPECTED_COUNT][11] =
{
    "APPDATA    ",
    "RESOURCE   ",
    "RK27BOOTBIN",
    "SYSTEM     ",
    "SYSTEM00   ",
    "VERSION BIN",
    "RK28IMG CHK",
};

static const unsigned char c4pro_expected_root_attr
    [C4PRO_ROOT_EXPECTED_COUNT] =
{
    ATTR_DIRECTORY,
    ATTR_DIRECTORY,
    ATTR_ARCHIVE,
    ATTR_DIRECTORY,
    ATTR_DIRECTORY,
    ATTR_ARCHIVE,
    ATTR_ARCHIVE,
};

struct c4pro_boot_probe
{
    bool valid;
    unsigned root_lba;
    unsigned data_lba;
    unsigned root_sectors;
    unsigned sectors_per_fat;
    unsigned fats;
    unsigned reserved;
};

struct c4pro_root_candidate
{
    int psec;
    int score;
    unsigned meta;
    unsigned sig;
    unsigned char attr;
};

static unsigned c4pro_probe_u16le(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned c4pro_probe_u32le(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int c4pro_check_expected_root_sector(const unsigned char *sector)
{
    for (int i = 0; i < C4PRO_ROOT_EXPECTED_COUNT; i++)
    {
        const unsigned char *entry = sector + i * 32;

        if (memcmp(entry, c4pro_expected_root_sfn[i], 11) ||
            entry[11] != c4pro_expected_root_attr[i])
            return i;
    }

    return -1;
}

static void c4pro_show_storage_verdict(void)
{
    static unsigned char sector[SECTOR_SIZE];
    unsigned sig = 0;
    unsigned bps = 0;
    unsigned spc = 0;
    unsigned rsvd = 0;
    unsigned nfats = 0;
    unsigned root_entries = 0;
    unsigned fatsz = 0;
    unsigned root_lba = 0;
    unsigned first_data = 0;
    unsigned fat1 = 0xffff;
    unsigned fat2 = 0xffff;
    unsigned root_sig = 0;
    int read0_rc;
    int read1_rc = -1;
    int read43_rc = -1;
    int root_rc = -1;
    int root_bad = C4PRO_ROOT_EXPECTED_COUNT;
    bool boot_ok = false;
    bool fat_ok = false;
    bool root_ok = false;
    bool verdict_ok = false;

    read0_rc = storage_read_sectors(IF_MD(0,) 0, 1, sector);
    if (read0_rc == 0)
    {
        sig = c4pro_probe_u16le(sector + 510);
        bps = c4pro_probe_u16le(sector + 11);
        spc = sector[13];
        rsvd = c4pro_probe_u16le(sector + 14);
        nfats = sector[16];
        root_entries = c4pro_probe_u16le(sector + 17);
        fatsz = c4pro_probe_u16le(sector + 22);
        root_lba = rsvd + nfats * fatsz;
        if (bps != 0)
            first_data = root_lba + (root_entries * 32 + bps - 1) / bps;

        boot_ok = sig == 0xaa55 && bps == SECTOR_SIZE &&
                  spc == 4 && rsvd == 1 && nfats == 2 &&
                  fatsz == 42 && root_lba == 85 && first_data == 117 &&
                  !memcmp(sector + 3, "MSDOS5.0", 8) &&
                  !memcmp(sector + 54, "FAT16   ", 8);
    }

    if (boot_ok)
    {
        read1_rc = storage_read_sectors(IF_MD(0,) 1, 1, sector);
        if (read1_rc == 0)
            fat1 = c4pro_probe_u16le(sector);

        read43_rc = storage_read_sectors(IF_MD(0,) 43, 1, sector);
        if (read43_rc == 0)
            fat2 = c4pro_probe_u16le(sector);

        fat_ok = read1_rc == 0 && read43_rc == 0 &&
                 fat1 == 0xfff8 && fat2 == 0x0000;

        root_rc = storage_read_sectors(IF_MD(0,) root_lba, 1, sector);
        if (root_rc == 0)
        {
            root_sig = c4pro_probe_u32le(sector);
            root_bad = c4pro_check_expected_root_sector(sector);
            root_ok = root_bad < 0;
        }
    }

    verdict_ok = boot_ok && fat_ok && root_ok;

    lcd_setfont(FONT_UI);
    lcd_clear_display();
    lcd_putsf(0, 0, "C4FS %s b%u r%u d%u",
              verdict_ok ? "OK" : "BAD", bps, root_lba, first_data);
    lcd_putsf(0, 1, "r%d f%d/%04x %d/%04x rr%d",
              read0_rc, read1_rc, fat1, read43_rc, fat2, root_rc);
    lcd_putsf(0, 2, "s%04x n%u z%d q%08x", sig, root_entries,
              root_bad, root_sig);
    lcd_putsf(0, 3, "y%d h%d u%d",
              c4pro_ftl_diag_synth_sectors,
              c4pro_ftl_diag_hash_mapped, c4pro_ftl_diag_hash_dupes);
    lcd_putsf(0, 4, "x%d/%d/%d d%d n%d",
              c4pro_ftl_diag_hash_raw, c4pro_ftl_diag_hash_secdec,
              c4pro_ftl_diag_hash_pagedec,
              c4pro_ftl_diag_hash_best_delta,
              c4pro_ftl_diag_hash_best_delta_count);
    lcd_putsf(0, 5, "b%d m%d F%d m%d",
              c4pro_ftl_diag_hash_boot_psec,
              c4pro_ftl_diag_hash_boot_mode,
              c4pro_ftl_diag_sig_fat_psec,
              c4pro_ftl_diag_sig_fat_mode);
    lcd_putsf(0, 6, "R%d m%d f%d m%d",
              c4pro_ftl_diag_sig_root_psec,
              c4pro_ftl_diag_sig_root_mode,
              c4pro_ftl_diag_hash_fat_psec,
              c4pro_ftl_diag_hash_fat_mode);
    lcd_putsf(0, 7, "r%d m%d",
              c4pro_ftl_diag_hash_root_psec,
              c4pro_ftl_diag_hash_root_mode);
    lcd_update();

    if (verdict_ok)
        sleep(3 * HZ);
    else
        while (1)
            sleep(HZ);
}

static unsigned c4pro_probe_meta(const unsigned char *spare, int sec)
{
    const unsigned char *s = spare + sec * 16;
    return ((unsigned)s[0] << 16) | ((unsigned)s[1] << 8) | (unsigned)s[2];
}

static bool c4pro_probe_is_sfn_char(unsigned char c)
{
    static const char extra[] = "$%'-_@~`!(){}^#&";

    if (c == ' ')
        return true;
    if (c >= 'A' && c <= 'Z')
        return true;
    if (c >= '0' && c <= '9')
        return true;

    return strchr(extra, c) != NULL;
}

static bool c4pro_probe_attr_valid(unsigned char attr)
{
    if (attr == 0 || attr == 0x0f)
        return false;
    if (attr & 0xc0)
        return false;
    if (attr & 0x08)
        return false;
    if ((attr & 0x30) == 0x30)
        return false;

    return (attr & 0x3f) != 0;
}

static bool c4pro_probe_repeated_fill(const unsigned char *sector)
{
    unsigned counts[256];
    int max = 0;
    int max_byte = 0;

    memset(counts, 0, sizeof(counts));

    for (int i = 0; i < SECTOR_SIZE; i++)
    {
        unsigned char b = sector[i];
        counts[b]++;
        if ((int)counts[b] > max)
        {
            max = counts[b];
            max_byte = b;
        }
    }

    return max_byte != 0x00 && max > 384;
}

static bool c4pro_probe_same_name(const unsigned char *a,
                                  const unsigned char *b)
{
    return memcmp(a, b, 11) == 0;
}

static int c4pro_probe_sfn_score(const unsigned char *entry)
{
    bool has_name = false;
    int score = 0;

    if (entry[0] == 0x00 || entry[0] == 0xe5 || entry[0] == 0x05)
        return 0;
    if (!c4pro_probe_attr_valid(entry[11]))
        return 0;

    for (int i = 0; i < 11; i++)
    {
        if (!c4pro_probe_is_sfn_char(entry[i]))
            return 0;
        if (entry[i] != ' ')
            has_name = true;
    }

    if (!has_name)
        return 0;

    if (entry[11] & 0x10)
        score += 3;
    if (entry[11] & 0x20)
        score += 2;

    return score + 1;
}

static int c4pro_probe_dir_score(const unsigned char *sector,
                                 unsigned *sig, unsigned char *attr)
{
    int score = 0;
    int entries = 0;
    int unique_entries = 0;
    unsigned char names[4][11];

    *sig = c4pro_probe_u32le(sector);
    *attr = sector[11];

    if (c4pro_probe_repeated_fill(sector))
        return 0;

    for (int off = 0; off < SECTOR_SIZE; off += 32)
    {
        int entry_score;
        bool duplicate = false;

        if (sector[off] == 0x00)
            break;

        entry_score = c4pro_probe_sfn_score(sector + off);
        if (entry_score > 0)
        {
            score += entry_score;
            entries++;

            for (int i = 0; i < unique_entries; i++)
            {
                if (c4pro_probe_same_name(names[i], sector + off))
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate && unique_entries < (int)ARRAYLEN(names))
            {
                memcpy(names[unique_entries], sector + off, 11);
                unique_entries++;
            }
        }
    }

    if (entries < 2 || unique_entries < 2)
        return 0;

    return score;
}

static void c4pro_probe_consider_root(struct c4pro_root_candidate *best,
                                      int psec, int score, unsigned meta,
                                      unsigned sig, unsigned char attr)
{
    struct c4pro_root_candidate cand =
    {
        .psec = psec,
        .score = score,
        .meta = meta,
        .sig = sig,
        .attr = attr,
    };

    for (int i = 0; i < 3; i++)
    {
        if (score <= best[i].score)
            continue;

        for (int j = 2; j > i; j--)
            best[j] = best[j - 1];
        best[i] = cand;
        break;
    }
}

static void c4pro_probe_parse_boot(const unsigned char *sector,
                                   struct c4pro_boot_probe *boot)
{
    unsigned bps = c4pro_probe_u16le(sector + 11);
    unsigned root_entries = c4pro_probe_u16le(sector + 17);

    memset(boot, 0, sizeof(*boot));

    if (sector[510] != 0x55 || sector[511] != 0xaa || bps != SECTOR_SIZE)
        return;

    boot->reserved = c4pro_probe_u16le(sector + 14);
    boot->fats = sector[16];
    boot->sectors_per_fat = c4pro_probe_u16le(sector + 22);
    boot->root_sectors = (root_entries * 32 + bps - 1) / bps;
    boot->root_lba = boot->reserved + boot->fats * boot->sectors_per_fat;
    boot->data_lba = boot->root_lba + boot->root_sectors;
    boot->valid = boot->fats > 0 && boot->sectors_per_fat > 0 &&
                  boot->root_sectors > 0;
}

static void c4pro_show_ftl_probe(void)
{
#ifndef C4PRO_FTL_PROBE
    return;
#endif

    static unsigned char page_data[C4PRO_FTL_PAGE_SIZE];
    static unsigned char page_spare[C4PRO_FTL_SPARE_SIZE];
    struct c4pro_boot_probe boot;
    struct c4pro_root_candidate best[3] =
    {
        { -1, 0, 0, 0, 0 },
        { -1, 0, 0, 0, 0 },
        { -1, 0, 0, 0, 0 },
    };
    int boot_psec = -1;
    unsigned boot_meta = 0xffffff;

    memset(&boot, 0, sizeof(boot));

    lcd_setfont(FONT_UI);
    lcd_clear_display();
    lcd_puts(0, 0, "C4 FTL scan");
    lcd_puts(0, 1, "wait");
    lcd_update();

    for (int page = 0; page < C4PRO_FTL_SCAN_PAGES; page++)
    {
        if (nand_read_page(0, page, page_data, page_spare, 1, 0))
            continue;

        for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE; sec++)
        {
            int psec = page * C4PRO_FTL_SECTORS_PER_PAGE + sec;
            unsigned char *sector = page_data + sec * SECTOR_SIZE;
            unsigned meta = c4pro_probe_meta(page_spare, sec);
            unsigned sig;
            unsigned char attr;
            int dir_score;

            if (psec == C4PRO_FTL_BASE_PAGE * C4PRO_FTL_SECTORS_PER_PAGE)
            {
                c4pro_probe_parse_boot(sector, &boot);
                boot_psec = psec;
                boot_meta = meta;
            }

            dir_score = c4pro_probe_dir_score(sector, &sig, &attr);
            if (dir_score > 0)
                c4pro_probe_consider_root(best, psec, dir_score, meta,
                                          sig, attr);
        }
    }

    lcd_clear_display();
    lcd_puts(0, 0, "FTL auto");
    lcd_putsf(0, 1, "B p%d m%06x", boot_psec, boot_meta);
    lcd_putsf(0, 2, "R l%u d%u f%u",
              boot.root_lba, boot.data_lba, boot.sectors_per_fat);
    for (int i = 0; i < 3; i++)
    {
        int base = (boot.valid && best[i].psec >= 0) ?
            best[i].psec - (int)boot.root_lba : -1;
        lcd_putsf(0, 3 + i, "%d p%d b%d q%d a%02x",
                  i + 1, best[i].psec, base, best[i].score, best[i].attr);
    }
    lcd_putsf(0, 6, "m %06x %06x", best[0].meta, best[1].meta);
    lcd_putsf(0, 7, "s %08x", best[0].sig);
    lcd_update();
    while (true)
        sleep(HZ);
}

#endif

#define C4PRO_BOOT_STEP(step) do { } while (0)
#define C4PRO_BOOT_HALT(step) do { } while (0)
#define C4PRO_APP_ENTRY_HALT() do { } while (0)
#define C4PRO_SYSTEM_HALT() do { } while (0)
#define C4PRO_KERNEL_HALT() do { } while (0)
#define C4PRO_FILESYSTEM_HALT() do { } while (0)
#define C4PRO_FREQ_HALT() do { } while (0)
#define C4PRO_CPU_HALT() do { } while (0)
#define C4PRO_INIT_MID_HALT() do { } while (0)
#define C4PRO_PRE_LOGO_HALT() do { } while (0)
#define C4PRO_LOGO_RETURN_HALT() do { } while (0)
#define C4PRO_PRE_STORAGE_HALT() do { } while (0)
#define C4PRO_STORAGE_HALT() do { } while (0)
#define C4PRO_MOUNT_READY_HALT() do { } while (0)
#define C4PRO_POST_CORE_INIT_HALT() do { } while (0)
#define C4PRO_PRE_ROOT_MENU_HALT(step) do { } while (0)
#else
#define C4PRO_BOOT_STEP(step) do { } while (0)
#define C4PRO_BOOT_HALT(step) do { } while (0)
#define C4PRO_APP_ENTRY_HALT() do { } while (0)
#define C4PRO_SYSTEM_HALT() do { } while (0)
#define C4PRO_KERNEL_HALT() do { } while (0)
#define C4PRO_FILESYSTEM_HALT() do { } while (0)
#define C4PRO_FREQ_HALT() do { } while (0)
#define C4PRO_CPU_HALT() do { } while (0)
#define C4PRO_INIT_MID_HALT() do { } while (0)
#define C4PRO_PRE_LOGO_HALT() do { } while (0)
#define C4PRO_LOGO_RETURN_HALT() do { } while (0)
#define C4PRO_PRE_STORAGE_HALT() do { } while (0)
#define C4PRO_STORAGE_HALT() do { } while (0)
#define C4PRO_MOUNT_READY_HALT() do { } while (0)
#define C4PRO_POST_CORE_INIT_HALT() do { } while (0)
#define C4PRO_PRE_ROOT_MENU_HALT(step) do { } while (0)
#endif

/* main(), and various functions called by main() and init() may be
 * be INIT_ATTR. These functions must not be called after the final call
 * to root_menu() at the end of main()
 * see definition of INIT_ATTR in config.h */
#ifdef HAVE_ARGV_MAIN
int main(int argc, char *argv[]) INIT_ATTR MAIN_NORETURN_ATTR ;
int main(int argc, char *argv[])
{
    sys_handle_argv(argc, argv);
#else
int main(void) INIT_ATTR MAIN_NORETURN_ATTR;
int main(void)
{
#endif
    C4PRO_APP_ENTRY_HALT();
    CHART(">init");
    init();
    CHART("<init");
    C4PRO_BOOT_HALT("init complete");
    FOR_NB_SCREENS(i)
    {
        screens[i].clear_display();
        screens[i].update();
    }
    list_init();
    C4PRO_BOOT_STEP("tree_init");
    tree_init();
#if defined(HAVE_DEVICEDATA) && !defined(BOOTLOADER) /* SIMULATOR */
    verify_device_data();
#endif
    /* Keep the order of this 3
     * Must be done before any code uses the multi-screen API */
#if defined(HAVE_USBSTACK) && !defined(COLORFLY_C4_PRO)
    /* All threads should be created and public queues registered by now */
    usb_start_monitoring();
#endif

#if !defined(DISABLE_ACTION_REMAP) && defined(CORE_KEYREMAP_FILE)
    if (file_exists(CORE_KEYREMAP_FILE))
    {
        int mapct = core_load_key_remap(CORE_KEYREMAP_FILE);
        if (mapct <= 0)
            splashf(HZ, "key remap failed: %d,  %s", mapct, CORE_KEYREMAP_FILE);
    }
#endif

#if !defined(BOOTLOADER)
#ifndef COLORFLY_C4_PRO
    allocate_playback_log();
    if (!file_exists(ROCKBOX_DIR"/playername.txt"))
    {
        int fd = open(ROCKBOX_DIR"/playername.txt", O_CREAT|O_WRONLY|O_TRUNC, 0666);
        if(fd >= 0)
        {
            fdprintf(fd, "%s", MODEL_NAME);
            close(fd);
        }
    }
#endif
#endif

#ifdef AUTOROCK
    {
        const char *file = ""AUTOROCK;
        file = *file
                ? file
                :
#ifdef APPLICATION
                                ROCKBOX_DIR
#else
                                PLUGIN_APPS_DIR
#endif
                                    "/autostart.rock";
        if (file_exists(file)) /* no complaint if it doesn't exist */
        {
            plugin_load(file,
#ifdef AUTOROCK_ARG
                AUTOROCK_ARG
#else
                NULL
#endif
                ); /* start if it does */
        }
    }
#endif /* #ifdef AUTOROCK */

    global_status.last_volume_change = 0;
    validate_start_directory_init();
    /* no calls INIT_ATTR functions after this point anymore!
     * see definition of INIT_ATTR in config.h */
    C4PRO_BOOT_STEP("root_menu");
    C4PRO_PRE_ROOT_MENU_HALT("pre root_menu");
#if defined(SIMULATOR)
    if (getenv("SIM_DUMP_DIR"))
    {
        sim_dump_readdir(getenv("SIM_DUMP_DIR"));
        sim_dump_readdir("/");
    }
#endif
    CHART(">root_menu");
    root_menu();
}

/* The disk isn't ready at boot, rblogo is stored in bin and erased after boot */
int show_logo_boot( void ) INIT_ATTR;
int show_logo_boot( void )
{
    unsigned char version[32];
    int font_h, ver_w;
    snprintf(version, sizeof(version), "Ver. %s", rbversion);
    ver_w = font_getstringsize(version, NULL, &font_h, FONT_SYSFIXED);
    lcd_clear_display();
    lcd_setfont(FONT_SYSFIXED);
#if defined(SANSA_CLIP) || defined(SANSA_CLIPV2) || defined(SANSA_CLIPPLUS)
    /* display the logo in the blue area of the screen (bottom 48 pixels) */
    if (ver_w > LCD_WIDTH)
        lcd_putsxy(0, 0, rbversion);
    else
        lcd_putsxy((LCD_WIDTH/2) - (ver_w/2), 0, version);
    lcd_bmp(&bm_rockboxlogo, (LCD_WIDTH - BMPWIDTH_rockboxlogo) / 2, 16);
#else
    lcd_bmp(&bm_rockboxlogo, (LCD_WIDTH - BMPWIDTH_rockboxlogo) / 2, 10);
    if (ver_w > LCD_WIDTH)
        lcd_putsxy(0, LCD_HEIGHT-font_h, rbversion);
    else
        lcd_putsxy((LCD_WIDTH/2) - (ver_w/2), LCD_HEIGHT-font_h, version);
#endif
    lcd_setfont(FONT_UI);
    lcd_update();
#ifdef HAVE_REMOTE_LCD
    lcd_remote_clear_display();
    lcd_remote_bmp(&bm_remote_rockboxlogo, 0, 10);
    lcd_remote_setfont(FONT_SYSFIXED);
    if (ver_w > LCD_REMOTE_WIDTH)
        lcd_remote_putsxy(0, LCD_REMOTE_HEIGHT-font_h, rbversion);
    else
        lcd_remote_putsxy((LCD_REMOTE_WIDTH/2) - (ver_w/2),
                      LCD_REMOTE_HEIGHT-font_h, version);
    lcd_remote_setfont(FONT_UI);
    lcd_remote_update();
#endif
#ifdef SIMULATOR
    sleep(HZ); /* sim is too fast to see logo */
#endif
    return 0;
}

#if defined(HAVE_DIRCACHE) && !defined(COLORFLY_C4_PRO)
static int INIT_ATTR init_dircache(bool preinit)
{
    if (preinit)
        dircache_init(MAX(global_status.dircache_size, 0));

    if (!global_settings.dircache)
        return -1;

    int result = -1;

#ifdef HAVE_EEPROM_SETTINGS
    if (firmware_settings.initialized &&
        firmware_settings.disk_clean &&
        preinit)
    {
        result = dircache_load();
        if (result < 0)
            firmware_settings.disk_clean = false;
    }
    else
#endif /* HAVE_EEPROM_SETTINGS */
    if (!preinit)
    {
        result = dircache_enable();
        if (result != 0)
        {
            if (result > 0)
            {
                /* Print "Scanning disk..." to the display. */
                splash(0, str(LANG_SCANNING_DISK));
                dircache_wait();
#ifndef COLORFLY_C4_PRO
                backlight_on();
#endif
                show_logo_boot();
            }

            struct dircache_info info;
            dircache_get_info(&info);
            global_status.dircache_size = info.size;
            status_save(true);
        }
        /* else don't wait or already enabled by load */
    }

    return result;
}
#endif /* HAVE_DIRCACHE */

#if defined(HAVE_TAGCACHE) && !defined(COLORFLY_C4_PRO)
static void init_tagcache(void) INIT_ATTR;
static void init_tagcache(void)
{
    bool clear = false;
#if 0
    long talked_tick = 0;
#endif
    tagcache_init();

    while (!tagcache_is_initialized())
    {
        int ret = tagcache_get_commit_step();

        if (ret > 0)
        {
#if 0 /* FIXME: Audio isn't even initialized yet! */
            /* hwcodec can't use voice here, as the database commit
             * uses the audio buffer. */
            if(global_settings.talk_menu
               && (talked_tick == 0
                   || TIME_AFTER(current_tick, talked_tick+7*HZ)))
            {
                talked_tick = current_tick;
                talk_id(LANG_TAGCACHE_INIT, false);
                talk_number(ret, true);
                talk_id(VOICE_OF, true);
                talk_number(tagcache_get_max_commit_step(), true);
            }
#endif
            if (lang_is_rtl())
            {
                splash_progress(ret, tagcache_get_max_commit_step(),
                               "[%d/%d] %s", ret, tagcache_get_max_commit_step(),
                               str(LANG_TAGCACHE_INIT));
            }
            else
            {
                splash_progress(ret, tagcache_get_max_commit_step(),
                                "%s [%d/%d]", str(LANG_TAGCACHE_INIT), ret,
                                tagcache_get_max_commit_step());
            }
            clear = true;
        }
        sleep(HZ/4);
    }
    tagtree_init();

    if (clear)
    {
#ifndef COLORFLY_C4_PRO
        backlight_on();
#endif
        show_logo_boot();
    }
}
#endif /* HAVE_TAGCACHE */

#if (CONFIG_PLATFORM & PLATFORM_HOSTED)

static void init(void)
{
    system_init();
    core_allocator_init();
    kernel_init();
#ifdef APPLICATION
    paths_init();
#endif
    enable_irq();
    lcd_init();
#ifdef HAVE_REMOTE_LCD
    lcd_remote_init();
#endif
    FOR_NB_SCREENS(i)
        global_status.font_id[i] = FONT_SYSFIXED;
    font_init();
    show_logo_boot();
    button_init();
    powermgmt_init();
#ifndef COLORFLY_C4_PRO
    backlight_init();
#endif
    unicode_init();
#ifdef HAVE_MULTIVOLUME
    init_volume_names();
#endif
#ifdef SIMULATOR
    sim_tasks_init();
#endif
#if (CONFIG_PLATFORM & PLATFORM_ANDROID)
    notification_init();
#endif
    lang_init(core_language_builtin, language_strings,
              LANG_LAST_INDEX_IN_ARRAY);
#ifdef DEBUG
    debug_init();
#endif
#if CONFIG_TUNER
    radio_init();
#endif
    /* Keep the order of this 3 (viewportmanager handles statusbars)
     * Must be done before any code uses the multi-screen API */
    gui_syncstatusbar_init(&statusbars);
    gui_sync_skin_init();
    sb_skin_init();
    viewportmanager_init();

    storage_init();
    pcm_init();
    dsp_init();
    settings_reset();
    settings_load();
    settings_apply(true);
    init_battery_tables();
#ifdef HAVE_DIRCACHE
    init_dircache(true);
    init_dircache(false);
#endif
#if defined(HAVE_TAGCACHE) && !defined(COLORFLY_C4_PRO)
    init_tagcache();
#endif
    tree_mem_init();
    filetype_init();
    playlist_init();
    shortcuts_init();

    audio_init();
    talk_announce_voice_invalid(); /* notify user w/ voice prompt if voice file invalid */
    settings_apply_skins();

/* do USB last so prompt (if enabled) can work correctly if USB was inserted with device off,
 * also doesn't hurt that it will display the nice pretty backdrop this way too. */
#ifndef USB_NONE
    usb_init();
    usb_start_monitoring();
#endif
}

#else /* ! (CONFIG_PLATFORM & PLATFORM_HOSTED) */

#include "errno.h"

static void init(void) INIT_ATTR;
static void init(void)
{
    int rc;
    bool mounted = false;

    system_init();
    C4PRO_SYSTEM_HALT();
    core_allocator_init();
    kernel_init();
    C4PRO_KERNEL_HALT();

#if defined(HAVE_BOOTDATA) && !defined(BOOTLOADER)
    verify_boot_data();
#endif

#if defined(HAVE_DEVICEDATA) && !defined(BOOTLOADER)
    verify_device_data();
#endif

    /* early early early! */
    filesystem_init();
    C4PRO_FILESYSTEM_HALT();

#ifdef HAVE_ADJUSTABLE_CPU_FREQ
    set_cpu_frequency(CPUFREQ_NORMAL);
    C4PRO_FREQ_HALT();
#ifdef CPU_COLDFIRE
    coldfire_set_pllcr_audio_bits(DEFAULT_PLLCR_AUDIO_BITS);
#endif
    cpu_boost(true);
#endif
    C4PRO_CPU_HALT();

    i2c_init();

    power_init();

    enable_irq();
#if defined(CPU_ARM_CLASSIC)
    enable_fiq();
#endif
    /* current_tick should be ticking by now */
    CHART("ticking");
    C4PRO_INIT_MID_HALT();

    unicode_init();
    lcd_init();
#ifdef HAVE_REMOTE_LCD
    lcd_remote_init();
#endif
    FOR_NB_SCREENS(i)
        global_status.font_id[i] = FONT_SYSFIXED;
    font_init();
    C4PRO_BOOT_STEP("lcd/font ok");

    settings_reset();
    C4PRO_PRE_LOGO_HALT();

    CHART(">show_logo");
    show_logo_boot();
    CHART("<show_logo");
    C4PRO_LOGO_RETURN_HALT();
    C4PRO_BOOT_STEP("logo ok");
    lang_init(core_language_builtin, language_strings,
              LANG_LAST_INDEX_IN_ARRAY);
    C4PRO_BOOT_STEP("lang ok");

#ifdef DEBUG
    debug_init();
#else
#ifdef HAVE_SERIAL
    serial_setup();
#endif
#endif

#if CONFIG_RTC
    rtc_init();
#endif

    adc_init();
    C4PRO_BOOT_STEP("adc ok");

#ifndef COLORFLY_C4_PRO
    usb_init();
#endif
#if CONFIG_USBOTG == USBOTG_ISP1362
    isp1362_init();
#elif CONFIG_USBOTG == USBOTG_M5636
    m5636_init();
#endif

#ifndef COLORFLY_C4_PRO
    backlight_init();
#endif

    button_init();
    C4PRO_BOOT_STEP("button ok");

    /* Don't initialize power management here if it could incorrectly
     * measure battery voltage, and it's not needed for charging. */
#if !defined(NEED_ATA_POWER_BATT_MEASURE) || \
    (CONFIG_CHARGING > CHARGING_MONITOR)
    powermgmt_init();
#endif

#if CONFIG_TUNER
    radio_init();
#endif

#ifdef HAVE_HARDWARE_CLICK
    piezo_init();
#endif

    /* Keep the order of this 3 (viewportmanager handles statusbars)
     * Must be done before any code uses the multi-screen API */
    CHART(">gui_syncstatusbar_init");
    C4PRO_BOOT_STEP("statusbar_init");
    gui_syncstatusbar_init(&statusbars);
    CHART("<gui_syncstatusbar_init");
    CHART(">sb_skin_init");
    C4PRO_BOOT_STEP("sb_skin_init");
    sb_skin_init();
    CHART("<sb_skin_init");
    CHART(">gui_sync_wps_init");
    C4PRO_BOOT_STEP("skin_init");
    gui_sync_skin_init();
    CHART("<gui_sync_wps_init");
    CHART(">viewportmanager_init");
    C4PRO_BOOT_STEP("viewport_init");
    viewportmanager_init();
    CHART("<viewportmanager_init");
    C4PRO_PRE_STORAGE_HALT();

    CHART(">storage_init");
    C4PRO_BOOT_STEP("storage_init");
    rc = storage_init();
    CHART("<storage_init");
    C4PRO_BOOT_STEP("storage ok");
    C4PRO_STORAGE_HALT();
    if(rc)
    {
        lcd_clear_display();
        lcd_putsf(0, 1, "ATA error: %d", rc);
        lcd_puts(0, 3, "Press button to debug");
        lcd_update();
        while(!(button_get(true) & BUTTON_REL)); /* DO NOT CHANGE TO ACTION SYSTEM */
        dbg_ports();
        panicf("ata: %d", rc);
    }

#if defined(NEED_ATA_POWER_BATT_MEASURE) && \
    (CONFIG_CHARGING <= CHARGING_MONITOR)
    /* After storage_init(), ATA power must be on, so battery voltage
     * can be measured. Initialize power management if it was delayed. */
    powermgmt_init();
#endif
#ifdef HAVE_EEPROM_SETTINGS
    CHART(">eeprom_settings_init");
    eeprom_settings_init();
    CHART("<eeprom_settings_init");
#endif

#ifndef HAVE_USBSTACK
    usb_start_monitoring();
    while (usb_detect() == USB_INSERTED)
    {
#ifdef HAVE_EEPROM_SETTINGS
        firmware_settings.disk_clean = false;
#endif
        /* enter USB mode early, before trying to mount */
        if (button_get_w_tmo(HZ/10) == SYS_USB_CONNECTED)
#if (CONFIG_STORAGE & STORAGE_MMC)
            if (!mmc_touched() ||
                (mmc_remove_request() == SYS_HOTSWAP_EXTRACTED))
#endif
            {
                gui_usb_screen_run(true, button_get_data());
                mounted = true; /* mounting done @ end of USB mode */
            }
#ifdef HAVE_USB_POWER
        /* if there is no host or user requested no USB, skip this */
        if (usb_powered_only())
            break;
#endif
    }
#endif

    if (!mounted)
    {
        CHART(">disk_mount_all");
        C4PRO_BOOT_STEP("disk_mount");
        rc = disk_mount_all();
        CHART("<disk_mount_all");
        if (rc<=0)
        {
            int line=0;
            lcd_clear_display();
#if defined(COLORFLY_C4_PRO)
            static unsigned char sector[SECTOR_SIZE];
            int read_rc;
            int fat_rc = -1;
            int root_rc = -1;
            bool all_zero = true;
            int i;
            unsigned sig = 0;
            unsigned byps = 0;
            unsigned spc = 0;
            unsigned rsvd = 0;
            unsigned nfats = 0;
            unsigned root_entries = 0;
            unsigned fatsz = 0;
            unsigned long totsec = 0;
            unsigned root_lba = 0;
            unsigned first_data = 0;
            unsigned head0 = 0;
            unsigned fat_head = 0;
            unsigned root_head = 0;
            char oem[9];
            char fs[9];

            oem[0] = '\0';
            fs[0] = '\0';

            read_rc = storage_read_sectors(IF_MD(0,) 0, 1, sector);
            if (read_rc == 0)
            {
                for (i = 0; i < SECTOR_SIZE; i++)
                {
                    if (sector[i] != 0)
                    {
                        all_zero = false;
                        break;
                    }
                }

                sig = sector[510] | (sector[511] << 8);
                byps = sector[11] | (sector[12] << 8);
                spc = sector[13];
                rsvd = sector[14] | (sector[15] << 8);
                nfats = sector[16];
                root_entries = sector[17] | (sector[18] << 8);
                totsec = sector[19] | (sector[20] << 8);
                if (totsec == 0)
                    totsec = (unsigned long)sector[32] |
                             ((unsigned long)sector[33] << 8) |
                             ((unsigned long)sector[34] << 16) |
                             ((unsigned long)sector[35] << 24);
                fatsz = sector[22] | (sector[23] << 8);
                root_lba = rsvd + nfats * fatsz;
                if (byps != 0)
                    first_data = root_lba + (root_entries * 32 + byps - 1) / byps;
                head0 = (sector[0] << 16) | (sector[1] << 8) | sector[2];
                memcpy(oem, sector + 3, 8);
                oem[8] = '\0';
                memcpy(fs, sector + 54, 8);
                fs[8] = '\0';

                fat_rc = storage_read_sectors(IF_MD(0,) 1, 1, sector);
                if (fat_rc == 0)
                    fat_head = sector[0] | (sector[1] << 8);

                root_rc = storage_read_sectors(IF_MD(0,) root_lba, 1, sector);
                if (root_rc == 0)
                    root_head = sector[0] | (sector[1] << 8);
            }

            lcd_setfont(FONT_SYSFIXED);
            lcd_clear_display();
            lcd_set_foreground(LCD_WHITE);
            lcd_puts(0, line++, "C4 STORE 20260613B");
            lcd_putsf(0, line++, "dm%d di%d r%d s%04x h%06x f0%d", rc,
                      c4pro_disk_diag_disk_init, c4pro_disk_diag_read_rc,
                      c4pro_disk_diag_sig, c4pro_disk_diag_head,
                      c4pro_disk_diag_fat0_rc);
            lcd_putsf(0, line++, "m%d r%d z%d s%04x h%06x", rc, read_rc,
                      all_zero ? 1 : 0, sig, head0);
            lcd_putsf(0, line++, "o%.8s fs%.8s", oem, fs);
            lcd_putsf(0, line++, "b%u c%u rs%u nf%u f%u", byps, spc, rsvd,
                      nfats, fatsz);
            lcd_putsf(0, line++, "t%lu rd%u fd%u re%u", totsec, root_lba,
                      first_data, root_entries);
            lcd_putsf(0, line++, "l1 %d %04x rt %d %04x", fat_rc, fat_head,
                      root_rc, root_head);
#else
            lcd_putsf(0, line++, "No partition found (%d).", rc);
#ifndef USB_NONE
            lcd_puts(0, line++, "Insert USB cable");
            lcd_puts(0, line++, "and fix it.");
#elif !defined(DEBUG) && !(CONFIG_STORAGE & STORAGE_RAMDISK)
            lcd_puts(0, line++, "Rebooting in 5s");
#endif
#endif
            lcd_puts(0, line++, rbversion);

#if !defined(COLORFLY_C4_PRO) && defined(STORAGE_GET_INFO)
            struct storage_info sinfo;
            storage_get_info(0, &sinfo);
#ifdef MAX_PHYS_SECTOR_SIZE
            lcd_putsf(0, line++, "id: '%s' s:%u*%u", sinfo.product, sinfo.sector_size, sinfo.phys_sector_mult);
#else
            lcd_putsf(0, line++, "id: '%s' s:%u", sinfo.product, sinfo.sector_size);
#endif
#endif
            struct partinfo pinfo;
            for (int i = 0 ; i < NUM_VOLUMES ; i++) {
                disk_partinfo(i, &pinfo);
                if (pinfo.type)
                    lcd_putsf(0, line++, "P%d T%02x S%llx",
                              i, pinfo.type, (unsigned long long)pinfo.size);
            }
            lcd_update();

#if defined(MAX_VIRT_SECTOR_SIZE) && defined(DEFAULT_VIRT_SECTOR_SIZE)
#ifdef HAVE_MULTIDRIVE
            for (int i = 0 ; i < NUM_DRIVES ; i++)
#endif
                disk_set_sector_multiplier(IF_MD(i,) DEFAULT_VIRT_SECTOR_SIZE/SECTOR_SIZE);
#endif

#if defined(COLORFLY_C4_PRO)
            while (1)
            {
                lcd_update();
                sleep(HZ / 2);
            }
#elif !defined(USB_NONE)
            usb_start_monitoring();
            while(button_get(true) != SYS_USB_CONNECTED) {};
            gui_usb_screen_run(true, button_get_data());
#elif !defined(DEBUG) && !(CONFIG_STORAGE & STORAGE_RAMDISK)
            sleep(HZ*5);
#endif

#if !defined(DEBUG) && !(CONFIG_STORAGE & STORAGE_RAMDISK)
            system_reboot();
#else
            rc = disk_mount_all();
            if (rc <= 0) {
                lcd_putsf(0, 4, "Error mounting: %08x", rc);
                lcd_update();
                sleep(HZ*5);
                system_reboot();
            }
#endif
        }
    }

    C4PRO_MOUNT_READY_HALT();
    C4PRO_BOOT_STEP("mount ok");
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
    c4pro_show_storage_verdict();
    c4pro_show_ftl_probe();
#endif
    pcm_init();
    C4PRO_BOOT_STEP("pcm ok");
    dsp_init();
    C4PRO_BOOT_STEP("dsp ok");

    CHART(">settings_load");
    C4PRO_BOOT_STEP("settings_load");
    settings_load();
    CHART("<settings_load");
    C4PRO_BOOT_STEP("settings ok");

#if defined(BUTTON_REC) || \
    (CONFIG_KEYPAD == GIGABEAT_PAD) || \
    (CONFIG_KEYPAD == IPOD_4G_PAD) || \
    (CONFIG_KEYPAD == IRIVER_H10_PAD)
    if (global_settings.clear_settings_on_hold &&
#ifdef SETTINGS_RESET
    /* Reset settings if holding the reset button. (Rec on Archos,
       A on Gigabeat) */
    ((button_status() & SETTINGS_RESET) == SETTINGS_RESET))
#else
    /* Reset settings if the hold button is turned on */
    (button_hold()))
#endif
    {
        splash(HZ*2, str(LANG_RESET_DONE_CLEAR));
        settings_reset();
    }
#endif
    CHART(">init_battery_tables");
    C4PRO_BOOT_STEP("battery_tables");
    init_battery_tables();
    CHART("<init_battery_tables");
    C4PRO_BOOT_STEP("battery ok");
#if defined(HAVE_DIRCACHE) && !defined(COLORFLY_C4_PRO)
    CHART(">init_dircache(true)");
    rc = init_dircache(true);
    CHART("<init_dircache(true)");
#ifdef HAVE_TAGCACHE
    if (rc < 0)
        tagcache_remove_statefile();
#endif /* HAVE_TAGCACHE */
#endif /* HAVE_DIRCACHE */

    CHART(">settings_apply(true)");
    C4PRO_BOOT_STEP("settings_apply");
    settings_apply(true);
    CHART("<settings_apply(true)");
    C4PRO_BOOT_STEP("apply ok");
#if defined(HAVE_DIRCACHE) && !defined(COLORFLY_C4_PRO)
    CHART(">init_dircache(false)");
    init_dircache(false);
    CHART("<init_dircache(false)");
#endif
#if defined(HAVE_TAGCACHE) && !defined(COLORFLY_C4_PRO)
    CHART(">init_tagcache");
    C4PRO_BOOT_STEP("tagcache");
    init_tagcache();
    CHART("<init_tagcache");
    C4PRO_BOOT_STEP("tagcache ok");
#endif

#ifdef HAVE_EEPROM_SETTINGS
    if (firmware_settings.initialized)
    {
        /* In case we crash. */
        firmware_settings.disk_clean = false;
        CHART(">eeprom_settings_store");
        eeprom_settings_store();
        CHART("<eeprom_settings_store");
    }
#endif
    playlist_init();
    C4PRO_BOOT_STEP("playlist ok");
    tree_mem_init();
    C4PRO_BOOT_STEP("tree_mem ok");
    filetype_init();
    C4PRO_BOOT_STEP("filetype ok");

    shortcuts_init();
    C4PRO_BOOT_STEP("shortcuts ok");

#ifndef COLORFLY_C4_PRO
    CHART(">audio_init");
    C4PRO_BOOT_STEP("audio_init");
    audio_init();
    CHART("<audio_init");
    C4PRO_BOOT_STEP("audio ok");
    talk_announce_voice_invalid(); /* notify user w/ voice prompt if voice file invalid */
    C4PRO_BOOT_STEP("talk ok");
#else
    C4PRO_BOOT_STEP("audio skipped");
#endif
    C4PRO_POST_CORE_INIT_HALT();

#ifdef HAVE_WIFI
    wifi_init();
#endif

    /* runtime database has to be initialized after audio_init() */
    cpu_boost(false);

#if CONFIG_CHARGING
    car_adapter_mode_init();
#endif
#ifdef IPOD_ACCESSORY_PROTOCOL
    iap_setup(global_settings.serial_bitrate);
#endif
#ifdef HAVE_ACCESSORY_SUPPLY
    accessory_supply_set(global_settings.accessory_supply);
#endif
#ifdef HAVE_LINEOUT_POWEROFF
    lineout_set(global_settings.lineout_active);
#endif
#ifdef HAVE_HOTSWAP_STORAGE_AS_MAIN
    CHART("<check_bootfile(false)");
    check_bootfile(false); /* remember write time and filesize */
    CHART(">check_bootfile(false)");
#endif
    CHART("<settings_apply_skins");
    C4PRO_BOOT_STEP("skins_apply");
    settings_apply_skins();
    CHART(">settings_apply_skins");
    C4PRO_BOOT_STEP("skins ok");
}

#ifdef CPU_PP
void cop_main(void) MAIN_NORETURN_ATTR;
void cop_main(void)
{
/* This is the entry point for the coprocessor
   Anyone not running an upgraded bootloader will never reach this point,
   so it should not be assumed that the coprocessor be usable even on
   platforms which support it.

   A kernel thread is initially setup on the coprocessor and immediately
   destroyed for purposes of continuity. The cop sits idle until at least
   one thread exists on it. */

#if NUM_CORES > 1
    system_init();
    kernel_init();
    /* This should never be reached */
#endif
    while(1) {
        sleep_core(COP);
    }
}
#endif /* CPU_PP */

#endif /* CONFIG_PLATFORM & PLATFORM_HOSTED */
