/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2002 by Linus Nielsen Feltzing
 *
 * iPod driver based on code from the ipodlinux project - http://ipodlinux.org
 * Adapted for Rockbox in January 2006
 * Original file: podzilla/usb.c
 * Copyright (C) 2005 Adam Johnston
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
#include "cpu.h"
#include "kernel.h"
#include "thread.h"
#include "system.h"
#include "debug.h"
#include "storage.h"
#include "disk.h"
#if defined(COLORFLY_C4_PRO) && !defined(BOOTLOADER)
#include "audio.h"   /* audio_hard_stop() before the C4 USB NAND-quiet */
#endif
#include "panic.h"
#include "lcd.h"
#include "usb.h"
#include "button.h"
#include "string.h"
#ifdef HAVE_USBSTACK
#include "usb_core.h"
#endif
#include "logf.h"
#include "screendump.h"
#include "powermgmt.h"

#ifndef BOOTLOADER
#include "misc.h"
#include "gui/yesno.h"
#include "settings.h"
#include "lang_enum.h"
#include "gui/skin_engine/skin_engine.h"
#endif

#if defined(HIBY_R3PROII) || defined(HIBY_R1) || defined(HIDIZS_AP80MAX)
#include "usb-hiby-gadget.h"
#endif
#if defined(DX50) || defined(DX90)
#include "usb-ibasso.h"
#endif

#if defined(IPOD_ACCESSORY_PROTOCOL)
#include "iap.h"
#endif

#if (!defined(BOOTLOADER) || defined(HAVE_BOOTLOADER_USB_MODE))
#define USB_FULL_INIT
#endif

/* USB detect debouncing interval (200ms taken from the usb polling code) */
#define USB_DEBOUNCE_POLL (200*HZ/1000)
/* NOTE: "usb_dw_gonak_effective:failed!" *PANIC*s can be observed when
         disconnecting the FiiO M3K from USB with the debounce interval
         for USB_STATUS_BY_EVENT set to 200ms, as above (see e75a3fb).
         Adjusting the interval to 10ms reduces likelihood of a panic. */
#define USB_DEBOUNCE_TIME (10*HZ/1000)

bool do_screendump_instead_of_usb = false;

#if !defined(SIMULATOR) && !defined(USB_NONE)
/* We assume that the USB cable is extracted */
static int usb_state = USB_EXTRACTED;
#if (CONFIG_STORAGE & STORAGE_MMC) && defined(USB_FULL_INIT) && !defined(HAVE_USBSTACK)
static int usb_mmc_countdown = 0;
#endif
#if defined(HAVE_USB_POWER) || defined(HAVE_USB_ADB)
#ifdef USB_FULL_INIT
static bool usb_power_only = false;
#endif
static int usb_mode = USBMODE_DEFAULT;
void usb_set_mode(int mode)
{
    usb_mode = mode;
#if defined(DX50) || defined(DX90)
    ibasso_set_usb_mode(mode);
#elif defined(HIBY_R3PROII) || defined(HIBY_R1) || defined(HIDIZS_AP80MAX)
    hiby_set_usb_mode(mode);
#endif
}
#endif

#ifdef USB_FULL_INIT
/* Make sure there's enough stack space for screendump */
#ifndef USB_EXTRA_STACK
#   define USB_EXTRA_STACK 0x0 /*Define in firmware/export/config/[target].h*/
#endif
#ifdef USB_ENABLE_IAP
#define IAP_EXTRA_STACK DEFAULT_STACK_SIZE*2
#else
#define IAP_EXTRA_STACK 0
#endif
static long usb_stack[(DEFAULT_STACK_SIZE*4 + DUMP_BMP_LINESIZE + IAP_EXTRA_STACK + USB_EXTRA_STACK)/sizeof(long)];
static const char usb_thread_name[] = "usb";
static unsigned int usb_thread_entry = 0;
static bool usb_monitor_enabled = false;

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(USB_FULL_INIT)
/* C4PRO USB monitor thread -- see usb_init(). Runs two watchdogs: the
 * marginal-HS-PHY re-present (always) and the sustained-write stall-kick (when
 * RX_STALL_KICK && PHASE_MARK are configured). */
#define C4PRO_USB_MONITOR 1
static long c4pro_usb_mon_stack[DEFAULT_STACK_SIZE/sizeof(long)];
static const char c4pro_usb_mon_name[] = "c4usbmon";
#endif
static bool exclusive_storage_enabled = false;
static bool exclusive_storage_requested = false;
static struct event_queue usb_queue SHAREDBSS_ATTR;
#ifdef USB_ENABLE_HID
static bool usb_hid = true;
#endif
#ifdef USB_ENABLE_SERIAL
static bool usb_serial = false;
#endif
#ifdef USB_ENABLE_AUDIO
static int usb_audio = 0;
#endif
static bool usb_host_present = false;
static int usb_num_acks_to_expect = 0;
static uint32_t usb_broadcast_seqnum = 0x80000000;
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(USB_FULL_INIT)
/* rk27xx driver hooks (firmware/target/arm/rk27xx/usb-drv-rk27xx.c) */
void c4pro_usb_detach(void);
void c4pro_usb_arm_reconnect(void);

/* Set at USB_INSERTED once the connect has been broadcast (so the USB screen is
 * up in step with the audio stop); cleared when the acks land and the real
 * bring-up runs. See the USB_INSERTED comment. */
static bool c4pro_attach_deferred = false;
static long c4pro_attach_deadline = 0;
#endif
#if defined(USB_FIREWIRE_HANDLING)
static void try_reboot(void)
{
#ifdef HAVE_DISK_STORAGE
    storage_sleepnow(); /* Immediately spindown the disk. */
    sleep(HZ*2);
#endif

#ifdef IPOD_ARCH  /* The following code is based on ipodlinux */
#if CONFIG_CPU == PP5020
    memcpy((void *)0x40017f00, "diskmode\0\0hotstuff\0\0\1", 21);
#elif CONFIG_CPU == PP5022
    memcpy((void *)0x4001ff00, "diskmode\0\0hotstuff\0\0\1", 21);
#endif /* CONFIG_CPU */
#endif /* IPOD_ARCH */

#ifdef IPOD_NANO2G
    memcpy((void *)0x0002bf00, "diskmodehotstuff\1\0\0\0", 20);
#endif

    system_reboot(); /* Reboot */
}
#endif /* USB_FIRWIRE_HANDLING */

/* Screen dump */
static inline bool usb_do_screendump(void)
{
    if(do_screendump_instead_of_usb)
    {
        screen_dump();
#ifdef HAVE_REMOTE_LCD
        remote_screen_dump();
#endif /* HAVE_REMOTE_LCD */
        return true;
    }
    return false;
}

#ifdef USB_FIREWIRE_HANDLING
static inline bool usb_reboot_button(void)
{
#ifdef HAVE_USB_POWER
    return (button_status() & ~USBPOWER_BTN_IGNORE);
#else
    return false;
#endif
}
#endif /* USB_FIREWIRE_HANDLING */


/*--- Routines that differ depending upon the presence of a USB stack ---*/

#ifdef HAVE_USBSTACK
/* Enable / disable USB when the stack is enabled - otherwise a noop */
static inline void usb_stack_enable(bool enable)
{
    usb_enable(enable);
}

#ifdef HAVE_HOTSWAP
static inline void usb_handle_hotswap(long id)
{
    switch(id)
    {
    case SYS_HOTSWAP_INSERTED:
    case SYS_HOTSWAP_EXTRACTED:
        usb_core_hotswap_event(1, id == SYS_HOTSWAP_INSERTED);
        break;
    }
    /* Note: No MMC storage handling is needed with the stack atm. */
}
#endif /* HAVE_HOTSWAP */

static inline void usb_configure_drivers(int for_state)
{
#ifdef USB_ENABLE_AUDIO
    // FIXME: doesn't seem to get set when loaded at boot...
    usb_audio = global_settings.usb_audio;
#endif
    switch(for_state)
    {
    case USB_POWERED:
#ifdef USB_ENABLE_STORAGE
        usb_core_enable_driver(USB_DRIVER_MASS_STORAGE, false);
#endif
#ifdef USB_ENABLE_HID
#ifdef USB_ENABLE_CHARGING_ONLY
        usb_core_enable_driver(USB_DRIVER_HID, false);
#else
        usb_core_enable_driver(USB_DRIVER_HID, true);
#endif /* USB_ENABLE_CHARGING_ONLY */
#endif /* USB_ENABLE_HID */
#ifdef USB_ENABLE_SERIAL
        usb_core_enable_driver(USB_DRIVER_SERIAL, usb_serial);
#endif
#ifdef USB_ENABLE_AUDIO
        usb_core_enable_driver(USB_DRIVER_AUDIO, (usb_audio == 1) || (usb_audio == 2)); // while "always" or "only in charge-only mode"
#endif /* USB_ENABLE_AUDIO */
#ifdef USB_ENABLE_IAP
        usb_core_enable_driver(USB_DRIVER_IAP, false);
#endif

#ifdef USB_ENABLE_CHARGING_ONLY
        usb_core_enable_driver(USB_DRIVER_CHARGING_ONLY, true);
#endif

        usb_attach();
        break;
        /* USB_POWERED: */

    case USB_INSERTED:
#ifdef USB_ENABLE_STORAGE
        usb_core_enable_driver(USB_DRIVER_MASS_STORAGE, true);
#endif
#if defined(COLORFLY_C4_PRO)
        /* MASS-STORAGE ONLY on C4PRO. A HID (or other) interface makes the Linux
         * host try to bind usbhid, which times out (-110) on this device and then
         * RESETS the whole device mid-enumeration, killing the SCSI scan before
         * the disk appears (observed in host dmesg). The old imperative serve also
         * force-configured MS-only for the same reason. */
#ifdef USB_ENABLE_HID
        usb_core_enable_driver(USB_DRIVER_HID, false);
#endif
#ifdef USB_ENABLE_AUDIO
        usb_core_enable_driver(USB_DRIVER_AUDIO, false);
#endif
#ifdef USB_ENABLE_IAP
        usb_core_enable_driver(USB_DRIVER_IAP, false);
#endif
#else
#ifdef USB_ENABLE_HID
        usb_core_enable_driver(USB_DRIVER_HID, usb_hid);
#endif
#ifdef USB_ENABLE_SERIAL
        usb_core_enable_driver(USB_DRIVER_SERIAL, usb_serial);
#endif
#ifdef USB_ENABLE_AUDIO
        usb_core_enable_driver(USB_DRIVER_AUDIO, (usb_audio == 1) || (usb_audio == 3)); // while "always" or "only in mass-storage mode"
#endif /* USB_ENABLE_AUDIO */
#ifdef USB_ENABLE_IAP
        usb_core_enable_driver(USB_DRIVER_IAP, true);
#endif
#endif /* COLORFLY_C4_PRO */
#ifdef USB_ENABLE_CHARGING_ONLY
        usb_core_enable_driver(USB_DRIVER_CHARGING_ONLY, false);
#endif

        usb_attach();
        break;
        /* USB_INSERTED: */

    case USB_EXTRACTED:
        /* do not call usb_release_exclusive_storage.
         * usb core handles it */
        break;
        /* USB_EXTRACTED: */
    }
}

static inline void usb_slave_mode(bool on)
{
    int rc;

    if(on)
    {
        disk_unmount_all();
    }
    else
    {
        /* Entered exclusive mode */
        rc = disk_mount_all();
        if(rc <= 0) /* no partition */
            panicf("mount: %d",rc);
    }
}

void usb_signal_transfer_completion(
    struct usb_transfer_completion_event_data* event_data)
{
    queue_post(&usb_queue, USB_TRANSFER_COMPLETION, (intptr_t)event_data);
}

void usb_clear_pending_transfer_completion_events(void)
{
#ifdef HAVE_EXTENDED_MESSAGING_AND_NAME
    while (queue_peek_ex(&usb_queue, NULL,
                         1 | QPEEK_REMOVE_EVENTS,
                         QPEEK_FILTER1(USB_TRANSFER_COMPLETION)));
#endif
}

void usb_signal_notify(long id, intptr_t data)
{
    queue_post(&usb_queue, id, data);
}

#else  /* !HAVE_USBSTACK */

static inline void usb_stack_enable(bool enable)
{
    (void)enable;
}

#ifdef HAVE_HOTSWAP
static inline void usb_handle_hotswap(long id)
{
#if (CONFIG_STORAGE & STORAGE_MMC)
    switch(id)
    {
    case SYS_HOTSWAP_INSERTED:
    case SYS_HOTSWAP_EXTRACTED:
        if(usb_state == USB_INSERTED)
        {
            usb_enable(false);
            usb_mmc_countdown = HZ/2; /* re-enable after 0.5 sec */
        }
        break;
    case USB_REENABLE:
        if(usb_state == USB_INSERTED)
            usb_enable(true);  /* reenable only if still inserted */
        break;
    }
#endif /* STORAGE_MMC */
    (void)id;
}
#endif /* HAVE_HOTSWAP */

static inline void usb_configure_drivers(int for_state)
{
    switch(for_state)
    {
    case USB_POWERED:
        exclusive_storage_requested = false;
        break;
    case USB_INSERTED:
        usb_request_exclusive_storage();
        break;
    case USB_EXTRACTED:
        usb_release_exclusive_storage();
        break;
    }
}

static inline void usb_slave_mode(bool on)
{
    int rc;

    if(on)
    {
        DEBUGF("Entering USB slave mode\n");
        disk_unmount_all();
        storage_soft_reset();
        storage_init();
        storage_enable(false);
        usb_enable(true);
        cpu_idle_mode(true);
    }
    else
    {
        DEBUGF("Leaving USB slave mode\n");

        cpu_idle_mode(false);

        /* Let the ISDx00 settle */
        sleep(HZ*1);

        usb_enable(false);

        rc = storage_init();
        if(rc)
            panicf("storage: %d",rc);

        sleep(HZ/10);
        rc = disk_mount_all();
        if(rc <= 0) /* no partition */
            panicf("mount: %d",rc);
    }
}
#endif /* HAVE_USBSTACK */

static void usb_set_host_present(bool present)
{
    if(usb_host_present == present)
        return;

    usb_host_present = present;

    if(!usb_host_present)
    {
        usb_configure_drivers(USB_EXTRACTED);
        return;
    }

#ifdef HAVE_USB_POWER
    if (usb_power_only)
    {
        /* Only charging is desired */
        usb_configure_drivers(USB_POWERED);
        return;
    }
#endif

    usb_configure_drivers(USB_INSERTED);
}

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(USB_FULL_INIT)
/* The deferred half of a C4 plug, run once every thread has acked the connect
 * (or, as a backstop, when the monitor thread's deadline expires). Ordered so
 * that ALL filesystem and NAND work is finished before the PHY goes live:
 *   unmount (with storage still alive, so the FAT flush actually lands)
 *   -> quiet the NAND (the marginal HS PHY cannot enumerate through NAND/AHB
 *      traffic -- host "error -71")
 *   -> attach.
 * Doing this the other way round -- attaching first and unmounting underneath a
 * live enumeration -- desyncs ep0: the usb thread blocks in the unmount while
 * EP0 completions keep arriving from INT_UDC, and the backlog it then replays
 * panics usb_core ("invalid control response ep_state=4"). */
static void c4pro_finish_deferred_attach(void)
{
    if(!c4pro_attach_deferred)
        return;
    c4pro_attach_deferred = false;

    /* Retire the outstanding handshake BEFORE acting on it. On the backstop
     * path some thread has not acked and may still ack later; left live, that
     * late ack would decrement the counter to zero and take the
     * SYS_USB_CONNECTED_ACK handler's first branch (usb_host_present is true
     * by then), re-running usb_slave_mode(true) -- a disk_unmount_all() in the
     * middle of a live MSC session. Bumping the sequence number makes every
     * outstanding ack stale.
     */
    usb_broadcast_seqnum += 1;
    usb_num_acks_to_expect = 0;

    usb_slave_mode(true);            /* disk_unmount_all */
    exclusive_storage_enabled = true;
    storage_enable(false);
    usb_stack_enable(true);

    /* Enable the class drivers NOW rather than waiting for the first transfer
     * completion to set usb_host_present (USB_DETECT_BY_REQUEST).
     * usb_core_init() has just cleared every driver's enabled flag, so if the
     * host reads the configuration descriptor before the usb thread dequeues
     * that completion it sees an EMPTY configuration -- host dmesg "config 1
     * has no interfaces?", device enumerates as 071b:3203 and no disk ever
     * appears.
     */
    usb_configure_drivers(USB_INSERTED);
}
#endif

/*--- General driver code ---*/
static void NORETURN_ATTR usb_thread(void)
{
    struct queue_event ev;

    while(1)
    {
        queue_wait(&usb_queue, &ev);

        reset_poweroff_timer(); /* Any USB event counts */

        switch(ev.id)
        {
        /*** Main USB thread duties ***/

#ifdef HAVE_USBSTACK
        case USB_NOTIFY_SET_ADDR:
#ifdef USB_DETECT_BY_REQUEST
            /* first sign of a host that addresses before anything else;
             * the class drivers must be on before interfaces are assigned */
            if(usb_state > USB_EXTRACTED) {
                usb_state = USB_INSERTED;
                usb_set_host_present(true);
            }
#endif
            /* intentional fallthrough */
        case USB_NOTIFY_SET_CONFIG:
        case USB_NOTIFY_BUS_RESET:
        case USB_NOTIFY_CLASS_DRIVER:
            if(usb_state <= USB_EXTRACTED)
                break;
            usb_core_handle_notify(ev.id, ev.data);
            break;
        case USB_TRANSFER_COMPLETION:
            if(usb_state <= USB_EXTRACTED)
                break;

#ifdef USB_DETECT_BY_REQUEST
            usb_state = USB_INSERTED;
            usb_set_host_present(true);
#endif

            usb_core_handle_transfer_completion(
                (struct usb_transfer_completion_event_data*)ev.data);
            break;
#endif /* HAVE_USBSTACK */

        case USB_INSERTED:
            if(usb_state != USB_EXTRACTED)
                break;

            if(usb_do_screendump())
            {
                usb_state = USB_SCREENDUMP;
                break;
            }

            usb_state = USB_POWERED;

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
#ifndef BOOTLOADER
            /* CHARGE-ONLY (System > USB > USB mode = Charge, or picked on the
             * USB screen for the rest of this plug). Decided HERE, before
             * anything destructive: the C4 charges from VBUS in hardware
             * (CHARGING_MONITOR), so charge-only means doing nothing at all --
             * playback keeps running, the NAND stays mounted and D+ is never
             * asserted, so the host sees no device. Do NOT fall through to
             * usb_configure_drivers(USB_POWERED): rk27xx has
             * USB_HAS_INTERRUPT, so config.h gives it USB_ENABLE_HID instead
             * of USB_ENABLE_CHARGING_ONLY, and a HID interface is exactly what
             * this device cannot survive (see the USB_INSERTED driver gate).
             * No button flip either (USBPOWER_BTN_IGNORE): the C4's single-
             * ADC-line pad can read a phantom press, and silently not
             * enumerating looks like broken hardware. The setting and the
             * screen are the only two levers.
             */
            usb_power_only = (usb_mode != USB_MODE_MASS_STORAGE);
            if(usb_power_only)
            {
                c4pro_usb_detach();
                break;
            }

            /* Hand storage over NOW, in step with the audio_hard_stop() above,
             * and DEFER the rest of the bring-up until every thread has acked.
             *
             * Stock only reaches usb_request_exclusive_storage() from
             * usb_core's SET_CONFIGURATION handler, i.e. after the host has
             * finished enumerating -- which on this marginal HS PHY takes
             * 3-10s of re-present retries, so the music died long before the
             * USB screen appeared. Requesting here shows the screen in step
             * with the stop.
             *
             * It must NOT be a plain early request, though: with
             * USB_DETECT_BY_REQUEST, usb_host_present is still false at this
             * point (it is set on the first transfer completion), so the
             * SYS_USB_CONNECTED_ACK handler's usb_host_present gate would drop
             * the handover on the floor -- the volume stays mounted and
             * exclusive_storage_enabled stays false. c4pro_attach_deferred
             * makes the ack completion do the unmount, quiet the NAND and only
             * THEN bring the PHY up, so the whole filesystem/NAND settling
             * happens with the bus still dark. The host sees us a beat later;
             * that is the intended trade (the screen is what has to be
             * instant).
             */
            /* Keep D+ down for the whole deferred window: usb_core is not
             * initialised until the attach, and the loader (or a previous
             * session) may have left SOFT_CN asserted -- the INT_UDC idle-guard
             * cannot cover that, VBUS being present. Same premise as the
             * charge-only detach below. */
            c4pro_usb_detach();

            /* Stop playback + close files BEFORE the storage handover. The
             * audio buffer thread may be actively reading the disk (continuous
             * multi-track buffering); moving storage under a live read is
             * unsafe. Measured fast on device (the plug-to-screen gap was
             * never here).
             */
            audio_hard_stop();

            send_event(SYS_EVENT_USB_INSERTED, &usb_mode);
            /* Backstop only for a thread that NEVER acks -- deliberately far
             * longer than the slowest legitimate ack. The main thread's ack
             * trails system_flush() + check_bootfile() (playlist/tree/plugin
             * cache writes: measured 5-9s on this device), so a tight deadline
             * fires mid-flush and unmounts the volume underneath the thread
             * still writing to it, then attaches into that NAND traffic --
             * enumeration fails and ep0 desyncs into
             * "usb_core: invalid control response". */
            c4pro_attach_deadline = current_tick + 60 * HZ;
            c4pro_attach_deferred = true;
            usb_request_exclusive_storage();
            break;
#else /* BOOTLOADER: no threads to hand storage over to -- stock ordering. */
            /* Quiet the NAND BEFORE bringing USB up: the marginal HS PHY's
             * chirp/descriptor reads are corrupted by concurrent NAND/AHB traffic
             * (host "error -71"). storage_enable(false) is the lever that silences
             * the controller -- the LIGHT quiet. Do NOT storage_soft_reset() /
             * storage_init() here: that re-runs the full (slow, 1-2 min on a stale
             * checkpoint) FTL mount scan on every plug -> the player appears to
             * hang -- and per field notes it also hammers/wedges the NAND. The
             * SYS_USB_CONNECTED handshake below (usb_slave_mode) does the real
             * unmount/re-mount once audio is stopped. */
            storage_enable(false);
#endif /* BOOTLOADER */
#endif /* COLORFLY_C4_PRO */
            usb_stack_enable(true);
#ifndef BOOTLOADER
#ifndef HAVE_USB_POWER
            int usb_mode = -1;
#endif
            send_event(SYS_EVENT_USB_INSERTED, &usb_mode);
#endif
#if defined(HAVE_USB_POWER) && \
    !(defined(COLORFLY_C4_PRO) && !defined(SIMULATOR))
            /* Power (charging-only) button */
            usb_power_only = usb_mode != USB_MODE_MASS_STORAGE;
            if(button_status() & ~USBPOWER_BTN_IGNORE) {
                usb_power_only = !usb_power_only;
            }
#endif

#ifndef USB_DETECT_BY_REQUEST
            usb_state = USB_INSERTED;
            usb_set_host_present(true);
#endif
            break;
            /* USB_INSERTED */

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && defined(USB_FULL_INIT)
        case USB_C4PRO_ATTACH_NOW:
            /* Backstop: a thread never acked the connect. Bring USB up anyway
             * -- a late screen beats a plug that never enumerates.
             */
            c4pro_finish_deferred_attach();
            break;

        case USB_C4PRO_CHARGE_ONLY:
            /* "Charge only" picked on the USB screen: detach and give the
             * player its storage back, without waiting for the cable to be
             * pulled. usb_stack_enable(false) -> usb_core_exit ->
             * set_config(0) -> usb_release_exclusive_storage(), which remounts
             * and broadcasts SYS_USB_DISCONNECTED -- the same exit the USB
             * screen already waits for. Re-enable the NAND first: the release
             * path mounts through it.
             */
            if(usb_state == USB_EXTRACTED || usb_power_only)
                break;
            c4pro_attach_deferred = false;
            usb_power_only = true;
            storage_enable(true);
            usb_stack_enable(false);
            /* usb_drv_exit() detaches only if it had initialised */
            c4pro_usb_detach();
            c4pro_usb_arm_reconnect();
            usb_state = USB_POWERED;
            break;
            /* USB_C4PRO_CHARGE_ONLY */
#endif

        case SYS_USB_CONNECTED_ACK:
            if((uint32_t)ev.data != usb_broadcast_seqnum) {
                DEBUGF("usb: late ack %lX < %lX", ev.data, usb_broadcast_seqnum);
                break;
            }
            if(usb_num_acks_to_expect == 0) {
                DEBUGF("usb: unexpected ack");
                break;
            }
            if(--usb_num_acks_to_expect > 0) {
                DEBUGF("usb: got ack, %d to go...\n", usb_num_acks_to_expect);
                break;
            }

            DEBUGF("usb: all threads have acknowledged the connect.\n");
            if(usb_host_present && exclusive_storage_requested) {
                usb_slave_mode(true);
                exclusive_storage_enabled = true;
#ifdef USB_ENABLE_STORAGE
                usb_signal_class_notify(USB_DRIVER_MASS_STORAGE, 0);
#endif
            }
#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
            else if(c4pro_attach_deferred) {
                c4pro_finish_deferred_attach();
            }
#endif
            break;
            /* SYS_USB_CONNECTED_ACK */

        case USB_EXTRACTED:
            if(usb_state == USB_EXTRACTED)
                break;

            if(usb_state == USB_POWERED || usb_state == USB_INSERTED)
                usb_stack_enable(false);

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR)
            {
                extern void c4pro_usb_arm_reconnect(void);
                /* Re-arm the recalibrating connect for the next plug (the
                 * did_soft_connect latch would otherwise skip it on a runtime
                 * replug -> stuck at -71). */
                c4pro_usb_arm_reconnect();
            }
            /* Restore the storage the player needs. USB_INSERTED quiets the NAND
             * (disk_unmount_all + storage_enable(false)); if the exclusive
             * handshake never completed (a plug that failed to enumerate),
             * usb_slave_mode(false) never ran to undo it -> the player shows no
             * files. Re-enable + re-mount here for that case. */
            if(!exclusive_storage_enabled && !usb_power_only)
            {
                storage_enable(true);
                disk_mount_all();
            }
            usb_power_only = false;
            c4pro_attach_deferred = false;
#endif

#ifdef IPOD_ACCESSORY_PROTOCOL
            iap_reset_state(IF_IAP_MP(0));
#endif

            usb_state = USB_EXTRACTED;
#ifndef BOOTLOADER
            send_event(SYS_EVENT_USB_EXTRACTED, NULL);
#endif
            usb_set_host_present(false);
            break;
            /* USB_EXTRACTED: */

        /*** Miscellaneous USB thread duties ***/

        /* HOTSWAP */
#ifdef HAVE_HOTSWAP
        case SYS_HOTSWAP_INSERTED:
        case SYS_HOTSWAP_EXTRACTED:
#if (CONFIG_STORAGE & STORAGE_MMC)
        case USB_REENABLE:
#endif
            usb_handle_hotswap(ev.id);
            break;
#endif

        /* FIREWIRE */
#ifdef USB_FIREWIRE_HANDLING
        case USB_REQUEST_REBOOT:
            if(usb_reboot_button())
                try_reboot();
            break;
#endif

        /* CHARGING */
#if defined(HAVE_USB_CHARGING_ENABLE) && defined(HAVE_USBSTACK)
        case USB_CHARGER_UPDATE:
            usb_charging_maxcurrent_change(usb_charging_maxcurrent());
            break;
#endif

        /* CLOSE */
#ifdef USB_DRIVER_CLOSE
        case USB_QUIT:
            thread_exit();
            break;
#endif
        } /* switch */
    } /* while */
}

#if defined(HAVE_USB_CHARGING_ENABLE) && defined(HAVE_USBSTACK)
void usb_charger_update(void)
{
    queue_post(&usb_queue, USB_CHARGER_UPDATE, 0);
}
#endif

#ifdef USB_STATUS_BY_EVENT
#if 0 /* usb_dw_gonak_effective: failed! PANIC */
static int usb_status_tmo_callback(struct timeout* tmo)
{
    if(usb_monitor_enabled)
    {
        int current_status = usb_detect();
        int* last_status = (int*)tmo->data;

        if(current_status != *last_status)
        {
            /* Signal changed during the timeout; wait longer */
            *last_status = current_status;
            return USB_DEBOUNCE_TIME;
        }

        /* Signal is stable, post the event. The thread will deal with
         * any spurious transitions (like inserted -> inserted). */
        queue_post(&usb_queue, current_status, 0);
    }

    return 0;
}
#endif

void usb_status_event(int current_status)
{
#if 0 /* usb_dw_gonak_effective: failed! PANIC */
    static struct timeout tmo;
    static int last_status = USB_EXTRACTED;
#endif

    /* Caller isn't expected to filter for changes in status.
     * current_status:
     *   USB_INSERTED, USB_EXTRACTED
     */
    if(usb_monitor_enabled)
    {
        int oldstatus = disable_irq_save(); /* Dual-use function */
#if 0 /* usb_dw_gonak_effective: failed! PANIC */
        last_status = current_status;
        timeout_register(&tmo, usb_status_tmo_callback, USB_DEBOUNCE_TIME,
                         (intptr_t)&last_status);
#else
        queue_remove_from_head(&usb_queue, current_status);
        queue_post(&usb_queue, current_status, 0);
#endif
        restore_irq(oldstatus);
    }
}

void usb_start_monitoring(void)
{
    int oldstatus = disable_irq_save(); /* Sync to event */
    int status = usb_detect();

    usb_monitor_enabled = true;

    /* An event may have been missed because it was sent before monitoring
     * was enabled due to the connector already having been inserted before
     * before or during boot. */
    usb_status_event(status);

#ifdef USB_FIREWIRE_HANDLING
    if(firewire_detect())
        usb_firewire_connect_event();
#endif

    restore_irq(oldstatus);
}

#ifdef USB_FIREWIRE_HANDLING
void usb_firewire_connect_event(void)
{
    queue_post(&usb_queue, USB_REQUEST_REBOOT, 0);
}
#endif /* USB_FIREWIRE_HANDLING */

#else /* !USB_STATUS_BY_EVENT */

static void usb_tick(void)
{
    static int usb_countdown = -1;
    static int last_usb_status = USB_EXTRACTED;
#ifdef USB_FIREWIRE_HANDLING
    static int firewire_countdown = -1;
    static int last_firewire_status = false;
#endif

    if(usb_monitor_enabled)
    {
#ifdef USB_FIREWIRE_HANDLING
        int current_firewire_status = firewire_detect();
        if(current_firewire_status != last_firewire_status)
        {
            last_firewire_status = current_firewire_status;
            firewire_countdown = USB_DEBOUNCE_POLL;
        }
        else
        {
            /* Count down until it gets negative */
            if(firewire_countdown >= 0)
                firewire_countdown--;

            /* Report status when the signal has been stable long enough */
            if(firewire_countdown == 0)
            {
                queue_post(&usb_queue, USB_REQUEST_REBOOT, 0);
            }
        }
#endif /* USB_FIREWIRE_HANDLING */

        int current_status = usb_detect();

        /* Only report when the status has changed */
        if(current_status != last_usb_status)
        {
            last_usb_status = current_status;
            usb_countdown = USB_DEBOUNCE_POLL;
        }
        else
        {
            /* Count down until it gets negative */
            if(usb_countdown >= 0)
                usb_countdown--;

            /* Report status when the signal has been stable long enough */
            if(usb_countdown == 0)
            {
                queue_post(&usb_queue, current_status, 0);
            }
        }
    }
#if (CONFIG_STORAGE & STORAGE_MMC)
    if(usb_mmc_countdown > 0)
    {
        usb_mmc_countdown--;
        if(usb_mmc_countdown == 0)
            queue_post(&usb_queue, USB_REENABLE, 0);
    }
#endif
}
void usb_start_monitoring(void)
{
    usb_monitor_enabled = true;
}
#endif /* USB_STATUS_BY_EVENT */

void usb_acknowledge(long id, intptr_t seqnum)
{
    queue_post(&usb_queue, id, seqnum);
}
#endif /* USB_FULL_INIT */

#ifdef C4PRO_USB_MONITOR
/* C4PRO USB monitor: a single PRIORITY_SYSTEM thread at 20ms cadence running two
 * independent watchdogs that must run even while the usb thread is blocked in
 * queue_wait.
 *
 * 1) MARGINAL-HS-PHY RE-PRESENT. The HS PHY is marginal: a plug enumerates only
 *    occasionally (host logs "device descriptor read/64, error -71" on a failed
 *    attempt). Re-present (quiet the NAND, drop D+, reset PHY, re-assert D+)
 *    whenever the cable is in, the host has NOT configured us (DEV_EN clear), and
 *    enumeration has STALLED -- i.e. the setup-packet counter hasn't advanced for
 *    ~5s. A healthy enumeration keeps setups flowing so this never fires
 *    mid-config (a mid-config detach would force the host to restart -> -71).
 *
 * 2) SUSTAINED-WRITE STALL-KICK (re-homed from the old imperative serve loop;
 *    compiled only when RX_STALL_KICK && PHASE_MARK are configured). The armed
 *    bulk-OUT receive (phase 13) silently never completes and the host then
 *    bus-resets at its 30s SCSI timeout; detect "phase 13 with no bulk-OUT
 *    progress for 100ms" and kick the receive engine (the kick re-validates under
 *    the IRQ mask, so an early kick on a healthy paused receive is safe).
 *
 * Both predicates are false in steady state, so the thread never perturbs the
 * serving/write hot path. */
static void c4pro_usb_monitor_thread(void)
{
    extern bool c4pro_usb_vbus_present(void);
    extern bool c4pro_usb_configured(void);
    extern volatile unsigned c4pro_usb_setup_seq;
    extern void c4pro_usb_represent(void);
    unsigned last_setup = 0;
    long setup_change = 0;
    long last_represent = 0;
#if defined(C4PRO_USB_RX_STALL_KICK) && defined(C4PRO_PHASE_MARK)
    extern volatile uint32_t c4pro_hb_phase;
    extern volatile unsigned c4pro_usb_diag_outn_count;
    extern void c4pro_usb_rx_stall_kick(void);
    unsigned last_outn = 0;
    long stall_since = -1;
#endif

    while(1)
    {
        /* (1) re-present the marginal HS PHY when enumeration has stalled */
        {
            unsigned su = c4pro_usb_setup_seq;
            if(su != last_setup)
            {
                last_setup = su;
                setup_change = current_tick;
            }
            /* Re-present a STALLED enumeration fast to cut plug-to-screen latency
             * on the marginal HS PHY. The setup_change gate already protects a
             * PROGRESSING enum (setups keep it fresh), so this only fires when the
             * host has gone quiet (a failed -71 attempt) -- ~1.5s after the last
             * setup is enough for the host to have given up, and retrying sooner
             * shortens the worst case (was 5s -> up to ~10s for two cycles). The
             * light re-present (no controller reset) does not thrash. */
            if(c4pro_attach_deferred)
            {
                /* Bring-up is waiting on the connect acks -- the driver is not
                 * initialised yet, so a re-present here would poke a dark PHY.
                 */
                if(TIME_AFTER(current_tick, c4pro_attach_deadline))
                {
                    /* fire once */
                    c4pro_attach_deadline = current_tick + 60 * HZ;
                    queue_post(&usb_queue, USB_C4PRO_ATTACH_NOW, 0);
                }
            }
            else if(!usb_powered_only() &&
               c4pro_usb_vbus_present() && !c4pro_usb_configured() &&
               TIME_AFTER(current_tick, last_represent + 3 * HZ / 2) &&
               TIME_AFTER(current_tick, setup_change + 3 * HZ / 2))
            {
                last_represent = current_tick;
                /* storage_enable(false) is the light lever that silences the
                 * NAND/AHB traffic corrupting the marginal PHY -- do NOT
                 * storage_soft_reset/init here (that hammers/wedges the NAND). */
                storage_enable(false);
                c4pro_usb_represent();
            }
        }

        /* (2) sustained-write bulk-OUT stall-kick */
#if defined(C4PRO_USB_RX_STALL_KICK) && defined(C4PRO_PHASE_MARK)
        if(c4pro_hb_phase == 13 &&
           c4pro_usb_diag_outn_count == last_outn)
        {
            if(stall_since < 0)
                stall_since = current_tick;
            else if(TIME_AFTER(current_tick, stall_since + HZ / 10))
            {
                c4pro_usb_rx_stall_kick();
                stall_since = -1;
            }
        }
        else
        {
            last_outn = c4pro_usb_diag_outn_count;
            stall_since = -1;
        }
#endif
        sleep(HZ / 50);
    }
}
#endif /* C4PRO_USB_MONITOR */

void usb_init(void)
{
    /* Do required hardware inits first. For software USB the driver has
     * to make sure this won't trigger a transfer completion before the
     * queue and thread are created. */
    usb_init_device();

#ifdef USB_FULL_INIT
    usb_enable(false);

    queue_init(&usb_queue, true);

    usb_thread_entry = create_thread(usb_thread, usb_stack,
                       sizeof(usb_stack), 0, usb_thread_name
                       IF_PRIO(, PRIORITY_SYSTEM) IF_COP(, CPU));

#ifdef C4PRO_USB_MONITOR
    create_thread(c4pro_usb_monitor_thread, c4pro_usb_mon_stack,
                  sizeof(c4pro_usb_mon_stack), 0, c4pro_usb_mon_name
                  IF_PRIO(, PRIORITY_SYSTEM) IF_COP(, CPU));
#endif

#ifndef USB_STATUS_BY_EVENT
    tick_add_task(usb_tick);
#endif
#endif /* USB_FULL_INIT */
}

void usb_wait_for_disconnect(struct event_queue *q)
{
#ifdef USB_FULL_INIT
    struct queue_event ev;

    /* Don't return until we get SYS_USB_DISCONNECTED */
    while(1)
    {
        queue_wait(q, &ev);
        if(ev.id == SYS_USB_DISCONNECTED)
            return;
    }
#endif /* USB_FULL_INIT */
    (void)q;
}

int usb_wait_for_disconnect_w_tmo(struct event_queue *q, int ticks)
{
#ifdef USB_FULL_INIT
    struct queue_event ev;

    /* Don't return until we get SYS_USB_DISCONNECTED or SYS_TIMEOUT */
    while(1)
    {
        queue_wait_w_tmo(q, &ev, ticks);
        switch(ev.id)
        {
            case SYS_USB_DISCONNECTED:
                return 0;
            case SYS_TIMEOUT:
                return 1;
        }
    }
#endif /* USB_FULL_INIT */
    (void)q; (void)ticks;
    return 0;
}

#ifdef USB_DRIVER_CLOSE
void usb_close(void)
{
    unsigned int thread = usb_thread_entry;
    usb_thread_entry = 0;

    if(thread == 0)
        return;

#ifndef USB_STATUS_BY_EVENT
    tick_remove_task(usb_tick);
#endif
    usb_monitor_enabled = false;
    queue_post(&usb_queue, USB_QUIT, 0);
    thread_wait(thread);
}
#endif /* USB_DRIVER_CLOSE */

bool usb_inserted(void)
{
    return usb_state == USB_INSERTED || usb_state == USB_POWERED;
}

#if defined(USB_FULL_INIT)
bool usb_exclusive_storage(void)
{
    /* Storage isn't actually exclusive until slave mode has been entered */
    return exclusive_storage_enabled;
}

/* exclusive storage mode transision
 * HAVE_USBSTACK:
 * (inserted)
 * usb_set_host_present(true)
 *   usb_configure_drivers(USB_INSERTED)
 * ...
 * (SET_CONFIG(n) which requires exclusive storage)
 * usb_core_do_set_config(n)
 *   usb_request_exclusive_storage()
 *     exclusive_storage_requested = true
 * ...
 * (all threads acked)
 * usb_slave_mode(true)
 *   disk_unmount_all()
 * exclusive_storage_enabled = true
 * (exclusive mode done)
 * ...
 * (extracted, or SET_CONFIG(m) which does not require exclusive storage)
 * usb_core_do_set_config(m)
 *   usb_release_exclusive_storage()
 *     exclusive_storage_requested = false
 *     exclusive_storage_enabled = false
 *       usb_slave_mode(false)
 *         disk_mount_all()
 *
 * !HAVE_USBSTACK:
 * (inserted)
 * usb_set_host_present(true)
 *   usb_configure_drivers(USB_INSERTED)
 *     usb_request_exclusive_storage()
 *       exclusive_storage_requested = true
 * ...
 * (all threads acked)
 * usb_slave_mode(true)
 *   disk_unmount_all()
 * exclusive_storage_enabled = true
 * ...
 * (extracted)
 *    usb_set_host_present(false)
 *      usb_configure_drivers(USB_EXTRACTED)
 *          usb_release_exclusive_storage()
 *          ..
 * */

void usb_request_exclusive_storage(void)
{
    if(exclusive_storage_requested)
        return;

    exclusive_storage_requested = true;
    usb_broadcast_seqnum += 1;
    usb_num_acks_to_expect = queue_broadcast(SYS_USB_CONNECTED, usb_broadcast_seqnum) - 1;
    DEBUGF("usb: waiting for %d acks...\n", usb_num_acks_to_expect);
    if(usb_num_acks_to_expect == 0 && usb_host_present) {
        usb_slave_mode(true);
        exclusive_storage_enabled = true;
#ifdef USB_ENABLE_STORAGE
        usb_signal_class_notify(USB_DRIVER_MASS_STORAGE, 0);
#endif
    }
}

void usb_release_exclusive_storage(void)
{
    int bccount;
    if(!exclusive_storage_requested) {
        return;
    }
    exclusive_storage_requested = false;
    usb_num_acks_to_expect = 0;

    if(exclusive_storage_enabled) {
        usb_slave_mode(false);
    }
    exclusive_storage_enabled = false;

    /* Tell all threads that we are back in business */
    bccount = queue_broadcast(SYS_USB_DISCONNECTED, 0) - 1;
#ifdef DEBUG
    DEBUGF("USB extracted. Broadcast to %d threads...\n", bccount);
#else
    (void)bccount;
#endif
    return;
}

#ifdef USB_ENABLE_HID
void usb_set_hid(bool enable)
{
    usb_hid = enable;
    usb_core_enable_driver(USB_DRIVER_HID, usb_hid);
}
#endif /* USB_ENABLE_HID */

#ifdef USB_ENABLE_SERIAL
void usb_set_serial(bool enable)
{
    usb_serial = enable;
    usb_core_enable_driver(USB_DRIVER_SERIAL, usb_serial);
}

bool usb_get_serial(void)
{
    return usb_serial;
}
#endif /* USB_ENABLE_SERIAL */

#ifdef USB_ENABLE_AUDIO
void usb_set_audio(int value)
{
    usb_audio = value;
}
#endif /* USB_ENABLE_AUDIO */

#ifdef HAVE_USB_POWER
bool usb_powered_only(void)
{
    return usb_power_only;
}

#if defined(COLORFLY_C4_PRO) && !defined(SIMULATOR) && !defined(BOOTLOADER)
void c4pro_usb_charge_only(void)
{
    queue_post(&usb_queue, USB_C4PRO_CHARGE_ONLY, 0);
}
#endif
#endif /* HAVE_USB_POWER */

#endif /* HAVE_USBSTACK && defined(USB_FULL_INIT) */

#elif defined(USB_NONE)
/* Dummy functions for USB_NONE  */

bool usb_inserted(void)
{
    return false;
}

void usb_acknowledge(long id, intptr_t seqnum)
{
    (void)id;
    (void)seqnum;
}

void usb_init(void)
{
}

void usb_start_monitoring(void)
{
}

int usb_detect(void)
{
    return USB_EXTRACTED;
}

void usb_wait_for_disconnect(struct event_queue *q)
{
   (void)q;
}
#endif /* USB_NONE */
