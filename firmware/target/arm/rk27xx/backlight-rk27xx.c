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
#include <stdbool.h>

#include "config.h"
#include "backlight.h"
#include "backlight-target.h"
#include "system.h"
#include "lcd.h"

static int brightness = DEFAULT_BRIGHTNESS_SETTING;

/* Based on http://www.poynton.com/PDFs/SMPTE93_Gamma.pdf
 * CIE standarized function that relates physical luminance
 * to perceived lightness
 *
 * L* = 116*(Y/Yn)^(1/3) - 16 (Y/Yn > 0 008856)
 * where Yn is luminance of reference white
 *
 * Actual function is lightly tweaked to account for the fact
 * that fill factor of the PWM below some value gives black.
 * So the function used to calculate the values in the matrix was:
 * f(x) = (PWM_FREQ_MAX - PWM_FREQ_MIN) * ((100*x/31 + 16)/116)^3 + PWM_FREQ_MIN
 */
#ifdef RK27_GENERIC
static const unsigned short lin_brightness[] = {
     8110,  8191,  8304,  8455,  8649,  8892,  9189,  9545,
     9966, 10457, 11024, 11671, 12406, 13232, 14156, 15182,
    16316, 17565, 18932, 20423, 22045, 23801, 25699, 27742,
    29937, 32289, 34803, 37485, 40340, 43374, 46592, 50000
};
#elif defined(HM801)
static const unsigned short lin_brightness[] = {
    541, 542, 543, 544, 547, 549, 553, 556,
    561, 566, 573, 580, 588, 597, 607, 618,
    631, 644, 659, 676, 693, 713, 733, 756,
    780, 806, 833, 862, 894, 927, 962, 1000
};
#elif defined(HM60X)
static const unsigned short lin_brightness[] = {
    456, 457, 458, 460, 463, 466, 470, 475,
    480, 486, 494, 502, 512, 522, 534, 548,
    562, 579, 596, 616, 637, 660, 684, 711,
    739, 770, 802, 837, 874, 914, 955, 1000
};
#elif defined(MA9) || defined(MA9C) || defined(MA8) || defined(MA8C)
static const unsigned short lin_brightness[] = {
      2,   4,   7,  10,  15,  21,  28,  36,
     46,  58,  72,  87, 104, 124, 146, 171,
    198, 227, 260, 295, 334, 376, 421, 470,
    522, 578, 638, 702, 770, 842, 918, 1000
};
#elif defined(IHIFI760) || defined(IHIFI960)
static const unsigned short lin_brightness[] = {
    700, 701, 702, 703, 704, 706, 708, 711,
    714, 717, 721, 726, 731, 737, 743, 751,
    759, 768, 778, 788, 800, 812, 826, 841,
    856, 873, 891, 910, 931, 952, 975, 1000
};
#elif defined(IHIFI770) || defined(IHIFI770C) || defined(IHIFI800)
static const unsigned short lin_brightness[] = {
     4096,  4215,  4381,  4603,  4887, 5243,   5679,  6201,
     6818,  7538,  8370,  9320, 10397, 11609, 12963, 14469,
    16133, 17963, 19968, 22156, 24534, 27110, 29893, 32890,
    36109, 39559, 43246, 47180, 51368, 55817, 60537, 65535
};
#elif defined(COLORFLY_C4_PRO)
/* C4 Pro PWMT0 backlight has INVERTED polarity vs the sibling rk27xx targets:
 * HRC=0 = full brightness, HRC=LRC(=1000) = off (confirmed on-device — driving
 * HRC=1000 killed the backlight). So this table holds the HRC values directly
 * and is DESCENDING (index 0 = dimmest .. index 31 = brightest). LRC is fixed
 * at 1000 in backlight_hw_init (NOT taken from this table, whose [MAX] is 0).
 *
 * On-device the LED stays dark for HRC > ~666, so the usable HRC window is only
 * [0..~650]; the whole 0..31 slider is mapped into it, gamma-shaped (MA9 duty),
 * level 0 = HRC 649 (dimmest visible) .. level 31 = HRC 0 (brightest). Tune
 * these values on-device as needed. */
static const unsigned short lin_brightness[] = {
    649, 647, 645, 644, 640, 636, 632, 627,
    620, 612, 603, 593, 582, 569, 555, 539,
    521, 502, 481, 458, 433, 406, 376, 344,
    311, 274, 235, 194, 150, 103,  53,   0
};
#endif

bool backlight_hw_init(void)
{
    /* configure PD4 as output */
    GPIO_PDCON |= (1<<4);

    /* set PD4 low (backlight off) */
    GPIO_PDDR &= ~(1<<4);

    /* IOMUXB - set PWM0 pin as GPIO */
    SCU_IOMUXB_CON &= ~(1 << 11); /* type<<11<<channel */

    /* DIV/2, PWM reset */
    PWMT0_CTRL = (0<<9) | (1<<7);

    /* set pwm frequency */
    /* (apb_freq/pwm_freq)/pwm_div = (50 000 000/pwm_freq)/2 */
#ifdef COLORFLY_C4_PRO
    /* fixed PWM period; the C4 lin_brightness table holds HRC directly (inverted
     * polarity, descending), so lin_brightness[MAX] is 0 and unusable as LRC */
    PWMT0_LRC = 1000;
#else
    PWMT0_LRC = lin_brightness[MAX_BRIGHTNESS_SETTING];
#endif
    PWMT0_HRC = lin_brightness[brightness];

    /* reset counter */
    PWMT0_CNTR = 0x00;
    
    /* DIV/2, PWM output enable, PWM timer enable */
    PWMT0_CTRL = (0<<9) | (1<<3) | (1<<0);

    return true;
}

void backlight_hw_on(void)
{
#ifdef HAVE_LCD_ENABLE
    lcd_enable(true);
#endif
    /* enable PWM clock */
    SCU_CLKCFG &= ~CLKCFG_PWM;

    /* set output pin as PWM pin */
    SCU_IOMUXB_CON |= (1<<11); /* type<<11<<channel */

    /* pwm enable */
    PWMT0_CTRL |= (1<<3) | (1<<0);
}

void backlight_hw_off(void)
{
#ifdef COLORFLY_C4_PRO
    /* INVERTED polarity: PD4 low = LED fully ON. The default path below parks
     * PD4 as a pulled-low GPIO, which on this panel blasts the backlight to MAX
     * on every timeout/fade-down. Drive PD4 HIGH (GPIO) so OFF is really off;
     * backlight_hw_on() re-muxes to PWM and restores the user's brightness. */
    SCU_IOMUXB_CON &= ~(1<<11);
    GPIO_PDCON |= (1<<4);
    GPIO_PDDR |= (1<<4);
    PWMT0_CTRL &= ~((1<<3) | (1<<0));
    SCU_CLKCFG |= CLKCFG_PWM;
#else
    /* setup PWM0 pin as GPIO which is pulled low */
    SCU_IOMUXB_CON &= ~(1<<11);

    /* stop pwm timer */
    PWMT0_CTRL &= ~(1<<3) | (1<<0);

    /* disable PWM clock */
    SCU_CLKCFG |= CLKCFG_PWM;
#endif
#ifdef HAVE_LCD_ENABLE
    lcd_enable(false);
#endif
}

void backlight_hw_brightness(int val)
{
    brightness = val & 0x1f;
    PWMT0_HRC = lin_brightness[brightness];
}
