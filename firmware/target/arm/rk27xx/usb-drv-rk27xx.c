/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
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
#include "usb_drv.h"

#include "cpu.h"
#include "system.h"
#include "kernel.h"
#include "panic.h"

#include "usb_ch9.h"
#include "usb_core.h"
#include <inttypes.h>
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
    volatile void *buf;          /* tx/rx buffer address */
    volatile int len;            /* size of the transfer (bytes) */
    volatile int cnt;            /* number of bytes transfered/received  */
    volatile bool block;         /* flag indicating that transfer is blocking */
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
    {num, USB_ENDPOINT_XFER_##type, USB_DIR_##dir, reg, NULL, 0, 0, true, {{0, 0}, 0, 0}}

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

struct usb_drv_ep_spec usb_drv_ep_specs[16]; /* filled in usb_drv_startup */
uint8_t usb_drv_ep_specs_flags = 0;

static volatile bool set_address = false;
static volatile bool set_configuration = false;

#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
static void udc_phy_reset(void);
static void udc_soft_connect(void);
static void c4pro_udc_arm_and_connect(void);
#endif

#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
static bool c4pro_usb_did_soft_connect;
#endif

#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
void c4pro_usb_diag_force_reattach(void)
{
    /* Do NOT set DEV_PHY16BIT: the RK27xx UDC uses an 8-bit UTMI+ PHY (60 MHz),
     * not a 16-bit one. Selecting the 16-bit interface mismatches the PHY data
     * width and corrupts the high-speed chirp handshake, forcing a full-speed
     * fallback (~1.5 MB/s, and bulk-OUT then has no PING flow control, so the
     * host resets the link during long NAND-write stalls). Stock firmware and
     * bootloader both leave DEV_CTL at 0x118 (no bit 6) and negotiate HS. */
    /* Re-present to the host: drop the pull-up, reset the PHY, then re-arm EP0
     * and reconnect. The real cold-boot enumeration failure was the USB host
     * clock (CLKCFG_UHC) being gated off in system_init() — on this OTG part it
     * shares the device PHY clock domain, so the PHY signaling was corrupted
     * (host saw "error -71", full-speed only). With UHC kept on for C4PRO the
     * standard phy_reset + arm-before-connect sequence enumerates. */
    /* Maximal preservation of the BootROM-initialised PHY: do NOT SOFT_POR the
     * PHY (udc_phy_reset) and do NOT hard-reset the controller (RSTCFG_UDC is
     * skipped in usb_init_device for C4PRO). Just toggle the D+ pull-up so the
     * host re-enumerates, keeping whatever HS-capable analog/clock state the
     * BootROM left. SOFT_POR + controller reset were corrupting the signalling
     * (host error -71, full-speed only) on a native boot. */
    c4pro_usb_did_soft_connect = true;
    DEV_CTL &= ~DEV_SOFT_CN;        /* disconnect */
    udelay(100000);                 /* let the host see the detach */
#ifdef C4PRO_USB_STOCK_PATH
    /* A/B: stock rk27xx reattach (SOFT_POR -> PHY16BIT set, then soft connect) */
    udc_phy_reset();
    udelay(10000);
    udc_soft_connect();
#else
    c4pro_udc_arm_and_connect();    /* re-arm EP0 + reconnect, no SOFT_POR */
#endif
}
#endif

#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
volatile unsigned c4pro_usb_diag_irq_count;
volatile unsigned c4pro_usb_diag_reset_count;
volatile unsigned c4pro_usb_diag_conn_count;
volatile unsigned c4pro_usb_diag_setup_count;
volatile unsigned c4pro_usb_diag_in0_count;
volatile unsigned c4pro_usb_diag_out0_count;
volatile unsigned c4pro_usb_diag_last_intsrc;
volatile unsigned c4pro_usb_diag_last_dev_info;
volatile unsigned c4pro_usb_diag_last_setup0;
volatile unsigned c4pro_usb_diag_last_setup1;
volatile unsigned c4pro_usb_diag_last_tx0stat;
volatile unsigned c4pro_usb_diag_last_rx0stat;
volatile unsigned c4pro_usb_diag_last_tx0con;
volatile unsigned c4pro_usb_diag_last_rx0con;
volatile unsigned c4pro_usb_diag_outn_count;
volatile unsigned c4pro_usb_diag_last_outn;
volatile unsigned c4pro_usb_diag_last_outn_stat;
volatile unsigned c4pro_usb_diag_last_outn_con;
volatile unsigned c4pro_usb_diag_live_dev_ctl;
volatile unsigned c4pro_usb_diag_live_dev_info;
volatile unsigned c4pro_usb_diag_live_en_int;
volatile unsigned c4pro_usb_diag_live_intcon;
volatile unsigned c4pro_usb_diag_live_int2flag;
volatile unsigned c4pro_usb_diag_live_tx0stat;
volatile unsigned c4pro_usb_diag_live_rx0stat;
volatile unsigned c4pro_usb_diag_live_tx0con;
volatile unsigned c4pro_usb_diag_live_rx0con;
volatile unsigned c4pro_usb_diag_live_scu_clkcfg;
volatile unsigned c4pro_usb_diag_live_scu_rstcfg;
volatile unsigned c4pro_usb_diag_live_scu_iomuxa;
volatile unsigned c4pro_usb_diag_live_scu_iomuxb;

void c4pro_usb_diag_sample(void)
{
    c4pro_usb_diag_live_dev_ctl = DEV_CTL;
    c4pro_usb_diag_live_dev_info = DEV_INFO;
    c4pro_usb_diag_live_en_int = EN_INT;
    c4pro_usb_diag_live_intcon = INTCON;
    c4pro_usb_diag_live_int2flag = INT2FLAG & 0x7fffff;
    c4pro_usb_diag_live_tx0stat = TX0STAT;
    c4pro_usb_diag_live_rx0stat = RX0STAT;
    c4pro_usb_diag_live_tx0con = TX0CON;
    c4pro_usb_diag_live_rx0con = RX0CON;
    c4pro_usb_diag_live_scu_clkcfg = SCU_CLKCFG;
    c4pro_usb_diag_live_scu_rstcfg = SCU_RSTCFG;
    c4pro_usb_diag_live_scu_iomuxa = SCU_IOMUXA_CON;
    c4pro_usb_diag_live_scu_iomuxb = SCU_IOMUXB_CON;
}

#endif

#undef ENDPOINT

static void setup_received(void)
{
    static uint32_t setup_data[2];
    logf("udc: setup");

    /* copy setup data from packet */
    setup_data[0] = SETUP1;
    setup_data[1] = SETUP2;
#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
    c4pro_usb_diag_setup_count++;
    c4pro_usb_diag_last_setup0 = setup_data[0];
    c4pro_usb_diag_last_setup1 = setup_data[1];
    if(setup_data[0] == 0 && setup_data[1] == 0)
        return;
#endif

    /* pass setup data to the upper layer */
    usb_core_setup_received((struct usb_ctrlrequest*)setup_data);
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

    while(remaining > 0)
    {
        int chunk = MIN(C4PRO_SYNC_IN_CHUNK, remaining);
        unsigned int timeout;

        /* 1. DMA engine idle */
        timeout = current_tick + HZ/10;
        while(DMAINCTL(endp) & DMA_START)
            if(TIME_AFTER(current_tick, timeout)) break;

        /* 2. FIFO not full */
        timeout = current_tick + HZ/10;
        while(TXBUF(endp) & TXFULL)
            if(TIME_AFTER(current_tick, timeout)) break;

        /* 3-5. program count + source and fire the DMA, then return to the
         * loop IMMEDIATELY - exactly per stock @0x607041BC, which does NOT wait
         * for DMA_START to self-clear and NEVER touches TXCON in the send path.
         * The double-buffer pipelines naturally: the wait at step 1/2 of the
         * NEXT chunk is the only synchronisation. A read-modify-write on TXCON
         * here would diverge from stock and can disturb the TX FIFO
         * double-buffer (slots stuck full, DMA retiring without refill). */
        TXSTAT(endp)      = chunk;
        DMAINLMADDR(endp) = (uint32_t)endp->buf;
        DMAINCTL(endp)    = DMA_START;

        endp->buf += chunk;
        remaining -= chunk;
    }

    /* The whole payload has been drained synchronously. Complete the transfer
     * WITHOUT relying on the TXACK interrupt, mirroring exactly what in_intr()
     * does on the final TXACK so the usb_storage state machine advances to the
     * CSW. endp->len is the total length set in _usb_drv_send(). */
    endp->cnt = 0;
    usb_core_transfer_complete(endp->ep_num, endp->dir, 0, endp->len);
    if(endp->block)
        semaphore_release(&endp->complete);
}
#endif /* COLORFLY_C4_PRO */

static void ep_write(struct endpoint_t *endp)
{
    int xfer_size = MIN(max_pkt_size(endp), endp->cnt);
    unsigned int timeout = current_tick + HZ/10;

#if defined(COLORFLY_C4_PRO)
    /* Divert bulk-IN data endpoints to the synchronous polled sender (stock
     * @0x607041BC). EP0/control and OUT stay on the interrupt-driven path. */
    if(c4pro_is_sync_in_ep(endp))
    {
        c4pro_ep_write_sync(endp);
        return;
    }
#endif

    while(TXBUF(endp) & TXFULL) /* TXFULL flag */
    {
        if(TIME_AFTER(current_tick, timeout))
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
    {
        unsigned int dma_to = current_tick + HZ/10;
        while(DMAINCTL(endp) & DMA_START)
        {
            if(TIME_AFTER(current_tick, dma_to))
                break;
        }
    }
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
    int xfer_size = MIN(max_pkt_size(endp), endp->cnt);

    /* setup DMA */
    RXSTAT(endp) = xfer_size;
    DMAOUTLMADDR(endp) = (uint32_t)endp->buf; /* local buffer address */
    DMAOUTCTL(endp) = DMA_START;
    /* clear NAK */
    RXCON(endp) &= ~RXNAK;
}

static void in_intr(struct endpoint_t *endp)
{
    uint32_t txstat = TXSTAT(endp);
#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
    if(endp->ep_num == 0)
    {
        c4pro_usb_diag_in0_count++;
        c4pro_usb_diag_last_tx0stat = txstat;
        c4pro_usb_diag_last_tx0con = TXCON(endp);
    }
#endif
    /* check if clear feature was sent by host */
    if(txstat & TXCFINT)
    {
        logf("clear_stall: %d", endp->ep_num);
        usb_drv_stall(endp->ep_num, false, true);
    }
#if defined(COLORFLY_C4_PRO)
    /* Bulk-IN data endpoints are sent synchronously in c4pro_ep_write_sync(),
     * which already drained the whole transfer and called
     * usb_core_transfer_complete() exactly once. The hardware still raises a
     * TXACK per packet afterwards; ignore those here so the transfer is not
     * completed (and the next one not re-armed) a second time. Control EP0 and
     * the OUT path are untouched. */
    if(c4pro_is_sync_in_ep(endp))
        return;
#endif
    /* check if a transfer has finished */
    if(txstat & TXACK)
    {
        logf("udc: ack(%d)", endp->ep_num);
        /* finished ? */
        if(endp->cnt <= 0)
        {
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
#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
    if(endp->ep_num == 0)
    {
        c4pro_usb_diag_out0_count++;
        c4pro_usb_diag_last_rx0stat = rxstat;
        c4pro_usb_diag_last_rx0con = RXCON(endp);
    }
    else
    {
        c4pro_usb_diag_outn_count++;
        c4pro_usb_diag_last_outn = endp->ep_num;
        c4pro_usb_diag_last_outn_stat = rxstat;
        c4pro_usb_diag_last_outn_con = RXCON(endp);
    }
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
        int xfer_size = rxstat & 0xffff;
        endp->cnt -= xfer_size;
        endp->buf += xfer_size;
        logf("udc: ack(%d) -> %d/%d", endp->ep_num, xfer_size, endp->cnt);
        /* finished ? */
        if(endp->cnt <= 0 || xfer_size < max_pkt_size(endp))
            usb_core_transfer_complete(endp->ep_num, endp->dir, 0,
                                       endp->len - endp->cnt);
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

#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
/* Arm EP0 (and latch CSR_DONE) BEFORE asserting the D+ pull-up, mirroring the
 * stock RK27xx first-stage loader's USB bring-up (init @0x60005438, EP config
 * @0x60004114): EN_INT/INTCON/TX0CON/RX0CON and CSR_DONE are all configured
 * first, and DEV_SOFT_CN (connect) is the very last write.
 *
 * Rockbox's default path instead asserted DEV_SOFT_CN in udc_soft_connect()
 * while EP0's receiver (RX0CON.RXEPEN) was still disarmed and CSR_DONE not yet
 * latched; EP0 only got armed later inside the USBRST ISR. On a cold NAND boot
 * (no MaskROM USB warm-up) the controller therefore had no armed EP0 SETUP
 * receiver when the host began enumeration, so the host's bus resets were seen
 * but the first SETUP was never captured (observed as rst>0, con0, setup0, and
 * no host enumeration). Arming before connect is the fix. */
static void c4pro_udc_arm_and_connect(void)
{
    EN_INT = EN_SETUP_INTR | EN_IN0_INTR | EN_OUT0_INTR |
             EN_USBRST_INTR | EN_RESUME_INTR | EN_SUSP_INTR;
    INTCON = UDC_INTHIGH_ACT | UDC_INTEN;
    TX0CON = TXNAK | TXACKINTEN;            /* 0x44 */
    RX0CON = RXNAK | RXEPEN | RXACKINTEN;   /* 0x98: arm EP0 SETUP/OUT receiver */
    /* EXPERIMENT: preserve the hardware-set DEV_PHY16BIT (16-bit PHY) rather
     * than forcing it clear. Clearing it to 0x118 did NOT enable HS (still
     * full-speed + host error -71), so the PHY may genuinely be 16-bit and need
     * the bit set for the chirp. Only clear SOFT_POR while latching CSR_DONE. */
    DEV_CTL = (DEV_CTL & ~SOFT_POR) | CSR_DONE | DEV_SELF_PWR;
    DEV_CTL |= DEV_SOFT_CN;                 /* assert pull-up LAST */
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

    /* SET CONFIGURATION request */
    if(!set_configuration)
        if(dev_info & DEV_EN)
        {
            set_configuration = true;
            usb_core_notify_set_config(((dev_info >> 7) & 0xf) + 1);
        }
}

void usb_drv_startup(void) {
    /* fill the endpoint spec table */
    usb_drv_ep_specs[0].type[DIR_OUT] = USB_ENDPOINT_XFER_CONTROL;
    usb_drv_ep_specs[0].type[DIR_IN] = USB_ENDPOINT_XFER_CONTROL;
    for(int ep_num = 1; ep_num < 16; ep_num++) {
        int dir = endpoints[ep_num].dir == USB_DIR_IN ? DIR_IN : DIR_OUT;
        int type = endpoints[ep_num].type;
        usb_drv_ep_specs[ep_num].type[dir] = type;
        usb_drv_ep_specs[ep_num].type[!dir] = USB_ENDPOINT_TYPE_NONE;
    }
}

/* return port speed FS=0, HS=1 */
int usb_drv_port_speed(void)
{
    return (DEV_INFO & DEV_SPEED) ? 0 : 1;
}

void usb_drv_ep_init(const struct usb_drv_ep_alloc_ctx* ctx, int ep)
{
    /* FIXME: support max packet size override */
    (void)ctx;

    const int epnum = EP_NUM(ep);
    const int epdir = EP_DIR(ep);

    struct endpoint_t *endp = &endpoints[epnum];

    if(epdir == DIR_IN)
#if defined(COLORFLY_C4_PRO)
        /* Match the stock C4 firmware's bulk-IN endpoint config (TX2CON=0x248):
         * TXEPEN|TXACKINTEN, with NO TXNAK and NO TXCFINTE. Leaving TXNAK set
         * (Rockbox default 0x124C) makes the controller transmit stale FIFO/DMA
         * residue for >=512-byte bulk reads instead of the freshly-DMA'd data
         * (host saw a {sector,~sector} pattern). Verified against stock EP-config
         * routine @0x60704114. */
        TXCON(endp) = (epnum << 8) | TXEPEN | TXACKINTEN;
#else
        TXCON(endp) = (epnum << 8) | TXEPEN | TXNAK | TXACKINTEN | TXCFINTE;
#endif
    else
#if defined(COLORFLY_C4_PRO)
        /* Stock bulk-OUT config (RX1CON=0x190): RXEPEN|RXACKINTEN, no RXNAK. */
        RXCON(endp) = (epnum << 8) | RXEPEN | RXACKINTEN;
#else
        RXCON(endp) = (epnum << 8) | RXEPEN | RXNAK | RXACKINTEN | RXCFINTE | RXERRINTEN;
#endif
    EN_INT |= 1 << (epnum + 7);
}

void usb_drv_ep_deinit(const struct usb_drv_ep_alloc_ctx* ctx, int ep)
{
    (void)ctx;

    int num = EP_NUM(ep);

    /* disable interrupt from this endpoint */
    EN_INT &= ~(1 << (num + 7));
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

    if (ep_num == 0)
        ep = &ctrlep[DIR_IN];
    else
        ep = &endpoints[ep_num];

    /* for send transfers, make sure the data is committed */
    commit_discard_dcache_range(ptr, length);
    ep->buf = ptr;
    ep->len = ep->cnt = length;
    ep->block = block;

    ep_write(ep);

    /* wait for transfer to end */
    if(block)
        semaphore_wait(&ep->complete, TIMEOUT_BLOCK);

    return 0;
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
        ep = &ctrlep[DIR_OUT];
    else
        ep = &endpoints[ep_num];

    /* for recv, discard the cache lines related to the buffer */
    commit_discard_dcache_range(ptr, length);
    ep->buf = ptr;
    ep->len = ep->cnt = length;
    ep_read(ep);
    return 0;
}

/* Kill all transfers. Usually you need to set a bit for each endpoint
 *  and flush fifos. You should also call the completion handler with
 * error status for everything
 */
void usb_drv_cancel_all_transfers(void)
{
}

/* Set test mode, you can forget that for now, usually it's sufficient
 * to bit copy the argument into some register of the controller
 */
void usb_drv_set_test_mode(int mode)
{
    (void)mode;
}

/* Check if endpoint is in stall state */
bool usb_drv_stalled(int endpoint, bool in)
{
    struct endpoint_t *endp = &endpoints[EP_NUM(endpoint)];

    if(in)
        return !!(TXCON(endp) & TXSTALL);
    else
        return !!(RXCON(endp) & RXSTALL);
}

/* Stall the endpoint. Usually set a flag in the controller */
void usb_drv_stall(int endpoint, bool stall, bool in)
{
    struct endpoint_t *endp = &endpoints[EP_NUM(endpoint)];
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

/* one time init (once per connection) - basicaly enable usb core */
void usb_drv_init(void)
{
    usb_drv_startup();

    /* init semaphore of ep0 */
    semaphore_init(&ctrlep[DIR_OUT].complete, 1, 0);
    semaphore_init(&ctrlep[DIR_IN].complete, 1, 0);

    for(int ep_num = 1; ep_num < USB_NUM_ENDPOINTS; ep_num++)
        semaphore_init(&endpoints[ep_num].complete, 1, 0);

#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
    if(!c4pro_usb_did_soft_connect)
    {
        c4pro_usb_did_soft_connect = true;
#ifdef C4PRO_USB_STOCK_PATH
        /* A/B EXPERIMENT: stock rk27xx connect path (as HM-801, which does
         * negotiate high-speed): SOFT_POR the PHY (hardware then sets PHY16BIT,
         * DEV_CTL->0x158), wait, then standard udc_soft_connect. Tests whether
         * the C4 'no POR / no PHY16BIT' customisation is what blocks HS. */
        DEV_CTL = DEV_SELF_PWR;
        udc_phy_reset();
        udelay(10000);
        udc_soft_connect();
#else
        /* Do NOT SOFT_POR the PHY here. Boot snapshot proves the BootROM (cold
         * boot) / MaskROM (s2run) leaves the UDC PHY at DEV_CTL=0x08: an 8-bit
         * UTMI state (PHY16BIT clear, SOFT_POR clear) that chirps and enumerates
         * HIGH-speed cleanly. A udc_phy_reset() (SOFT_POR) re-inits the PHY,
         * after which the hardware re-detects 16-bit mode (DEV_CTL->0x158) and
         * the high-speed chirp no longer matches what the host saw from the ROM,
         * so it falls back to corrupted full-speed (host error -71). The stock
         * RK27xx loader never PORs the PHY either. Inherit the ROM's known-good
         * HS PHY state and only arm EP0 + assert the pull-up. */
        c4pro_udc_arm_and_connect();
#endif
    }
#endif
}

/* turn off usb core */
void usb_drv_exit(void)
{
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

/* UDC ISR function */
void INT_UDC(void)
{
    /* read what caused UDC irq */
    uint32_t intsrc = INT2FLAG & 0x7fffff;
    if(intsrc == 0)
        return;
#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
    c4pro_usb_diag_irq_count++;
    c4pro_usb_diag_last_intsrc = intsrc;
    c4pro_usb_diag_last_dev_info = DEV_INFO;
#endif

    if(intsrc & USBRST_INTR) /* usb reset */
    {
#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
        c4pro_usb_diag_reset_count++;
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
#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
        c4pro_usb_diag_last_tx0con = TX0CON;
        c4pro_usb_diag_last_rx0con = RX0CON;
#endif

        set_address = false;
        set_configuration = false;
        usb_core_bus_reset();
    }
    /* This needs to be processed AFTER usb reset */
    udc_helper();

    if(intsrc & SETUP_INTR) /* setup interrupt */
    {
        setup_received();
    }
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
    if(intsrc & USBRST_INTR)
    {
        /* usb reset */
        usb_drv_init();
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
#if defined(C4PRO_USB_DIAG_EXPORT) || defined(C4PRO_USB_MSC_ENABLE)
        c4pro_usb_diag_conn_count++;
#ifdef C4PRO_USB_STOCK_PATH
        /* A/B: stock CONN handler (phy reset + soft connect) */
        udc_phy_reset();
        udelay(10000);
        udc_soft_connect();
#endif
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
}
