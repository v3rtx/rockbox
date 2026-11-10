/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2011 by Marcin Bukat
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
#include "usb.h"
#include "usb-rk27xx.h"
#include "usb_drv.h"

#include "cpu.h"
#include "system.h"
#include "kernel.h"
#include "panic.h"

#include "usb_ch9.h"
#include "usb_core.h"
#include <inttypes.h>
#include <string.h>
#include "power.h"

#define LOGF_ENABLE
#include "logf.h"

typedef volatile uint32_t reg32;

#ifdef LOGF_ENABLE
#define XFER_DIR_STR(dir) ((dir) ? "IN" : "OUT")
#define XFER_TYPE_STR(type) \
    ((type) == USB_ENDPOINT_XFER_CONTROL ? "CTRL" : \
     ((type) == USB_ENDPOINT_XFER_ISOC ? "ISOC" : \
      ((type) == USB_ENDPOINT_XFER_BULK ? "BULK" : \
       ((type) == USB_ENDPOINT_XFER_INT ? "INTR" : "INVL"))))
#endif

struct endpoint_t
{
    const int ep_num;            /* EP number */
    const int type;              /* EP type */
    const int dir;               /* DIR_IN/DIR_OUT */
    volatile unsigned long *stat; /* RXSTAT/TXSTAT register */
    bool enabled;                /* set up by usb_drv_ep_init() */
    volatile void *buf;          /* tx/rx buffer address */
    volatile int len;            /* size of the transfer (bytes) */
    volatile int cnt;            /* number of bytes transfered/received  */
    volatile bool block;         /* flag indicating that transfer is blocking */
    volatile bool busy;          /* a transfer is armed and not yet finished */
    volatile bool zlp;           /* IN: end a max-packet-sized transfer with a
                                  * zero length packet */
    volatile int result;         /* 0 = completed, -1 = cancelled */
    struct semaphore complete;   /* semaphore for blocking transfers */
};

/* compute RXCON address from RXSTAT, and so on */
#define RXSTAT(endp)        *((endp)->stat)
#define RXCON(endp)         *(1 + (endp)->stat)
#define DMAOUTCTL(endp)     *(2 + (endp)->stat)
#define DMAOUTLMADDR(endp)  *(3 + (endp)->stat)
/* compute TXCON address from TXSTAT, and so on */
#define TXSTAT(endp)        *((endp)->stat)
#define TXCON(endp)         *(1 + (endp)->stat)
#define TXBUF(endp)         *(2 + (endp)->stat)
#define DMAINCTL(endp)      *(3 + (endp)->stat)
#define DMAINLMADDR(endp)   *(4 + (endp)->stat)


#define ENDPOINT(num, type, dir, reg) \
    {num, USB_ENDPOINT_XFER_##type, USB_DIR_##dir, reg, false, NULL, 0, 0, \
     true, false, false, 0, {{0, 0}, 0, 0}}

static struct endpoint_t ctrlep[2] =
{
    ENDPOINT(0, CONTROL, OUT, &RX0STAT),
    ENDPOINT(0, CONTROL, IN, &TX0STAT),
};

static struct endpoint_t endpoints[16] =
{
    ENDPOINT(0,  CONTROL, OUT, NULL),  /* stub */
    ENDPOINT(1,  BULK, OUT, &RX1STAT),  /* BOUT1 */
    ENDPOINT(2,  BULK, IN,  &TX2STAT),  /* BIN2 */
    ENDPOINT(3,  INT,  IN,  &TX3STAT),  /* IIN3 */
    ENDPOINT(4,  BULK, OUT, &RX4STAT),  /* BOUT4 */
    ENDPOINT(5,  BULK, IN,  &TX5STAT),  /* BIN5 */
    ENDPOINT(6,  INT,  IN,  &TX6STAT),  /* IIN6 */
    ENDPOINT(7,  BULK, OUT, &RX7STAT),  /* BOUT7 */
    ENDPOINT(8,  BULK, IN,  &TX8STAT),  /* BIN8 */
    ENDPOINT(9,  INT,  IN,  &TX9STAT),  /* IIN9 */
    ENDPOINT(10, BULK, OUT, &RX10STAT), /* BOUT10 */
    ENDPOINT(11, BULK, IN,  &TX11STAT), /* BIN11 */
    ENDPOINT(12, INT,  IN,  &TX12STAT), /* IIN12 */
    ENDPOINT(13, BULK, OUT, &RX13STAT), /* BOUT13 */
    ENDPOINT(14, BULK, IN,  &TX14STAT), /* BIN14 */
    ENDPOINT(15, INT,  IN,  &TX15STAT), /* IIN15 */
};

static volatile bool set_address = false;
static volatile bool set_configuration = false;

/* The C4PRO connect/PHY layer is needed whenever USB runs on C4PRO. It used to
 * be gated on (DIAG_EXPORT || MSC_ENABLE) because USB only ran via the old
 * imperative serve; under the stock reactive path usb_drv_init() must assert the
 * connect on EVERY connection, so these are now enabled for all C4PRO builds. */
#if defined(COLORFLY_C4_PRO)
static void udc_phy_reset(void);
static void udc_soft_connect(void);
#endif

#if defined(COLORFLY_C4_PRO)
static bool c4pro_usb_did_soft_connect;
#endif

/* Set by usb_init_device() at boot: was VBUS present (cable inserted) when the
 * BootROM/loader last hard-reset and calibrated the UDC PHY? The RK27xx PHY's
 * analog trim (HS terminations / chirp current) is only valid if VBUS is live
 * during that reset. A device booted with NO cable therefore carries a PHY
 * calibrated against a dead bus; the later runtime plug only does a soft
 * SOFT_POR, which does not re-derive the analog trim, so the HS chirp is
 * corrupted (host "error -71", full-speed fallback). usb_drv_init() consumes
 * this to redo a full RSTCFG_UDC reset -- with VBUS now live -- on the dry-boot
 * path only. Defaults to true so the known-good cable-at-boot path is unchanged. */
volatile bool c4pro_vbus_at_boot = true;

#ifdef C4PRO_USB_TX_WEDGE_GUARD
#ifndef C4PRO_USB_TX_WEDGE_ITERS
#define C4PRO_USB_TX_WEDGE_ITERS 2000000u  /* ~tens of ms at 200 MHz, bounded */
#endif
#endif /* C4PRO_USB_TX_WEDGE_GUARD */

#if defined(C4PRO_USB_RX_STALL_KICK) && defined(C4PRO_PHASE_MARK)
/* Progress counter for bulk-OUT transfers used by the watchdog kick. */
volatile unsigned c4pro_usb_diag_outn_count;
#endif

#if defined(COLORFLY_C4_PRO)
volatile unsigned c4pro_usb_setup_seq;   /* monotonic setup-packet counter */
#endif

#undef ENDPOINT

/* EP0 control transfers: the USB core runs the control request state
 * machine. The driver hands it each SETUP and reports the end of every EP0
 * transfer the core arms, the status stages included.
 *
 * The UDC completes SET_ADDRESS and SET_CONFIGURATION by itself: no SETUP
 * reaches the driver for either, and udc_helper() reports them to the core
 * from DEV_INFO instead. */

/* wLength of the latest request: an answer is never longer, and one shorter
 * that fills whole packets ends with a zero length packet */
static uint16_t ep0_wlength;

/* status stage OUT lands here; the host sends no data, but DMA wants a
 * target */
static uint8_t ep0_dummy[64] __attribute__((aligned(32)));

/* drop whatever EP0 transfer is in flight, both directions, silently */
static void ep0_abort(void)
{
    ctrlep[DIR_IN].busy = false;
    ctrlep[DIR_OUT].busy = false;

    TX0CON |= TXNAK | TXCLR;
    TX0CON &= ~TXCLR;
    RX0CON |= RXNAK | RXCLR;
    RX0CON &= ~RXCLR;
}

/* End the EP0 transfers the core is waiting on as failed, so that its state
 * machine gets back to taking requests. OUT first: in a control read the
 * core expects the status OUT to end before the data IN. */
static void ep0_fail_transfers(void)
{
    bool out = ctrlep[DIR_OUT].busy;
    bool in = ctrlep[DIR_IN].busy;

    ep0_abort();
    if(out)
        usb_core_transfer_complete(EP_CONTROL, USB_DIR_OUT, -1, 0);
    if(in)
        usb_core_transfer_complete(EP_CONTROL, USB_DIR_IN, -1, 0);
}

static void ep0_setup_received(void)
{
    /* the two SETUP registers are the request's 8 bytes */
    union
    {
        uint32_t words[2];
        struct usb_ctrlrequest req;
    } setup;

    setup.words[0] = SETUP1;
    setup.words[1] = SETUP2;
    logf("udc: setup %08lx %08lx", setup.words[0], setup.words[1]);

    /* Right after connect the UDC reports one SETUP with both registers
     * zero. No host sends that - a host-to-device GET_STATUS - and answering
     * it puts two bytes nobody asked for into the EP0 FIFO. */
    if(setup.words[0] == 0 && setup.words[1] == 0)
        return;

#if defined(COLORFLY_C4_PRO)
    /* Bump a monotonic setup sequence so the C4PRO USB monitor thread can tell a
     * live (advancing) enumeration from a stalled one and re-present the marginal
     * HS PHY only when it is genuinely stuck. */
    c4pro_usb_setup_seq++;
#endif

    /* a SETUP ends a protocol stall */
    TX0CON &= ~TXSTALL;
    RX0CON &= ~RXSTALL;

    /* a transfer still in flight belongs to a request the host abandoned */
    ep0_fail_transfers();

    ep0_wlength = setup.req.wLength;
    usb_core_setup_received(&setup.req);
}

static int max_pkt_size(struct endpoint_t *endp)
{
    switch(endp->type)
    {
        case USB_ENDPOINT_XFER_CONTROL: return 64;
        case USB_ENDPOINT_XFER_BULK: return usb_drv_port_speed() ? 512 : 64;
        case USB_ENDPOINT_XFER_INT: return usb_drv_port_speed() ? 1024 : 64;
        default: panicf("die"); return 0;
    }
}

#if defined(COLORFLY_C4_PRO)
/* Chunk size for the synchronous bulk-IN sender. One USB max-packet (512) per
 * DMA is the simplest, guaranteed-aligned choice. The stock firmware uses a
 * runtime halfword chunk (var [0x6070F510], <=2047 = TXLEN field width) and
 * lets the controller auto-packetize each chunk into 512-byte USB packets;
 * bump this to a larger <=2047 value later if a single multi-packet DMA per
 * chunk is desired. */
#define C4PRO_SYNC_IN_CHUNK   512

/* Return true for the bulk-IN data endpoints whose send is diverted to the
 * synchronous polled path (ep != 0, IN, BULK). EP0/control and OUT stay on the
 * stock interrupt-driven path. */
static inline bool c4pro_is_sync_in_ep(struct endpoint_t *endp)
{
    return endp->ep_num != 0 &&
           endp->dir == USB_DIR_IN &&
           endp->type == USB_ENDPOINT_XFER_BULK;
}

/* Synchronous, polled bulk-IN (device->host) sender mirroring the stock
 * Colorfly C4 Pro firmware's single-DMA send @0x607041BC, looped over the whole
 * transfer (@0x60704990). The interrupt-driven per-packet ep_write() path does
 * not transmit >=512-byte reads reliably on this controller; the stock path
 * never depends on TXACK to advance packets.
 *
 * Per chunk, exactly per 0x607041BC:
 *   1. wait while(DMAINCTL & DMA_START)   ; previous DMA engine idle
 *   2. wait while(TXBUF & TXFULL)         ; FIFO not full
 *   3. TXSTAT  = chunk count              ; byte count for this DMA
 *   4. DMAINLMADDR = src                  ; source buffer address
 *   5. DMAINCTL = DMA_START               ; fire the DMA (GO bit) and continue
 *
 * Like stock, a chunk does NOT wait for its own DMA to self-clear: the wait at
 * step 1 of the NEXT send (including the CSW, which uses this same bulk-IN ep)
 * serialises it. Every poll is tick-bounded (HZ/10) so a stuck controller can't
 * hang the thread. The data has already been committed out of the D-cache by
 * _usb_drv_send()/commit_discard_dcache_range() before we get here. */
static void c4pro_ep_write_sync(struct endpoint_t *endp)
{
    int remaining = endp->cnt;

#ifdef C4PRO_PHASE_MARK
    /* phase 11 = inside the polled bulk-IN sender (CSW/data DMA + TX FIFO
     * waits). A freeze frozen at P11 pinpoints this loop despite its iteration
     * caps (e.g. a fault mid-loop after a host link reset). */
    extern volatile uint32_t c4pro_hb_phase;
    c4pro_hb_phase = 11;
#endif

    while(remaining > 0)
    {
        int chunk = MIN(C4PRO_SYNC_IN_CHUNK, remaining);
#ifdef C4PRO_USB_TX_WEDGE_GUARD
        unsigned int guard;

        /* 1. DMA engine idle */
        guard = C4PRO_USB_TX_WEDGE_ITERS;
        while((DMAINCTL(endp) & DMA_START) && --guard)
            ;

        /* 2. FIFO not full */
        guard = C4PRO_USB_TX_WEDGE_ITERS;
        while((TXBUF(endp) & TXFULL) && --guard)
            ;
#else
        unsigned int timeout;

        /* 1. DMA engine idle */
        timeout = current_tick + HZ/10;
        while(DMAINCTL(endp) & DMA_START)
            if(TIME_AFTER(current_tick, timeout)) break;

        /* 2. FIFO not full */
        timeout = current_tick + HZ/10;
        while(TXBUF(endp) & TXFULL)
            if(TIME_AFTER(current_tick, timeout)) break;
#endif

        TXSTAT(endp)      = chunk;
        DMAINLMADDR(endp) = (uint32_t)endp->buf;
        DMAINCTL(endp)    = DMA_START;

        endp->buf += chunk;
        remaining -= chunk;
    }

#ifdef C4PRO_USB_DEFER_WRITE
    {
        unsigned int guard =
#ifdef C4PRO_USB_TX_WEDGE_ITERS
            C4PRO_USB_TX_WEDGE_ITERS;
#else
            2000000u;
#endif
        while((DMAINCTL(endp) & DMA_START) && --guard)
            ;
    }
#endif /* C4PRO_USB_DEFER_WRITE */

    /* The whole payload has been drained synchronously. Complete the transfer
     * WITHOUT relying on the TXACK interrupt, mirroring exactly what in_intr()
     * does on the final TXACK so the usb_storage state machine advances to the
     * CSW. endp->len is the total length set in _usb_drv_send(). */
    endp->cnt = 0;
    endp->busy = false;
    endp->result = 0;
    usb_core_transfer_complete(endp->ep_num, endp->dir, 0, endp->len);
    if(endp->block)
        semaphore_release(&endp->complete);
}
#endif /* COLORFLY_C4_PRO */

static void ep_write(struct endpoint_t *endp)
{
    int xfer_size = MIN(max_pkt_size(endp), endp->cnt);
#if defined(COLORFLY_C4_PRO) && !defined(C4PRO_USB_STANDARD_TRANSFER)
    /* Divert bulk-IN data endpoints to the synchronous polled sender (stock
     * @0x607041BC). EP0/control and OUT stay on the interrupt-driven path.
     *
     * C4PRO_USB_STANDARD_TRANSFER (default OFF) REVERTS this to the upstream
     * rk27xx behaviour: bulk-IN uses the normal IRQ-driven ep_write below (fire
     * ONE packet + DMA, return) and in_intr()'s TXACK handler chains the next
     * packet - no polled sender, no per-transfer busy-waits held in the caller.
     * The enumeration path (c4pro_udc_arm_and_connect) is unaffected. */
    if(c4pro_is_sync_in_ep(endp))
    {
        c4pro_ep_write_sync(endp);
        return;
    }
#endif

    /* Bounded by iterations, not by current_tick: this also runs in the
     * interrupt handler (the next packet of a transfer), where the tick
     * never advances and a tick timeout spins forever. */
    for(int spin = 0; TXBUF(endp) & TXFULL; spin++) /* TXFULL flag */
    {
        if(spin > 200000)
            break;
    }

    /* setup transfer size and DMA */
    TXSTAT(endp) = xfer_size;
    DMAINLMADDR(endp) = (uint32_t)endp->buf; /* local buffer address */
    DMAINCTL(endp) = DMA_START;
#if defined(COLORFLY_C4_PRO)
    /* Wait for the DMA engine to finish loading the TX FIFO before the packet
     * is clocked out. The stock bulk-IN sender (@0x607041BC) polls the
     * DMA_START bit until it self-clears after every chunk; without this wait
     * the controller can transmit before the DMA has filled the FIFO and clock
     * out stale FIFO residue for >=512-byte transfers. */
#ifdef C4PRO_USB_TX_WEDGE_GUARD
    {
        unsigned int guard = C4PRO_USB_TX_WEDGE_ITERS;
        while((DMAINCTL(endp) & DMA_START) && --guard)
            ;
    }
#else
    {
        unsigned int dma_to = current_tick + HZ/10;
        while(DMAINCTL(endp) & DMA_START)
        {
            if(TIME_AFTER(current_tick, dma_to))
                break;
        }
    }
#endif
#endif
    /* Decrement by max packet size is intentional.
     * This way if we have final packet short one we will get negative len
     * after transfer, which in turn indicates we *don't* need to send
     * zero length packet. If the final packet is max sized packet we will
     * get zero len after transfer which indicates we need to send
     * zero length packet to signal host end of the transfer.
     */
    endp->cnt -= max_pkt_size(endp);
    endp->buf += xfer_size;
    /* clear NAK */
    TXCON(endp) &= ~TXNAK;
}

static void ep_read(struct endpoint_t *endp)
{
#ifdef C4PRO_USB_RX_DMA_GUARD
    {
        unsigned guard =
#ifdef C4PRO_USB_TX_WEDGE_ITERS
            C4PRO_USB_TX_WEDGE_ITERS;
#else
            2000000u;
#endif
        if(DMAOUTCTL(endp) & DMA_START)
        {
            while((DMAOUTCTL(endp) & DMA_START) && --guard)
                ;
            if(!guard)
            {
                DMAOUTCTL(endp) = 0;
                RXCON(endp) |= RXCLR;
                RXCON(endp) &= ~RXCLR;
            }
        }
    }

    DMAOUTLMADDR(endp) = (uint32_t)endp->buf; /* local buffer address */
    DMAOUTCTL(endp) = DMA_START;
    RXCON(endp) &= ~RXNAK;
#else
    int xfer_size = MIN(max_pkt_size(endp), endp->cnt);

    /* setup DMA */
    RXSTAT(endp) = xfer_size;
    DMAOUTLMADDR(endp) = (uint32_t)endp->buf; /* local buffer address */
    DMAOUTCTL(endp) = DMA_START;
    /* clear NAK */
    RXCON(endp) &= ~RXNAK;
#endif
}

static void in_intr(struct endpoint_t *endp)
{
    uint32_t txstat = TXSTAT(endp);
#ifdef C4PRO_USB_RESET_STALE_GUARD
    if(endp->ep_num != 0 && usb_core_bus_reset_pending())
        return;
#endif
    /* check if clear feature was sent by host */
    if(txstat & TXCFINT)
    {
        logf("clear_stall: %d", endp->ep_num);
        usb_drv_stall(endp->ep_num, false, true);
    }
#if defined(COLORFLY_C4_PRO) && !defined(C4PRO_USB_STANDARD_TRANSFER)
    /* Bulk-IN data endpoints are sent synchronously in c4pro_ep_write_sync(),
     * which already drained the whole transfer and called
     * usb_core_transfer_complete() exactly once. The hardware still raises a
     * TXACK per packet afterwards; ignore those here so the transfer is not
     * completed (and the next one not re-armed) a second time. Control EP0 and
     * the OUT path are untouched.
     *
     * With C4PRO_USB_STANDARD_TRANSFER we DO NOT early-return: the TXACK handler
     * below drives the standard IRQ-driven multi-packet bulk-IN (chain next
     * packet via ep_write, or complete on the last) - the upstream behaviour. */
    if(c4pro_is_sync_in_ep(endp))
        return;
#endif
    /* check if a transfer has finished */
    if(txstat & TXACK)
    {
        logf("udc: ack(%d)", endp->ep_num);
        /* nothing armed: a transfer cancelled by a bus reset */
        if(!endp->busy)
            return;
        /* finished ? cnt 0 means the last packet was a full one: done,
         * unless the transfer must end with a zero length packet */
        if(endp->cnt < 0 || (endp->cnt == 0 && !endp->zlp))
        {
            endp->busy = false;
            endp->result = 0;
            usb_core_transfer_complete(endp->ep_num, endp->dir, 0, endp->len);
            /* release semaphore for blocking transfer */
            if(endp->block)
                semaphore_release(&endp->complete);
        }
        else /* more data to send */
            ep_write(endp);
    }
}

static void out_intr(struct endpoint_t *endp)
{
    uint32_t rxstat = RXSTAT(endp);
#if defined(C4PRO_USB_RX_STALL_KICK) && defined(C4PRO_PHASE_MARK)
    if(endp->ep_num != 0)
        c4pro_usb_diag_outn_count++;
#endif
#ifdef C4PRO_USB_RESET_STALE_GUARD
    if(endp->ep_num != 0 && usb_core_bus_reset_pending())
        return;
#endif
    logf("udc: out intr(%d)", endp->ep_num);
    /* check if clear feature was sent by host */
    if(rxstat & RXCFINT)
    {
        logf("clear_stall: %d", endp->ep_num);
        usb_drv_stall(endp->ep_num, false, false);
    }
    /* check if a transfer has finished */
    if(rxstat & RXACK)
    {
        /* nothing armed: a transfer cancelled by a bus reset */
        if(!endp->busy)
            return;
        int xfer_size = rxstat & 0xffff;
        endp->cnt -= xfer_size;
        endp->buf += xfer_size;
        logf("udc: ack(%d) -> %d/%d", endp->ep_num, xfer_size, endp->cnt);
        /* finished ? */
        if(endp->cnt <= 0 || xfer_size < max_pkt_size(endp))
        {
            endp->busy = false;
            endp->result = 0;
            usb_core_transfer_complete(endp->ep_num, endp->dir, 0,
                                       endp->len - endp->cnt);
            if(endp->block)
                semaphore_release(&endp->complete);
            /* EP0: a status OUT while the data IN is still going means the
             * host took less than was offered, which is allowed - the data
             * stage is over too */
            if(endp->ep_num == 0 && ctrlep[DIR_IN].busy)
            {
                struct endpoint_t *in = &ctrlep[DIR_IN];

                in->busy = false;
                TX0CON |= TXNAK | TXCLR;
                TX0CON &= ~TXCLR;
                usb_core_transfer_complete(EP_CONTROL, USB_DIR_IN, 0, in->len);
            }
        }
        else
            ep_read(endp);
    }
}

static void udc_phy_reset(void)
{
    DEV_CTL |= SOFT_POR;
    udelay(10000);                 /* min 10ms */
    DEV_CTL &= ~SOFT_POR;
}

static void udc_soft_connect(void)
{
    DEV_CTL |= CSR_DONE    |
               DEV_SOFT_CN |
               DEV_SELF_PWR;
}

#if defined(COLORFLY_C4_PRO)
/* Dry-boot PHY recalibration: pulse the full UDC controller/PHY reset with VBUS
 * now live, reproducing (at plug time) the analog calibration the ROM performs
 * when a cable is present at boot. Only invoked on the dry-boot path (see
 * c4pro_vbus_at_boot); the subsequent connect sequence in usb_drv_init() then
 * runs on a freshly-calibrated PHY. */
static void c4pro_udc_recalibrate_phy(void)
{
    DEV_CTL &= ~DEV_SOFT_CN;               /* detach before resetting */
    SCU_CLKCFG &= ~CLKCFG_UDC;             /* keep the UDC clock ungated */
    SCU_RSTCFG |= RSTCFG_UDC;              /* hard-reset controller + PHY... */
    udelay(10000);
    SCU_RSTCFG &= ~RSTCFG_UDC;             /* ...recalibrated against live VBUS */
    udelay(10000);
    INTC_IMR |= (1<<16);                   /* re-enable the UDC interrupt line */
    INTC_IECR |= (1<<16);
}
#endif

static void udc_helper(void)
{
    uint32_t dev_info = DEV_INFO;

    /* This polls for DEV_EN bit set in DEV_INFO  register
     * as well as tracks current requested configuration
     * (DEV_INFO [11:8]). On state change it notifies usb stack
     * about it.
     */

    /* SET ADDRESS request */
    if(!set_address)
        if(dev_info & 0x7f)
        {
            set_address = true;
            usb_core_notify_set_address(dev_info & 0x7f);
        }

    /* SET CONFIGURATION request - cfg_number is DEV_INFO [11:8], counted
     * from 0; bit 7 is DEV_EN itself */
    if(!set_configuration)
        if(dev_info & DEV_EN)
        {
            set_configuration = true;
            usb_core_notify_set_config(((dev_info >> 8) & 0xf) + 1);
        }
}

/* The UDC completes SET_ADDRESS and SET_CONFIGURATION without raising an
 * interrupt, so udc_helper() in the interrupt handler only notices them at
 * the next UDC interrupt. At first enumeration GET_MAX_LUN provides one.
 * After a bus reset of a configured device (usbreset, or an error recovery)
 * the host re-sends SET_CONFIGURATION and goes straight to a bulk command:
 * nothing else arrives on EP0, the bulk endpoint NAKs because the class was
 * never re-armed, and NAKs raise no interrupt - the device hangs until the
 * host times out, resets, and hangs again. So poll from the tick as well,
 * for as long as the device is not configured. */
static void udc_tick(void)
{
    if(!set_configuration)
        udc_helper();
}

/* return port speed FS=0, HS=1 */
int usb_drv_port_speed(void)
{
    return (DEV_INFO & DEV_SPEED) ? 0 : 1;
}

/* The UDC's endpoints come in groups of three - bulk OUT, bulk IN,
 * interrupt IN: 1-3, 4-6 and so on - each endpoint with one fixed type and
 * direction. An interrupt endpoint in the same group as bulk endpoints in
 * use slows the bulk transfers down: every IN token the host polls it with
 * and it NAKs costs the group's bulk traffic. With HID on endpoint 3 beside
 * mass storage on 1 and 2, writes ran at 0.03 MB/s, and polled every 125 us
 * instead of every 16 ms they all but stopped; on endpoint 6 they ran at
 * 2.3 MB/s, as with no HID at all. So interrupt and bulk endpoints never
 * share a group - which depends on what else is allocated, hence the
 * driver's own allocator (usb_drv.h, option 2). */
void usb_drv_ep_reset_alloc_ctx(struct usb_drv_ep_alloc_ctx* ctx)
{
    memset(ctx->type, -1, sizeof(ctx->type));
    memset(ctx->max_packet_size, 0, sizeof(ctx->max_packet_size));
}

bool usb_drv_ep_allocate(struct usb_drv_ep_alloc_ctx* ctx, int ep, int type,
                         int max_packet_size)
{
    int ep_num = EP_NUM(ep);
    int dir = EP_DIR(ep);
    struct endpoint_t *endp = &endpoints[ep_num];

    if(ep_num == 0 || endp->type != type ||
       endp->dir != (dir == DIR_IN ? USB_DIR_IN : USB_DIR_OUT))
        return false;

    int first = (ep_num - 1) / 3 * 3 + 1;
    for(int i = first; i < first + 3 && i < USB_NUM_ENDPOINTS; i++)
    {
        for(int d = 0; d < 2; d++)
        {
            if(ctx->type[i][d] != -1 && ctx->type[i][d] != type)
                return false;
        }
    }

    ctx->type[ep_num][dir] = type;
    ctx->max_packet_size[ep_num][dir] = max_packet_size;
    return true;
}

/* one-time init: nothing to do, the endpoints are fixed in endpoints[] */
void usb_drv_startup(void)
{
}

void usb_drv_ep_init(const struct usb_drv_ep_alloc_ctx* ctx, int ep)
{
    /* FIXME: support max packet size override */
    (void)ctx;

    int ep_num = EP_NUM(ep);
    struct endpoint_t *endp = &endpoints[ep_num];

    if(EP_DIR(ep) == DIR_IN)
#if defined(COLORFLY_C4_PRO)
        /* Match the stock C4 firmware's bulk-IN endpoint config (TX2CON=0x248):
         * TXEPEN|TXACKINTEN, with NO TXNAK and NO TXCFINTE. Leaving TXNAK set
         * (Rockbox default 0x124C) makes the controller transmit stale FIFO/DMA
         * residue for >=512-byte bulk reads instead of the freshly-DMA'd data
         * (host saw a {sector,~sector} pattern). Verified against stock EP-config
         * routine @0x60704114. */
        TXCON(endp) = (ep_num << 8) | TXEPEN | TXACKINTEN;
#else
        TXCON(endp) = (ep_num << 8) | TXEPEN | TXNAK | TXACKINTEN | TXCFINTE;
#endif
    else
#if defined(COLORFLY_C4_PRO)
        /* Stock bulk-OUT config (RX1CON=0x190): RXEPEN|RXACKINTEN, no RXNAK. */
        RXCON(endp) = (ep_num << 8) | RXEPEN | RXACKINTEN;
#else
        RXCON(endp) = (ep_num << 8) | RXEPEN | RXNAK | RXACKINTEN | RXCFINTE |
                      RXERRINTEN;
#endif
    endp->enabled = true;
    EN_INT |= 1 << (ep_num + 7);
    logf("add: ep%d %s", ep_num, XFER_DIR_STR(EP_DIR(ep) == DIR_IN));
}

void usb_drv_ep_deinit(const struct usb_drv_ep_alloc_ctx* ctx, int ep)
{
    (void)ctx;

    int ep_num = EP_NUM(ep);

    logf("rel: ep%d", ep_num);
    endpoints[ep_num].enabled = false;

    /* disable interrupt from this endpoint */
    EN_INT &= ~(1 << (ep_num + 7));
}

/* Set the address (usually it's in a register).
 * There is a problem here: some controller want the address to be set between
 * control out and ack and some want to wait for the end of the transaction.
 * In the first case, you need to write some code special code when getting
 * setup packets and ignore this function (have a look at other drives)
 */
void usb_drv_set_address(int address)
{
    (void)address;
    /* UDC seems to set this automaticaly */
}

static int _usb_drv_send(int endpoint, void *ptr, int length, bool block)
{
    logf("udc: send(%x)", endpoint);
    struct endpoint_t *ep;
    int ep_num = EP_NUM(endpoint);

    bool zlp = false;

    if (ep_num == 0)
    {
        ep = &ctrlep[DIR_IN];
        /* never send more than asked for, and end a short answer that fills
         * whole packets with a zero length packet */
        if(length > ep0_wlength)
            length = ep0_wlength;
        zlp = length > 0 && length < ep0_wlength && length % 64 == 0;
    }
    else
        ep = &endpoints[ep_num];

    /* for send transfers, make sure the data is committed */
    if(length)
        commit_discard_dcache_range(ptr, length);

    /* drop a release left over from a transfer that ended (or was
     * cancelled) after its waiter had already given up */
    if(block)
        semaphore_wait(&ep->complete, TIMEOUT_NOBLOCK);

    /* the interrupt handler works on these fields too */
    int oldlevel = disable_irq_save();
    ep->buf = ptr;
    ep->len = ep->cnt = length;
    ep->block = block;
    ep->zlp = zlp;
    ep->busy = true;
    ep->result = 0;
    ep_write(ep);
    restore_irq(oldlevel);

    if(!block)
        return 0;

    /* wait for transfer to end - but not forever: a host that gave up on
     * the transfer would otherwise wedge the calling thread (the USB
     * thread, for control responses) for good */
    if(semaphore_wait(&ep->complete, HZ) == OBJ_WAIT_TIMEDOUT)
    {
        oldlevel = disable_irq_save();
        bool pending = ep->busy;
        ep->busy = false;
        restore_irq(oldlevel);
        if(pending)
            return -1;
    }

    return ep->result;
}

/* Setup a send transfer. (blocking) */
int usb_drv_send(int endpoint, void *ptr, int length)
{
    return _usb_drv_send(endpoint, ptr, length, true);
}

/* Setup a send transfer. (non blocking) */
int usb_drv_send_nonblocking(int endpoint, void *ptr, int length)
{
    return _usb_drv_send(endpoint, ptr, length, false);
}

/* Setup a receive transfer. (non blocking) */
int usb_drv_recv_nonblocking(int endpoint, void* ptr, int length)
{
    logf("udc: recv(%x)", endpoint);
    struct endpoint_t *ep;
    int ep_num = EP_NUM(endpoint);

    if(ep_num == 0)
    {
        ep = &ctrlep[DIR_OUT];
        if(ptr == NULL)
            ptr = ep0_dummy;
    }
    else
        ep = &endpoints[ep_num];

    /* for recv, discard the cache lines related to the buffer */
    if(length)
        commit_discard_dcache_range(ptr, length);

    /* the interrupt handler works on these fields too */
    int oldlevel = disable_irq_save();
    ep->buf = ptr;
    ep->len = ep->cnt = length;
    ep->block = false;
    ep->busy = true;
    ep->result = 0;
    ep_read(ep);
    restore_irq(oldlevel);
    return 0;
}

static void cancel_transfer(struct endpoint_t *endp)
{
    if(!endp->busy)
        return;

    endp->busy = false;
    endp->result = -1;
    endp->cnt = 0;
    /* wake a blocked sender - it sees result -1 */
    if(endp->block)
        semaphore_release(&endp->complete);
}

/* Kill all transfers: after a bus reset nothing armed before it will ever
 * complete. Blocked senders are woken with an error. Class drivers are not
 * called - the host re-configures after a reset, and init_connection()
 * re-arms them from scratch. EP0 is not touched: a bus reset drops its
 * transfers with ep0_abort(), and the core resets its own EP0 state. */
void usb_drv_cancel_all_transfers(void)
{
    int oldlevel = disable_irq_save();

    for(int ep_num = 1; ep_num < USB_NUM_ENDPOINTS; ep_num++)
    {
        struct endpoint_t *endp = &endpoints[ep_num];

        cancel_transfer(endp);
        if(!endp->enabled)
            continue;

        /* NAK and flush whatever is left in the FIFO */
        if(endp->dir == USB_DIR_IN)
        {
            TXCON(endp) |= TXNAK | TXCLR;
            TXCON(endp) &= ~TXCLR;
        }
        else
        {
            RXCON(endp) |= RXNAK | RXCLR;
            RXCON(endp) &= ~RXCLR;
        }
    }

    restore_irq(oldlevel);
}

/* Set test mode, you can forget that for now, usually it's sufficient
 * to bit copy the argument into some register of the controller
 */
void usb_drv_set_test_mode(int mode)
{
    (void)mode;
}

/* endpoints[0] is a stub without registers: EP0 lives in ctrlep[] */
static struct endpoint_t *stall_ep(int endpoint, bool in)
{
    int ep_num = EP_NUM(endpoint);

    if(ep_num == 0)
        return &ctrlep[in ? DIR_IN : DIR_OUT];
    return &endpoints[ep_num];
}

/* Check if endpoint is in stall state */
bool usb_drv_stalled(int endpoint, bool in)
{
    struct endpoint_t *endp = stall_ep(endpoint, in);

    if(in)
        return !!(TXCON(endp) & TXSTALL);
    else
        return !!(RXCON(endp) & RXSTALL);
}

/* Stall the endpoint. Usually set a flag in the controller */
void usb_drv_stall(int endpoint, bool stall, bool in)
{
    struct endpoint_t *endp = stall_ep(endpoint, in);
    if(in)
    {
        if(stall)
            TXCON(endp) |= TXSTALL;
        else
            TXCON(endp) &= ~TXSTALL;
    }
    else
    {
        if(stall)
            RXCON(endp) |= RXSTALL;
        else
            RXCON(endp) &= ~RXSTALL;
    }
}

#if defined(COLORFLY_C4_PRO)
/* Production accessor + re-present used by the C4PRO USB monitor thread to
 * recover the marginal HS PHY (host logs "device descriptor read/64, error -71"
 * on a failed attempt).
 *
 * usb_drv_init()'s recalibrating connect runs only ONCE per boot (guarded by
 * c4pro_usb_did_soft_connect), so on a runtime REPLUG -- and especially a replug
 * after a full serve session -- the re-present here is the only connect the host
 * gets. A light phy_reset+soft_connect ("strategy 4") does NOT re-derive the PHY
 * analog trim and gets stuck at -71 on the second plug; c4pro_udc_recalibrate_phy
 * (full RSTCFG_UDC reset, VBUS live) is what re-derives it. So the re-present
 * mirrors the dry-boot path exactly: detach, hard-reset+recalibrate the PHY, then
 * the winning STOCK HS connect (SOFT_POR then udc_soft_connect -> DEV_CTL 0x158).
 * EP0 is re-armed by the USBRST ISR once the host starts enumerating (same as the
 * boot connect, which also does not arm EP0 itself). */
bool c4pro_usb_vbus_present(void) { return (DEV_INFO & VBUS_STS) != 0; }
bool c4pro_usb_configured(void)   { return (DEV_INFO & DEV_EN) != 0; }

void c4pro_usb_represent(void)
{
    /* LIGHT re-present nudge: detach, SOFT_POR the PHY, re-assert D+. Deliberately
     * does NOT do the full RSTCFG_UDC hard controller reset (c4pro_udc_recalibrate_phy)
     * -- that tears the whole controller down and, fired repeatedly on the
     * monitor's cadence while the host is mid-retry, prevents enumeration from
     * ever completing (observed: device number churns, permanent -71). The
     * once-per-plug analog recalibration is done by usb_drv_init() in the correct
     * context (re-armed on disconnect via c4pro_usb_arm_reconnect); this is only
     * the gentle "try again" for a marginal chirp. */
    DEV_CTL &= ~DEV_SOFT_CN;        /* detach: give the host a clean disconnect */
    udelay(100000);
    udc_phy_reset();
    udelay(10000);
    udc_soft_connect();             /* STOCK HS connect -> DEV_CTL 0x158 */
}

/* Make sure D+ is NOT asserted. Used by the plug paths that do not bring the
 * stack up (charge-only, and the window before a deferred attach): if a
 * previous session or the loader left DEV_SOFT_CN set, the host would start
 * enumerating into an uninitialised driver -- and the INT_UDC idle-guard
 * cannot catch that, because VBUS is present. */
void c4pro_usb_detach(void)
{
    DEV_CTL &= ~DEV_SOFT_CN;
}

/* Re-arm usb_drv_init()'s one-shot connect so the NEXT plug re-runs the full
 * recalibrating connect (the proven first-plug path) instead of skipping it via
 * the latch. Called on USB disconnect. Forces recalibration next time because
 * VBUS will be live at the next plug and the PHY analog trim decays across a
 * disconnect (a runtime replug otherwise gets stuck at -71). */
void c4pro_usb_arm_reconnect(void)
{
    c4pro_usb_did_soft_connect = false;
    c4pro_vbus_at_boot = false;
}
#endif

/* Transfer-completion semaphores start fresh at every stack init. Never from
 * the interrupt handler: re-initialising a semaphore a thread is blocked on
 * loses that thread for good - a bus reset cancels transfers instead. */
static void udc_reset_semaphores(void)
{
    usb_drv_startup();

    /* init semaphore of ep0 */
    semaphore_init(&ctrlep[DIR_OUT].complete, 1, 0);
    semaphore_init(&ctrlep[DIR_IN].complete, 1, 0);
    ctrlep[DIR_OUT].busy = ctrlep[DIR_IN].busy = false;

    for(int ep_num = 1; ep_num < USB_NUM_ENDPOINTS; ep_num++)
    {
        semaphore_init(&endpoints[ep_num].complete, 1, 0);
        endpoints[ep_num].busy = false;
    }
}

/* one time init (once per connection) - basicaly enable usb core */
void usb_drv_init(void)
{
    udc_reset_semaphores();
    tick_add_task(udc_tick);

    /* enable USB interrupts in interrupt controller - here, as
     * usb_drv_exit() disables them at every disconnect */
    INTC_IMR |= IRQ_ARM_UDC;
    INTC_IECR |= IRQ_ARM_UDC;
}

/* Present ourselves to the host. Called by usb_enable() once usb_core_init()
 * has finished - not from usb_drv_init(), which usb_core_init() calls FIRST,
 * before the class drivers are set up and before it sets its own state. A
 * host quick enough to enumerate in that window had its requests handled
 * against state usb_core_init() then overwrote: the descriptor read timed
 * out, and whether it did depended on timing.
 *
 * Connecting only on CONN_INTR, as the interrupt handler does, needs a
 * cable-insert edge after the stack is up - and there is none when the cable
 * was already in: booting with it plugged, or taking the controller over
 * from the ROM loader or hwstub, which leaves it enumerated as a different
 * device the host has no reason to re-enumerate. So drop off the bus, reset
 * the PHY and reconnect: the host sees a fresh device either way. */
void usb_drv_connect(void)
{
#if defined(COLORFLY_C4_PRO)
    if(!c4pro_usb_did_soft_connect)
    {
        c4pro_usb_did_soft_connect = true;
        /* Detach FIRST (drop the D+ pull-up) and let the host see it. Across an
         * app reboot the cable stays plugged and VBUS stays high, so the host
         * retains its stale pre-reboot view of the device; re-asserting D+ it
         * already considers "connected" triggers no fresh enumeration. A clean
         * disconnect->reconnect forces the host to re-enumerate. (This is what
         * the old imperative serve did via c4pro_usb_diag_force_reattach; the
         * dry-boot recalibrate below also detaches, so this is its equivalent
         * for the cable-at-boot path.) */
        DEV_CTL &= ~DEV_SOFT_CN;
        udelay(100000);                /* ~100ms so the host registers the detach */
        if(!c4pro_vbus_at_boot)
            /* Booted with no cable: the boot-time PHY calibration ran against a
             * dead bus. Redo the full hard reset now, with VBUS live, before the
             * connect sequence below runs -- otherwise the HS chirp is corrupted
             * (host "error -71", full-speed only) on this runtime plug. */
            c4pro_udc_recalibrate_phy();
        /* Connect for the stock reactive path. EMPIRICALLY CONFIRMED (on-device,
         * via the strategy cycler + a fresh cable plug): the STOCK sequence
         * -- SOFT_POR the PHY, then udc_soft_connect() -> DEV_CTL=0x158 -- is what
         * enumerates HIGH-speed. The host reads descriptors, binds usb-storage,
         * and reports HS. (The "arm EP0 + clear PHY16BIT -> 0x118" variant only
         * ever reached full-speed + host error -71; 0x158 is correct here.) The
         * detach above gives the host a clean disconnect->reconnect edge. */
        DEV_CTL = DEV_SELF_PWR;
        udc_phy_reset();
        udelay(10000);
        udc_soft_connect();
    }
#else
    DEV_CTL &= ~DEV_SOFT_CN;
    udelay(20000);
    udc_phy_reset();
    udelay(10000);
    udc_soft_connect();
#endif
}

/* turn off usb core */
void usb_drv_exit(void)
{
    tick_remove_task(udc_tick);
    DEV_CTL = DEV_SELF_PWR;

    /* disable USB interrupts in interrupt controller */
    INTC_IMR &= ~IRQ_ARM_UDC;
    INTC_IECR &= ~IRQ_ARM_UDC;

    /* we cannot disable UDC clock since this causes data abort
     * when reading DEV_INFO in order to check usb connect event
     */
}

int usb_detect(void)
{
    if(DEV_INFO & VBUS_STS)
        return USB_INSERTED;
    else
        return USB_EXTRACTED;
}

#ifdef C4PRO_USB_RESET_DMA_ABORT
/* Abort every in-flight UDC DMA engine and flush the per-endpoint FIFOs.
 *
 * ROOT-CAUSE HYPOTHESIS (sustained-write/format hard hang preceded by host bus
 * RESETs, R climbs then freeze; F=0, T=D frozen together => IRQs-masked
 * busy-spin, screen blank): when a host USBRST arrives while a bulk-OUT receive
 * DMA (DMAOUTCTL) or bulk-IN send DMA (DMAINCTL) is IN FLIGHT, the link dies
 * mid-burst and that DMA's GO/busy bit (DMA_START) never self-clears, leaving
 * the UDC's DMA engine holding the AHB bus. The very next bus/cache access or
 * DMA arm then spins forever with IRQs masked -> the F=0 / T=D lockup. The old
 * C4PRO_USB_RESET_GUARD re-inits + ACKs but NEVER releases the wedged master.
 *
 * Fix: on every USBRST, BEFORE any re-init, force DMA_START low on all DMA
 * control registers (the write is the abort - DMA_START is both the GO command
 * and the busy status on this controller) and pulse the FIFO-clear bits
 * (TXCLR / RXCLR, exactly as the RESUME handler already does for EP0) so no DMA
 * is left mid-burst and no FIFO residue survives into the re-armed link.
 *
 * IRQ-SAFE / BOUNDED: this is pure register writes - no polling, no wait, no
 * dependence on the (frozen) tick. Every UDC DMA engine that physically exists
 * is covered: EP0 (TX0/RX0) plus endpoints[1..15], each of which is fixed to a
 * single direction by the hardware endpoint map, so iterating by endp->dir
 * touches exactly one real DMA control register per endpoint number. */
#ifdef C4PRO_USB_RX_STALL_KICK
void c4pro_usb_rx_stall_kick(void)
{
    struct endpoint_t *endp = &endpoints[1];   /* BOUT1 = the MSC OUT pipe */
    int oldlevel = disable_irq_save();
    unsigned long ctl = DMAOUTCTL(endp);
    uint32_t rxstat = (uint32_t)RXSTAT(endp);

    if (ctl & DMA_START)
    {
        if ((INT2FLAG & 0x100u) || !(DMAOUTCTL(endp) & DMA_START))
        {
            restore_irq(oldlevel);
            return;
        }
        DMAOUTCTL(endp) = 0;
        RXCON(endp) |= RXCLR;
        RXCON(endp) &= ~RXCLR;
        RXCON(endp) &= ~RXNAK;
        DMAOUTLMADDR(endp) = (uint32_t)endp->buf;
        DMAOUTCTL(endp) = DMA_START;
    }
    else
    {
        if (INT2FLAG & 0x100u)
        {
            restore_irq(oldlevel);
            return;
        }
        if (rxstat & RXACK)
        {
            int xfer_size = rxstat & 0xffff;
            endp->cnt -= xfer_size;
            endp->buf += xfer_size;
            if (endp->cnt <= 0 || xfer_size < max_pkt_size(endp))
            {
                endp->busy = false;
                endp->result = 0;
                usb_core_transfer_complete(endp->ep_num, endp->dir, 0,
                                           endp->len - endp->cnt);
                if (endp->block)
                    semaphore_release(&endp->complete);
            }
            else
                ep_read(endp);   /* arms at the advanced buf */
        }
        else
        {
            /* Idle with no ACK latched: nothing to account, clean re-arm. */
            DMAOUTCTL(endp) = 0;
            RXCON(endp) |= RXCLR;
            RXCON(endp) &= ~RXCLR;
            RXCON(endp) &= ~RXNAK;
            DMAOUTLMADDR(endp) = (uint32_t)endp->buf;
            DMAOUTCTL(endp) = DMA_START;
        }
    }

    restore_irq(oldlevel);
}
#endif /* C4PRO_USB_RX_STALL_KICK */

static void c4pro_usb_reset_dma_abort(void)
{
    /* EP0 control endpoint: both TX0 and RX0 DMA engines + FIFOs. */
    TX0DMAINCTL = 0;                 /* clear IN  DMA GO -> release AHB master */
    RX0DMACTLO  = 0;                 /* clear OUT DMA GO -> release AHB master */
    TX0CON |= TXCLR;  TX0CON &= ~TXCLR;   /* flush EP0 TX FIFO */
    RX0CON |= RXCLR;  RX0CON &= ~RXCLR;   /* flush EP0 RX FIFO */

    for(int ep_num = 1; ep_num < 16; ep_num++)
    {
        struct endpoint_t *endp = &endpoints[ep_num];
        if(endp->stat == NULL)       /* unmapped (ep0 stub) */
            continue;
        if(endp->dir == USB_DIR_IN)
        {
            DMAINCTL(endp) = 0;      /* abort in-flight bulk/int IN DMA */
            TXCON(endp) |= TXCLR;    /* pulse TX FIFO clear (preserves epnum) */
            TXCON(endp) &= ~TXCLR;
        }
        else
        {
            DMAOUTCTL(endp) = 0;     /* abort in-flight bulk OUT DMA */
            RXCON(endp) |= RXCLR;    /* pulse RX FIFO clear (preserves epnum) */
            RXCON(endp) &= ~RXCLR;
        }
    }
}
#endif /* C4PRO_USB_RESET_DMA_ABORT */

/* UDC ISR function */
void INT_UDC(void)
{
    /* read what caused UDC irq */
    uint32_t intsrc = INT2FLAG & 0x7fffff;
    if(intsrc == 0)
        return;

#ifdef C4PRO_USB_IDLE_UDC_GUARD
    /* Root-cause guard for the ~4s playback crack AND the intermittent song-open
     * INT_UDC data-abort. usb_init_device() enables the UDC IRQ at BOOT and it
     * stays live all through normal playback, so a spurious/stale UDC interrupt
     * -- even with NO cable attached -- drops us into INT_UDC. Servicing a stale
     * SETUP/EP-completion against un-enumerated ep0 state dereferences a bad
     * pointer -> data abort in IRQ context (the 6006b7xx crash); and the repeated
     * heavy ISR entry (its EP0/DMA wait loops) steals codec time -> periodic
     * pcmbuf underrun = the crack. A real USB session REQUIRES VBUS (usb_detect()
     * polls exactly DEV_INFO & VBUS_STS), so with VBUS ABSENT there is nothing
     * legitimate to service: ACK whatever latched (INT2FLAG is write-1-to-clear)
     * and return WITHOUT touching endpoint state. Do NOT mask the IRQ line -- it
     * is enabled only once at boot in usb_init_device() and is NOT reliably
     * re-enabled on the next plug, so masking would break USB-MSC. Acking is
     * enough; a real cable sets VBUS_STS and the normal path below runs untouched.
     * Disconnect cleanup is done by the polling usb thread (usb_enable(false)),
     * not here. */
    if(!(DEV_INFO & VBUS_STS))
    {
        INT2FLAG = intsrc;
        return;
    }
#endif

    if(intsrc & USBRST_INTR) /* usb reset */
    {
#ifdef C4PRO_USB_RESET_DMA_ABORT
        /* FIRST thing on a reset: release any in-flight UDC DMA (AHB bus
         * master) and flush the FIFOs, BEFORE touching EN_INT / re-init. A host
         * link reset that lands mid-burst otherwise leaves DMA_START stuck ->
         * wedged AHB master -> the F=0 / T=D IRQs-masked lockup. Pure register
         * writes: bounded, IRQ-safe (no tick dependence). Runs on every USBRST
         * regardless of C4PRO_USB_RESET_GUARD. */
        c4pro_usb_reset_dma_abort();
#endif
        logf("udc_int: reset, %ld", current_tick);

        EN_INT = EN_SUSP_INTR   |  /* Enable Suspend Irq */
                 EN_RESUME_INTR |  /* Enable Resume Irq */
                 EN_USBRST_INTR |  /* Enable USB Reset Irq */
                 EN_OUT0_INTR   |  /* Enable OUT Token receive Irq EP0 */
                 EN_IN0_INTR    |  /* Enable IN Token transmit Irq EP0 */
                 EN_SETUP_INTR;    /* Enable SETUP Packet Receive Irq */

        INTCON = UDC_INTHIGH_ACT | /* interrupt high active */
                 UDC_INTEN;        /* enable EP0 irqs */

        TX0CON = TXACKINTEN |      /* Set as one to enable the EP0 tx irq */
                 TXNAK;            /* Set as one to response NAK handshake */

        RX0CON = RXACKINTEN |
                 RXEPEN;           /* Keep EP0 OUT/SETUP receptive after reset. */

        set_address = false;
        set_configuration = false;
        /* before any SETUP in this same interrupt is handled: whatever
         * was in flight is gone, including the control request */
        usb_drv_cancel_all_transfers();
        ep0_abort();
        usb_core_bus_reset();
    }
    /* This needs to be processed AFTER usb reset */
    udc_helper();

    /* EP0 completions before a SETUP: when both are pending, the
     * completion belongs to the older request, which the SETUP ends */
    if(intsrc & IN0_INTR)
    {
        /* EP0 IN done */
        in_intr(&ctrlep[DIR_IN]);
    }
    if(intsrc & OUT0_INTR)
    {
        /* EP0 OUT done */
        out_intr(&ctrlep[DIR_OUT]);
    }
    if(intsrc & SETUP_INTR) /* setup interrupt */
    {
        ep0_setup_received();
    }
    if(intsrc & RESUME_INTR)
    {
        /* usb resume */
        TX0CON |=  TXCLR;  /* TxClr */
        TX0CON &= ~TXCLR;
        RX0CON |=  RXCLR; /* RxClr */
        RX0CON &= ~RXCLR;
    }
    if(intsrc & SUSP_INTR)
    {
        /* usb suspend */
    }
    if(intsrc & CONN_INTR)
    {
#if defined(COLORFLY_C4_PRO)
        /* C4 (no-POR path) is already armed and connected from init
         * (c4pro_udc_arm_and_connect). The stock RK27xx loader's CONN handler
         * only reads DEV_SPEED; it does NOT pulse SOFT_POR. Re-resetting the
         * PHY here would tear the link down mid-enumeration, so skip it. */
#else
        /* usb connect */
        udc_phy_reset();
        udelay(10000);         /* wait at least 10ms */
        udc_soft_connect();
#endif
    }
    /* other endpoints */
#ifdef C4PRO_USB_IRQ_DRAIN
    /* DRAIN: service every endpoint completion, INCLUDING ones that land while
     * we are still in this handler, all within this single interrupt. Acking at
     * exit (original) writes back the entry snapshot and ERASES a completion that
     * arrived mid-handler -> lost RXACK -> phase-13 sustained-write stall. Acking
     * at entry (early-ACK) fixes the loss but makes each such completion cost a
     * whole FRESH interrupt -> ~halves throughput on this interrupt-latency-bound
     * RX. Draining does neither: ack the control bits once, then per endpoint-bit
     * batch ack-then-service and re-read, until no completion remains -- no bit
     * lost, no redundant IRQ. (Endpoint completions = bits 8..22; control/one-time
     * events = bits 1..7, handled once above.) */
    INT2FLAG = intsrc & ~0x7fff00u;   /* ack control/one-time bits once */
    {
        uint32_t epbits = intsrc & 0x7fff00u;
        while(epbits)
        {
            INT2FLAG = epbits;        /* ack this batch BEFORE servicing */
            for(int ep_num = 1; ep_num < 16; ep_num++)
            {
                if(!(epbits & (1 << (ep_num + 7))))
                    continue;
                struct endpoint_t *endp = &endpoints[ep_num];
                if(endp->dir == USB_DIR_IN)
                    in_intr(endp);
                else
                    out_intr(endp);
            }
            epbits = INT2FLAG & 0x7fff00u;   /* completions that arrived meanwhile */
        }
    }
#else
    for(int ep_num = 1; ep_num < 16; ep_num++)
    {
        if(!(intsrc & (1 << (ep_num + 7))))
            continue;
        struct endpoint_t *endp = &endpoints[ep_num];
        if(endp->dir == USB_DIR_IN)
            in_intr(endp);
        else
            out_intr(endp);
    }

    INT2FLAG = intsrc;
#endif
}
