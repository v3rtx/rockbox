/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 ****************************************************************************/

#include "system.h"
#include "kernel.h"
#include "audiohw.h"
#include "cs4398.h"
#include "pcm_sampr.h"
#ifdef HAVE_SW_VOLUME_CONTROL
#include "pcm_sw_volume.h"
#endif

#define CS8422_I2C_ADDR     0x10

extern int rk27xx_i2c_write(unsigned char slave, int address, int len,
                            const unsigned char *data);
extern int rk27xx_i2c_read(unsigned char slave, int address, int len,
                           unsigned char *data);

static unsigned char c4pro_cs4398_addr = CS4398_I2C_ADDR << 1;
static unsigned char c4pro_cs8422_addr = CS8422_I2C_ADDR << 1;
static bool c4pro_codecs_detected;

static void c4pro_gpio_out_pc(int bit)
{
    switch (bit)
    {
    case 1:
        break;
    case 4:
        SCU_IOMUXB_CON &= ~(1u << 2);
        break;
    case 6:
        SCU_IOMUXB_CON &= ~(1u << 4);
        break;
    default:
        return;
    }

    GPIO_PCCON |= 1u << bit;
}

static void c4pro_gpio_set_pc(int bit, bool high)
{
    if (high)
        GPIO_PCDR |= 1u << bit;
    else
        GPIO_PCDR &= ~(1u << bit);
}

static void c4pro_clockgen_write(unsigned int word)
{
    c4pro_gpio_out_pc(1);
    c4pro_gpio_out_pc(4);
    c4pro_gpio_out_pc(6);

    c4pro_gpio_set_pc(6, false);
    c4pro_gpio_set_pc(4, true);

    for (int bit = 15; bit >= 0; bit--)
    {
        c4pro_gpio_set_pc(1, (word & (1u << bit)) != 0);
        udelay(2000);
        c4pro_gpio_set_pc(6, true);
        udelay(2000);
        c4pro_gpio_set_pc(6, false);
    }

    c4pro_gpio_set_pc(4, false);
    sleep(HZ / 50);
    c4pro_gpio_set_pc(4, true);
    sleep(HZ / 50);
    c4pro_gpio_set_pc(4, false);
}

static unsigned int c4pro_clockgen_word(int fsel)
{
    switch (fsel)
    {
#ifdef HW_HAVE_8
    case HW_FREQ_8: return 0x0010;
#endif
#ifdef HW_HAVE_11
    case HW_FREQ_11: return 0x0001;
#endif
#ifdef HW_HAVE_12
    case HW_FREQ_12: return 0x0011;
#endif
#ifdef HW_HAVE_16
    case HW_FREQ_16: return 0x0012;
#endif
#ifdef HW_HAVE_22
    case HW_FREQ_22: return 0x0003;
#endif
#ifdef HW_HAVE_24
    case HW_FREQ_24: return 0x0013;
#endif
#ifdef HW_HAVE_32
    case HW_FREQ_32: return 0x0014;
#endif
#ifdef HW_HAVE_44
#ifdef C4PRO_CLOCKGEN_44_WORD
    case HW_FREQ_44: return C4PRO_CLOCKGEN_44_WORD;
#else
    case HW_FREQ_44: return 0x0005;
#endif
#endif
#ifdef HW_HAVE_48
    /* 0x0015, NOT 0x0005: 0x0005 is the 44.1kHz word, so 48kHz files were clocked
     * at 44.1kHz -> ~8% slow + pitched down. The clockgen encodes rate families as
     * two sequences where doubling the rate adds 2 to the word; the 48kHz family is
     * 8k=0x10,12k=0x11,16k=0x12,24k=0x13,32k=0x14, so 48k (=24k x2) = 0x15. (An
     * earlier probe only tried 0x06/0x07, both in the 44.1 bank, and wrongly
     * settled on 0x0005.) */
    case HW_FREQ_48: return 0x0015;
#endif
#ifdef HW_HAVE_96
    /* 0x0017, NOT 0x0007: 0x0007 is the 88.2kHz word (44.1 family: 11k=0x01,
     * 22k=0x03, 44.1k=0x05, 88.2k=0x07), so 96kHz played ~8% slow. 96k = 48k x2 in
     * the 48kHz family (0x15 -> 0x17). */
    case HW_FREQ_96: return 0x0017;
#endif
    default: return 0x0001;
    }
}

static int cs4398_read_reg8(unsigned char reg)
{
    unsigned char val = 0xff;
    rk27xx_i2c_read(c4pro_cs4398_addr, reg, 1, &val);
    return val;
}

static void c4pro_amp_enable(bool enable)
{
    GPIO_PDCON |= (1u << 7);

    if (enable)
        GPIO_PDDR |= (1u << 7);
    else
        GPIO_PDDR &= ~(1u << 7);
}

static void cs8422_write_reg(unsigned char reg, unsigned char val)
{
    rk27xx_i2c_write(c4pro_cs8422_addr, reg, 1, &val);
}

static void c4pro_detect_audio_codecs(void)
{
    unsigned char chipid = 0xff;
    c4pro_codecs_detected = true;

    for (unsigned char addr7 = 0x4c; addr7 <= 0x4f; addr7++)
    {
        unsigned char addr = addr7 << 1;

        if (rk27xx_i2c_read(addr, CS4398_REG_CHIPID, 1, &chipid) == 0 &&
            (chipid & CS4398_PART_MASK) == CS4398_PART_CS4398)
        {
            c4pro_cs4398_addr = addr;
            break;
        }
        else
            c4pro_cs4398_addr = CS4398_I2C_ADDR << 1;
    }

    for (unsigned char addr7 = 0x10; addr7 <= 0x17; addr7++)
    {
        unsigned char addr = addr7 << 1;

        if (rk27xx_i2c_read(addr, 0x01, 1, &chipid) == 0 &&
            (chipid & 0xf8) == 0x10)
        {
            c4pro_cs8422_addr = addr;
            break;
        }
        else
            c4pro_cs8422_addr = CS8422_I2C_ADDR << 1;
    }
}

static void cs8422_init(void)
{
    cs8422_write_reg(0x02, 0x80);
#ifdef C4PRO_RK_I2S_SLAVE_TEST
    /* Exact T1 tone-test route: RK I2S slave, CS8422 SAI master/SRC I2S. */
    cs8422_write_reg(0x03, 0x08);
    cs8422_write_reg(0x04, 0x84);
    cs8422_write_reg(0x07, 0x40);
    cs8422_write_reg(0x08, 0x40);
    cs8422_write_reg(0x09, 0x00);
    cs8422_write_reg(0x0a, 0x00);
    cs8422_write_reg(0x0b, 0x88);
    cs8422_write_reg(0x0c, 0x84);
    cs8422_write_reg(0x0d, 0x84);
    cs8422_write_reg(0x02, 0x08);
#else
    cs8422_write_reg(0x03, 0x84);
    cs8422_write_reg(0x04, 0x40);
    cs8422_write_reg(0x07, 0x40);
    cs8422_write_reg(0x08, 0x40);
    cs8422_write_reg(0x09, 0x00);
    cs8422_write_reg(0x0a, 0x00);
    cs8422_write_reg(0x0b, 0x88);
    cs8422_write_reg(0x0c, 0x84);
    cs8422_write_reg(0x0d, 0x84);
    cs8422_write_reg(0x02, 0x08);
#endif
}

static void c4pro_cs4398_unmute(void)
{
    int muteflags = CS4398_MUTE_A | CS4398_MUTE_B | CS4398_DAMUTE |
                    CS4398_PAMUTE | CS4398_MUTEC_A_EQ_B;
    cs4398_write_reg(CS4398_REG_MUTE, cs4398_read_reg8(CS4398_REG_MUTE) &
                     ~muteflags);
}

void audiohw_preinit(void)
{
    c4pro_detect_audio_codecs();
    c4pro_clockgen_write(0x0001);
}

void audiohw_init(void)
{
    if (!c4pro_codecs_detected)
        c4pro_detect_audio_codecs();

    cs8422_init();

    cs4398_write_reg(CS4398_REG_MISC, CS4398_CPEN | CS4398_PDN);
    cs4398_write_reg(CS4398_REG_MODECTL, CS4398_FM_SINGLE |
                     CS4398_DIF_I2S);
    cs4398_write_reg(CS4398_REG_VOLMIX, CS4398_VOL_B_EQ_A |
                     CS4398_ATAPI_A_R | CS4398_ATAPI_B_L);
    cs4398_write_reg(CS4398_REG_MUTE, CS4398_PAMUTE);
    cs4398_write_reg(CS4398_REG_VOL_A, 0x00);
    cs4398_write_reg(CS4398_REG_VOL_B, 0x00);
    cs4398_write_reg(CS4398_REG_RAMPFILT, CS4398_SOFT_RAMP |
                     CS4398_RMP_UP | CS4398_RMP_DN |
                     CS4398_FILT_SEL);
    cs4398_write_reg(CS4398_REG_MISC, CS4398_CPEN);
}

void audiohw_postinit(void)
{
    SCU_IOMUXB_CON &= ~(1u << 14);
    /* Amp OFF first. Do NOT pre-drive PD7 high (the old GPIO_PDDR |= 1<<7 before
     * amp_enable(false) briefly enabled the amp while the DAC was still
     * unpowered -> turn-on pop). c4pro_amp_enable() sets the pin to output-low
     * directly. */
    c4pro_amp_enable(false);
    audiohw_init();                 /* DAC powered up but MUTED (PAMUTE set) */

    /* Do NOT reset the SW master volume here. postinit runs on every playback
     * wake, and pcm_set_master_volume(0,0) forces the software master to unity
     * (0 dB) -- clobbering the UI volume that sound.c applied via
     * audiohw_set_volume, so playback always came out at full scale regardless
     * of the setting. The DAC hardware is kept at 0 dB by the cs4398 VOL_A/B
     * writes; the software master owns the UI volume and must persist. */

    /* Enable the amp while the DAC is still muted, so the amp's turn-on
     * transient lands in silence, then soft-ramp the DAC up -- a smooth fade-in
     * with no click. (Old order unmuted first and enabled the amp last, so the
     * amp turned on into a live signal -> audible pop.) */
    sleep(HZ / 20);                 /* DAC settles while muted (~50ms) */
    c4pro_amp_enable(true);
    sleep(HZ / 20);                 /* amp turn-on transient decays in silence */
    c4pro_cs4398_unmute();          /* soft-ramp fade-in */
}

void audiohw_close(void)
{
    /* Pop-free shutdown. Previously this just asserted PDN, powering the DAC
     * down while the HP amp was still enabled and unmuted -- the DAC output
     * collapse passed straight through the live amp as a loud pop. Instead:
     * soft-ramp the DAC to silence, kill the amp (now it sees silence, so no
     * transient), THEN power the DAC down. */
    cs4398_write_reg(CS4398_REG_MUTE, CS4398_MUTE_A | CS4398_MUTE_B |
                     CS4398_DAMUTE | CS4398_PAMUTE | CS4398_MUTEC_A_EQ_B);
    sleep(HZ / 20);                 /* let the soft ramp reach silence (~50ms) */
    c4pro_amp_enable(false);
    sleep(HZ / 50);                 /* amp fully off before the DAC collapses */
    cs4398_write_reg(CS4398_REG_MISC, CS4398_CPEN | CS4398_PDN);
}

void audiohw_set_frequency(int fsel)
{
    c4pro_clockgen_write(c4pro_clockgen_word(fsel));
}

void audiohw_set_volume(int vol_l, int vol_r)
{
    /* SW master volume owns the UI volume. The hardware chain is
     * RK27xx I2S -> CS8422 (async SRC) -> CS4398 DAC -> AD823 -> analog ALPS pot
     * -> headphone amps; the ALPS pot is the stock (analog) volume control and
     * the digital chain runs bit-perfect at unity. Attenuating the PCM data the
     * RK27xx DMAs to I2S is the single upstream lever that reduces loudness, and
     * pcm_sw_volume scales exactly that buffer. (The CS4398 digital volume was
     * verified NOT to attenuate through the CS8422 SRC path, so we leave the DAC
     * at 0 dB like stock and do all UI volume in software.) vol_l/vol_r arrive
     * from sound.c in centibels (tenth-dB) = pcm_set_master_volume's unit. */
#ifdef HAVE_SW_VOLUME_CONTROL
    pcm_set_master_volume(vol_l, vol_r);
#endif
    /* DAC stays at 0 dB (unity), matching stock. */
    cs4398_write_reg(CS4398_REG_VOL_A, 0x00);
    cs4398_write_reg(CS4398_REG_VOL_B, 0x00);
}

void audiohw_set_lineout_volume(int vol_l, int vol_r)
{
    /* No-op on the C4: there is no independent analog line-out level to set --
     * the RCA jack carries bit-perfect SPDIF (no volume) and the only analog
     * output is the headphone path, whose level is the SW master volume set by
     * audiohw_set_volume(). AUDIOHW_CAPS advertises LINEOUT_CAP (from the shared
     * cs4398.h, needed by the jz4760), so sound.c calls this with (0,0) right
     * after every audiohw_set_volume(). Aliasing it to audiohw_set_volume (the
     * old behavior) therefore reset the UI volume to 0 dB / full scale on every
     * single change -- which defeated SW volume, the smooth-transition path, and
     * the CS4398 HW volume alike. Keep it a no-op so the UI volume sticks. */
    (void)vol_l;
    (void)vol_r;
}

void audiohw_set_filter_roll_off(int value)
{
    unsigned char ramp = CS4398_SOFT_RAMP | CS4398_RMP_UP | CS4398_RMP_DN;

    if (value != 0)
        ramp |= CS4398_FILT_SEL;

    cs4398_write_reg(CS4398_REG_RAMPFILT, ramp);
}

/* ----------------------------------------------------------------------- *
 * S/PDIF DAC-input mode: coax S/PDIF into an RCA jack -> CS8422 AES3
 * receiver -> async SRC -> CS4398 -> AD823 -> analog out. The output half
 * (SRC -> SDOUT1 -> CS4398, clocked from the clockgen/TCXO on XTI) is
 * identical to normal playback; the only change is the SRC's *input* source,
 * which switches from the RK27xx I2S (SDIN) to the on-chip AES3 receiver
 * (0Ah SRCD bit). The async SRC reclocks any incoming coax rate to the fixed
 * output rate, so no per-rate tracking is needed.
 *
 * The physical RX pin the coax jack is wired to is not documented, so the
 * caller passes a candidate Receiver Input Control (03h) value and watches
 * the CS8422 lock/rate status to identify it. See the CS8422 datasheet
 * (register map 02h/03h/09h/0Ah, status 15h/16h).
 * ----------------------------------------------------------------------- */

int c4pro_cs8422_read(unsigned char reg)
{
    unsigned char val = 0xff;
    rk27xx_i2c_read(c4pro_cs8422_addr, reg, 1, &val);
    return val;
}

/* Point the CS8422 SRC at the AES3 receiver with the given 03h (RX input)
 * value. PDN-cycles around the format change as the datasheet requires. */
static void cs8422_receiver_route(unsigned char rx_input_ctl)
{
    cs8422_write_reg(0x02, 0x80);          /* PDN=1: reconfigure           */
    cs8422_write_reg(0x03, rx_input_ctl);  /* receiver input select        */
    cs8422_write_reg(0x04, 0x40);          /* HOLD last sample on rx error */
    cs8422_write_reg(0x09, 0x08);          /* SRC soft-mute on unlock      */
    cs8422_write_reg(0x0a, 0x02);          /* SRCD=1: SRC src = AES3 rx,   */
                                           /* SDOUT1 still sourced by SRC  */
    cs8422_write_reg(0x02, 0x00);          /* PDN=0, RMCK from recov. rate */
}

void c4pro_spdif_dac_enter(unsigned char rx_input_ctl)
{
    if (!c4pro_codecs_detected)
        c4pro_detect_audio_codecs();

    /* Fixed 48kHz SRC output master clock (XTI from the clockgen). The async
     * SRC converts any coax input rate to this; 48k keeps the CS4398 in its
     * known-good single-speed (FM_SINGLE) configuration. */
    c4pro_clockgen_write(0x0015);

    /* Known-good CS4398 + SAO/clock/format bring-up (as for playback) ... */
    audiohw_init();
    /* ... then re-point the SRC input from SDIN to the AES3 receiver. */
    cs8422_receiver_route(rx_input_ctl);

    /* Amp on into a muted DAC, then unmute -- pop-free, same as postinit. */
    c4pro_amp_enable(false);
    sleep(HZ / 20);
    c4pro_amp_enable(true);
    sleep(HZ / 20);
    c4pro_cs4398_unmute();
}

/* Live-switch the receiver input candidate without a full re-init. */
void c4pro_spdif_dac_set_rx(unsigned char rx_input_ctl)
{
    cs8422_receiver_route(rx_input_ctl);
}

void c4pro_spdif_dac_exit(void)
{
    /* Mute, kill the amp in silence, then power the DAC down (as audiohw_close). */
    cs4398_write_reg(CS4398_REG_MUTE, CS4398_MUTE_A | CS4398_MUTE_B |
                     CS4398_DAMUTE | CS4398_PAMUTE | CS4398_MUTEC_A_EQ_B);
    sleep(HZ / 20);
    c4pro_amp_enable(false);
    sleep(HZ / 50);
    cs4398_write_reg(CS4398_REG_MISC, CS4398_CPEN | CS4398_PDN);
}
