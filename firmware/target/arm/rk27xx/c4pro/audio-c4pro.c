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

#define CS8422_I2C_ADDR     0x10

extern int rk27xx_i2c_write(unsigned char slave, int address, int len,
                            const unsigned char *data);
extern int rk27xx_i2c_read(unsigned char slave, int address, int len,
                           unsigned char *data);

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
    case HW_FREQ_44: return 0x0001;
#endif
#ifdef HW_HAVE_48
    case HW_FREQ_48: return 0x0005;
#endif
#ifdef HW_HAVE_96
    case HW_FREQ_96: return 0x0007;
#endif
    default: return 0x0001;
    }
}

static int cs4398_read_reg8(unsigned char reg)
{
    unsigned char val = 0xff;
    rk27xx_i2c_read(CS4398_I2C_ADDR << 1, reg, 1, &val);
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
    rk27xx_i2c_write(CS8422_I2C_ADDR << 1, reg, 1, &val);
}

static void cs8422_init(void)
{
    cs8422_write_reg(0x02, 0x80);
    cs8422_write_reg(0x03, 0x80);
    cs8422_write_reg(0x04, 0x84);
    cs8422_write_reg(0x07, 0x40);
    cs8422_write_reg(0x08, 0x10);
}

static void c4pro_cs4398_unmute(void)
{
    int muteflags = CS4398_MUTE_A | CS4398_MUTE_B | CS4398_DAMUTE |
                    CS4398_PAMUTE | CS4398_MUTEC_A_EQ_B;
    cs4398_write_reg(CS4398_REG_MUTE, cs4398_read_reg8(CS4398_REG_MUTE) &
                     ~muteflags);
}

static int vol_tenthdb2hw(int tdb)
{
    if (tdb <= CS4398_VOLUME_MIN)
        return 0xff;

    if (tdb >= CS4398_VOLUME_MAX)
        return 0x00;

    return -tdb / 5;
}

void audiohw_preinit(void)
{
    c4pro_clockgen_write(0x0001);
}

void audiohw_init(void)
{
    cs8422_init();

    cs4398_write_reg(CS4398_REG_MISC, CS4398_CPEN | CS4398_PDN);
    cs4398_write_reg(CS4398_REG_MODECTL, CS4398_FM_SINGLE | CS4398_DIF_I2S);
    cs4398_write_reg(CS4398_REG_VOLMIX, CS4398_VOL_B_EQ_A |
                     CS4398_ATAPI_A_R | CS4398_ATAPI_B_L);
    cs4398_write_reg(CS4398_REG_MUTE, CS4398_PAMUTE);
    cs4398_write_reg(CS4398_REG_VOL_A, 0x00);
    cs4398_write_reg(CS4398_REG_VOL_B, 0x00);
    cs4398_write_reg(CS4398_REG_RAMPFILT, CS4398_SOFT_RAMP |
                     CS4398_RMP_UP | CS4398_RMP_DN | CS4398_FILT_SEL);
    cs4398_write_reg(CS4398_REG_MISC, CS4398_CPEN);
}

void audiohw_postinit(void)
{
    SCU_IOMUXB_CON &= ~(1u << 14);
    GPIO_PDDR |= 1u << 7;
    GPIO_PDCON |= 1u << 7;
    c4pro_amp_enable(false);
    audiohw_init();
    c4pro_cs4398_unmute();
    sleep(HZ / 5);
    c4pro_amp_enable(true);
}

void audiohw_close(void)
{
    cs4398_write_reg(CS4398_REG_MISC, CS4398_CPEN | CS4398_PDN);
}

void audiohw_set_frequency(int fsel)
{
    c4pro_clockgen_write(c4pro_clockgen_word(fsel));
}

void audiohw_set_volume(int vol_l, int vol_r)
{
    cs4398_write_reg(CS4398_REG_VOL_A, vol_tenthdb2hw(vol_l));
    cs4398_write_reg(CS4398_REG_VOL_B, vol_tenthdb2hw(vol_r));
}

void audiohw_set_lineout_volume(int vol_l, int vol_r)
{
    audiohw_set_volume(vol_l, vol_r);
}

void audiohw_set_filter_roll_off(int value)
{
    unsigned char ramp = CS4398_SOFT_RAMP | CS4398_RMP_UP | CS4398_RMP_DN;

    if (value != 0)
        ramp |= CS4398_FILT_SEL;

    cs4398_write_reg(CS4398_REG_RAMPFILT, ramp);
}
