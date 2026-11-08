/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 by Rockbox contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/

#include "config.h"
#include "system.h"
#include "button.h"
#include "backlight.h"
#include "adc.h"
#include "adc-target.h"

void button_init_device(void)
{
}

bool button_hold(void)
{
    return false;
}

int button_read_device(void)
{
    int adc = adc_read(ADC_BUTTONS);

    if (adc < 70)
        return BUTTON_MENU;
    if (adc < 225)
        return BUTTON_SELECT;
    if (adc < 406)
        return BUTTON_BACK;
    if (adc < 470)
        return BUTTON_LEFT;
    if (adc < 580)
        return BUTTON_SRC;
    if (adc < 680)
        return BUTTON_EQ;
    if (adc < 800)
        return BUTTON_UP;
    if (adc < 895)
        return BUTTON_DOWN;
    if (adc < 960)
        return BUTTON_RIGHT;

    return 0;
}
