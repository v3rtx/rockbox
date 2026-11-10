/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
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
#if defined(COLORFLY_C4_PRO)
#include "nand-target.h"
#endif

#if defined(COLORFLY_C4_PRO) && \
    defined(C4PRO_APP_SYSTEM_INSTALLER) && \
    defined(C4PRO_APP_NATIVE_ROCKBOX_INSTALLER)
#ifdef C4PRO_APP_SYSTEM_INSTALLER_EMBEDDED
#include "c4pro-installer-payload.h"
#endif

#define C4PRO_INSTALL_PAGES_PER_BLOCK 128u

static unsigned char c4pro_install_page[4096];
static unsigned char c4pro_install_verify[4096];
static unsigned char c4pro_install_verify_spare[128];
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

#ifndef C4PRO_INSTALLER_NO_AUTOREBOOT
static void c4pro_installer_reboot(void) NORETURN_ATTR;
static void c4pro_installer_reboot(void)
{
    /* Auto cold-boot the freshly-flashed firmware instead of requiring a manual
     * power-cycle (awkward once the battery is attached). Clear the RKUSB-request
     * handoff slot so the loader boots the payload normally rather than dropping
     * to download mode, show "done" briefly, then WDT-reset: the BootROM re-reads
     * the NAND boot pages -> a normal cold boot into the new firmware. Only used
     * on the SUCCESS paths; failures still halt so the error stays on screen. */
    volatile uint32_t *handoff = (volatile uint32_t *)0x606fff00u;
    handoff[4] = 0u;   /* HANDOFF[4] = 0: loader boots normally, not into RKUSB */
    /* Flush to physical DRAM -- the loader reads HANDOFF uncached at entry (its
     * cache is off), matching the BACK-hold and USB-intercept reboot paths. */
    commit_discard_dcache_range((void *)handoff, 64);
    sleep(2 * HZ);
    commit_discard_dcache_range((void *)handoff, 64);
    system_reboot();
    while (1)   /* unreachable; satisfies NORETURN */
        ;
}
#endif

#ifdef C4PRO_INSTALLER_NO_AUTOREBOOT
#define c4pro_installer_finish() c4pro_installer_halt()
#else
#define c4pro_installer_finish() c4pro_installer_reboot()
#endif

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

static uint32_t c4pro_install_get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

#ifdef C4PRO_PAYLOAD_HWECC
/* Write/read the payload via the hardware buffered-ECC engine (the same path the
 * FTL/owned volume uses successfully), so the loader's fast buffered read reads
 * it byte-identically. Hardware generates the ECC (8 sectors/page); the software
 * spare is unused. This replaces the raw/linear payload write that the buffered
 * read could not decode. */
static uint32_t c4pro_native_nand_write_page_hwecc(uint32_t page,
                                                   const void *data)
{
    int old_irq = disable_irq_save();
    uint32_t rc = c4pro_nand_write_page_hwecc(0, page, data, 8u);
    restore_irq(old_irq);
    return rc;
}

static uint32_t c4pro_native_nand_read_page_hwecc(uint32_t page, void *data)
{
    int old_irq = disable_irq_save();
    uint32_t rc = c4pro_nand_read_page_hwecc(0, page, data, 8u);
    restore_irq(old_irq);
    return rc;
}
#endif

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

#ifdef C4PRO_PAYLOAD_HWECC
            if (c4pro_native_nand_write_page_hwecc(phys_page,
                                                   c4pro_install_page))
            {
                if (!fatal)
                    return -(int)(page + 1u);
                c4pro_installer_fail(label, (int)page);
            }

            if (c4pro_native_nand_read_page_hwecc(phys_page,
                                                  c4pro_install_verify))
            {
                if (!fatal)
                    return -(int)(page + 1u);
                c4pro_installer_fail("payload hwecc read", (int)page);
            }
#else
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
#endif

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
        /* Read back with the SAME engine the payload was written with (and that
         * the loader reads it with at boot). HWECC pages carry inline per-sector
         * ECC; a linear read would slurp those ECC bytes as data and CRC to
         * garbage. */
#ifdef C4PRO_PAYLOAD_HWECC
        if (c4pro_native_nand_read_page_hwecc(start_page,
                                              c4pro_install_verify))
            return -1;
#else
        if (c4pro_native_nand_read_linear_page(start_page,
                                               c4pro_install_verify))
            return -1;
#endif

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
        /* Byte-level detail of the write-verify mismatch (last candidate): which
         * candidate/page, the failing byte offset within the page, and the
         * expected vs actual byte read back. A 1-bit delta => marginal
         * program/ECC; act=ff => page never programmed; garbage => bad block. */
        lcd_putsf(0, 5, "fc%lu fpg%lu foff%lu",
                  (unsigned long)c4pro_native_fail_candidate,
                  (unsigned long)c4pro_native_fail_page,
                  (unsigned long)c4pro_native_fail_off);
        lcd_putsf(0, 6, "exp_b %02lx act_b %02lx",
                  (unsigned long)((c4pro_native_fail_expact >> 8) & 0xffu),
                  (unsigned long)(c4pro_native_fail_expact & 0xffu));
        lcd_update();
        c4pro_installer_halt();
    }

    c4pro_installer_status("native install", "erase boot", 0, boot_pages);
    if (c4pro_native_nand_erase_block(0))
        c4pro_installer_fail("erase boot", 0);

    c4pro_native_write_pages("boot ok", 0, boot_image, boot_pages, true);

    c4pro_installer_status("done", "native", payload_start_page, boot_pages);
    c4pro_installer_finish();
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
    CHART(">init");
    init();
    CHART("<init");
    FOR_NB_SCREENS(i)
    {
        screens[i].clear_display();
        screens[i].update();
    }
    list_init();
    tree_init();
#if defined(HAVE_DEVICEDATA) && !defined(BOOTLOADER) /* SIMULATOR */
    verify_device_data();
#endif
    /* Keep the order of this 3
     * Must be done before any code uses the multi-screen API */
#ifdef HAVE_USBSTACK
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
    CHART(">root_menu");
    root_menu();
}

/* The disk isn't ready at boot, rblogo is stored in bin and erased after boot */
int show_logo_boot( void ) INIT_ATTR;
int show_logo_boot( void )
{
#if defined(COLORFLY_C4_PRO)
    lcd_clear_display();
    lcd_update();
    return 0;
#else
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
#endif /* COLORFLY_C4_PRO */
}

#ifdef HAVE_DIRCACHE
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
                backlight_on();
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

#ifdef HAVE_TAGCACHE
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
        backlight_on();
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
    prng_init();

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
#ifdef HAVE_TAGCACHE
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
    core_allocator_init();
    kernel_init();

#if defined(HAVE_BOOTDATA) && !defined(BOOTLOADER)
    verify_boot_data();
#endif

#if defined(HAVE_DEVICEDATA) && !defined(BOOTLOADER)
    verify_device_data();
#endif

    /* early early early! */
    filesystem_init();

#ifdef HAVE_ADJUSTABLE_CPU_FREQ
    set_cpu_frequency(CPUFREQ_NORMAL);
#ifdef CPU_COLDFIRE
    coldfire_set_pllcr_audio_bits(DEFAULT_PLLCR_AUDIO_BITS);
#endif
    cpu_boost(true);
#endif

    i2c_init();

    power_init();

    enable_irq();
#if defined(CPU_ARM_CLASSIC)
    enable_fiq();
#endif
    /* current_tick should be ticking by now */
    CHART("ticking");

    unicode_init();
    lcd_init();
#ifdef HAVE_REMOTE_LCD
    lcd_remote_init();
#endif
    FOR_NB_SCREENS(i)
        global_status.font_id[i] = FONT_SYSFIXED;
    font_init();

    settings_reset();

    CHART(">show_logo");
    show_logo_boot();
    CHART("<show_logo");
    lang_init(core_language_builtin, language_strings,
              LANG_LAST_INDEX_IN_ARRAY);

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

    usb_init();
#if CONFIG_USBOTG == USBOTG_ISP1362
    isp1362_init();
#elif CONFIG_USBOTG == USBOTG_M5636
    m5636_init();
#endif

    backlight_init();
#if defined(COLORFLY_C4_PRO)
    backlight_on();
#endif

    button_init();

    /* Don't initialize power management here if it could incorrectly
     * measure battery voltage, and it's not needed for charging. */
#if !defined(NEED_ATA_POWER_BATT_MEASURE) || \
    (CONFIG_CHARGING > CHARGING_MONITOR)
    powermgmt_init();
#endif

#if CONFIG_TUNER
    radio_init();
#endif
    prng_init();  /* After RTC */

#ifdef HAVE_HARDWARE_CLICK
    piezo_init();
#endif

    /* Keep the order of this 3 (viewportmanager handles statusbars)
     * Must be done before any code uses the multi-screen API */
    CHART(">gui_syncstatusbar_init");
    gui_syncstatusbar_init(&statusbars);
    CHART("<gui_syncstatusbar_init");
    CHART(">sb_skin_init");
    sb_skin_init();
    CHART("<sb_skin_init");
    CHART(">gui_sync_wps_init");
    gui_sync_skin_init();
    CHART("<gui_sync_wps_init");
    CHART(">viewportmanager_init");
    viewportmanager_init();
    CHART("<viewportmanager_init");

    CHART(">storage_init");
    rc = storage_init();
    CHART("<storage_init");
    if(rc)
    {
        lcd_clear_display();
        lcd_putsf(0, 1, "Disk error: %d", rc);
        lcd_puts(0, 3, "Press button to debug");
        lcd_update();
        while(!(button_get(true) & BUTTON_REL)); /* DO NOT CHANGE TO ACTION SYSTEM */
#ifdef HAVE_DBG_PORTS
        dbg_ports();
#endif
        panicf("disk: %d", rc);
    }

#if defined(COLORFLY_C4_PRO) && \
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
        rc = disk_mount_all();
        CHART("<disk_mount_all");
        if (rc<=0)
        {
            int line=0;
            lcd_clear_display();
            lcd_putsf(0, line++, "No partition found (%d).", rc);
#ifndef USB_NONE
            lcd_puts(0, line++, "Insert USB cable");
            lcd_puts(0, line++, "and fix it.");
#elif !defined(DEBUG) && !(CONFIG_STORAGE & STORAGE_RAMDISK)
            lcd_puts(0, line++, "Rebooting in 5s");
#endif
            lcd_puts(0, line++, rbversion);

#ifdef STORAGE_GET_INFO
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

#ifndef USB_NONE
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

#if defined(HAVE_MULTIVOLUME) && defined(COLORFLY_C4_PRO)
    init_volume_names();
#endif

    pcm_init();
    dsp_init();

    CHART(">settings_load");
    settings_load();
    CHART("<settings_load");
#if defined(COLORFLY_C4_PRO)
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
    init_battery_tables();
    CHART("<init_battery_tables");
#ifdef HAVE_DIRCACHE
    CHART(">init_dircache(true)");
    rc = init_dircache(true);
    CHART("<init_dircache(true)");
#ifdef HAVE_TAGCACHE
    if (rc < 0)
        tagcache_remove_statefile();
#endif /* HAVE_TAGCACHE */
#endif /* HAVE_DIRCACHE */

    CHART(">settings_apply(true)");
    settings_apply(true);
    CHART("<settings_apply(true)");
#ifdef HAVE_DIRCACHE
    CHART(">init_dircache(false)");
    init_dircache(false);
    CHART("<init_dircache(false)");
#endif
#ifdef HAVE_TAGCACHE
    CHART(">init_tagcache");
    init_tagcache();
    CHART("<init_tagcache");
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
    tree_mem_init();
    filetype_init();

    shortcuts_init();

    CHART(">audio_init");
    audio_init();
    CHART("<audio_init");
    talk_announce_voice_invalid(); /* notify user w/ voice prompt if voice file invalid */

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
    settings_apply_skins();
    CHART(">settings_apply_skins");
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
