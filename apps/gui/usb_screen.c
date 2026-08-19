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

#include <stdio.h>
#include <stdbool.h>
#include "action.h"
#include "font.h"
#include "lcd.h"
#ifdef HAVE_REMOTE_LCD
#include "lcd-remote.h"
#endif
#include "lang.h"
#include "usb.h"
#if defined(HAVE_USBSTACK)
#include "usb_core.h"
#ifdef USB_ENABLE_HID
#include "usb_keymaps.h"
#endif
#endif
#include "settings.h"
#include "led.h"
#include "appevents.h"
#include "usb_screen.h"
#include "skin_engine/skin_engine.h"
#include "playlist.h"
#include "misc.h"
#include "icons.h"

#include "bitmaps/usblogo.h"

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
/* HID is force-disabled for this target in usb_configure_drivers() (the host's
 * usbhid bind times out and resets the device mid-enumeration), so the keypad-
 * mode title is dead UI here -- and its "ensure the USB mode strings get
 * cached" loop faults glyphs in off the NAND before the screen is painted.
 */
#define USB_SCREEN_HID_ACTIVE   false
#else
#define USB_SCREEN_HID_ACTIVE   (global_settings.usb_hid)
#endif

#ifdef HAVE_REMOTE_LCD
#include "bitmaps/remote_usblogo.h"
#endif

#if (CONFIG_STORAGE & STORAGE_MMC)
#include "ata_mmc.h"
#endif

struct usb_screen_vps_t
{
    struct viewport parent;
    struct viewport logo;
#ifndef USB_ENABLE_HID
};
#else
    struct viewport title;
};

int usb_keypad_mode;
static void draw_usb_keypad_mode(struct viewport *title)
{
    struct screen *screen = &screens[SCREEN_MAIN];
    struct viewport *last_vp = screen->set_viewport(title);
    screen->clear_viewport();
    title->flags |= VP_FLAG_ALIGN_CENTER;
    if (title->width > 1)
        screen->puts_scroll(0, 0, str(keypad_mode_name_get(usb_keypad_mode)));
    screen->set_viewport(last_vp);
}
#endif /* USB_ENABLE_HID */

static void handle_usb_events(struct viewport *title)
{
#if (CONFIG_STORAGE & STORAGE_MMC) && !defined(SIMULATOR)
    int next_update = 0;
#endif /* STORAGE_MMC */
    int button;

    /* Don't return until we get SYS_USB_DISCONNECTED */
    while(1)
    {
#ifndef USB_ENABLE_HID
        (void)title;
#else
        if (USB_SCREEN_HID_ACTIVE)
        {
            button = get_hid_usb_action();
            if (button == ACTION_USB_HID_MODE_SWITCH_NEXT ||
                button == ACTION_USB_HID_MODE_SWITCH_PREV)
                draw_usb_keypad_mode(title);
        }
        else
#endif
        {
            /* hid emits the event in get_action */
            send_event(GUI_EVENT_ACTIONUPDATE, NULL);
            button = button_get_w_tmo(HZ/2);
        }
        if (button == SYS_USB_DISCONNECTED)
            return;
        if (button == SYS_CHARGER_DISCONNECTED)
            reset_runtime();

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
        /* BACK held ~2s on the USB screen -> reboot into RKUSB flash mode.
         * Re-homed from the old imperative c4pro_usb_msc_serve loop. */
        {
            extern void c4pro_usb_screen_reflash_poll(void);
            extern void c4pro_usb_screen_chargeonly_poll(void);
            c4pro_usb_screen_reflash_poll();
            c4pro_usb_screen_chargeonly_poll();
        }
#endif

#if defined(COLORFLY_C4_PRO) && defined(C4PRO_USB_RX_TIMING)
        /* Bulk-OUT write bottleneck breakdown (TMR2 @50MHz -> /50 = us).
         * w = avg/max us per 512B packet WITHIN a buffer (receive latency),
         * g = avg/max us at each BUFFER boundary (usb-thread + the SERIALIZED
         * NAND write). Total per 128KB buffer ~= 256*w + g; whichever term is
         * bigger is the wall. SW/LD/TP = NAND per-page entry-wait/load/confirm. */
        {
            extern volatile uint32_t c4pro_rxt_win_sum, c4pro_rxt_win_cnt, c4pro_rxt_win_max;
            extern volatile uint32_t c4pro_rxt_gap_sum, c4pro_rxt_gap_cnt, c4pro_rxt_gap_max;
            extern volatile uint32_t c4pro_wp_load_sum, c4pro_wp_tprog_sum, c4pro_wp_cnt, c4pro_wp_start_sum;
            if (c4pro_rxt_win_cnt)
            {
                lcd_putsf(0, 5, "w%lu/%lu n%lu",
                          (unsigned long)(c4pro_rxt_win_sum / c4pro_rxt_win_cnt / 50),
                          (unsigned long)(c4pro_rxt_win_max / 50),
                          (unsigned long)c4pro_rxt_win_cnt);
                lcd_putsf(0, 6, "g%lu/%lu n%lu",
                          (unsigned long)(c4pro_rxt_gap_cnt ? c4pro_rxt_gap_sum / c4pro_rxt_gap_cnt / 50 : 0),
                          (unsigned long)(c4pro_rxt_gap_max / 50),
                          (unsigned long)c4pro_rxt_gap_cnt);
                if (c4pro_wp_cnt)
                    lcd_putsf(0, 7, "SW%lu LD%lu TP%lu",
                              (unsigned long)(c4pro_wp_start_sum / c4pro_wp_cnt / 50),
                              (unsigned long)(c4pro_wp_load_sum / c4pro_wp_cnt / 50),
                              (unsigned long)(c4pro_wp_tprog_sum / c4pro_wp_cnt / 50));
                lcd_update();
            }
        }
#endif

/* USB-MMC bridge can report activity */
#if (CONFIG_STORAGE & STORAGE_MMC) && !defined(SIMULATOR)
        if(TIME_AFTER(current_tick,next_update))
        {
            if(usb_inserted()) {
                led(mmc_usb_active(HZ));
            }
            next_update=current_tick+HZ/2;
        }
#endif /* STORAGE_MMC */
    }
}

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
/* What this screen can do besides serving the host. Lines 0-2 sit under the
 * status bar and are not readable, so the legend starts at 4. */
static void c4pro_usb_screen_hints(void)
{
    lcd_setfont(FONT_SYSFIXED);
    lcd_puts(0, 4, "PLAY:  charge only");
    lcd_puts(0, 5, "BACK held 2s: flash");
    lcd_update();
}

#endif

static void usb_screen_fix_viewports(struct screen *screen,
        struct usb_screen_vps_t *usb_screen_vps)
{
    int logo_width, logo_height;
    struct viewport *parent = &usb_screen_vps->parent;
    struct viewport *logo = &usb_screen_vps->logo;

#ifdef HAVE_REMOTE_LCD
    if (screen->screen_type == SCREEN_REMOTE)
    {
        logo_width = BMPWIDTH_remote_usblogo;
        logo_height = BMPHEIGHT_remote_usblogo;
    }
    else
#endif
    {
        logo_width = BMPWIDTH_usblogo;
        logo_height = BMPHEIGHT_usblogo;
    }

    viewportmanager_theme_enable(screen->screen_type, true, parent);

    if (logo_width  > parent->width)
        logo_width  = parent->width;
    if (logo_height > parent->height)
        logo_height = parent->height;

    *logo = *parent;
    logo->x = parent->x + parent->width - logo_width;
#ifdef HAVE_LCD_SPLIT
    switch (statusbar_position(screen))
    {
         /* start beyond split */
         case STATUSBAR_OFF:
             logo->y = parent->y + LCD_SPLIT_POS;
             break;
         case STATUSBAR_TOP:
             logo->y = parent->y + LCD_SPLIT_POS - STATUSBAR_HEIGHT;
             break;
         /* start at the top for maximum space */
         default:
             logo->y = parent->y;
             break;
    }
#else
    logo->y = parent->y + (parent->height - logo_height) / 2;
#endif
    logo->width = logo_width;
    logo->height = logo_height;

#ifdef USB_ENABLE_HID
    if (USB_SCREEN_HID_ACTIVE)
    {
        struct viewport *title = &usb_screen_vps->title;
        int char_height = font_get(parent->font)->height;
        *title = *parent;
        title->y = logo->y + logo->height + char_height;
        title->height = char_height;
        /* try to fit logo and title to parent */
        if (parent->y + parent->height < title->y + title->height)
        {
            logo->y = parent->y;
            title->y = parent->y + logo->height;
        }

        int i =0, langid = LANG_USB_KEYPAD_MODE;
        while (langid >= 0) /* ensure the USB mode strings get cached */
        {
            font_getstringsize(str(langid), NULL, NULL, title->font);
            langid = keypad_mode_name_get(i++);
        }
    }
#endif
}

static void usb_screens_draw(struct usb_screen_vps_t *usb_screen_vps_ar)
{
    struct viewport *last_vp;
    static const struct bitmap* logos[NB_SCREENS] = {
        &bm_usblogo,
#ifdef HAVE_REMOTE_LCD
        &bm_remote_usblogo,
#endif
    };
    FOR_NB_SCREENS(i)
    {
        struct screen *screen = &screens[i];
        struct usb_screen_vps_t *usb_screen_vps = &usb_screen_vps_ar[i];
        struct viewport *parent = &usb_screen_vps->parent;
        struct viewport *logo = &usb_screen_vps->logo;

        last_vp = screen->set_viewport(parent);
        screen->clear_viewport();
        screen->backlight_on();
        screen->set_viewport(logo);
        screen->bmp(logos[i], 0, 0);
        screen->set_viewport(last_vp);
    }
#ifdef USB_ENABLE_HID
    if (USB_SCREEN_HID_ACTIVE)
        draw_usb_keypad_mode(&usb_screen_vps_ar[SCREEN_MAIN].title);
#endif
}

void gui_usb_screen_run(bool early_usb, intptr_t seqnum)
{
#ifdef SIMULATOR /* the sim allows toggling USB fast enough to overflow viewportmanagers stack */
    static bool in_usb_screen = false;
    if (in_usb_screen)
        return;
    in_usb_screen = true;
#endif

    struct usb_screen_vps_t usb_screen_vps_ar[NB_SCREENS];
    struct viewport *title = NULL;
#if defined HAVE_TOUCHSCREEN
    enum touchscreen_mode old_mode = touchscreen_get_mode();

    /* TODO: Paint buttons on screens OR switch to point mode and use
     * touchscreen as a touchpad to move the host's mouse cursor */
    touchscreen_set_mode(TOUCHSCREEN_BUTTON);
#endif

    push_current_activity(ACTIVITY_USBSCREEN);

#if defined(USB_ENABLE_HID) && \
    !(defined(COLORFLY_C4_PRO) && !defined(SIMULATOR))
    usb_keypad_mode = global_settings.usb_keypad_mode;
    title = &usb_screen_vps_ar[SCREEN_MAIN].title;
#else
    /* usb_screen_fix_viewports() never initialises the title viewport when HID
     * is inert, so there is nothing valid to point at. */
    title = NULL;
#endif

    FOR_NB_SCREENS(i)
    {
        struct screen *screen = &screens[i];
        /* we might be coming from anywhere, and the originating screen
         * can't be practically expected to cleanup the UI because
         * we're invoked via default_event_handler(), therefore we make a
         * generic cleanup here */
        screen->set_viewport(NULL);
        screen->scroll_stop();
        usb_screen_fix_viewports(screen, &usb_screen_vps_ar[i]);
    }

#if 0 /* handled in usb_screen_fix_viewports() */
    /* update the UI before disabling fonts, this maximizes the propability
     * that font cache lookups succeed during USB */
    send_event(GUI_EVENT_ACTIONUPDATE, NULL);
#endif

#if !(defined(COLORFLY_C4_PRO) && !defined(SIMULATOR))
    if(!early_usb)
    {
        /* The font system leaves the .fnt fd's open, so we need for force close them all */
        font_disable_all();
    }
#endif
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
    /* PAINT FIRST, then do the slow work, then ack. default_event_handler()
     * used to run system_flush() (playlist_shutdown + tree_flush +
     * open_plugin_cache_flush + the storage idle notifys -- seconds of NAND
     * writes on this device) and check_bootfile() BEFORE calling us, so the
     * screen trailed the plug by that whole flush. Both now happen here, after
     * the screen is on the LCD and still before the acknowledge -- the ack is
     * what releases the usb thread to unmount, so files are closed in time.
     *
     * usb_screens_draw() only renders into the framebuffer; the C4's buffered
     * LCD shows nothing until an lcd_update, hence the explicit update.
     */
    usb_screens_draw(usb_screen_vps_ar);
    FOR_NB_SCREENS(i)
        screens[i].update();
    c4pro_usb_screen_hints();

    /* Everything that has to finish before the volume moves, but that the user
     * should not have to wait to SEE the screen. font_disable_all() is in here
     * because font_disable() calls glyph_cache_save() -- a file write per
     * loaded font, i.e. NAND writes; it was the bulk of the 8.4s plug-to-paint
     * gap.
     */
    if(!early_usb)
    {
        font_disable_all();
#ifndef USB_NONE
        /* Guarded like its definition in misc.c. early_usb runs from init(),
         * before the playlist/tree/plugin-cache subsystems system_flush()
         * would touch exist -- stock only ever ran it from
         * default_event_handler().
         */
        {
            extern void c4pro_usb_pre_ack_flush(void);
            c4pro_usb_pre_ack_flush();
        }
#endif
    }
    usb_acknowledge(SYS_USB_CONNECTED_ACK, seqnum);
#else
    usb_acknowledge(SYS_USB_CONNECTED_ACK, seqnum);
    usb_screens_draw(usb_screen_vps_ar);
#endif
    handle_usb_events(title);

#if defined(USB_ENABLE_HID)
    if (USB_SCREEN_HID_ACTIVE)
        screens[SCREEN_MAIN].scroll_stop_viewport(title);
    if (global_settings.usb_keypad_mode != usb_keypad_mode)
    {
        global_settings.usb_keypad_mode = usb_keypad_mode;
        settings_save();
    }
#endif

#ifdef HAVE_TOUCHSCREEN
    touchscreen_set_mode(old_mode);
#endif

    if(!early_usb)
    {
        font_enable_all();
        /* Not pretty, reload all settings so fonts are loaded again correctly */
        settings_apply(true);
        /* Reload playlist */
        int resume = playlist_resume();
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
        /* Charge-only was picked here: the cable is still in and storage is
         * back, so pick the music up where the plug's audio_hard_stop() left
         * it.
         */
        if(usb_powered_only() && resume != -1 &&
           global_status.resume_index != -1)
            playlist_start(global_status.resume_index,
                           global_status.resume_elapsed,
                           global_status.resume_offset);
#else
        (void)resume;
#endif
    }

    FOR_NB_SCREENS(i)
    {
        screens[i].backlight_on();
        viewportmanager_theme_undo(i, false);
    }

    pop_current_activity();
#ifdef SIMULATOR
    in_usb_screen = false;
#endif
}
