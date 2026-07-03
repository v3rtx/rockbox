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
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    (defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE))
extern void c4pro_usb_diag_enter_storage_mode(void);
extern void c4pro_usb_diag_force_reattach(void);
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_USB_DIAG_EXPORT)
#include "usb_core.h"
#endif
#include "wifi.h"
#include "powermgmt.h"
#include "adc.h"
#include "i2c.h"
#ifndef DEBUG
#include "serial.h"
#endif
#include "audio.h"
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_PCM_TONE_TEST)
#include "pcm.h"
#include "pcm-internal.h"
#include "pcm_sw_volume.h"
#endif
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

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_APP_SYSTEM_INSTALLER)
#ifdef C4PRO_APP_SYSTEM_INSTALLER_EMBEDDED
#include "c4pro-installer-payload.h"
#endif

#define C4PRO_INSTALL_SYSTEM_PSEC_BASE 20480u
#define C4PRO_INSTALL_SYSTEM_SECTORS 43008u
#define C4PRO_INSTALL_SECTORS_PER_PAGE 8u
#define C4PRO_INSTALL_PAGES_PER_BLOCK 128u
#define C4PRO_INSTALL_BYTES_PER_SECTOR 512u
#define C4PRO_INSTALL_SECTORS_PER_CLUSTER 4u
#define C4PRO_INSTALL_RESERVED_SECTORS 1u
#define C4PRO_INSTALL_FAT_COUNT 2u
#define C4PRO_INSTALL_ROOT_ENTRIES 512u
#define C4PRO_INSTALL_SECTORS_PER_FAT 42u
#define C4PRO_INSTALL_ROOT_SECTORS \
    ((C4PRO_INSTALL_ROOT_ENTRIES * 32u) / C4PRO_INSTALL_BYTES_PER_SECTOR)
#define C4PRO_INSTALL_FIRST_FAT_SECTOR C4PRO_INSTALL_RESERVED_SECTORS
#define C4PRO_INSTALL_FIRST_ROOT_SECTOR \
    (C4PRO_INSTALL_RESERVED_SECTORS + \
     C4PRO_INSTALL_FAT_COUNT * C4PRO_INSTALL_SECTORS_PER_FAT)
#define C4PRO_INSTALL_FIRST_DATA_SECTOR \
    (C4PRO_INSTALL_FIRST_ROOT_SECTOR + C4PRO_INSTALL_ROOT_SECTORS)
#define C4PRO_INSTALL_CLUSTER_BYTES \
    (C4PRO_INSTALL_SECTORS_PER_CLUSTER * C4PRO_INSTALL_BYTES_PER_SECTOR)

static unsigned char c4pro_install_page[4096];
#ifdef C4PRO_APP_SYSTEM_INSTALLER_STOCK_RAW_WRITE
static unsigned char c4pro_install_verify[4096];
#endif
#ifdef C4PRO_APP_LOADPATH_CORRUPTOR
static unsigned char c4pro_install_verify[4096];
#endif
#ifdef C4PRO_APP_NATIVE_ROCKBOX_INSTALLER
static unsigned char c4pro_install_verify[4096];
static unsigned char c4pro_install_verify_spare[128];
#endif
static unsigned char c4pro_install_spare[128];

static uint32_t c4pro_installer_crc32_update(uint32_t crc,
                                             const unsigned char *buf,
                                             uint32_t len)
{
    while (len--)
    {
        crc ^= *buf++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
    }

    return crc;
}

static void c4pro_installer_status(const char *a, const char *b,
                                   uint32_t x, uint32_t y)
{
    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "C4 installer");
    lcd_puts(0, 1, a);
    lcd_puts(0, 2, b);
    lcd_putsf(0, 3, "a %lu", (unsigned long)x);
    lcd_putsf(0, 4, "b %lu", (unsigned long)y);
    lcd_update();
}

static void c4pro_installer_halt(void) NORETURN_ATTR;
static void c4pro_installer_halt(void)
{
    while (1)
        sleep(HZ);
}

static void c4pro_installer_fail(const char *where, int rc) NORETURN_ATTR;
static void c4pro_installer_fail(const char *where, int rc)
{
    c4pro_installer_status("FAIL", where, (uint32_t)rc, 0);
    c4pro_installer_halt();
}

static void c4pro_installer_fail2(const char *where, uint32_t x,
                                  uint32_t y) NORETURN_ATTR;
static void c4pro_installer_fail2(const char *where, uint32_t x,
                                  uint32_t y)
{
    c4pro_installer_status("FAIL", where, x, y);
    c4pro_installer_halt();
}

static void c4pro_install_put16(unsigned char *p, uint32_t value)
{
    p[0] = value & 0xffu;
    p[1] = (value >> 8) & 0xffu;
}

static void c4pro_install_put32(unsigned char *p, uint32_t value)
{
    p[0] = value & 0xffu;
    p[1] = (value >> 8) & 0xffu;
    p[2] = (value >> 16) & 0xffu;
    p[3] = (value >> 24) & 0xffu;
}

static uint32_t c4pro_install_get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t c4pro_install_base_clusters(void)
{
    return (c4pro_installer_payload_size + C4PRO_INSTALL_CLUSTER_BYTES - 1u) /
           C4PRO_INSTALL_CLUSTER_BYTES;
}

static bool c4pro_install_cluster_in_file(uint32_t cluster, uint32_t first,
                                          uint32_t clusters)
{
    return cluster >= first && cluster < first + clusters;
}

static uint32_t c4pro_install_fat_entry(uint32_t cluster)
{
    uint32_t base_clusters = c4pro_install_base_clusters();
    uint32_t system_dir = 2u;
    uint32_t system00_dir = 3u;
    uint32_t base0_first = 4u;
    uint32_t base1_first = base0_first + base_clusters;

    if (cluster == 0)
        return 0xfff8u;
    if (cluster == 1 || cluster == system_dir || cluster == system00_dir)
        return 0xffffu;

    if (c4pro_install_cluster_in_file(cluster, base0_first, base_clusters))
    {
        if (cluster == base0_first + base_clusters - 1u)
            return 0xffffu;
        return cluster + 1u;
    }

    if (c4pro_install_cluster_in_file(cluster, base1_first, base_clusters))
    {
        if (cluster == base1_first + base_clusters - 1u)
            return 0xffffu;
        return cluster + 1u;
    }

    return 0;
}

static void c4pro_install_dirent(unsigned char *ent, const char name[11],
                                 uint8_t attr, uint32_t cluster,
                                 uint32_t size)
{
    memset(ent, 0, 32);
    memcpy(ent, name, 11);
    ent[11] = attr;
    c4pro_install_put16(ent + 26, cluster);
    c4pro_install_put32(ent + 28, size);
}

static void c4pro_install_fill_boot(unsigned char *sector)
{
    memset(sector, 0, 512);
    sector[0] = 0xeb;
    sector[1] = 0x3c;
    sector[2] = 0x90;
    memcpy(sector + 3, "MSDOS5.0", 8);
    c4pro_install_put16(sector + 11, C4PRO_INSTALL_BYTES_PER_SECTOR);
    sector[13] = C4PRO_INSTALL_SECTORS_PER_CLUSTER;
    c4pro_install_put16(sector + 14, C4PRO_INSTALL_RESERVED_SECTORS);
    sector[16] = C4PRO_INSTALL_FAT_COUNT;
    c4pro_install_put16(sector + 17, C4PRO_INSTALL_ROOT_ENTRIES);
    c4pro_install_put16(sector + 19, C4PRO_INSTALL_SYSTEM_SECTORS);
    sector[21] = 0xf8;
    c4pro_install_put16(sector + 22, C4PRO_INSTALL_SECTORS_PER_FAT);
    c4pro_install_put16(sector + 24, 63);
    c4pro_install_put16(sector + 26, 255);
    c4pro_install_put32(sector + 28, 0);
    c4pro_install_put32(sector + 32, 0);
    sector[36] = 0x80;
    sector[38] = 0x29;
    c4pro_install_put32(sector + 39, 0xc4000002u);
    memcpy(sector + 43, "C4 ROCKBOX ", 11);
    memcpy(sector + 54, "FAT16   ", 8);
    sector[510] = 0x55;
    sector[511] = 0xaa;
}

static void c4pro_install_fill_fat(uint32_t sector_lba, unsigned char *sector)
{
    uint32_t fat_sector = (sector_lba - C4PRO_INSTALL_FIRST_FAT_SECTOR) %
                          C4PRO_INSTALL_SECTORS_PER_FAT;
    uint32_t first_cluster = fat_sector * 256u;

    memset(sector, 0, 512);
    for (uint32_t i = 0; i < 256u; i++)
        c4pro_install_put16(sector + i * 2u,
                            c4pro_install_fat_entry(first_cluster + i));
}

static void c4pro_install_fill_root(unsigned char *sector)
{
    memset(sector, 0, 512);
    c4pro_install_dirent(sector + 0, "SYSTEM     ", 0x10, 2u, 0);
    c4pro_install_dirent(sector + 32, "SYSTEM00   ", 0x10, 3u, 0);
}

static void c4pro_install_fill_system_dir(unsigned char *sector,
                                          bool second_copy)
{
    uint32_t base_clusters = c4pro_install_base_clusters();
    uint32_t dir_cluster = second_copy ? 3u : 2u;
    uint32_t base_first = second_copy ? 4u + base_clusters : 4u;

    memset(sector, 0, 512);
    c4pro_install_dirent(sector + 0, ".          ", 0x10, dir_cluster, 0);
    c4pro_install_dirent(sector + 32, "..         ", 0x10, 0, 0);
    c4pro_install_dirent(sector + 64, "BASE    RKW", 0x20, base_first,
                         c4pro_installer_payload_size);
}

static void c4pro_install_fill_data(uint32_t sector_lba, unsigned char *sector)
{
    uint32_t cluster = 2u + (sector_lba - C4PRO_INSTALL_FIRST_DATA_SECTOR) /
                             C4PRO_INSTALL_SECTORS_PER_CLUSTER;
    uint32_t sector_in_cluster =
        (sector_lba - C4PRO_INSTALL_FIRST_DATA_SECTOR) %
        C4PRO_INSTALL_SECTORS_PER_CLUSTER;
    uint32_t base_clusters = c4pro_install_base_clusters();
    uint32_t base0_first = 4u;
    uint32_t base1_first = base0_first + base_clusters;
    uint32_t file_cluster;
    uint32_t file_off;

    memset(sector, 0, 512);
    if (cluster == 2u || cluster == 3u)
    {
        if (sector_in_cluster == 0)
            c4pro_install_fill_system_dir(sector, cluster == 3u);
        return;
    }

    if (c4pro_install_cluster_in_file(cluster, base0_first, base_clusters))
        file_cluster = cluster - base0_first;
    else if (c4pro_install_cluster_in_file(cluster, base1_first, base_clusters))
        file_cluster = cluster - base1_first;
    else
        return;

    file_off = file_cluster * C4PRO_INSTALL_CLUSTER_BYTES +
               sector_in_cluster * C4PRO_INSTALL_BYTES_PER_SECTOR;
    if (file_off < c4pro_installer_payload_size)
    {
        uint32_t n = c4pro_installer_payload_size - file_off;
        if (n > 512u)
            n = 512u;
        memcpy(sector, c4pro_installer_payload + file_off, n);
    }
}

static void c4pro_install_fill_sector(uint32_t sector_lba,
                                      unsigned char *sector)
{
    if (sector_lba == 0)
        c4pro_install_fill_boot(sector);
    else if (sector_lba >= C4PRO_INSTALL_FIRST_FAT_SECTOR &&
             sector_lba < C4PRO_INSTALL_FIRST_ROOT_SECTOR)
        c4pro_install_fill_fat(sector_lba, sector);
    else if (sector_lba >= C4PRO_INSTALL_FIRST_ROOT_SECTOR &&
             sector_lba < C4PRO_INSTALL_FIRST_DATA_SECTOR)
    {
        if (sector_lba == C4PRO_INSTALL_FIRST_ROOT_SECTOR)
            c4pro_install_fill_root(sector);
        else
            memset(sector, 0, 512);
    }
    else if (sector_lba < C4PRO_INSTALL_SYSTEM_SECTORS)
        c4pro_install_fill_data(sector_lba, sector);
    else
        memset(sector, 0, 512);
}

static void c4pro_install_fill_page(uint32_t page, unsigned char *page_buf)
{
    for (uint32_t sec = 0; sec < C4PRO_INSTALL_SECTORS_PER_PAGE; sec++)
        c4pro_install_fill_sector(page * C4PRO_INSTALL_SECTORS_PER_PAGE + sec,
                                  page_buf + sec * 512u);
}

#ifdef C4PRO_APP_BOOT_INSTALLER
#define C4PRO_BOOT_INSTALL_PAGE_BYTES 4224u
#define C4PRO_BOOT_INSTALL_DATA_BYTES 4096u
#define C4PRO_BOOT_INSTALL_SPARE_BYTES 128u
#define C4PRO_BOOT_INSTALL_PAGE_COUNT 30u

static void c4pro_app_boot_installer(void) NORETURN_ATTR;
static void c4pro_app_boot_installer(void)
{
    uint32_t crc = 0xffffffffu;

    if (c4pro_installer_payload_size !=
        C4PRO_BOOT_INSTALL_PAGE_COUNT * C4PRO_BOOT_INSTALL_PAGE_BYTES)
        c4pro_installer_fail("payload size",
                             (int)c4pro_installer_payload_size);

    c4pro_installer_status("boot install", "erase block0",
                           C4PRO_BOOT_INSTALL_PAGE_COUNT,
                           c4pro_installer_payload_size);

    if (c4pro_nand_erase_block(0, 0))
        c4pro_installer_fail("erase boot", 0);

    for (uint32_t page = 0; page < C4PRO_BOOT_INSTALL_PAGE_COUNT; page++)
    {
        const unsigned char *src = c4pro_installer_payload +
                                   page * C4PRO_BOOT_INSTALL_PAGE_BYTES;

        memcpy(c4pro_install_page, src, C4PRO_BOOT_INSTALL_DATA_BYTES);
        memcpy(c4pro_install_spare, src + C4PRO_BOOT_INSTALL_DATA_BYTES,
               C4PRO_BOOT_INSTALL_SPARE_BYTES);

        crc = c4pro_installer_crc32_update(crc, c4pro_install_page,
                                           C4PRO_BOOT_INSTALL_DATA_BYTES);
        crc = c4pro_installer_crc32_update(crc, c4pro_install_spare,
                                           C4PRO_BOOT_INSTALL_SPARE_BYTES);

        if (c4pro_nand_write_page(0, page, c4pro_install_page,
                                  c4pro_install_spare))
            c4pro_installer_fail("write boot", (int)page);

#ifdef C4PRO_APP_BOOT_INSTALLER_VERIFY_DATA
        if (c4pro_nand_read_linear_page(0, page, c4pro_install_verify))
            c4pro_installer_fail("read boot", (int)page);
        if (memcmp(c4pro_install_page, c4pro_install_verify,
                   C4PRO_BOOT_INSTALL_DATA_BYTES))
            c4pro_installer_fail("verify boot", (int)page);
#endif

        c4pro_installer_status("write boot", "native pages",
                               page + 1u, C4PRO_BOOT_INSTALL_PAGE_COUNT);
    }

    c4pro_installer_status("done", "boot native", crc ^ 0xffffffffu,
                           C4PRO_BOOT_INSTALL_PAGE_COUNT);
    c4pro_installer_halt();
}
#endif

#ifdef C4PRO_APP_NATIVE_ROCKBOX_INSTALLER
#define C4PRO_NATIVE_INSTALL_MAGIC 0x31494e43u /* CNI1 */
#define C4PRO_NATIVE_INSTALL_VERSION 1u
#define C4PRO_NATIVE_PAGE_BYTES 4224u
#define C4PRO_NATIVE_DATA_BYTES 4096u
#define C4PRO_NATIVE_SPARE_BYTES 128u
#define C4PRO_NATIVE_HEADER_BYTES 512u
#define C4PRO_NATIVE_RECORD_DATA_BYTES 512u
#define C4PRO_NATIVE_RECORD_BYTES 528u
#define C4PRO_NATIVE_RECORDS_PER_PAGE 4u
#ifdef C4PRO_NATIVE_PAYLOAD_FULLPAGE
#define C4PRO_NATIVE_PAYLOAD_BYTES_PER_PAGE C4PRO_NATIVE_DATA_BYTES
#else
#define C4PRO_NATIVE_PAYLOAD_BYTES_PER_PAGE \
    (C4PRO_NATIVE_RECORD_DATA_BYTES * C4PRO_NATIVE_RECORDS_PER_PAGE)
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_START
#define C4PRO_NATIVE_PAYLOAD_PBLK_START 512u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_ALT1
#define C4PRO_NATIVE_PAYLOAD_PBLK_ALT1 640u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_ALT2
#define C4PRO_NATIVE_PAYLOAD_PBLK_ALT2 768u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_ALT3
#define C4PRO_NATIVE_PAYLOAD_PBLK_ALT3 896u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_ALT4
#define C4PRO_NATIVE_PAYLOAD_PBLK_ALT4 1024u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_ALT5
#define C4PRO_NATIVE_PAYLOAD_PBLK_ALT5 1152u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_ALT6
#define C4PRO_NATIVE_PAYLOAD_PBLK_ALT6 1280u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PBLK_ALT7
#define C4PRO_NATIVE_PAYLOAD_PBLK_ALT7 1408u
#endif
#ifndef C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE
#define C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE C4PRO_INSTALL_PAGES_PER_BLOCK
#endif

static const uint32_t c4pro_native_payload_pblk_candidates[] = {
    C4PRO_NATIVE_PAYLOAD_PBLK_START,
    C4PRO_NATIVE_PAYLOAD_PBLK_ALT1,
    C4PRO_NATIVE_PAYLOAD_PBLK_ALT2,
    C4PRO_NATIVE_PAYLOAD_PBLK_ALT3,
    C4PRO_NATIVE_PAYLOAD_PBLK_ALT4,
    C4PRO_NATIVE_PAYLOAD_PBLK_ALT5,
    C4PRO_NATIVE_PAYLOAD_PBLK_ALT6,
    C4PRO_NATIVE_PAYLOAD_PBLK_ALT7,
};

static uint32_t c4pro_native_fail_candidate;
static uint32_t c4pro_native_fail_page;
static uint32_t c4pro_native_fail_off;
static uint32_t c4pro_native_fail_expact;

static void c4pro_native_fail_payload(void) NORETURN_ATTR;
static void c4pro_native_fail_payload(void)
{
    uint32_t pblk = 0;

    if (c4pro_native_fail_candidate <
        ARRAYLEN(c4pro_native_payload_pblk_candidates))
        pblk = c4pro_native_payload_pblk_candidates[c4pro_native_fail_candidate] +
               c4pro_native_fail_page / C4PRO_INSTALL_PAGES_PER_BLOCK;

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "FAIL payload");
    lcd_putsf(0, 1, "cand %lu",
              (unsigned long)c4pro_native_fail_candidate);
    lcd_putsf(0, 2, "pblk %lu pg %lu", (unsigned long)pblk,
              (unsigned long)c4pro_native_fail_page);
    lcd_putsf(0, 3, "off %lu", (unsigned long)c4pro_native_fail_off);
    lcd_putsf(0, 4, "exp %02lx act %02lx",
              (unsigned long)(c4pro_native_fail_expact >> 8),
              (unsigned long)(c4pro_native_fail_expact & 0xffu));
    lcd_update();
    c4pro_installer_halt();
}

static uint32_t c4pro_native_crc_region(const unsigned char *buf,
                                        uint32_t len)
{
    return c4pro_installer_crc32_update(0xffffffffu, buf, len) ^
           0xffffffffu;
}

static uint32_t c4pro_native_nand_read_linear_page(uint32_t page, void *data)
{
    int old_irq = disable_irq_save();
    uint32_t rc = c4pro_nand_read_linear_page(0, page, data);
    restore_irq(old_irq);
    return rc;
}

static uint32_t c4pro_native_nand_read_sector_spare(uint32_t page,
                                                    uint32_t sector,
                                                    void *spare)
{
    int old_irq = disable_irq_save();
    uint32_t rc = c4pro_nand_read_sector(0, page, sector, NULL, spare);
    restore_irq(old_irq);
    return rc;
}

static uint32_t c4pro_native_nand_write_page(uint32_t page,
                                             const void *data,
                                             const void *spare)
{
    int old_irq = disable_irq_save();
    uint32_t rc = c4pro_nand_write_page(0, page, data, spare);
    restore_irq(old_irq);
    return rc;
}

static uint32_t c4pro_native_nand_erase_block(uint32_t page)
{
    int old_irq = disable_irq_save();
    uint32_t rc = c4pro_nand_erase_block(0, page);
    restore_irq(old_irq);
    return rc;
}

static int c4pro_native_write_pages(const char *label, uint32_t start_page,
                                    const unsigned char *pages,
                                    uint32_t page_count, bool fatal)
{
    for (uint32_t page = 0; page < page_count; page++)
    {
        const unsigned char *src = pages + page * C4PRO_NATIVE_PAGE_BYTES;
        bool verified = false;

        memcpy(c4pro_install_page, src, C4PRO_NATIVE_DATA_BYTES);
        memcpy(c4pro_install_spare, src + C4PRO_NATIVE_DATA_BYTES,
               C4PRO_NATIVE_SPARE_BYTES);

        for (uint32_t attempt = 0; attempt < 4u && !verified; attempt++)
        {
            verified = true;

            if (c4pro_native_nand_write_page(start_page + page,
                                             c4pro_install_page,
                                             c4pro_install_spare))
            {
                if (!fatal)
                    return -(int)(page + 1u);
                c4pro_installer_fail(label, (int)page);
            }

            if (c4pro_native_nand_read_linear_page(start_page + page,
                                                   c4pro_install_verify))
            {
                if (!fatal)
                    return -(int)(page + 1u);
                c4pro_installer_fail("verify read", (int)page);
            }

            if (memcmp(c4pro_install_page, c4pro_install_verify,
                       C4PRO_NATIVE_DATA_BYTES))
            {
                verified = false;

                if (attempt + 1u >= 4u)
                {
                    for (uint32_t off = 0; off < C4PRO_NATIVE_DATA_BYTES; off++)
                    {
                        if (c4pro_install_page[off] !=
                            c4pro_install_verify[off])
                        {
                            if (!fatal)
                                return -(int)(page + 1u);
                            c4pro_installer_status("FAIL", "data pg/off",
                                                   start_page + page,
                                                   (page << 16) | off);
                            sleep(2 * HZ);
                            c4pro_installer_fail2("exp/act off", off,
                                ((uint32_t)c4pro_install_page[off] << 8) |
                                c4pro_install_verify[off]);
                        }
                    }
                    c4pro_installer_fail("verify data", (int)page);
                }

                continue;
            }

            for (uint32_t sector = 0; sector < 8; sector++)
            {
                if (c4pro_native_nand_read_sector_spare(
                        start_page + page, sector,
                        c4pro_install_verify_spare + sector * 16u))
                {
                    if (!fatal)
                        return -(int)(page + 1u);
                    c4pro_installer_fail("verify spare read", (int)page);
                }
            }

            if (memcmp(c4pro_install_spare, c4pro_install_verify_spare,
                       C4PRO_NATIVE_SPARE_BYTES))
            {
                verified = false;

                if (attempt + 1u >= 4u)
                {
                    for (uint32_t off = 0; off < C4PRO_NATIVE_SPARE_BYTES;
                         off++)
                    {
                        if (c4pro_install_spare[off] !=
                            c4pro_install_verify_spare[off])
                        {
                            if (!fatal)
                                return -(int)(page + 1u);
                            c4pro_installer_status("FAIL", "spare pg/off",
                                                   start_page + page,
                                                   (page << 16) | off);
                            sleep(2 * HZ);
                            c4pro_installer_fail2("exp/act off", off,
                                ((uint32_t)c4pro_install_spare[off] << 8) |
                                c4pro_install_verify_spare[off]);
                        }
                    }
                    c4pro_installer_fail("verify spare", (int)page);
                }

                continue;
            }
        }

        if (!verified)
        {
            if (!fatal)
                return -(int)(page + 1u);
            c4pro_installer_fail("verify data", (int)page);
        }

        if ((page & 0x1fu) == 0 || page + 1u == page_count)
            c4pro_installer_status(label, "pages", page + 1u, page_count);
    }

    return 0;
}

static int c4pro_native_write_payload_pages(const char *label,
                                            uint32_t start_page,
                                            uint32_t candidate,
                                            const unsigned char *pages,
                                            uint32_t page_count,
                                            bool fatal)
{
    for (uint32_t page = 0; page < page_count; page++)
    {
        uint32_t phys_page = start_page +
                             page * C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE;
        const unsigned char *src = pages + page * C4PRO_NATIVE_PAGE_BYTES;
        const unsigned char *spare = src + C4PRO_NATIVE_DATA_BYTES;
        bool verified = false;

        memcpy(c4pro_install_page, src, C4PRO_NATIVE_DATA_BYTES);
        memcpy(c4pro_install_spare, spare, C4PRO_NATIVE_SPARE_BYTES);

        for (uint32_t attempt = 0; attempt < 4u && !verified; attempt++)
        {
            verified = true;

            if (c4pro_native_nand_write_page(phys_page,
                                             c4pro_install_page,
                                             c4pro_install_spare))
            {
                if (!fatal)
                    return -(int)(page + 1u);
                c4pro_installer_fail(label, (int)page);
            }

            if (c4pro_native_nand_read_linear_page(phys_page,
                                                   c4pro_install_verify))
            {
                if (!fatal)
                    return -(int)(page + 1u);
                c4pro_installer_fail("payload read", (int)page);
            }

#ifdef C4PRO_NATIVE_PAYLOAD_FULLPAGE
            for (uint32_t off = 0; off < C4PRO_NATIVE_DATA_BYTES; off++)
            {
                if (src[off] != c4pro_install_verify[off])
                {
                    verified = false;
                    c4pro_native_fail_candidate = candidate;
                    c4pro_native_fail_page = page;
                    c4pro_native_fail_off = off;
                    c4pro_native_fail_expact =
                        ((uint32_t)src[off] << 8) |
                        c4pro_install_verify[off];
                    break;
                }
            }
#else
            for (uint32_t record = 0; record < C4PRO_NATIVE_RECORDS_PER_PAGE;
                 record++)
            {
                uint32_t base = record * C4PRO_NATIVE_RECORD_BYTES;

                for (uint32_t i = 0; i < C4PRO_NATIVE_RECORD_DATA_BYTES; i++)
                {
                    uint32_t off = base + i;

                    if (src[off] != c4pro_install_verify[off])
                    {
                        verified = false;
                        c4pro_native_fail_candidate = candidate;
                        c4pro_native_fail_page = page;
                        c4pro_native_fail_off = off;
                        c4pro_native_fail_expact =
                            ((uint32_t)src[off] << 8) |
                            c4pro_install_verify[off];
                        break;
                    }
                }

                if (!verified)
                    break;
            }
#endif
        }

        if (!verified)
        {
            if (!fatal)
                return -(int)(page + 1u);
            c4pro_native_fail_payload();
        }

        if ((page & 0x1fu) == 0 || page + 1u == page_count)
            c4pro_installer_status(label, "payload pages",
                                   page + 1u, page_count);
    }

    return 0;
}

static int c4pro_native_crc_payload_region(uint32_t start_page,
                                           uint32_t byte_count,
                                           uint32_t expected_crc,
                                           uint32_t *actual_crc)
{
    uint32_t done = 0;
    uint32_t crc = 0xffffffffu;

    while (done < byte_count)
    {
        if (c4pro_native_nand_read_linear_page(start_page,
                                               c4pro_install_verify))
            return -1;

#ifdef C4PRO_NATIVE_PAYLOAD_FULLPAGE
        {
            uint32_t n = byte_count - done;
            if (n > C4PRO_NATIVE_DATA_BYTES)
                n = C4PRO_NATIVE_DATA_BYTES;

            crc = c4pro_installer_crc32_update(crc,
                                               c4pro_install_verify, n);
            done += n;
        }
#else
        for (uint32_t record = 0;
             record < C4PRO_NATIVE_RECORDS_PER_PAGE && done < byte_count;
             record++)
        {
            uint32_t n = byte_count - done;
            uint32_t off = record * C4PRO_NATIVE_RECORD_BYTES;

            if (n > C4PRO_NATIVE_RECORD_DATA_BYTES)
                n = C4PRO_NATIVE_RECORD_DATA_BYTES;

            crc = c4pro_installer_crc32_update(crc,
                                               c4pro_install_verify + off, n);
            done += n;
        }
#endif

        start_page += C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE;
    }

    *actual_crc = crc ^ 0xffffffffu;
    return *actual_crc == expected_crc ? 0 : -2;
}

static bool c4pro_native_erase_pages(uint32_t start_page, uint32_t page_count)
{
    for (uint32_t page = 0; page < page_count; page++)
    {
        uint32_t phys_page = start_page +
                             page * C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE;

        if (c4pro_native_nand_erase_block(phys_page))
            return false;
    }

    return true;
}

static int c4pro_native_verify_blank_payload(uint32_t start_page,
                                             uint32_t page_count)
{
    for (uint32_t page = 0; page < page_count; page++)
    {
        uint32_t phys_page = start_page +
                             page * C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE;

        if (c4pro_native_nand_read_linear_page(phys_page,
                                               c4pro_install_verify))
            return -(int)(page + 1u);

#ifdef C4PRO_NATIVE_PAYLOAD_FULLPAGE
        for (uint32_t i = 0; i < C4PRO_NATIVE_DATA_BYTES; i++)
        {
            if (c4pro_install_verify[i] != 0xffu)
                return (int)(page + 1u);
        }
#else
        for (uint32_t record = 0; record < C4PRO_NATIVE_RECORDS_PER_PAGE;
             record++)
        {
            uint32_t base = record * C4PRO_NATIVE_RECORD_BYTES;

            for (uint32_t i = 0; i < C4PRO_NATIVE_RECORD_DATA_BYTES; i++)
            {
                if (c4pro_install_verify[base + i] != 0xffu)
                    return (int)(page + 1u);
            }
        }
#endif
    }

    return 0;
}

static void c4pro_app_native_rockbox_installer(void) NORETURN_ATTR;
static void c4pro_app_native_rockbox_installer(void)
{
    const unsigned char *hdr = c4pro_installer_payload;
    uint32_t magic = c4pro_install_get32(hdr + 0);
    uint32_t version = c4pro_install_get32(hdr + 4);
    uint32_t boot_pages = c4pro_install_get32(hdr + 8);
    uint32_t payload_pages = c4pro_install_get32(hdr + 12);
    uint32_t boot_crc = c4pro_install_get32(hdr + 16);
    uint32_t payload_crc = c4pro_install_get32(hdr + 20);
    uint32_t boot_bytes = c4pro_install_get32(hdr + 24);
    uint32_t payload_bytes = c4pro_install_get32(hdr + 28);
    const unsigned char *boot_image;
    const unsigned char *payload_image;
    uint32_t expected_size;
    uint32_t payload_start_page = 0;
    uint32_t last_payload_start_page = 0;
    uint32_t boot_payload_pages;
    uint32_t last_payload_pblk = 0;
    uint32_t last_payload_stage = 0;
    uint32_t last_payload_detail = 0;
    uint32_t last_payload_expected = 0;

    if (magic != C4PRO_NATIVE_INSTALL_MAGIC ||
        version != C4PRO_NATIVE_INSTALL_VERSION)
        c4pro_installer_fail("native header", (int)magic);

    expected_size = C4PRO_NATIVE_HEADER_BYTES + boot_bytes + payload_bytes;
    if (boot_pages == 0 || payload_pages == 0 ||
        boot_bytes != boot_pages * C4PRO_NATIVE_PAGE_BYTES ||
        payload_bytes != payload_pages * C4PRO_NATIVE_PAGE_BYTES ||
        expected_size != c4pro_installer_payload_size)
        c4pro_installer_fail("native size", (int)c4pro_installer_payload_size);

    boot_image = c4pro_installer_payload + C4PRO_NATIVE_HEADER_BYTES;
    payload_image = boot_image + boot_bytes;

    if (c4pro_native_crc_region(boot_image, boot_bytes) != boot_crc)
        c4pro_installer_fail("boot crc", (int)boot_crc);
    if (c4pro_native_crc_region(payload_image, payload_bytes) != payload_crc)
        c4pro_installer_fail("payload crc", (int)payload_crc);

    boot_payload_pages =
        (c4pro_install_get32(payload_image + 8) +
         C4PRO_NATIVE_PAYLOAD_BYTES_PER_PAGE - 1u) /
        C4PRO_NATIVE_PAYLOAD_BYTES_PER_PAGE;

    c4pro_installer_status("native install", "find payload",
                           payload_pages, boot_pages);
    for (uint32_t candidate = 0;
         candidate < ARRAYLEN(c4pro_native_payload_pblk_candidates);
         candidate++)
    {
        uint32_t start_page =
            c4pro_native_payload_pblk_candidates[candidate] *
            C4PRO_INSTALL_PAGES_PER_BLOCK;
        bool erase_ok = true;

        last_payload_start_page = start_page;
        c4pro_installer_status("native install", "erase payload",
                               c4pro_native_payload_pblk_candidates[candidate],
                               candidate);
        erase_ok = c4pro_native_erase_pages(start_page, payload_pages);

        if (!erase_ok)
        {
            last_payload_pblk = c4pro_native_payload_pblk_candidates[candidate];
            last_payload_stage = 1;
            last_payload_detail = start_page;
            last_payload_expected = 0;
            continue;
        }

        int blank_rc = c4pro_native_verify_blank_payload(start_page,
                                                         payload_pages);
        if (blank_rc)
        {
            last_payload_pblk = c4pro_native_payload_pblk_candidates[candidate];
            last_payload_stage = 5;
            last_payload_detail = (uint32_t)(blank_rc < 0 ? -blank_rc :
                                             blank_rc);
            last_payload_expected = 0;
            c4pro_installer_status("payload bad", "erase blank",
                                   last_payload_pblk, last_payload_detail);
            continue;
        }

        int write_rc = c4pro_native_write_payload_pages("payload ok",
                                                        start_page,
                                                        candidate,
                                                        payload_image,
                                                        payload_pages,
                                                        false);
        last_payload_pblk = c4pro_native_payload_pblk_candidates[candidate];
        if (write_rc == 0)
        {
            uint32_t actual_boot_crc = 0;
            uint32_t actual_app_crc = 0;
            uint32_t expected_boot_crc = c4pro_install_get32(payload_image + 16);
            uint32_t expected_app_crc = c4pro_install_get32(payload_image + 20);
            uint32_t boot_size = c4pro_install_get32(payload_image + 8);
            uint32_t app_size = c4pro_install_get32(payload_image + 12);
            int boot_rc;
            int app_rc;

            boot_rc = c4pro_native_crc_payload_region(
                start_page + C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE,
                boot_size, expected_boot_crc, &actual_boot_crc);
            if (boot_rc == 0 && app_size != 0)
                app_rc = c4pro_native_crc_payload_region(
                    start_page + (1u + boot_payload_pages) *
                                     C4PRO_NATIVE_PAYLOAD_PAGE_STRIDE,
                    app_size, expected_app_crc, &actual_app_crc);
            else if (boot_rc == 0)
                app_rc = 0;
            else
                app_rc = -99;

            if (boot_rc == 0 && app_rc == 0)
            {
                payload_start_page = start_page;
                break;
            }

            if (boot_rc)
            {
                last_payload_stage = 3;
                last_payload_detail = actual_boot_crc;
                last_payload_expected = expected_boot_crc;
                c4pro_installer_status("payload bad", "boot crc",
                                       last_payload_pblk, actual_boot_crc);
            }
            else
            {
                last_payload_stage = 4;
                last_payload_detail = actual_app_crc;
                last_payload_expected = expected_app_crc;
                c4pro_installer_status("payload bad", "app crc",
                                       last_payload_pblk, actual_app_crc);
            }
        }
        else
        {
            last_payload_stage = 2;
            last_payload_detail = (uint32_t)(-write_rc);
            last_payload_expected = 0;
            c4pro_installer_status("payload bad", "write page",
                                   last_payload_pblk, last_payload_detail);
        }
    }

    if (payload_start_page == 0)
    {
        lcd_setfont(FONT_SYSFIXED);
        lcd_clear_display();
        lcd_puts(0, 0, "FAIL payload");
        lcd_putsf(0, 1, "pblk %lu stage %lu",
                  (unsigned long)last_payload_pblk,
                  (unsigned long)last_payload_stage);
        lcd_putsf(0, 2, "page %lu", (unsigned long)last_payload_start_page);
        lcd_putsf(0, 3, "exp %08lx", (unsigned long)last_payload_expected);
        lcd_putsf(0, 4, "act %08lx", (unsigned long)last_payload_detail);
        lcd_update();
        c4pro_installer_halt();
    }

    c4pro_installer_status("native install", "erase boot", 0, boot_pages);
    if (c4pro_native_nand_erase_block(0))
        c4pro_installer_fail("erase boot", 0);

    c4pro_native_write_pages("boot ok", 0, boot_image, boot_pages, true);

    c4pro_installer_status("done", "native", payload_start_page, boot_pages);
    c4pro_installer_halt();
}
#endif

#ifdef C4PRO_APP_LOADPATH_CORRUPTOR
static void c4pro_app_loadpath_corruptor(void) NORETURN_ATTR;
static void c4pro_app_loadpath_corruptor(void)
{
#ifdef C4PRO_APP_LOADPATH_SCAN
    int hits[8];
    unsigned hit_count = 0;

    for (unsigned i = 0; i < ARRAYLEN(hits); i++)
        hits[i] = -1;

    lcd_setfont(FONT_SYSFIXED);
    c4pro_installer_status("scan base", "wait", 0, 0);

    for (uint32_t psec = 0; psec < 1048576u && hit_count < ARRAYLEN(hits);
         psec++)
    {
        uint32_t page = psec / C4PRO_INSTALL_SECTORS_PER_PAGE;
        uint32_t sector = psec % C4PRO_INSTALL_SECTORS_PER_PAGE;

        if (c4pro_nand_read_linear_page(0, page, c4pro_install_page))
            continue;

        for (uint32_t off = sector * 512u; off + 11u <= sector * 512u + 512u;
             off += 32u)
        {
            if (!memcmp(c4pro_install_page + off, "BASE    RKW", 11))
            {
                hits[hit_count++] = (int)psec;
                break;
            }
        }

        if ((psec & 0x3fffu) == 0)
            c4pro_installer_status("scan base", "psec", psec, hit_count);
    }

    lcd_clear_display();
    lcd_putsf(0, 0, "BASE hits %u", hit_count);
    for (unsigned i = 0; i < ARRAYLEN(hits) && i < 7; i++)
        lcd_putsf(0, i + 1, "%u p%d", i, hits[i]);
    lcd_update();
    c4pro_installer_halt();
#else
    static const uint32_t target_psecs[] = { 324369u, 331461u };
    static const char *target_names[] = { "SYSTEM", "SYSTEM00" };
    uint32_t crc = 0xffffffffu;
    const unsigned char zero = 0;

    for (unsigned target = 0; target < ARRAYLEN(target_psecs); target++)
    {
        uint32_t psec = target_psecs[target];
        uint32_t page = psec / C4PRO_INSTALL_SECTORS_PER_PAGE;
        uint32_t sector = psec % C4PRO_INSTALL_SECTORS_PER_PAGE;
        uint32_t sector_base = sector * 512u;
        uint32_t column = 0xffffffffu;

        if (c4pro_nand_read_linear_page(0, page, c4pro_install_page))
            c4pro_installer_fail("read page", (int)page);

        for (uint32_t off = sector_base; off < sector_base + 512u; off += 32u)
        {
            if (!memcmp(c4pro_install_page + off, "BASE    RKW", 11))
            {
                column = off;
                break;
            }
        }

        if (column == 0xffffffffu)
            c4pro_installer_fail("no base ent", (int)page);

        crc = c4pro_installer_crc32_update(crc, c4pro_install_page,
                                           sizeof(c4pro_install_page));

        if (c4pro_nand_program_bytes(0, page, column, &zero, 1))
            c4pro_installer_fail("patch byte", (int)page);

        if (c4pro_nand_read_linear_page(0, page, c4pro_install_verify))
            c4pro_installer_fail("verify read", (int)page);

        if (c4pro_install_verify[column] != 0)
            c4pro_installer_fail("verify byte", (int)page);

        c4pro_installer_status("hide base", target_names[target],
                               page, psec);
        sleep(2 * HZ);
    }

    c4pro_installer_status("done", "hide base", crc ^ 0xffffffffu,
                           ARRAYLEN(target_psecs));
    c4pro_installer_halt();
#endif
}
#endif

static void c4pro_app_system_installer(void) NORETURN_ATTR;
static void c4pro_app_system_installer(void)
{
    uint32_t image_pages = C4PRO_INSTALL_SYSTEM_SECTORS /
                           C4PRO_INSTALL_SECTORS_PER_PAGE;
    uint32_t start_page = C4PRO_INSTALL_SYSTEM_PSEC_BASE /
                          C4PRO_INSTALL_SECTORS_PER_PAGE;
    uint32_t first_block = start_page / C4PRO_INSTALL_PAGES_PER_BLOCK;
    uint32_t last_page = start_page + image_pages - 1u;
    uint32_t last_block = last_page / C4PRO_INSTALL_PAGES_PER_BLOCK;
    uint32_t crc = 0xffffffffu;

    c4pro_installer_status("embedded", "BASE.RKW", image_pages,
                           c4pro_installer_payload_size);

#ifdef C4PRO_APP_SYSTEM_INSTALLER_STOCK_RAW_WRITE
    for (uint32_t block = first_block; block <= last_block; block++)
    {
        uint32_t page = block * C4PRO_INSTALL_PAGES_PER_BLOCK;
        if (c4pro_nand_erase_block(0, page))
            c4pro_installer_fail("erase", (int)block);
        if ((block & 3u) == 0)
            c4pro_installer_status("erase", "stock raw", block, last_block);
    }
#else
    c4pro_installer_status("verify-only", "no erase", first_block, last_block);
#endif

    memset(c4pro_install_spare, 0xff, sizeof(c4pro_install_spare));
    for (uint32_t page = 0; page < image_pages; page++)
    {
        c4pro_install_fill_page(page, c4pro_install_page);
        crc = c4pro_installer_crc32_update(crc, c4pro_install_page,
                                           sizeof(c4pro_install_page));

#ifdef C4PRO_APP_SYSTEM_INSTALLER_STOCK_RAW_WRITE
        if (c4pro_nand_write_page(0, start_page + page, c4pro_install_page,
                                  c4pro_install_spare))
            c4pro_installer_fail("write", (int)page);

        if (c4pro_nand_read_linear_page(0, start_page + page,
                                        c4pro_install_verify))
            c4pro_installer_fail("readback", (int)page);

        if (memcmp(c4pro_install_page, c4pro_install_verify,
                   sizeof(c4pro_install_page)))
            c4pro_installer_fail("verify", (int)page);
#endif

        if ((page & 0x7fu) == 0)
            c4pro_installer_status("page", "system image", page, image_pages);
    }

    c4pro_installer_status("done",
#ifdef C4PRO_APP_SYSTEM_INSTALLER_STOCK_RAW_WRITE
                           "raw stock write",
#else
                           "verified embed",
#endif
                           crc ^ 0xffffffffu, image_pages);
    c4pro_installer_halt();
}
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

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    (defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE))
extern volatile unsigned c4pro_usb_diag_irq_count;
extern volatile unsigned c4pro_usb_diag_reset_count;
extern volatile unsigned c4pro_usb_diag_conn_count;
extern volatile unsigned c4pro_usb_diag_setup_count;
extern volatile unsigned c4pro_usb_diag_in0_count;
extern volatile unsigned c4pro_usb_diag_out0_count;
extern volatile unsigned c4pro_usb_diag_last_intsrc;
extern volatile unsigned c4pro_usb_diag_last_dev_info;
extern volatile unsigned c4pro_usb_diag_last_setup0;
extern volatile unsigned c4pro_usb_diag_last_setup1;
extern volatile unsigned c4pro_usb_diag_last_tx0stat;
extern volatile unsigned c4pro_usb_diag_last_rx0stat;
extern volatile unsigned c4pro_usb_diag_last_tx0con;
extern volatile unsigned c4pro_usb_diag_last_rx0con;
extern volatile unsigned c4pro_usb_diag_outn_count;
extern volatile unsigned c4pro_usb_diag_last_outn;
extern volatile unsigned c4pro_usb_diag_last_outn_stat;
extern volatile unsigned c4pro_usb_diag_last_outn_con;
extern volatile unsigned c4pro_usb_diag_live_dev_ctl;
extern volatile unsigned c4pro_usb_diag_live_dev_info;
extern volatile unsigned c4pro_usb_diag_live_en_int;
extern volatile unsigned c4pro_usb_diag_live_intcon;
extern volatile unsigned c4pro_usb_diag_live_int2flag;
extern volatile unsigned c4pro_usb_diag_live_tx0stat;
extern volatile unsigned c4pro_usb_diag_live_rx0stat;
extern volatile unsigned c4pro_usb_diag_live_tx0con;
extern volatile unsigned c4pro_usb_diag_live_rx0con;
extern volatile unsigned c4pro_usb_diag_live_scu_clkcfg;
extern volatile unsigned c4pro_usb_diag_live_scu_rstcfg;
extern volatile unsigned c4pro_usb_diag_live_scu_iomuxa;
extern volatile unsigned c4pro_usb_diag_live_scu_iomuxb;
extern volatile int c4pro_usb_diag_ep0_state;
extern volatile unsigned c4pro_usb_diag_xfer_count;
extern volatile unsigned c4pro_usb_diag_response_count;
extern volatile unsigned c4pro_usb_diag_last_xfer;
extern volatile unsigned c4pro_usb_diag_last_response;
extern volatile unsigned c4pro_usb_diag_core_state;
extern volatile unsigned c4pro_usb_diag_core_config;
extern volatile unsigned c4pro_usb_diag_core_storage;
extern volatile unsigned c4pro_usb_diag_core_interfaces;
extern volatile unsigned c4pro_usb_storage_diag_xfer_count;
extern volatile unsigned c4pro_usb_storage_diag_init_count;
extern volatile unsigned c4pro_usb_storage_diag_arm_count;
extern volatile unsigned c4pro_usb_storage_diag_last_xfer;
extern volatile unsigned c4pro_usb_storage_diag_cbw_count;
extern volatile unsigned c4pro_usb_storage_diag_bad_sig;
extern volatile unsigned c4pro_usb_storage_diag_last_cmd;
extern volatile unsigned c4pro_usb_storage_diag_last_lun;
extern volatile unsigned c4pro_usb_storage_diag_last_len;
extern volatile unsigned c4pro_usb_storage_diag_block_size;
extern volatile unsigned c4pro_usb_storage_diag_block_count;
extern volatile unsigned c4pro_usb_storage_diag_exclusive;
extern volatile unsigned c4pro_usb_storage_diag_lun_present;
extern volatile unsigned c4pro_usb_storage_diag_state;
extern volatile unsigned c4pro_usb_storage_diag_write_cmd_count;
extern volatile unsigned c4pro_usb_storage_diag_write_lba;
extern volatile unsigned c4pro_usb_storage_diag_write_blocks;
extern volatile unsigned c4pro_usb_storage_diag_write_arm_size;
extern volatile unsigned c4pro_usb_storage_diag_recv_blocks_count;
extern volatile unsigned c4pro_usb_storage_diag_recv_length;
extern volatile unsigned c4pro_usb_storage_diag_recv_status;
extern volatile unsigned c4pro_usb_storage_diag_unexpected_length;
extern volatile unsigned c4pro_usb_storage_diag_storage_write_calls;
extern volatile int c4pro_usb_storage_diag_storage_write_result;
extern int c4own_diag_format_count;
extern int c4own_diag_write_count;
extern int c4own_diag_read_count;
extern int c4own_diag_last_write_sector;
extern int c4own_diag_last_write_count;
extern int c4own_diag_last_read_sector;
extern int c4own_diag_map0;
extern int c4own_diag_map2;
extern int c4own_diag_lba0_sig;
extern int c4own_diag_lba2048_sig;
extern int c4own_diag_last_write_result;
extern int c4own_diag_last_writeback_sig;
extern int c4own_diag_cache_flush_count;
extern int c4own_diag_cache_logical;
extern void c4pro_usb_diag_sample(void);
extern void c4pro_usb_diag_force_reattach(void);
extern void c4pro_usb_diag_enter_storage_mode(void);
extern void c4pro_usb_diag_exit_storage_mode(void);
extern void c4pro_usb_diag_force_storage_config(void);
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_FTL_SYNTH_AUDIT)
extern int c4pro_ftl_audit_total;
extern int c4pro_ftl_audit_ok;
extern int c4pro_ftl_audit_missing;
extern int c4pro_ftl_audit_bad;
extern int c4pro_ftl_audit_first_lba;
extern int c4pro_ftl_audit_first_psec;
extern int c4pro_ftl_audit_first_mode;
extern int c4pro_ftl_audit_first_expected;
extern int c4pro_ftl_audit_first_actual;
extern void c4pro_ftl_synth_audit(void);
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
extern int c4pro_ftl_diag_hash_bootbin_psec;
extern int c4pro_ftl_diag_hash_version_psec;
extern int c4pro_ftl_diag_hash_rk28_psec;
extern int c4pro_ftl_diag_hash_boot_mode;
extern int c4pro_ftl_diag_hash_fat_mode;
extern int c4pro_ftl_diag_hash_root_mode;
extern int c4pro_ftl_diag_hash_bootbin_mode;
extern int c4pro_ftl_diag_hash_version_mode;
extern int c4pro_ftl_diag_hash_rk28_mode;
extern int c4pro_ftl_diag_hash_best_delta;
extern int c4pro_ftl_diag_hash_best_delta_count;
extern int c4pro_ftl_diag_sig_fat_psec;
extern int c4pro_ftl_diag_sig_root_psec;
extern int c4pro_ftl_diag_sig_fat_mode;
extern int c4pro_ftl_diag_sig_root_mode;
extern int c4pro_ftl_diag_synth_sectors;
#ifdef C4PRO_FTL_LBA_DIAG
extern int c4pro_ftl_diag_probe_lba[4];
extern int c4pro_ftl_diag_probe_psec[4];
extern int c4pro_ftl_diag_probe_inferred[4];
extern int c4pro_ftl_diag_probe_mode[4];
extern int c4pro_ftl_diag_probe_synth[4];
#endif
#endif

#if !defined(SIMULATOR)
#define C4PRO_FTL_BASE_PAGE 2560
#define C4PRO_FTL_SCAN_PAGES 8192
#define C4PRO_FTL_SECTORS_PER_PAGE 8
#define C4PRO_FTL_PAGE_SIZE 4096
#define C4PRO_FTL_SPARE_SIZE 128
#define C4PRO_NAND_BLOCKS 8192
#define C4PRO_NAND_PAGES_PER_BLOCK 128
#define C4PRO_SYSTEM_BOOT_PSEC 20480
#define C4PRO_NAND_DIAG_BANKS 2

#ifdef C4PRO_STORAGE_VERDICT
#define C4PRO_ROOT_EXPECTED_COUNT 7

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
#endif

#ifdef C4PRO_FTL_PROBE
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
#endif

#ifdef C4PRO_FTL_TARGET_SCAN
#ifndef C4PRO_FTL_TARGET_SCAN_START
#define C4PRO_FTL_TARGET_SCAN_START 0u
#endif
#ifndef C4PRO_FTL_TARGET_SCAN_SECTORS
#define C4PRO_FTL_TARGET_SCAN_SECTORS 1048576u
#endif
#ifndef C4PRO_FTL_TARGET_SCAN_BANKS
#define C4PRO_FTL_TARGET_SCAN_BANKS 2
#endif

struct c4pro_target_scan_hit
{
    int bank;
    int psec;
    int mode;
    unsigned meta;
};

#ifdef C4PRO_FTL_NEXT_SECTOR_SCAN
#define C4PRO_TARGET_SCAN_COUNT 1
#else
#define C4PRO_TARGET_SCAN_COUNT 7
#endif

static uint32_t c4pro_probe_fnv1a_sector(const unsigned char *data)
{
    uint32_t hash = 2166136261u;

    for (int i = 0; i < SECTOR_SIZE; i++)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}

static uint32_t c4pro_probe_fnv1a_sector_xor(const unsigned char *data,
                                             const unsigned char *key)
{
    uint32_t hash = 2166136261u;

    for (int i = 0; i < SECTOR_SIZE; i++)
    {
        hash ^= data[i] ^ key[i];
        hash *= 16777619u;
    }

    return hash;
}

static void c4pro_probe_make_rk27_key(unsigned char *out, int size)
{
    static const unsigned char rk27_key[16] =
    {
        0x7c, 0x4e, 0x03, 0x04, 0x55, 0x05, 0x09, 0x07,
        0x2d, 0x2c, 0x7b, 0x38, 0x17, 0x0d, 0x17, 0x11,
    };
    unsigned char key1[256];
    unsigned char key2[256];
    int index = 0;

    for (int i = 0; i < 256; i++)
    {
        key1[i] = i;
        key2[i] = rk27_key[i & 0xf];
    }

    for (int i = 0; i < 256; i++)
    {
        unsigned char x = key1[i];
        index = (key1[i] + index + key2[i]) & 0xff;
        key1[i] = key1[index];
        key1[index] = x;
    }

    index = 0;
    for (int i = 0; i < size; i++)
    {
        unsigned char x = key1[(i + 1) & 0xff];
        index = (x + index) & 0xff;
        key1[(i + 1) & 0xff] = key1[index];
        key1[index] = x;
        out[i] = key1[(key1[(i + 1) & 0xff] + x) & 0xff];
    }
}

static bool c4pro_target_nand_id_present(const unsigned char id[5])
{
    bool any_zero = false;
    bool any_ff = false;
    bool any_other = false;

    for (int i = 0; i < 5; i++)
    {
        if (id[i] == 0x00)
            any_zero = true;
        else if (id[i] == 0xff)
            any_ff = true;
        else
            any_other = true;
    }

    return any_other && !(any_zero && !any_ff && !any_other);
}

static unsigned c4pro_target_spare_meta(const unsigned char *spare, int sec)
{
    const unsigned char *s = spare + sec * 16;

    return ((unsigned)s[0] << 16) | ((unsigned)s[1] << 8) | (unsigned)s[2];
}

static void c4pro_show_target_scan(void)
{
    static unsigned char page_data[C4PRO_FTL_PAGE_SIZE];
    static unsigned char page_spare[C4PRO_FTL_SPARE_SIZE];
    static unsigned char page_key[C4PRO_FTL_PAGE_SIZE];
    static bool scan_ran;
    static const uint32_t target_hashes[C4PRO_TARGET_SCAN_COUNT] =
    {
#ifdef C4PRO_FTL_NEXT_SECTOR_SCAN
        0xea0263adu, /* LBA 47489 */
#else
        0x0c3de6f2u, /* LBA 47488, /PDA Music/YoYo - v2.wav */
        0xea0263adu, /* LBA 47489 */
        0xfd2a323du, /* LBA 47490 */
        0xbf83b252u, /* LBA 47491 */
        0x88523b6du, /* LBA 47492 */
        0xe1bee0d3u, /* LBA 47493 */
        0xec79abe5u, /* LBA 47494 */
#endif
    };
    struct c4pro_target_scan_hit hits[C4PRO_TARGET_SCAN_COUNT] =
    {
        { -1, -1, -1, 0xffffff },
#ifndef C4PRO_FTL_NEXT_SECTOR_SCAN
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
#endif
    };
    uint32_t start = C4PRO_FTL_TARGET_SCAN_START;
    uint32_t end = start + C4PRO_FTL_TARGET_SCAN_SECTORS;
    int found = 0;

    if (scan_ran)
        return;
    scan_ran = true;

    c4pro_probe_make_rk27_key(page_key, sizeof(page_key));

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_putsf(0, 0, "TSCAN %u+%u", start, C4PRO_FTL_TARGET_SCAN_SECTORS);
    lcd_puts(0, 1, "wait");
    lcd_update();

    for (int bank = 0; bank < C4PRO_FTL_TARGET_SCAN_BANKS &&
         found < C4PRO_TARGET_SCAN_COUNT; bank++)
    {
        unsigned char id[5];

        if (c4pro_nand_read_id(bank, id) || !c4pro_target_nand_id_present(id))
            continue;

        for (uint32_t psec = start; psec < end &&
             found < C4PRO_TARGET_SCAN_COUNT; psec += 8)
        {
            uint32_t page = psec / C4PRO_FTL_SECTORS_PER_PAGE;

            if (nand_read_page(bank, page, page_data, page_spare, 1, 0))
                continue;

            if (((psec - start) & 0x3fffu) == 0)
            {
                lcd_putsf(0, 1, "b%d p%lu", bank,
                          (unsigned long)psec);
                lcd_putsf(0, 2, "f%d", found);
                lcd_update();
            }

            for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE; sec++)
            {
                const unsigned char *sector = page_data + sec * SECTOR_SIZE;
                uint32_t hashes[3];

                hashes[0] = c4pro_probe_fnv1a_sector(sector);
                hashes[1] = c4pro_probe_fnv1a_sector_xor(sector, page_key);
                hashes[2] = c4pro_probe_fnv1a_sector_xor(
                    sector, page_key + sec * SECTOR_SIZE);

                for (int target = 0; target < C4PRO_TARGET_SCAN_COUNT; target++)
                {
                    if (hits[target].psec >= 0)
                        continue;

                    for (int mode = 0; mode < 3; mode++)
                    {
                        if (hashes[mode] != target_hashes[target])
                            continue;

                        hits[target].bank = bank;
                        hits[target].psec = (int)psec + sec;
                        hits[target].mode = mode;
                        hits[target].meta =
                            c4pro_target_spare_meta(page_spare, sec);
                        found++;
                        break;
                    }
                }
            }
        }
    }

#ifdef C4PRO_FTL_NEXT_SECTOR_SCAN
    lcd_clear_display();
    if (hits[0].psec >= 0)
    {
        lcd_puts(0, 0, "NEXT F");
        lcd_putsf(0, 1, "b%d p%07d", hits[0].bank, hits[0].psec);
        lcd_putsf(0, 2, "m%d x%06x", hits[0].mode, hits[0].meta);
    }
    else
    {
        lcd_puts(0, 0, "NEXT N");
        lcd_putsf(0, 1, "%u+%u", start, C4PRO_FTL_TARGET_SCAN_SECTORS);
    }
#else
    lcd_clear_display();
    lcd_putsf(0, 0, "TSCAN done f%d", found);
    for (int i = 0; i < C4PRO_TARGET_SCAN_COUNT; i++)
    {
        lcd_putsf(0, i + 1, "%db%d p%07d m%d x%06x", i, hits[i].bank,
                  hits[i].psec, hits[i].mode, hits[i].meta);
    }
#endif
    lcd_puts(0, 7, "hold");
    lcd_update();

    button_clear_queue();
    while (true)
    {
        lcd_update();
        sleep(HZ / 2);
    }
}

static void c4pro_show_hit_page_probe(void)
{
    static unsigned char page_data[C4PRO_FTL_PAGE_SIZE];
    static unsigned char page_spare[C4PRO_FTL_SPARE_SIZE];
    static unsigned char page_key[C4PRO_FTL_PAGE_SIZE];
    uint32_t page = 297128u / C4PRO_FTL_SECTORS_PER_PAGE;
    extern int c4pro_ftl_diag_hash_lookup(uint32_t hash);

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "HITPAGE wait");
    lcd_update();
    c4pro_probe_make_rk27_key(page_key, sizeof(page_key));

    if (nand_read_page(0, page, page_data, page_spare, 1, 0))
    {
        lcd_clear_display();
        lcd_puts(0, 0, "HITPAGE read fail");
    }
    else
    {
        lcd_clear_display();
        lcd_putsf(0, 0, "HITPAGE p%lu", (unsigned long)(page * 8));
        for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE; sec++)
        {
            const unsigned char *data = page_data + sec * SECTOR_SIZE;
            uint32_t hashes[3];
            int lba = -1;
            int mode = -1;
            unsigned meta = c4pro_target_spare_meta(page_spare, sec);

            hashes[0] = c4pro_probe_fnv1a_sector(data);
            hashes[1] = c4pro_probe_fnv1a_sector_xor(data, page_key);
            hashes[2] = c4pro_probe_fnv1a_sector_xor(
                data, page_key + sec * SECTOR_SIZE);

            for (int probe_mode = 0; probe_mode < 3; probe_mode++)
            {
                lba = c4pro_ftl_diag_hash_lookup(hashes[probe_mode]);
                if (lba >= 0)
                {
                    mode = probe_mode;
                    break;
                }
            }

            lcd_putsf(0, sec + 1, "%d l%06d m%d x%06x", sec, lba, mode, meta);
        }
    }
    lcd_update();

    while (true)
    {
        lcd_update();
        sleep(HZ / 2);
    }
}
#endif

#ifdef C4PRO_FTL_META_TAG_PROBE
static void c4pro_meta_put_near(int x, int y, int tag, int hit, int start,
                                int count)
{
    if (hit < 0)
        lcd_putsf(x, y, "%d ----%02d", tag, count);
    else
        lcd_putsf(x, y, "%d %04x%02d", tag, hit - start, count);
}

static void c4pro_show_meta_tag_probe(void)
{
    static unsigned char page_data[C4PRO_FTL_PAGE_SIZE];
    static unsigned char page_spare[C4PRO_FTL_SPARE_SIZE];
#ifndef C4PRO_FTL_META_TAG_SCAN_START
#define C4PRO_FTL_META_TAG_SCAN_START 294912u
#endif
#ifndef C4PRO_FTL_META_TAG_SCAN_SECTORS
#define C4PRO_FTL_META_TAG_SCAN_SECTORS 8192u
#endif
    int hits[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int best_dist[8] =
    {
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff,
        0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff
    };
    int counts[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t start = C4PRO_FTL_META_TAG_SCAN_START;
    uint32_t end = start + C4PRO_FTL_META_TAG_SCAN_SECTORS;
    const int known_psec = 297128;

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_putsf(0, 0, "C4META %lu+%lu", (unsigned long)start,
              (unsigned long)C4PRO_FTL_META_TAG_SCAN_SECTORS);
    lcd_puts(0, 1, "wait");
    lcd_update();

    for (uint32_t psec = start & ~7u; psec < end; psec += 8)
    {
        uint32_t page = psec / C4PRO_FTL_SECTORS_PER_PAGE;

        if (nand_read_page(0, page, page_data, page_spare, 1, 0))
            continue;

        for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE; sec++)
        {
            unsigned meta = c4pro_target_spare_meta(page_spare, sec);

            for (int target = 0; target < 8; target++)
            {
                unsigned want = ((unsigned)target << 16) | 0x00f200u;

                if (meta == want)
                {
                    int hit = (int)psec + sec;
                    int dist = hit > known_psec ? hit - known_psec :
                        known_psec - hit;

                    if (counts[target] < 99)
                        counts[target]++;

                    if (dist < best_dist[target])
                    {
                        best_dist[target] = dist;
                        hits[target] = hit;
                    }
                }
            }
        }
    }

    lcd_clear_display();
    lcd_putsf(0, 0, "C4META near %04x", known_psec - (int)start);
    c4pro_meta_put_near(0, 1, 0, hits[0], (int)start, counts[0]);
    c4pro_meta_put_near(10, 1, 1, hits[1], (int)start, counts[1]);
    c4pro_meta_put_near(0, 2, 2, hits[2], (int)start, counts[2]);
    c4pro_meta_put_near(10, 2, 3, hits[3], (int)start, counts[3]);
    c4pro_meta_put_near(0, 3, 4, hits[4], (int)start, counts[4]);
    c4pro_meta_put_near(10, 3, 5, hits[5], (int)start, counts[5]);
    c4pro_meta_put_near(0, 4, 6, hits[6], (int)start, counts[6]);
    c4pro_meta_put_near(10, 4, 7, hits[7], (int)start, counts[7]);
    lcd_puts(0, 6, "tag off count");
    lcd_update();

    while (true)
    {
        lcd_update();
        sleep(HZ / 2);
    }
}
#endif

#ifdef C4PRO_FTL_NEAR_HASH_PROBE
struct c4pro_near_hash_hit
{
    int psec;
    int lba;
    int mode;
    unsigned meta;
    int dist;
};

static void c4pro_near_hash_consider(struct c4pro_near_hash_hit hits[6],
                                     int psec, int lba, int mode,
                                     unsigned meta, int center)
{
    int dist = psec > center ? psec - center : center - psec;
    int slot = -1;

    for (int i = 0; i < 6; i++)
    {
        if (hits[i].psec < 0 || dist < hits[i].dist)
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
        return;

    for (int i = 5; i > slot; i--)
        hits[i] = hits[i - 1];

    hits[slot].psec = psec;
    hits[slot].lba = lba;
    hits[slot].mode = mode;
    hits[slot].meta = meta;
    hits[slot].dist = dist;
}

static void c4pro_show_near_hash_probe(void)
{
    static unsigned char page_data[C4PRO_FTL_PAGE_SIZE];
    static unsigned char page_spare[C4PRO_FTL_SPARE_SIZE];
    static unsigned char page_key[C4PRO_FTL_PAGE_SIZE];
    struct c4pro_near_hash_hit hits[6] =
    {
        { -1, -1, -1, 0xffffff, 0x7fffffff },
        { -1, -1, -1, 0xffffff, 0x7fffffff },
        { -1, -1, -1, 0xffffff, 0x7fffffff },
        { -1, -1, -1, 0xffffff, 0x7fffffff },
        { -1, -1, -1, 0xffffff, 0x7fffffff },
        { -1, -1, -1, 0xffffff, 0x7fffffff },
    };
#ifndef C4PRO_FTL_NEAR_HASH_CENTER
#define C4PRO_FTL_NEAR_HASH_CENTER 297128u
#endif
#ifndef C4PRO_FTL_NEAR_HASH_RADIUS
#define C4PRO_FTL_NEAR_HASH_RADIUS 4096u
#endif
    uint32_t center = C4PRO_FTL_NEAR_HASH_CENTER;
    uint32_t radius = C4PRO_FTL_NEAR_HASH_RADIUS;
    uint32_t start = (center > radius) ? center - radius : 0;
    uint32_t end = center + radius;
    int found = 0;
    extern int c4pro_ftl_diag_hash_lookup(uint32_t hash);

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_putsf(0, 0, "NEARHASH %lu", (unsigned long)center);
    lcd_puts(0, 1, "wait");
    lcd_update();

    start &= ~7u;
    c4pro_probe_make_rk27_key(page_key, sizeof(page_key));

    for (uint32_t psec = start; psec < end; psec += 8)
    {
        uint32_t page = psec / C4PRO_FTL_SECTORS_PER_PAGE;

        if (nand_read_page(0, page, page_data, page_spare, 1, 0))
            continue;

        for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE; sec++)
        {
            const unsigned char *data = page_data + sec * SECTOR_SIZE;
            uint32_t hashes[3];
            int lba = -1;
            int mode = -1;

            hashes[0] = c4pro_probe_fnv1a_sector(data);
            hashes[1] = c4pro_probe_fnv1a_sector_xor(data, page_key);
            hashes[2] = c4pro_probe_fnv1a_sector_xor(
                data, page_key + sec * SECTOR_SIZE);

            for (int probe_mode = 0; probe_mode < 3; probe_mode++)
            {
                lba = c4pro_ftl_diag_hash_lookup(hashes[probe_mode]);
                if (lba >= 0)
                {
                    mode = probe_mode;
                    break;
                }
            }

            if (lba < 0)
                continue;

            if (found < 999)
                found++;

            c4pro_near_hash_consider(hits, (int)psec + sec, lba, mode,
                                     c4pro_target_spare_meta(page_spare, sec),
                                     (int)center);
        }
    }

    lcd_clear_display();
    lcd_putsf(0, 0, "NEARHASH n%d", found);
    for (int i = 0; i < 6; i++)
    {
        int off = hits[i].psec < 0 ? -9999 : hits[i].psec - (int)center;
        const char *sign = off < 0 ? "-" : "+";
        int mag = off < 0 ? -off : off;

        lcd_putsf(0, i + 1, "%d p%s%d l%d m%d x%06x", i, sign, mag,
                  hits[i].lba, hits[i].mode, hits[i].meta);
    }
    lcd_update();

    while (true)
    {
        lcd_update();
        sleep(HZ / 2);
    }
}
#endif

#ifdef C4PRO_FTL_LBA_SLOT_PROBE
struct c4pro_lba_slot_hit
{
    int bank;
    int psec;
    int mode;
    unsigned meta;
};

static void c4pro_show_lba_slot_probe(void)
{
    static unsigned char page_data[C4PRO_FTL_PAGE_SIZE];
    static unsigned char page_spare[C4PRO_FTL_SPARE_SIZE];
    static unsigned char page_key[C4PRO_FTL_PAGE_SIZE];
    struct c4pro_lba_slot_hit hits[8] =
    {
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
        { -1, -1, -1, 0xffffff },
    };
#ifndef C4PRO_FTL_LBA_SLOT_BASE
#define C4PRO_FTL_LBA_SLOT_BASE 47488
#endif
#ifndef C4PRO_FTL_LBA_SLOT_CENTER
#define C4PRO_FTL_LBA_SLOT_CENTER 297128u
#endif
#ifndef C4PRO_FTL_LBA_SLOT_RADIUS
#define C4PRO_FTL_LBA_SLOT_RADIUS 4096u
#endif
    int base_lba = C4PRO_FTL_LBA_SLOT_BASE;
    uint32_t center = C4PRO_FTL_LBA_SLOT_CENTER;
    uint32_t radius = C4PRO_FTL_LBA_SLOT_RADIUS;
    uint32_t start = (center > radius) ? center - radius : 0;
    uint32_t end = center + radius;
    int found = 0;
    extern int c4pro_ftl_diag_hash_lookup(uint32_t hash);

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_putsf(0, 0, "SLOT %d wait", base_lba);
    lcd_update();

    start &= ~7u;
    c4pro_probe_make_rk27_key(page_key, sizeof(page_key));

    for (int bank = 0; bank < C4PRO_FTL_TARGET_SCAN_BANKS && found < 8; bank++)
    {
        unsigned char id[5];

        if (c4pro_nand_read_id(bank, id) || !c4pro_target_nand_id_present(id))
            continue;

        for (uint32_t psec = start; psec < end && found < 8; psec += 8)
        {
            uint32_t page = psec / C4PRO_FTL_SECTORS_PER_PAGE;

            if (nand_read_page(bank, page, page_data, page_spare, 1, 0))
                continue;

            for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE && found < 8; sec++)
            {
                const unsigned char *data = page_data + sec * SECTOR_SIZE;
                uint32_t hashes[3];
                int lba = -1;
                int mode = -1;

                hashes[0] = c4pro_probe_fnv1a_sector(data);
                hashes[1] = c4pro_probe_fnv1a_sector_xor(data, page_key);
                hashes[2] = c4pro_probe_fnv1a_sector_xor(
                    data, page_key + sec * SECTOR_SIZE);

                for (int probe_mode = 0; probe_mode < 3; probe_mode++)
                {
                    lba = c4pro_ftl_diag_hash_lookup(hashes[probe_mode]);
                    if (lba >= 0)
                    {
                        mode = probe_mode;
                        break;
                    }
                }

                if (lba < base_lba || lba >= base_lba + 8)
                    continue;

                int slot = lba - base_lba;

                if (hits[slot].psec >= 0)
                    continue;

                hits[slot].bank = bank;
                hits[slot].psec = (int)psec + sec;
                hits[slot].mode = mode;
                hits[slot].meta = c4pro_target_spare_meta(page_spare, sec);
                found++;
            }
        }
    }

    lcd_clear_display();
    lcd_putsf(0, 0, "SLOT f%d", found);
    for (int i = 0; i < 7; i++)
    {
        if (hits[i].psec < 0)
        {
            lcd_putsf(0, i + 1, "%d ----", i);
        }
        else
        {
            int off = hits[i].psec - (int)center;
            const char *sign = off < 0 ? "-" : "+";
            int mag = off < 0 ? -off : off;

            lcd_putsf(0, i + 1, "%d b%d p%s%d m%d x%06x", i,
                      hits[i].bank, sign, mag, hits[i].mode, hits[i].meta);
        }
    }
    lcd_update();

    while (true)
    {
        lcd_update();
        sleep(HZ / 2);
    }
}
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_USB_DIAG_EXPORT)
static void c4pro_show_usb_diag_loop(void)
{
    lcd_setfont(FONT_SYSFIXED);
    usb_start_monitoring();
#ifdef USB_ENABLE_STORAGE
    usb_core_enable_driver(USB_DRIVER_MASS_STORAGE, true);
#endif
    c4pro_usb_diag_enter_storage_mode();
    c4pro_usb_diag_force_reattach();
    sleep(2 * HZ);
    c4pro_usb_diag_force_storage_config();

    while (true)
    {
        c4pro_usb_diag_sample();
        lcd_clear_display();
        lcd_puts(0, 0, "C4USB MSC");
        lcd_putsf(0, 1, "core s%u c%u if%u",
                  c4pro_usb_diag_core_state,
                  c4pro_usb_diag_core_config,
                  c4pro_usb_diag_core_interfaces);
        lcd_putsf(0, 2, "init%u arm%u out%u",
                  c4pro_usb_storage_diag_init_count,
                  c4pro_usb_storage_diag_arm_count,
                  c4pro_usb_diag_outn_count);
        lcd_putsf(0, 3, "cbw%u bad%u cmd%02x",
                  c4pro_usb_storage_diag_cbw_count,
                  c4pro_usb_storage_diag_bad_sig,
                  c4pro_usb_storage_diag_last_cmd);
        lcd_putsf(0, 4, "wc%u l%u n%u",
                  c4pro_usb_storage_diag_write_cmd_count,
                  c4pro_usb_storage_diag_write_lba,
                  c4pro_usb_storage_diag_write_blocks);
        lcd_putsf(0, 5, "fmt%d fw%d/%d cf%d",
                  c4own_diag_format_count,
                  c4own_diag_write_count,
                  c4own_diag_last_write_result,
                  c4own_diag_cache_flush_count);
        lcd_putsf(0, 6, "cl%d m2%d s2%08x",
                  c4own_diag_cache_logical,
                  c4own_diag_map2,
                  c4own_diag_lba2048_sig);
        lcd_putsf(0, 7, "rs%u sw%u/%d",
                  c4pro_usb_storage_diag_recv_status,
                  c4pro_usb_storage_diag_storage_write_calls,
                  c4pro_usb_storage_diag_storage_write_result);
        lcd_update();
        sleep(HZ / 2);
    }
}

#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_WDT_CRASH_CAPTURE)
/* JTAG-free crash capture (docs/c4-usb-msc-write-freeze.md s6 option 2).
 * DRAM confirmed 2026-07-02 to survive a WDT-triggered reset (the survival
 * test above). c4pro_wdt_capture_start() (system-rk27xx.c) arms the WDT;
 * INT_TIMER0 (kernel-rk27xx.c) pets it every tick while also stashing the
 * current interrupted PC into this same DRAM record, so the LAST value
 * written just before the tick genuinely stops (the freeze itself) is what
 * survives into the next boot -- this checks for and reports it.
 * C4PRO_WDT_CAPTURE_ADDR/MAGIC/RELOAD live in colorflyc4pro.h, shared with
 * the tick writer (kernel-rk27xx.c) and the arm sequence (system-rk27xx.c)
 * so the three pieces of this tool can't silently drift out of sync. */

extern void c4pro_wdt_capture_start(void);

static void c4pro_wdt_crash_capture_check_and_report(void)
{
    volatile unsigned long *rec = (volatile unsigned long *)C4PRO_WDT_CAPTURE_ADDR;

    if (rec[0] == C4PRO_WDT_CAPTURE_MAGIC)
    {
#ifdef C4PRO_WDT_AUTOREBOOT
        /* Daily-driver behaviour: the watchdog already reset+rebooted us out of a
         * (rare, residual) freeze. Just clear the marker and continue booting so
         * the device auto-recovers -- USB re-enumerates on its own, no diagnostic
         * halt screen, no manual replug. The failed copy is retried by the host. */
        rec[0] = 0;
#else
        unsigned long pc = rec[1];
        unsigned long tick = rec[2];
        unsigned long phase = rec[3];
        unsigned long stage = rec[4];
        unsigned long stlast = rec[5];
        unsigned long stage_tick = rec[6];

        lcd_setfont(FONT_SYSFIXED);
        lcd_clear_display();
        lcd_puts(0, 0, "WDT CRASH CAPTURE");
        lcd_puts(0, 2, "FREEZE CAUGHT!");
        lcd_putsf(0, 3, "pc  =0x%08lx", pc);
        lcd_putsf(0, 4, "tick=%lu", tick);
        /* st = (region<<8)|sector; regions: 1 PAGE_BUF copy, 2 program engine
         * wait, 3 tPROG poll, 4 read engine wait, 5 read drain, 6 write entry,
         * 7 write cmd/addr, 8 deselect, 9 read cmd/addr, A read tR wait,
         * B erase issue, C tERASE wait, 0 idle. Both are TICK samples (duty-
         * cycle biased): st = last tick's value, sl/age = last NONZERO st and
         * ticks since -- sl==0 after heavy writes means the marker plumbing is
         * broken (positive control), NOT that no NAND op ever ran. */
        lcd_putsf(0, 5, "phase=%lu st=0x%03lx", phase, stage);
        lcd_putsf(0, 6, "sl=0x%03lx age=%lu", stlast, tick - stage_tick);
        /* Stall-kick state: kf=fires, dc=DMAOUTCTL@kick (bit0=armed),
         * cn=cnt remaining, rx=RXSTAT. kf==0 => watchdog never fired (main
         * thread starved or phase!=13); dc&1 => armed (clean rearm), else idle
         * (lost RXACK). */
        lcd_putsf(0, 7, "kf=%lu dc=%lx", rec[7], rec[8]);
        lcd_putsf(0, 8, "cn=%ld rx=%lx", (long)rec[9], rec[10]);
        lcd_update();

        rec[0] = 0;  /* clear so a future unrelated boot doesn't misreport */
        while (1) { }
#endif
    }

    c4pro_wdt_capture_start();
}
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_USB_MSC_ENABLE)
static void c4pro_usb_msc_settle_storage(void)
{
    for (int i = 0; i < 16; i++)
    {
        c4pro_usb_diag_sample();
        if (c4pro_usb_diag_setup_count != 0 &&
            c4pro_usb_diag_core_config == 0)
        {
            c4pro_usb_diag_enter_storage_mode();
            c4pro_usb_diag_force_storage_config();
        }

        sleep(HZ / 4);
    }
}

#ifdef C4PRO_USB_MSC_STATUS
static void c4pro_show_usb_msc_status(void)
{
    lcd_setfont(FONT_SYSFIXED);

    while (1)
    {
        c4pro_usb_msc_settle_storage();
        c4pro_usb_diag_sample();
        lcd_clear_display();
        lcd_puts(0, 0, "C4MSC final");
        lcd_putsf(0, 1, "irq%u rst%u con%u",
                  c4pro_usb_diag_irq_count,
                  c4pro_usb_diag_reset_count,
                  c4pro_usb_diag_conn_count);
        lcd_putsf(0, 2, "setup%u in%u out%u",
                  c4pro_usb_diag_setup_count,
                  c4pro_usb_diag_in0_count,
                  c4pro_usb_diag_out0_count);
        lcd_putsf(0, 3, "core s%u c%u if%u",
                  c4pro_usb_diag_core_state,
                  c4pro_usb_diag_core_config,
                  c4pro_usb_diag_core_interfaces);
        lcd_putsf(0, 4, "ep%d xf%u rp%u",
                  c4pro_usb_diag_ep0_state,
                  c4pro_usb_diag_xfer_count,
                  c4pro_usb_diag_response_count);
        lcd_putsf(0, 5, "stor i%u a%u x%u",
                  c4pro_usb_storage_diag_init_count,
                  c4pro_usb_storage_diag_arm_count,
                  c4pro_usb_storage_diag_xfer_count);
        lcd_putsf(0, 6, "cbw%u bad%u cmd%02x",
                  c4pro_usb_storage_diag_cbw_count,
                  c4pro_usb_storage_diag_bad_sig,
                  c4pro_usb_storage_diag_last_cmd);
        lcd_putsf(0, 7, "ex%u lun%u bs%u",
                  c4pro_usb_storage_diag_exclusive,
                  c4pro_usb_storage_diag_lun_present,
                  c4pro_usb_storage_diag_block_size);
        lcd_update();
        sleep(HZ);
    }
}
#endif

#ifdef C4PRO_FTL_META_TAG_PROBE
static void c4pro_show_meta_tag_probe(void)
{
    static unsigned char page_data[C4PRO_FTL_PAGE_SIZE];
    static unsigned char page_spare[C4PRO_FTL_SPARE_SIZE];
#ifndef C4PRO_FTL_META_TAG_SCAN_START
#define C4PRO_FTL_META_TAG_SCAN_START 294912u
#endif
#ifndef C4PRO_FTL_META_TAG_SCAN_SECTORS
#define C4PRO_FTL_META_TAG_SCAN_SECTORS 8192u
#endif
    int hits[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    uint32_t start = C4PRO_FTL_META_TAG_SCAN_START;
    uint32_t end = start + C4PRO_FTL_META_TAG_SCAN_SECTORS;

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_putsf(0, 0, "C4META %lu+%lu", (unsigned long)start,
              (unsigned long)C4PRO_FTL_META_TAG_SCAN_SECTORS);
    lcd_puts(0, 1, "wait");
    lcd_update();

    for (uint32_t psec = start & ~7u; psec < end; psec += 8)
    {
        uint32_t page = psec / C4PRO_FTL_SECTORS_PER_PAGE;

        if (nand_read_page(0, page, page_data, page_spare, 1, 0))
            continue;

        for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE; sec++)
        {
            unsigned meta = c4pro_target_spare_meta(page_spare, sec);

            for (int target = 0; target < 8; target++)
            {
                unsigned want = ((unsigned)target << 16) | 0x00f200u;

                if (hits[target] < 0 && meta == want)
                    hits[target] = (int)psec + sec;
            }
        }
    }

    lcd_clear_display();
    lcd_putsf(0, 0, "C4META s%lu", (unsigned long)start);
    lcd_putsf(0, 1, "0 o%04x 1 o%04x",
              hits[0] < 0 ? -1 : hits[0] - (int)start,
              hits[1] < 0 ? -1 : hits[1] - (int)start);
    lcd_putsf(0, 2, "2 o%04x 3 o%04x",
              hits[2] < 0 ? -1 : hits[2] - (int)start,
              hits[3] < 0 ? -1 : hits[3] - (int)start);
    lcd_putsf(0, 3, "4 o%04x 5 o%04x",
              hits[4] < 0 ? -1 : hits[4] - (int)start,
              hits[5] < 0 ? -1 : hits[5] - (int)start);
    lcd_putsf(0, 4, "6 o%04x 7 o%04x",
              hits[6] < 0 ? -1 : hits[6] - (int)start,
              hits[7] < 0 ? -1 : hits[7] - (int)start);
    lcd_puts(0, 6, "p = start + offset");
    lcd_update();

    while (true)
    {
        lcd_update();
        sleep(HZ / 2);
    }
}
#endif
#endif

#if defined(C4PRO_STORAGE_VERDICT) || defined(C4PRO_FTL_PROBE)
static unsigned c4pro_probe_u16le(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned c4pro_probe_u32le(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
#endif

#ifdef C4PRO_STORAGE_VERDICT
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

static bool c4pro_is_power_of_two(unsigned value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static bool c4pro_sector_looks_like_fat_boot(const unsigned char *sector)
{
    unsigned sig = c4pro_probe_u16le(sector + 510);
    unsigned bps = c4pro_probe_u16le(sector + 11);
    unsigned spc = sector[13];
    unsigned rsvd = c4pro_probe_u16le(sector + 14);
    unsigned nfats = sector[16];
    unsigned root_entries = c4pro_probe_u16le(sector + 17);
    unsigned fatsz16 = c4pro_probe_u16le(sector + 22);
    unsigned fatsz32 = c4pro_probe_u32le(sector + 36);

    if (sig != 0xaa55 || bps != SECTOR_SIZE ||
        !c4pro_is_power_of_two(spc) || spc > 128 ||
        rsvd == 0 || (nfats != 1 && nfats != 2))
        return false;

    if (root_entries > 0 && fatsz16 > 0 &&
        (!memcmp(sector + 54, "FAT16   ", 8) ||
         !memcmp(sector + 54, "FAT12   ", 8)))
        return true;

    if (root_entries == 0 && fatsz32 > 0 &&
        !memcmp(sector + 82, "FAT32   ", 8))
        return true;

    return false;
}

static bool c4pro_sector_looks_like_mbr(const unsigned char *sector)
{
    if (c4pro_probe_u16le(sector + 510) != 0xaa55)
        return false;

    for (int i = 0; i < 4; i++)
    {
        const unsigned char *part = sector + 446 + i * 16;
        unsigned type = part[4];
        unsigned size = c4pro_probe_u32le(part + 12);

        if (type != 0 && size != 0)
            return true;
    }

    return false;
}

struct c4pro_bank_scan
{
    unsigned char id[5];
    bool present;
    int fat_count;
    int first_fat_psec;
    int mbr_count;
    int first_mbr_psec;
};

static bool c4pro_nand_id_present(const unsigned char id[5])
{
    bool any_zero = false;
    bool any_ff = false;
    bool any_other = false;

    for (int i = 0; i < 5; i++)
    {
        if (id[i] == 0x00)
            any_zero = true;
        else if (id[i] == 0xff)
            any_ff = true;
        else
            any_other = true;
    }

    return any_other && !(any_zero && !any_ff && !any_other);
}

static unsigned c4pro_nand_id_head(const unsigned char id[5])
{
    return ((unsigned)id[0] << 24) | ((unsigned)id[1] << 16) |
           ((unsigned)id[2] << 8) | (unsigned)id[3];
}

static void c4pro_scan_raw_volume_candidates(int bank,
                                             struct c4pro_bank_scan *scan)
{
    static unsigned char sector[SECTOR_SIZE];

    memset(scan, 0, sizeof(*scan));
    scan->fat_count = 0;
    scan->first_fat_psec = -1;
    scan->mbr_count = 0;
    scan->first_mbr_psec = -1;

    if (c4pro_nand_read_id(bank, scan->id))
        return;

    scan->present = c4pro_nand_id_present(scan->id);
    if (!scan->present)
        return;

    for (int block = 0; block < C4PRO_NAND_BLOCKS; block++)
    {
        uint32_t page = (uint32_t)block * C4PRO_NAND_PAGES_PER_BLOCK;

        for (int sec = 0; sec < C4PRO_FTL_SECTORS_PER_PAGE; sec++)
        {
            int psec = (int)(page * C4PRO_FTL_SECTORS_PER_PAGE + sec);

            if (psec == C4PRO_SYSTEM_BOOT_PSEC)
                continue;

            if (c4pro_nand_read_sector(bank, page, sec, sector, NULL))
                continue;

            if (c4pro_sector_looks_like_fat_boot(sector))
            {
                if (scan->first_fat_psec < 0)
                    scan->first_fat_psec = psec;
                scan->fat_count++;
            }

            if (c4pro_sector_looks_like_mbr(sector))
            {
                if (scan->first_mbr_psec < 0)
                    scan->first_mbr_psec = psec;
                scan->mbr_count++;
            }
        }
    }
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
    unsigned bootbin_sig = 0;
    unsigned version_sig = 0;
    unsigned rk28_sig = 0;
    int read0_rc;
    int read1_rc = -1;
    int read43_rc = -1;
    int root_rc = -1;
    int root_bad = C4PRO_ROOT_EXPECTED_COUNT;
    int content_ok = 0;
    bool boot_ok = false;
    bool fat_ok = false;
    bool root_ok = false;
    bool verdict_ok = false;
    bool generic_fat = false;
    const char *vol_kind = "BAD";
    struct c4pro_bank_scan bank_scan[C4PRO_NAND_DIAG_BANKS];

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
        generic_fat = sig == 0xaa55 && bps == SECTOR_SIZE &&
                      spc > 0 && nfats > 0 && root_entries > 0 &&
                      fatsz > 0 && root_lba > 0 && first_data > root_lba;
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
            root_bad = c4pro_check_expected_root_sector(sector);
            root_ok = root_bad < 0;
        }

        if (storage_read_sectors(IF_MD(0,) 26985, 1, sector) == 0)
            bootbin_sig = c4pro_probe_u32le(sector);

        if (storage_read_sectors(IF_MD(0,) 37117, 1, sector) == 0)
            version_sig = c4pro_probe_u32le(sector);

        if (storage_read_sectors(IF_MD(0,) 37121, 1, sector) == 0)
            rk28_sig = c4pro_probe_u32le(sector);

        content_ok = bootbin_sig == 0x73726f50 &&
                     version_sig == 0x00000198 &&
                     rk28_sig == 0x38324b52;
    }

    verdict_ok = boot_ok && fat_ok && root_ok;
    if (verdict_ok)
        vol_kind = "SYS";
    else if (generic_fat)
        vol_kind = "FAT";

    for (int bank = 0; bank < C4PRO_NAND_DIAG_BANKS; bank++)
        c4pro_scan_raw_volume_candidates(bank, &bank_scan[bank]);

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_putsf(0, 0, "C4VOL %s u%d c%d", vol_kind,
              verdict_ok ? 0 : (generic_fat ? 1 : -1), content_ok);
    lcd_putsf(0, 1, "B0 %08x%02x rf%d rm%d",
              c4pro_nand_id_head(bank_scan[0].id), bank_scan[0].id[4],
              bank_scan[0].present ? bank_scan[0].fat_count : -1,
              bank_scan[0].present ? bank_scan[0].mbr_count : -1);
    lcd_putsf(0, 2, "B1 %08x%02x rf%d rm%d",
              c4pro_nand_id_head(bank_scan[1].id), bank_scan[1].id[4],
              bank_scan[1].present ? bank_scan[1].fat_count : -1,
              bank_scan[1].present ? bank_scan[1].mbr_count : -1);
    lcd_putsf(0, 3, "boot%d fat%d root%d bad%d", boot_ok, fat_ok,
              root_ok, root_bad);
    lcd_putsf(0, 4, "F0 %d M0 %d F1 %d M1 %d",
              bank_scan[0].first_fat_psec, bank_scan[0].first_mbr_psec,
              bank_scan[1].first_fat_psec, bank_scan[1].first_mbr_psec);
#ifdef C4PRO_FTL_LBA_DIAG
    lcd_putsf(0, 0, "C4LBA h%d d%d n%d", c4pro_ftl_diag_hash_mapped,
              c4pro_ftl_diag_hash_best_delta,
              c4pro_ftl_diag_hash_best_delta_count);
    lcd_putsf(0, 1, "a%d p%d i%d m%d s%d", c4pro_ftl_diag_probe_lba[0],
              c4pro_ftl_diag_probe_psec[0],
              c4pro_ftl_diag_probe_inferred[0],
              c4pro_ftl_diag_probe_mode[0],
              c4pro_ftl_diag_probe_synth[0]);
    lcd_putsf(0, 2, "b%d p%d i%d m%d s%d", c4pro_ftl_diag_probe_lba[1],
              c4pro_ftl_diag_probe_psec[1],
              c4pro_ftl_diag_probe_inferred[1],
              c4pro_ftl_diag_probe_mode[1],
              c4pro_ftl_diag_probe_synth[1]);
    lcd_putsf(0, 3, "c%d p%d i%d m%d s%d", c4pro_ftl_diag_probe_lba[2],
              c4pro_ftl_diag_probe_psec[2],
              c4pro_ftl_diag_probe_inferred[2],
              c4pro_ftl_diag_probe_mode[2],
              c4pro_ftl_diag_probe_synth[2]);
    lcd_putsf(0, 4, "d%d p%d i%d m%d s%d", c4pro_ftl_diag_probe_lba[3],
              c4pro_ftl_diag_probe_psec[3],
              c4pro_ftl_diag_probe_inferred[3],
              c4pro_ftl_diag_probe_mode[3],
              c4pro_ftl_diag_probe_synth[3]);
    lcd_putsf(0, 5, "x%d/%d/%d y%d", c4pro_ftl_diag_hash_raw,
              c4pro_ftl_diag_hash_secdec,
              c4pro_ftl_diag_hash_pagedec,
              c4pro_ftl_diag_synth_sectors);
    lcd_putsf(0, 6, "fat%d root%d", c4pro_ftl_diag_hash_fat_psec,
              c4pro_ftl_diag_hash_root_psec);
#else
    lcd_putsf(0, 5, "y%d h%d d%d n%d", c4pro_ftl_diag_synth_sectors,
              c4pro_ftl_diag_hash_mapped, c4pro_ftl_diag_hash_best_delta,
              c4pro_ftl_diag_hash_best_delta_count);
    lcd_putsf(0, 6, "x%d/%d/%d", c4pro_ftl_diag_hash_raw,
              c4pro_ftl_diag_hash_secdec,
              c4pro_ftl_diag_hash_pagedec);
#endif
    lcd_puts(0, 7, "15s then key");
    lcd_update();

    sleep(15 * HZ);
    button_clear_queue();
    while (button_get_w_tmo(HZ / 2) == BUTTON_NONE)
        ;
    button_clear_queue();
}
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_FTL_SYNTH_AUDIT)
static void c4pro_show_ftl_synth_audit(void)
{
    c4pro_ftl_synth_audit();
    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "C4AUDIT");
    lcd_putsf(0, 1, "t%d ok%d", c4pro_ftl_audit_total,
              c4pro_ftl_audit_ok);
    lcd_putsf(0, 2, "miss%d bad%d", c4pro_ftl_audit_missing,
              c4pro_ftl_audit_bad);
    lcd_putsf(0, 3, "l%d", c4pro_ftl_audit_first_lba);
    lcd_putsf(0, 4, "p%d m%d", c4pro_ftl_audit_first_psec,
              c4pro_ftl_audit_first_mode);
    lcd_putsf(0, 5, "e%08x", c4pro_ftl_audit_first_expected);
    lcd_putsf(0, 6, "a%08x", c4pro_ftl_audit_first_actual);
    lcd_puts(0, 7, "press key");
    lcd_update();
    button_clear_queue();
    while (button_get_w_tmo(HZ / 2) == BUTTON_NONE)
        ;
    button_clear_queue();
}
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_PCM_TONE_TEST)
static int16_t c4pro_pcm_tone[4096] __attribute__((aligned(4)));
static int c4pro_pcm_tone_chunks;
static int c4pro_pcm_tone_status;
static int c4pro_pcm_tone_callbacks;
static unsigned char c4pro_pcm_tone_dac_addr;
static unsigned char c4pro_pcm_tone_src_addr;
static unsigned char c4pro_pcm_tone_dac_id;
static unsigned char c4pro_pcm_tone_src_id;
static int c4pro_pcm_tone_i2c_errors;
static unsigned char c4pro_cs8422_status_13;
static unsigned char c4pro_cs8422_status_14;
static unsigned char c4pro_cs8422_status_15;
static unsigned char c4pro_cs8422_status_16;
static unsigned long c4pro_gpio_base_dr[6];
static unsigned long c4pro_gpio_base_con[6];
static unsigned long c4pro_gpio_base_iomuxb;

extern int rk27xx_i2c_write(unsigned char slave, int address, int len,
                            const unsigned char *data);
extern int rk27xx_i2c_read(unsigned char slave, int address, int len,
                           unsigned char *data);

#define C4PRO_CS4398_REG_CHIPID  0x01
#define C4PRO_CS4398_REG_MODECTL 0x02
#define C4PRO_CS4398_REG_VOLMIX  0x03
#define C4PRO_CS4398_REG_MUTE    0x04
#define C4PRO_CS4398_REG_VOL_A   0x05
#define C4PRO_CS4398_REG_VOL_B   0x06
#define C4PRO_CS4398_REG_RAMP    0x07
#define C4PRO_CS4398_REG_MISC    0x08

static int c4pro_i2c_write8(unsigned char addr, unsigned char reg,
                            unsigned char val)
{
    int rc = rk27xx_i2c_write(addr, reg, 1, &val);
    if (rc != 0)
        c4pro_pcm_tone_i2c_errors++;
    return rc;
}

static unsigned char c4pro_i2c_read8(unsigned char addr, unsigned char reg)
{
    unsigned char val = 0xff;
    int rc = rk27xx_i2c_read(addr, reg, 1, &val);
    if (rc != 0)
        c4pro_pcm_tone_i2c_errors++;
    return val;
}

static void c4pro_cs4398_basic_init(unsigned char addr, unsigned char modectl,
                                    unsigned char volmix, bool mute_pin_high)
{
    (void)mute_pin_high;
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_MISC, 0xc0); /* CPEN | PDN */
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_MODECTL, modectl);
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_VOLMIX, volmix);
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_MUTE, 0x80);
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_VOL_A, 0x00);
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_VOL_B, 0x00);
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_RAMP, 0xb4);
    c4pro_i2c_write8(addr, C4PRO_CS4398_REG_MISC, 0x40); /* CPEN, power up */
}

struct c4pro_cs8422_mode
{
    const char *label;
    unsigned char muxb_low;
    unsigned char dac_modectl;
    unsigned char dac_volmix;
    unsigned char clock_ctl;
    unsigned char rx_control;
    unsigned char data_flow;
    unsigned char sai_clock;
    unsigned char sao_clock;
    unsigned char rmck_misc;
    unsigned char route;
    unsigned char sai_format;
    unsigned char sao1_format;
    unsigned char sao2_format;
    unsigned char src_policy;
};

static void c4pro_cs8422_init(unsigned char addr,
                              const struct c4pro_cs8422_mode *mode)
{
    if (mode->src_policy == 0)
        return;

    c4pro_i2c_write8(addr, 0x02, 0x80); /* configure while powered down */
    c4pro_i2c_write8(addr, 0x03, mode->rx_control);
    c4pro_i2c_write8(addr, 0x04, mode->data_flow);
    c4pro_i2c_write8(addr, 0x07, mode->sai_clock);
    c4pro_i2c_write8(addr, 0x08, mode->sao_clock);

    if (mode->src_policy == 1)
        return;

    c4pro_i2c_write8(addr, 0x09, mode->rmck_misc);
    c4pro_i2c_write8(addr, 0x0a, mode->route);
    c4pro_i2c_write8(addr, 0x0b, mode->sai_format);
    c4pro_i2c_write8(addr, 0x0c, mode->sao1_format);
    c4pro_i2c_write8(addr, 0x0d, mode->sao2_format);
    c4pro_i2c_write8(addr, 0x02, mode->clock_ctl);
}

static void c4pro_cs8422_read_status(unsigned char addr)
{
    c4pro_cs8422_status_13 = c4pro_i2c_read8(addr, 0x13);
    c4pro_cs8422_status_14 = c4pro_i2c_read8(addr, 0x14);
    c4pro_cs8422_status_15 = c4pro_i2c_read8(addr, 0x15);
    c4pro_cs8422_status_16 = c4pro_i2c_read8(addr, 0x16);
}

static void c4pro_audio_hw_probe(void)
{
    c4pro_pcm_tone_dac_addr = 0;
    c4pro_pcm_tone_src_addr = 0;
    c4pro_pcm_tone_dac_id = 0xff;
    c4pro_pcm_tone_src_id = 0xff;
    c4pro_pcm_tone_i2c_errors = 0;

    for (unsigned int addr7 = 0x4c; addr7 <= 0x4f; addr7++)
    {
        unsigned char addr = addr7 << 1;
        unsigned char id = c4pro_i2c_read8(addr, C4PRO_CS4398_REG_CHIPID);

        if ((id & 0xf8) == 0x70)
        {
            c4pro_pcm_tone_dac_addr = addr;
            c4pro_pcm_tone_dac_id = id;
            break;
        }
    }

    for (unsigned int addr7 = 0x10; addr7 <= 0x17; addr7++)
    {
        unsigned char addr = addr7 << 1;
        unsigned char id = c4pro_i2c_read8(addr, 0x01);

        if ((id & 0xf8) == 0x10)
        {
            c4pro_pcm_tone_src_addr = addr;
            c4pro_pcm_tone_src_id = id;
            break;
        }
    }
}

static void c4pro_audio_hw_config(const struct c4pro_cs8422_mode *mode,
                                  bool mute_pin_high)
{
    if (c4pro_pcm_tone_src_addr != 0)
        c4pro_cs8422_init(c4pro_pcm_tone_src_addr, mode);

    if (c4pro_pcm_tone_dac_addr != 0)
        c4pro_cs4398_basic_init(c4pro_pcm_tone_dac_addr, mode->dac_modectl,
                                mode->dac_volmix, mute_pin_high);

    sleep(HZ / 10);
}

static unsigned long c4pro_gpio_dr(int port)
{
    switch (port)
    {
    case 0: return GPIO_PADR;
    case 1: return GPIO_PBDR;
    case 2: return GPIO_PCDR;
    case 3: return GPIO_PDDR;
    case 4: return GPIO_PEDR;
    default: return GPIO_PFDR;
    }
}

static unsigned long c4pro_gpio_con(int port)
{
    switch (port)
    {
    case 0: return GPIO_PACON;
    case 1: return GPIO_PBCON;
    case 2: return GPIO_PCCON;
    case 3: return GPIO_PDCON;
    case 4: return GPIO_PECON;
    default: return GPIO_PFCON;
    }
}

static void c4pro_gpio_set_dr(int port, unsigned long value)
{
    switch (port)
    {
    case 0: GPIO_PADR = value; break;
    case 1: GPIO_PBDR = value; break;
    case 2: GPIO_PCDR = value; break;
    case 3: GPIO_PDDR = value; break;
    case 4: GPIO_PEDR = value; break;
    default: GPIO_PFDR = value; break;
    }
}

static void c4pro_gpio_set_con(int port, unsigned long value)
{
    switch (port)
    {
    case 0: GPIO_PACON = value; break;
    case 1: GPIO_PBCON = value; break;
    case 2: GPIO_PCCON = value; break;
    case 3: GPIO_PDCON = value; break;
    case 4: GPIO_PECON = value; break;
    default: GPIO_PFCON = value; break;
    }
}

struct c4pro_gpio_candidate
{
    int port;
    int bit;
    unsigned long iomuxb_clear;
};

static const struct c4pro_gpio_candidate c4pro_gpio_candidates[] =
{
    { 2, 1, 0 },
    { 2, 4, 1u << 2 },
    { 2, 6, 1u << 4 },
    { 2, 7, 1u << 5 },
    { 3, 0, 3u << 6 },
    { 3, 1, 3u << 8 },
    { 3, 2, 1u << 10 },
    { 3, 3, 1u << 21 },
    { 3, 5, 1u << 12 },
    { 3, 6, 1u << 13 },
    { 3, 7, 1u << 14 },
    { 5, 0, 1u << 16 },
    { 5, 3, 1u << 19 },
    { 5, 4, 1u << 20 },
};

static int c4pro_gpio_candidate_count(void)
{
    return (int)(sizeof(c4pro_gpio_candidates) /
                 sizeof(c4pro_gpio_candidates[0]));
}

static void c4pro_gpio_apply_candidate_mux(const struct c4pro_gpio_candidate *c)
{
    SCU_IOMUXB_CON &= ~c->iomuxb_clear;
}

static void c4pro_gpio_make_output(int port, int bit)
{
    c4pro_gpio_set_con(port, c4pro_gpio_con(port) | (1u << bit));
}

static void c4pro_gpio_restore(void)
{
    SCU_IOMUXB_CON = c4pro_gpio_base_iomuxb;

    for (int port = 0; port < 6; port++)
    {
        c4pro_gpio_set_dr(port, c4pro_gpio_base_dr[port]);
        c4pro_gpio_set_con(port, c4pro_gpio_base_con[port]);
    }
}

static void c4pro_gpio_set_one(int port, int bit, bool high)
{
    unsigned long dr = c4pro_gpio_dr(port);

    if (high)
        dr |= 1u << bit;
    else
        dr &= ~(1u << bit);

    c4pro_gpio_set_dr(port, dr);
}

static void c4pro_gpio_snapshot(void)
{
    c4pro_gpio_base_iomuxb = SCU_IOMUXB_CON;

    for (int port = 0; port < 6; port++)
    {
        c4pro_gpio_base_dr[port] = c4pro_gpio_dr(port);
        c4pro_gpio_base_con[port] = c4pro_gpio_con(port);
    }
}

static void c4pro_gpio_prepare_one(int port, int bit)
{
    for (int i = 0; i < c4pro_gpio_candidate_count(); i++)
    {
        if (c4pro_gpio_candidates[i].port == port &&
            c4pro_gpio_candidates[i].bit == bit)
        {
            c4pro_gpio_apply_candidate_mux(&c4pro_gpio_candidates[i]);
            c4pro_gpio_make_output(port, bit);
            return;
        }
    }
}

static void c4pro_clockgen_program_44100(void)
{
    const unsigned int word = 0x0001;

    c4pro_gpio_prepare_one(2, 1);
    c4pro_gpio_prepare_one(2, 4);
    c4pro_gpio_prepare_one(2, 6);

    c4pro_gpio_set_one(2, 6, false);
    c4pro_gpio_set_one(2, 4, true);

    for (int bit = 15; bit >= 0; bit--)
    {
        c4pro_gpio_set_one(2, 1, (word & (1u << bit)) != 0);
        udelay(2000);
        c4pro_gpio_set_one(2, 6, true);
        udelay(2000);
        c4pro_gpio_set_one(2, 6, false);
    }

    c4pro_gpio_set_one(2, 4, false);
    sleep(HZ / 50);
    c4pro_gpio_set_one(2, 4, true);
    sleep(HZ / 50);
    c4pro_gpio_set_one(2, 4, false);
}

static void c4pro_pcm_tone_more(const void **start, size_t *size)
{
    if (c4pro_pcm_tone_chunks <= 0)
    {
        *start = NULL;
        *size = 0;
        return;
    }

    c4pro_pcm_tone_chunks--;
    *start = c4pro_pcm_tone;
    *size = sizeof(c4pro_pcm_tone);
}

static enum pcm_dma_status c4pro_pcm_tone_status_cb(enum pcm_dma_status status)
{
    c4pro_pcm_tone_callbacks++;
    c4pro_pcm_tone_status = status;
    return status;
}

#ifdef C4PRO_I2S_WIRE_PROBE
static void c4pro_i2s_wire_probe(void)
{
    static const struct
    {
        int bit;
        const char *name;
        const char *target;
    } probes[] =
    {
        { 6, "PC6 MCLK", "CS8422 XTI/RMCK?" },
        { 3, "PC3 LRCK", "CS8422 pin13 ILRCK" },
        { 2, "PC2 SCLK", "CS8422 pin14 ISCLK" },
        { 5, "PC5 SDO",  "CS8422 pin15 SDIN" },
    };
    unsigned long base_iomuxb = SCU_IOMUXB_CON;
    unsigned long base_pccon = GPIO_PCCON;
    unsigned long base_pcdr = GPIO_PCDR;

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "C4 I2S WIRE");
    lcd_puts(0, 1, "DC meter");
    lcd_update();
    sleep(HZ);

    for (unsigned int i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
    {
        int bit = probes[i].bit;
        unsigned long mask = 1u << bit;

        SCU_IOMUXB_CON &= ~(1u << (bit - 2));
        GPIO_PCCON |= mask;

        GPIO_PCDR |= mask;
        lcd_clear_display();
        lcd_puts(0, 0, probes[i].name);
        lcd_puts(0, 1, probes[i].target);
        lcd_puts(0, 2, "HIGH");
        lcd_puts(0, 3, "measure, press key");
        lcd_update();
        button_clear_queue();
        while (button_get_w_tmo(HZ / 2) == BUTTON_NONE)
            ;
        button_clear_queue();

        GPIO_PCDR &= ~mask;
        lcd_clear_display();
        lcd_puts(0, 0, probes[i].name);
        lcd_puts(0, 1, probes[i].target);
        lcd_puts(0, 2, "LOW");
        lcd_puts(0, 3, "measure, press key");
        lcd_update();
        button_clear_queue();
        while (button_get_w_tmo(HZ / 2) == BUTTON_NONE)
            ;
        button_clear_queue();
    }

    SCU_IOMUXB_CON = base_iomuxb;
    GPIO_PCDR = base_pcdr;
    GPIO_PCCON = base_pccon;

    lcd_clear_display();
    lcd_puts(0, 0, "C4 WIRE DONE");
    lcd_puts(0, 1, "press key");
    lcd_update();
    button_clear_queue();
    while (button_get_w_tmo(HZ / 2) == BUTTON_NONE)
        ;
    button_clear_queue();
}
#endif

static void c4pro_pcm_tone_test(void)
{
#ifdef C4PRO_I2S_WIRE_PROBE
    c4pro_i2s_wire_probe();
    return;
#endif

    static const struct c4pro_cs8422_mode modes[] =
    {
#ifdef C4PRO_CS8422_RX_SWEEP
        {
            "R0 diff rx0",
            0x0b, 0x10, 0x89, 0x08, 0x00, 0x84, 0x40, 0x40,
            0x00, 0x54, 0x08, 0x84, 0x84, 2
        },
        {
            "R1 dig rx0",
            0x0b, 0x10, 0x89, 0x08, 0x84, 0x84, 0x40, 0x40,
            0x00, 0x54, 0x08, 0x84, 0x84, 2
        },
        {
            "R2 dig rx1",
            0x0b, 0x10, 0x89, 0x08, 0xac, 0x84, 0x40, 0x40,
            0x00, 0x54, 0x08, 0x84, 0x84, 2
        },
        {
            "R3 diff rx2",
            0x0b, 0x10, 0x89, 0x08, 0x50, 0x84, 0x40, 0x40,
            0x00, 0x54, 0x08, 0x84, 0x84, 2
        },
        {
            "R4 dig rx3",
            0x0b, 0x10, 0x89, 0x08, 0xfc, 0x84, 0x40, 0x40,
            0x00, 0x54, 0x08, 0x84, 0x84, 2
        },
#elif defined(C4PRO_VENDOR_AUDIO_TEST)
        {
            "M0 stock0b",
            0x0b, 0x10, 0x86, 0x00, 0x80, 0x84, 0x40, 0x10,
            0x00, 0x00, 0x00, 0x00, 0x00, 1
        },
        {
            "M1 stock1f",
            0x1f, 0x10, 0x86, 0x00, 0x80, 0x84, 0x40, 0x10,
            0x00, 0x00, 0x00, 0x00, 0x00, 1
        },
        {
            "M2 nosrc0b",
            0x0b, 0x10, 0x86, 0x00, 0x80, 0x84, 0x40, 0x10,
            0x00, 0x00, 0x00, 0x00, 0x00, 0
        },
        {
            "M3 full10",
            0x0b, 0x10, 0x86, 0x00, 0x80, 0x84, 0x40, 0x20,
            0x40, 0x10, 0x08, 0x84, 0x84, 2
        },
        {
            "M4 full12",
            0x0b, 0x10, 0x89, 0x00, 0x80, 0x84, 0x40, 0x20,
            0x40, 0x12, 0x08, 0x84, 0x84, 2
        },
#elif defined(C4PRO_RK_I2S_SLAVE_TEST)
        {
            "T0 saiM dir i2s",
            0x0b, 0x10, 0x89, 0x08, 0x08, 0x84, 0x40, 0x40,
            0x40, 0xa0, 0x88, 0x84, 0x84, 2
        },
        {
            "T1 saiM src i2s",
            0x0b, 0x10, 0x89, 0x08, 0x08, 0x84, 0x40, 0x40,
            0x00, 0x00, 0x88, 0x84, 0x84, 2
        },
        {
            "T2 saiM dir lj",
            0x0b, 0x00, 0x86, 0x08, 0x08, 0x84, 0x40, 0x40,
            0x40, 0xa0, 0x80, 0x80, 0x80, 2
        },
        {
            "T3 saiM src lj",
            0x0b, 0x00, 0x86, 0x08, 0x08, 0x84, 0x40, 0x40,
            0x00, 0x00, 0x80, 0x80, 0x80, 2
        },
        {
            "T4 saiM src 512",
            0x0b, 0x10, 0x89, 0x08, 0x08, 0x84, 0x60, 0x60,
            0x00, 0x00, 0x88, 0x84, 0x84, 2
        },
#else
        {
            "T0 dir slv i2s",
            0x0b, 0x10, 0x89, 0x00, 0x08, 0x84, 0x40, 0x60,
            0x08, 0xa0, 0x08, 0x04, 0x04, 2
        },
        {
            "T1 dir mst i2s",
            0x0b, 0x10, 0x89, 0x08, 0x08, 0x84, 0x48, 0x60,
            0x40, 0xa0, 0x08, 0x84, 0x84, 2
        },
        {
            "T2 src mst i2s",
            0x0b, 0x10, 0x89, 0x08, 0x08, 0x84, 0x48, 0x48,
            0x00, 0x00, 0x08, 0x84, 0x84, 2
        },
        {
            "T3 dir mst lj",
            0x0b, 0x00, 0x86, 0x08, 0x08, 0x84, 0x48, 0x60,
            0x40, 0xa0, 0x00, 0x80, 0x80, 2
        },
        {
            "T4 src mst lj",
            0x0b, 0x00, 0x86, 0x08, 0x08, 0x84, 0x48, 0x48,
            0x00, 0x00, 0x00, 0x80, 0x80, 2
        },
#endif
    };
    const int samples = (int)(sizeof(c4pro_pcm_tone) / (2 * sizeof(int16_t)));
    const int mode_count = (int)(sizeof(modes) / sizeof(modes[0]));
    int playing = 0;
    int callbacks = 0;
    int status = 0;
    int chunks = 0;
    unsigned long hdma_con0 = 0;
    unsigned long hdma_isr = 0;
    unsigned long hdma_dsr = 0;
    unsigned char mode_s15[5] = { 0 };
    unsigned char mode_s16[5] = { 0 };

    for (int i = 0; i < samples; i++)
    {
        int16_t sample = (i & 16) ? 18000 : -18000;
        c4pro_pcm_tone[i * 2] = sample;
        c4pro_pcm_tone[i * 2 + 1] = sample;
    }

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "C4 PCM TONE");
    lcd_puts(0, 1, "probing audio");
    lcd_update();

#ifdef HAVE_SW_VOLUME_CONTROL
    pcm_set_master_volume(0, 0);
#endif
    pcm_postinit();
    pcm_set_frequency(44100);
    pcm_apply_settings();
    sleep(HZ / 20);
    c4pro_audio_hw_probe();
    c4pro_gpio_snapshot();

    lcd_clear_display();
    lcd_puts(0, 0, "C4 CLOCKGEN");
    lcd_puts(0, 1, "w0001");
    lcd_update();
    c4pro_clockgen_program_44100();
    sleep(HZ / 5);

    lcd_clear_display();
    lcd_puts(0, 0, "PD7 HIGH");
    lcd_puts(0, 1, "amp/mute enable");
    lcd_update();
    c4pro_gpio_prepare_one(3, 7);
    c4pro_gpio_set_one(3, 7, true);
    sleep(HZ / 5);

    for (int m = 0; m < mode_count; m++)
    {
        const void *start = c4pro_pcm_tone;
        size_t size = sizeof(c4pro_pcm_tone);

        lcd_clear_display();
        lcd_puts(0, 0, "C4 AUDIO SWEEP");
        lcd_puts(0, 1, modes[m].label);
        lcd_putsf(0, 2, "A d%02x s%02x R d%02x s%02x", c4pro_pcm_tone_dac_addr,
                  c4pro_pcm_tone_src_addr, c4pro_pcm_tone_dac_id,
                  c4pro_pcm_tone_src_id);
        lcd_puts(0, 3, "listen for tone");
        lcd_update();

        SCU_IOMUXA_CON |= (1 << 19);
        SCU_IOMUXB_CON = (SCU_IOMUXB_CON & ~0x1f) | modes[m].muxb_low;
        c4pro_audio_hw_config(&modes[m], false);
        c4pro_pcm_tone_chunks = 700;
        c4pro_pcm_tone_status = 0;
        c4pro_pcm_tone_callbacks = 0;
        pcm_play_data(c4pro_pcm_tone_more, c4pro_pcm_tone_status_cb,
                      start, size);

        sleep(6 * HZ);
        playing = pcm_is_playing();
        callbacks = c4pro_pcm_tone_callbacks;
        status = c4pro_pcm_tone_status;
        chunks = c4pro_pcm_tone_chunks;
        hdma_con0 = HDMA_CON0;
        hdma_isr = HDMA_ISR;
        hdma_dsr = HDMA_DSR;
        if (c4pro_pcm_tone_src_addr != 0)
            c4pro_cs8422_read_status(c4pro_pcm_tone_src_addr);
        mode_s15[m] = c4pro_cs8422_status_15;
        mode_s16[m] = c4pro_cs8422_status_16;
        pcm_play_stop();

        lcd_clear_display();
        lcd_puts(0, 0, "C4 AUDIO SWEEP");
        lcd_puts(0, 1, modes[m].label);
        lcd_puts(0, 2, "next in 2s");
        lcd_puts(0, 3, "hear? note T#");
        lcd_putsf(0, 4, "S13 %02x S15 %02x S16 %02x",
                  c4pro_cs8422_status_13, c4pro_cs8422_status_15,
                  c4pro_cs8422_status_16);
        lcd_update();
        sleep(2 * HZ);
    }

    c4pro_gpio_restore();

    lcd_clear_display();
    lcd_puts(0, 0, "C4 SWEEP DONE");
    lcd_puts(0, 1, "report heard T?");
    lcd_putsf(0, 2, "p%d cb%d st%d ch%d", playing,
              callbacks, status, chunks);
    lcd_putsf(0, 3, "I %04lx %04lx %04lx", I2S_OPR & 0xffff,
              I2S_FIFOSTS & 0xffff, I2S_ISR & 0xffff);
    lcd_putsf(0, 4, "H %04lx %04lx %04lx", hdma_con0 & 0xffff,
              hdma_isr & 0xffff, hdma_dsr & 0xffff);
    lcd_putsf(0, 5, "C %02x %02x %02x", c4pro_cs8422_status_13,
              c4pro_cs8422_status_14, c4pro_cs8422_status_15);
    lcd_putsf(0, 6, "T15 %02x %02x %02x %02x %02x", mode_s15[0],
              mode_s15[1], mode_s15[2], mode_s15[3], mode_s15[4]);
    lcd_putsf(0, 7, "T16 %02x %02x %02x %02x %02x", mode_s16[0],
              mode_s16[1], mode_s16[2], mode_s16[3], mode_s16[4]);
    lcd_update();
    button_clear_queue();
    while (button_get_w_tmo(HZ / 2) == BUTTON_NONE)
        ;
    button_clear_queue();
}
#endif

#ifdef C4PRO_FTL_PROBE
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

#endif /* !defined(SIMULATOR) */
#endif /* COLORFLY_C4_PRO */

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_BOOT_PROFILE)
#define C4PRO_BOOT_PROFILE_MAX 32
static const char *c4pro_boot_profile_step[C4PRO_BOOT_PROFILE_MAX];
static long c4pro_boot_profile_tick[C4PRO_BOOT_PROFILE_MAX];
static int c4pro_boot_profile_count;

static void c4pro_boot_profile_add(const char *step)
{
    if (c4pro_boot_profile_count < C4PRO_BOOT_PROFILE_MAX)
    {
        int i = c4pro_boot_profile_count++;
        c4pro_boot_profile_step[i] = step;
        c4pro_boot_profile_tick[i] = current_tick;
    }
}

static void c4pro_boot_profile_show(const char *where)
{
    int best[6];
    int best_count = 0;

    for (int i = 0; i < 6; i++)
        best[i] = -1;

    for (int i = 0; i < c4pro_boot_profile_count; i++)
    {
        long prev = i > 0 ? c4pro_boot_profile_tick[i - 1] : 0;
        long delta = c4pro_boot_profile_tick[i] - prev;
        int insert = best_count;

        for (int j = 0; j < best_count; j++)
        {
            int b = best[j];
            long bprev = b > 0 ? c4pro_boot_profile_tick[b - 1] : 0;
            long bdelta = c4pro_boot_profile_tick[b] - bprev;

            if (delta > bdelta)
            {
                insert = j;
                break;
            }
        }

        if (insert < 6)
        {
            if (best_count < 6)
                best_count++;
            for (int j = best_count - 1; j > insert; j--)
                best[j] = best[j - 1];
            best[insert] = i;
        }
    }

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "C4BOOT");
    lcd_putsf(0, 1, "%s t%ld n%d", where, current_tick,
              c4pro_boot_profile_count);
    for (int line = 2, i = 0; i < best_count && line < 8; i++, line++)
    {
        int step = best[i];
        long prev = step > 0 ? c4pro_boot_profile_tick[step - 1] : 0;
        lcd_putsf(0, line, "%ld +%ld %.12s",
                  c4pro_boot_profile_tick[step],
                  c4pro_boot_profile_tick[step] - prev,
                  c4pro_boot_profile_step[step]);
    }
    lcd_update();
    sleep(10 * HZ);
}

#define C4PRO_BOOT_STEP(step) c4pro_boot_profile_add(step)
#define C4PRO_PRE_ROOT_MENU_HALT(step) c4pro_boot_profile_show(step)
#else
#define C4PRO_BOOT_STEP(step) do { } while (0)
#define C4PRO_PRE_ROOT_MENU_HALT(step) do { } while (0)
#endif

#if 1
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

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_USB_MSC_ENABLE)
/* Serve the owned volume as USB Mass Storage while the cable is connected.
 * Factored out of main() so the mount-failure path can reuse it: a freshly
 * formatted (or otherwise FAT-less) owned volume must still expose the raw
 * device to the host for formatting instead of dead-looping unreachable.
 * Returns when the user ejects with MENU (or the cable is pulled). USB is the
 * only power on a battery-less unit, so usb_detect() (VBUS) never drops while
 * the cable is in; MENU eject disconnects the USB device but keeps power. */
#ifdef C4PRO_NAND_WRITE_STRESS
/* Standalone NAND write stress: hammer the FTL write path with NO USB attached
 * (called before enter_storage_mode, so the pull-up is never asserted -> no
 * enumeration, no SOF traffic, no UDC interrupts). Discriminates whether the
 * residual phase-5 AHB wedge needs the UDC present at all:
 *   - wedges standalone  -> pure NANDC issue, USB exonerated
 *   - never wedges       -> the UDC's presence (ISR MMIO during NAND ops) is
 *                           implicated and becomes the fix target
 * Single-4KB writes scattered over a wide LBA span are the worst case: each one
 * COWs a whole 128-page block (program burst + fill reads), the exact workload
 * that trips the wedge fastest (host mkfs). WDT crash capture is armed by the
 * caller, so a wedge produces the FREEZE screen with phase/st on reboot. */
static void c4pro_nand_write_stress(void)
{
    static unsigned char buf[4096] __attribute__((aligned(32)));
    uint32_t seed = 0xc4c4c4c4u;
    unsigned long n = 0, errs = 0;
    int menu_samples = 0;

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "NAND WRITE STRESS");
    lcd_puts(0, 1, "standalone, no USB");
    lcd_puts(0, 2, "hold MENU: exit->USB");
    lcd_update();

    for (;;)
    {
        /* Escape hatch: a held MENU drops out of the stress into the normal
         * USB-MSC serve, so a stress build is never stranded unreflashable
         * (BACK-hold RKUSB entry lives in the serve loop). */
        if (button_read_device() == BUTTON_MENU)
        {
            if (++menu_samples >= 5)
                return;
        }
        else
            menu_samples = 0;

        seed = seed * 1103515245u + 12345u;
        /* 8-sector (4KB) writes scattered across a ~2GB span above the
         * partition start: recycles a few thousand logical blocks, keeping
         * steady erase+program+fill-read churn (maximal COW amplification). */
        uint32_t lba = 2048u + (((seed >> 7) % 4000000u) & ~7u);

        for (unsigned i = 0; i < 4096u; i += 4)
            *(uint32_t *)(buf + i) = seed ^ (n + i);

        if (storage_write_sectors(IF_MD(0,) lba, 8, buf))
            errs++;
        n++;

        if ((n & 15u) == 0)
        {
            lcd_putsf(0, 3, "w=%lu e=%lu", n, errs);
            lcd_update();
        }
#ifdef HAVE_STORAGE_FLUSH
        if ((n & 255u) == 0)
            storage_flush();   /* exercise the sync-flush path too */
#endif
        /* (No explicit flush otherwise: the FTL's eager high-water flush drains
         * dirty blocks continuously during these writes anyway.) */

        yield();
    }
}
#endif /* C4PRO_NAND_WRITE_STRESS */

static void c4pro_usb_msc_serve(void)
{
    /* ~3 consecutive ~20ms ADC reads (~60ms) = a stable MENU press */
    const int C4PRO_MENU_EJECT_SAMPLES = 3;
    /* BACK held ~2s (deliberate; guards against an accidental tap dropping the
     * device out of storage) enters the mask-ROM RKUSB download mode so new
     * firmware can be flashed with the case closed -- no CE# short needed. */
    const int C4PRO_BACK_UPDATE_SAMPLES = 100;
    bool eject = false;
    int menu_samples = 0;
    int back_samples = 0;

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_WDT_CRASH_CAPTURE)
    /* Same entry-point reasoning as the survival test above. Check-and-report
     * runs first (reports+halts if a previous session's freeze was caught),
     * otherwise arms capture for THIS session before anything else runs. */
    c4pro_wdt_crash_capture_check_and_report();
#endif

#ifdef C4PRO_NAND_WRITE_STRESS
    /* Runs with USB never attached; returns only on a MENU hold, after which
     * the normal USB-MSC serve below proceeds. */
    c4pro_nand_write_stress();
#endif

    c4pro_usb_diag_enter_storage_mode();
    c4pro_usb_diag_force_reattach();
    sleep(2 * HZ);
    c4pro_usb_diag_force_storage_config();
    c4pro_usb_msc_settle_storage();
#ifdef C4PRO_USB_MSC_STATUS
    c4pro_show_usb_msc_status();
#endif

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "USB Mass Storage");
    lcd_puts(0, 1, "Connected to PC");
    lcd_puts(0, 3, "MENU: Rockbox player");
    lcd_puts(0, 2, "Hold BACK: USB flash");
    lcd_update();

    /* Poll the button ADC directly with a small debounce so MENU ejects on the
     * first press; the storage keepalive is inlined (non-blocking). */
    while (!eject && usb_detect() == USB_INSERTED)
    {
        c4pro_usb_diag_sample();
        if (c4pro_usb_diag_setup_count != 0 &&
            c4pro_usb_diag_core_config == 0)
        {
            c4pro_usb_diag_enter_storage_mode();
            c4pro_usb_diag_force_storage_config();
        }

        {
            int btn = button_read_device();

            if (btn == BUTTON_MENU)
            {
                if (++menu_samples >= C4PRO_MENU_EJECT_SAMPLES)
                    eject = true;
            }
            else
                menu_samples = 0;

#ifdef C4PRO_NAND_2PLANE
            /* 1Hz two-plane effectiveness display: P = pages written as plane
             * PAIRS (each = 2 pages/1 tPROG), B = pair-batches, F = serial
             * single-plane fallback flushes. P>>F means the 2x lever is live;
             * F dominating means the pair path is degrading to serial (the
             * 1.9MB/s-instead-of-4 signature under investigation). Painted at
             * most once a second so it cannot perturb the write hot path. */
            {
                extern volatile uint32_t c4pro_2p_batch, c4pro_2p_pairs;
                extern int c4own_diag_cache_flush_count;
                static long c4pro_2p_last_paint;
                if (TIME_AFTER(current_tick, c4pro_2p_last_paint + HZ))
                {
                    c4pro_2p_last_paint = current_tick;
                    lcd_putsf(0, 10, "P%lu B%lu F%d   ",
                              (unsigned long)c4pro_2p_pairs,
                              (unsigned long)c4pro_2p_batch,
                              c4own_diag_cache_flush_count);
                    lcd_update();
                }
            }
#endif
#if defined(C4PRO_USB_RX_STALL_KICK) && defined(C4PRO_PHASE_MARK)
            /* Bulk-OUT stall watchdog: the residual sustained-write freeze is
             * the armed receive (phase 13) silently never completing; the host
             * then kills the device with a bus reset at its 30s SCSI timeout.
             * This loop runs in the MAIN thread (alive while the usb thread is
             * blocked waiting), so it can detect "phase 13 with zero RX
             * completions for 2s" and kick the receive engine back to life
             * (see c4pro_usb_rx_stall_kick). Kicks are rare one-off events, so
             * painting a counter here does not perturb the hot path. */
            {
                extern volatile uint32_t c4pro_hb_phase;
                extern volatile unsigned c4pro_usb_diag_outn_count;
                extern void c4pro_usb_rx_stall_kick(void);
                extern volatile unsigned c4pro_usb_diag_rx_kick_busy;
                extern volatile unsigned c4pro_usb_diag_rx_kick_done;
                static unsigned c4pro_stall_last_outn;
                static long c4pro_stall_since = -1;

                if (c4pro_hb_phase == 13 &&
                    c4pro_usb_diag_outn_count == c4pro_stall_last_outn)
                {
                    /* 100ms window. Residual lost completions still occur
                     * (~15/128MB even with C4PRO_USB_IRQ_DRAIN), so the window
                     * IS a throughput component: at 2s per recovery a 128MB
                     * write lost ~60s (measured 4 -> 1.4 MB/s); at 100ms the
                     * same stalls cost ~1.5s total. Firing early on a HEALTHY
                     * paused receive is safe because the kick itself now
                     * re-validates under the IRQ mask (live INT2FLAG pending +
                     * DMAOUTCTL re-read) and bails if any completion is in
                     * flight -- the RXCLR ACKed-packet-discard race that made
                     * a short window dangerous is closed at the kick, not the
                     * window. Aborting a truly-quiet armed receive is
                     * position-exact by design (nothing was ACKed). */
                    if (c4pro_stall_since < 0)
                        c4pro_stall_since = current_tick;
                    else if (TIME_AFTER(current_tick,
                                        c4pro_stall_since + HZ / 10))
                    {
                        c4pro_usb_rx_stall_kick();
                        c4pro_stall_since = -1;
                        lcd_putsf(0, 9, "K%u D%u",
                                  c4pro_usb_diag_rx_kick_busy,
                                  c4pro_usb_diag_rx_kick_done);
                        lcd_update();
                    }
                }
                else
                {
                    c4pro_stall_last_outn = c4pro_usb_diag_outn_count;
                    c4pro_stall_since = -1;
                }
            }
#endif

#ifdef C4PRO_USB_RX_TIMING
            /* Per-packet cost breakdown on the bulk-OUT MSC pipe. w = avg/max us
             * between packets WITHIN a buffer (per-packet receive latency),
             * g = avg/max us across a BUFFER boundary (usb-thread + serialized
             * NAND write), h = avg us inside out_intr. Throttled to ~1s so the
             * LCD update does not perturb what it measures. */
            {
                extern volatile uint32_t c4pro_rxt_win_sum, c4pro_rxt_win_cnt, c4pro_rxt_win_max;
                extern volatile uint32_t c4pro_rxt_gap_sum, c4pro_rxt_gap_cnt, c4pro_rxt_gap_max;
                extern volatile uint32_t c4pro_rxt_h_sum, c4pro_rxt_h_cnt;
                static long c4pro_rxt_next;
                if (c4pro_rxt_win_cnt && TIME_AFTER(current_tick, c4pro_rxt_next))
                {
                    c4pro_rxt_next = current_tick + HZ;
                    lcd_putsf(0, 6, "w%lu/%lu n%lu",
                              c4pro_rxt_win_sum / c4pro_rxt_win_cnt / 50,
                              c4pro_rxt_win_max / 50, c4pro_rxt_win_cnt);
                    lcd_putsf(0, 7, "g%lu/%lu n%lu",
                              c4pro_rxt_gap_cnt ? c4pro_rxt_gap_sum / c4pro_rxt_gap_cnt / 50 : 0,
                              c4pro_rxt_gap_max / 50, c4pro_rxt_gap_cnt);
                    {
                        extern volatile uint32_t c4pro_wp_load_sum, c4pro_wp_tprog_sum, c4pro_wp_cnt, c4pro_wp_start_sum;
                        /* SW = entry wait (previous page's tPROG, paid serially),
                         * LD = 8 per-sector BCH FLCTL loads, TP = 0x10 confirm.
                         * us/page. SW+LD are the two real levers. */
                        lcd_putsf(0, 8, "SW%lu LD%lu TP%lu",
                              c4pro_wp_cnt ? c4pro_wp_start_sum / c4pro_wp_cnt / 50 : 0,
                              c4pro_wp_cnt ? c4pro_wp_load_sum / c4pro_wp_cnt / 50 : 0,
                              c4pro_wp_cnt ? c4pro_wp_tprog_sum / c4pro_wp_cnt / 50 : 0);
                    }
#ifdef C4PRO_NAND_DIE_INTERLEAVE
                    /* B = batched multi-die flushes, N = avg dies/batch (~4 =>
                     * overlap, ~1 => serial fallback), S = serial single-slot
                     * flushes (evictions). S >> B => batching not firing. */
                    {
                        extern volatile uint32_t c4pro_wp_load_sum, c4pro_wp_tprog_sum, c4pro_wp_cnt;
                        /* LD = avg us/page in the 8 per-sector BCH FLCTL loads,
                         * TP = avg us/page in 0x10 + tPROG wait. Whichever
                         * dominates is the real write wall. */
                        lcd_putsf(0, 9, "LD%lu TP%lu",
                                  c4pro_wp_cnt ? c4pro_wp_load_sum / c4pro_wp_cnt / 50 : 0,
                                  c4pro_wp_cnt ? c4pro_wp_tprog_sum / c4pro_wp_cnt / 50 : 0);
                    }
#endif
                    lcd_update();
                }
            }
#endif

            /* BACK held long enough -> drop into mask-ROM RKUSB download mode
             * (071b:3201) for a CE#-free reflash. rk27xx_enter_rkusb() never
             * returns; give the user an on-screen confirmation first, and paint
             * a progress hint partway through the hold so a partial press is
             * visibly ignored. */
            if (btn == BUTTON_BACK)
            {
                if (++back_samples == C4PRO_BACK_UPDATE_SAMPLES / 2)
                {
                    lcd_puts(0, 2, "Keep holding BACK...");
                    lcd_update();
                }
                if (back_samples >= C4PRO_BACK_UPDATE_SAMPLES)
                {
                    /* Reliable CE#-free entry: DON'T jump to the ROM download
                     * handler from here -- USB is already enumerated as MSC, so
                     * the host never re-enumerates and never sees 071b:3201.
                     * Instead request it via the loader across a watchdog reset:
                     * write the magic to the shared HANDOFF DRAM slot (DRAM
                     * survives a WDT reset), flush it to physical DRAM (the loader
                     * reads it uncached at entry), then reboot. The loader sees
                     * the flag with a fresh post-reset USB -- the exact cold-boot
                     * condition under which enter_rkusb() enumerates reliably. */
                    volatile uint32_t *handoff =
                        (volatile uint32_t *)0x606fff00u;

                    lcd_clear_display();
                    lcd_puts(0, 0, "Entering USB flash");
                    lcd_puts(0, 1, "mode, rebooting...");
                    lcd_update();
                    c4pro_usb_diag_exit_storage_mode();
                    handoff[4] = 0xC4E9B00Bu;   /* HANDOFF_RKUSB_MAGIC */
                    commit_discard_dcache_range((void *)handoff, 64);
                    sleep(HZ / 4);
                    commit_discard_dcache_range((void *)handoff, 64);
                    system_reboot();            /* never returns */
                }
            }
            else
                back_samples = 0;
        }

#ifdef C4PRO_BOOT_HEARTBEAT
        /* Liveness heartbeat. Four counters, each owned by a different context:
         *   T - kernel tick (INT_TIMER0, kernel-rk27xx.c)
         *   U - this main/UI-serve loop
         *   W - usb thread's write path, per completed WRITE(10) buffer
         *       (usb_storage.c, bumped after storage_write_sectors returns)
         *   D - TMR0CON timer-drain fires in irq_handler (system-rk27xx.c)
         *
         * PACED WITH sleep() (cooperative, below), NOT udelay(): the previous
         * udelay busy-wait pinned the CPU and starved the usb thread, so the
         * copy never ran (W stayed 0) and the real hang was never reproduced.
         * sleep() yields, so the usb/copy thread runs at FULL speed and a
         * genuine sustained write is reproduced. Repaint is throttled to ~100ms
         * so LCD traffic is negligible; button polling keeps the 20ms cadence.
         *
         * Trade-off: sleep() depends on the tick, so if the tick itself dies the
         * painter also freezes. Acceptable because (a) the copy must run for the
         * hang to reproduce, and (b) the leading hypothesis is that the hang is
         * NOT a tick-freeze - in which case T keeps climbing and the frozen
         * counter pinpoints the culprit. Interpretation when the copy hangs:
         *   T,U,D climb, W frozen    -> usb thread stuck in the write/NAND path
         *                               (copy hang; scheduler + tick alive)
         *   T,D climb, U+W frozen     -> UI/serve loop wedged, tick alive
         *   everything frozen         -> tick died / total lockup; D's last value
         *                               shows whether the drain was still firing
         *   D never climbs under load -> the TMR0CON drain is not firing */
        {
            extern volatile unsigned long c4pro_hb_tick;
            extern volatile unsigned long c4pro_hb_usb;
            extern volatile unsigned long c4pro_hb_drain;
            extern volatile uint32_t c4pro_hb_phase;             /* write stage */
            extern volatile unsigned int c4pro_uirq_last_src;    /* true UIRQ src */
            /* R = host bus-reset count, X = last INT2FLAG (UDC event bits) */
            extern volatile unsigned c4pro_usb_diag_reset_count;
            extern volatile unsigned c4pro_usb_diag_last_intsrc;
#ifdef C4PRO_FAULT_VISIBLE
            /* F = CPU-fault count: F>0 at a freeze => UIE was entered => it IS a
             * fault (that couldn't paint). F==0 => never faulted => busy-spin. */
            extern volatile uint32_t c4pro_fault_count;
#endif
            /* Diag only: force the backlight full-on EVERY serve iteration and
             * override any idle dim, so the heartbeat (and a fault frame) stays
             * readable for the whole copy even with no button presses. If the
             * device freezes, the PWM holds its last (on) state -> counters
             * remain visible. */
            extern void backlight_hw_on(void);
            extern void backlight_hw_brightness(int brightness);
            static unsigned long c4pro_hb_ui;
            static unsigned int c4pro_hb_div;

            backlight_hw_on();
            backlight_hw_brightness(MAX_BRIGHTNESS_SETTING);

            c4pro_hb_ui++;
            if ((c4pro_hb_div++ % 5u) == 0u)   /* ~every 100ms (5 x 20ms) */
            {
                unsigned int s = c4pro_uirq_last_src & 0x1f;
#ifdef C4PRO_FAULT_VISIBLE
                unsigned long f = (unsigned long)c4pro_fault_count;
#else
                unsigned long f = 0;
#endif
#ifdef C4PRO_CACHE_SPIN_CAP
                extern volatile uint32_t c4pro_cache_spin_timeouts;
                unsigned long c = (unsigned long)c4pro_cache_spin_timeouts;
#else
                unsigned long c = 0;
#endif

                lcd_putsf(0, 4, "T%lu U%lu", c4pro_hb_tick, c4pro_hb_ui);
                lcd_putsf(0, 5, "W%lu D%lu P%lu", c4pro_hb_usb, c4pro_hb_drain,
                          (unsigned long)c4pro_hb_phase);
                /* DECISIVE LINE: F = fault count, R = bus-reset count,
                 * X = last INT2FLAG (UDC event bits). */
                lcd_putsf(0, 6, "F%lu R%u", f,
                          c4pro_usb_diag_reset_count);
                /* O = bulk-OUT receive completions (out_intr). Compare to bytes
                 * written: O ~= totalbytes/512 => RX re-arms per 512B packet
                 * (per-packet overhead = the write-speed cap); O ~= buffers =>
                 * per-buffer. This is the step-1 diagnostic for the RX rework. */
                {
                    extern volatile unsigned c4pro_usb_diag_outn_count;
                    lcd_putsf(0, 7, "X%x O%u", c4pro_usb_diag_last_intsrc,
                              c4pro_usb_diag_outn_count);
                }
                /* C = cache-op busy-wait timeouts: C>0 means the CACHEOP cap
                 * fired -> the masked-IRQ cache spin WAS the hang, now survived. */
                lcd_putsf(0, 8, "IRQ%u C%lu", s, c);
#ifdef C4PRO_USB_RX_DMA_GUARD
                /* B = pre-arm found OUT DMA still busy (the race the guard
                 * absorbs); S = bounded wait expired -> engine was stuck and
                 * got abort+RXCLR recovery at arm time (freeze doc s7). */
                {
                    extern volatile unsigned c4pro_usb_diag_rx_arm_busy_count;
                    extern volatile unsigned c4pro_usb_diag_rx_arm_stuck_count;
                    lcd_putsf(0, 9, "B%u S%u",
                              c4pro_usb_diag_rx_arm_busy_count,
                              c4pro_usb_diag_rx_arm_stuck_count);
                }
#endif
                lcd_update();
            }
        }
#endif
        sleep(HZ / 50);
    }

    if (eject)
    {
        lcd_clear_display();
        lcd_puts(0, 0, "Ejecting USB...");
        lcd_update();
        c4pro_usb_diag_exit_storage_mode();
        sleep(HZ / 2);
    }
}
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_INTC_PROBE)
/* On-device localization of the INTC_SCR-priority boot hang. Run at an
 * LCD-available point (after init()) so the effect of writing INTC priorities
 * on the kernel tick is directly observable. Does the same SCR write that
 * C4PRO_USB_TIMER_PRIO used to do (all sources -> level 1, TIMER0 -> level 2),
 * then paints INTC_ISR/IPR and a current_tick heartbeat in a tick-INDEPENDENT
 * loop (yield()+udelay(), never sleep()), so if the SCR write kills the tick we
 * see current_tick (tick/d) FREEZE while the loop keeps repainting. Interpret:
 *   tick 'd' keeps climbing after POST SCR -> SCR write is harmless; hang is
 *                                             elsewhere.
 *   tick 'd' frozen at 0 after POST SCR     -> the SCR write killed the kernel
 *                                             tick; read ISR/IPR to see what the
 *                                             INTC now reports.
 * ftl=5A confirms ftl_init ran during init(); ftl=00 means storage init was
 * never reached. Never returns. */
extern volatile unsigned long c4pro_intc_ftl_mark;   /* set at ftl_init entry */
static void c4pro_intc_probe(void)
{
    volatile unsigned long *intc = (volatile unsigned long *)0x18080000u;
    unsigned long isr_pre, ipr_pre;
    long tick0;
    int n;

    lcd_setfont(FONT_SYSFIXED);
    lcd_clear_display();
    lcd_puts(0, 0, "INTC PROBE");

    isr_pre = intc[0x104 / 4];
    ipr_pre = intc[0x108 / 4];
    lcd_putsf(0, 1, "pre ISR=%08lx", isr_pre);
    lcd_putsf(0, 2, "pre IPR=%08lx", ipr_pre);
    lcd_putsf(0, 3, "ftl=%02lx", c4pro_intc_ftl_mark & 0xffu);
    lcd_update();

    /* The suspect write (stock-faithful): all sources level 1, TIMER0 level 2.
     * SCRn is at intc + n (word index = byte offset n*4 / 4). */
    for (n = 0; n <= 23; n++)
        intc[n] = (intc[n] & ~0xc0u) | 0x40u;
    intc[2] = (intc[2] & ~0xc0u) | 0x80u;

    tick0 = current_tick;
    for (;;)
    {
        int k;

        lcd_putsf(0, 4, "POST SCR");
        lcd_putsf(0, 5, "ISR=%08lx", intc[0x104 / 4]);
        lcd_putsf(0, 6, "IPR=%08lx", intc[0x108 / 4]);
        lcd_putsf(0, 7, "tk=%ld d=%ld", current_tick, current_tick - tick0);
        lcd_update();

        for (k = 0; k < 10; k++)
        {
            yield();
            udelay(2000);
        }
    }
}
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
    C4PRO_BOOT_STEP("init done");
    C4PRO_BOOT_HALT("init complete");
    FOR_NB_SCREENS(i)
    {
        screens[i].clear_display();
        screens[i].update();
    }
    C4PRO_BOOT_STEP("screens ok");
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_INTC_PROBE)
    c4pro_intc_probe();   /* does the SCR write + tick-independent display; never returns */
#endif
    list_init();
    C4PRO_BOOT_STEP("list ok");
    C4PRO_BOOT_STEP("tree_init");
    tree_init();
    C4PRO_BOOT_STEP("tree ok");
#if defined(HAVE_DEVICEDATA) && !defined(BOOTLOADER) /* SIMULATOR */
    verify_device_data();
#endif
    /* Keep the order of this 3
     * Must be done before any code uses the multi-screen API */
#if defined(HAVE_USBSTACK) && \
    (!defined(COLORFLY_C4_PRO) || defined(C4PRO_USB_DIAG_EXPORT) || \
     defined(C4PRO_USB_MSC_ENABLE))
    /* All threads should be created and public queues registered by now */
    usb_start_monitoring();
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_USB_MSC_ENABLE)
    c4pro_usb_msc_serve();
#endif
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
    C4PRO_BOOT_STEP("startdir ok");
    /* no calls INIT_ATTR functions after this point anymore!
     * see definition of INIT_ATTR in config.h */
    C4PRO_BOOT_STEP("root_menu");
    C4PRO_PRE_ROOT_MENU_HALT("pre root_menu");
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_FTL_SYNTH_AUDIT)
    c4pro_show_ftl_synth_audit();
#endif
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_FTL_TARGET_SCAN)
#ifdef C4PRO_FTL_LBA_SLOT_PROBE
    c4pro_show_lba_slot_probe();
#elif defined(C4PRO_FTL_NEAR_HASH_PROBE)
    c4pro_show_near_hash_probe();
#elif defined(C4PRO_FTL_HIT_PAGE_PROBE)
    c4pro_show_hit_page_probe();
#elif defined(C4PRO_FTL_META_TAG_PROBE)
    c4pro_show_meta_tag_probe();
#else
    c4pro_show_target_scan();
#endif
#endif
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
    /* C4 Pro: backlight subsystem re-enabled so brightness control works.
     * backlight_init() starts the thread that applies brightness changes
     * (backlight_set_brightness -> queue -> backlight_hw_brightness). */
    backlight_init();
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

#if !defined(COLORFLY_C4_PRO) || defined(C4PRO_USB_DIAG_EXPORT) || \
    defined(C4PRO_USB_MSC_ENABLE)
    usb_init();
#endif
#if CONFIG_USBOTG == USBOTG_ISP1362
    isp1362_init();
#elif CONFIG_USBOTG == USBOTG_M5636
    m5636_init();
#endif

    /* C4 Pro: backlight subsystem re-enabled so brightness control works.
     * backlight_init() starts the thread that applies brightness changes
     * (backlight_set_brightness -> queue -> backlight_hw_brightness). */
    backlight_init();

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

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(C4PRO_PCM_TONE_TEST)
    pcm_init();
    C4PRO_BOOT_STEP("pcm ok");
    c4pro_pcm_tone_test();
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

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_FTL_SYNTH_AUDIT)
    c4pro_show_ftl_synth_audit();
#endif

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_APP_SYSTEM_INSTALLER) && \
    defined(C4PRO_APP_NATIVE_ROCKBOX_INSTALLER)
    c4pro_app_native_rockbox_installer();
#endif

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
#ifdef C4PRO_FTL_SYNTH_AUDIT
            c4pro_ftl_synth_audit();
            lcd_setfont(FONT_SYSFIXED);
            lcd_clear_display();
            lcd_puts(0, 0, "C4AUDIT mount");
            lcd_putsf(0, 1, "t%d ok%d", c4pro_ftl_audit_total,
                      c4pro_ftl_audit_ok);
            lcd_putsf(0, 2, "miss%d bad%d", c4pro_ftl_audit_missing,
                      c4pro_ftl_audit_bad);
            lcd_putsf(0, 3, "l%d p%d", c4pro_ftl_audit_first_lba,
                      c4pro_ftl_audit_first_psec);
            lcd_putsf(0, 4, "m%d", c4pro_ftl_audit_first_mode);
            lcd_putsf(0, 5, "e%08x", c4pro_ftl_audit_first_expected);
            lcd_putsf(0, 6, "a%08x", c4pro_ftl_audit_first_actual);
            lcd_puts(0, 7, "press key");
            lcd_update();
            button_clear_queue();
            while (button_get_w_tmo(HZ / 2) == BUTTON_NONE)
                ;
            button_clear_queue();
#endif
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
#ifdef C4PRO_USB_DIAG_EXPORT
            if (usb_detect() == USB_INSERTED)
            {
                lcd_puts(0, line++, "usb diag");
                lcd_update();
                c4pro_show_usb_diag_loop();
            }
#endif
#if defined(C4PRO_USB_MSC_ENABLE)
            /* The owned volume mounted in the FTL but carries no FAT yet (e.g.
             * just reformatted after the v3->v4 ECC migration). Expose the raw
             * device as USB Mass Storage so the host can lay down a filesystem,
             * then reboot to mount the new FAT -- instead of dead-looping here,
             * unreachable, because this branch runs in init() before main()'s
             * own USB-MSC serving block. */
            usb_start_monitoring();
            c4pro_usb_msc_serve();
#else
            while (1)
            {
                lcd_update();
                sleep(HZ / 2);
            }
#endif
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
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_APP_SYSTEM_INSTALLER)
#ifdef C4PRO_APP_LOADPATH_CORRUPTOR
    c4pro_app_loadpath_corruptor();
#elif defined(C4PRO_APP_BOOT_INSTALLER)
    c4pro_app_boot_installer();
#elif !defined(C4PRO_APP_NATIVE_ROCKBOX_INSTALLER)
    c4pro_app_system_installer();
#else
#endif
#endif
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && \
    defined(C4PRO_USB_DIAG_EXPORT)
    if (usb_detect() == USB_INSERTED)
    {
        c4pro_show_usb_diag_loop();
    }
#endif
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
#ifdef C4PRO_STORAGE_VERDICT
    c4pro_show_storage_verdict();
#endif
#ifdef C4PRO_FTL_TARGET_SCAN
#ifdef C4PRO_FTL_LBA_SLOT_PROBE
    c4pro_show_lba_slot_probe();
#elif defined(C4PRO_FTL_NEAR_HASH_PROBE)
    c4pro_show_near_hash_probe();
#elif defined(C4PRO_FTL_HIT_PAGE_PROBE)
    c4pro_show_hit_page_probe();
#elif defined(C4PRO_FTL_META_TAG_PROBE)
    c4pro_show_meta_tag_probe();
#else
    c4pro_show_target_scan();
#endif
#endif
#ifdef C4PRO_FTL_PROBE
    c4pro_show_ftl_probe();
#endif
#endif
#if !defined(COLORFLY_C4_PRO) || !defined(C4PRO_PCM_TONE_TEST)
    pcm_init();
    C4PRO_BOOT_STEP("pcm ok");
#endif
    dsp_init();
    C4PRO_BOOT_STEP("dsp ok");

    CHART(">settings_load");
    C4PRO_BOOT_STEP("settings_load");
    settings_load();
    CHART("<settings_load");
    C4PRO_BOOT_STEP("settings ok");
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
    global_settings.dirfilter = SHOW_ALL;
#endif

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

    CHART(">audio_init");
    C4PRO_BOOT_STEP("audio_init");
    audio_init();
    CHART("<audio_init");
    C4PRO_BOOT_STEP("audio ok");
    talk_announce_voice_invalid(); /* notify user w/ voice prompt if voice file invalid */
    C4PRO_BOOT_STEP("talk ok");
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
