/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2016 by Roman Stolyarov
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

#include "system.h"
#include "cs4398.h"
#include "config.h"
#include "audio.h"
#include "audiohw.h"
#include "i2c.h"

#ifdef COLORFLY_C4_PRO
extern int rk27xx_i2c_write(unsigned char slave, int address, int len,
                            const unsigned char *data);
extern int rk27xx_i2c_read(unsigned char slave, int address, int len,
                           unsigned char *data);
#endif

void cs4398_write_reg(uint8_t reg, uint8_t val)
{
#ifdef COLORFLY_C4_PRO
    rk27xx_i2c_write(CS4398_I2C_ADDR << 1, reg, 1, &val);
#else
    unsigned char buf[2] = {reg, val};
    i2c_write(CS4398_I2C_ADDR, (unsigned char *)&buf, 2);
#endif
}

uint8_t cs4398_read_reg(uint8_t reg)
{
#ifdef COLORFLY_C4_PRO
    unsigned char val = 0xff;
    rk27xx_i2c_read(CS4398_I2C_ADDR << 1, reg, 1, &val);
    return val;
#else
    unsigned char buf[2] = {reg, 0xff};
    i2c_write(CS4398_I2C_ADDR, (unsigned char *)&buf[0], 1);
    i2c_read(CS4398_I2C_ADDR, (unsigned char *)&buf[1], 1);
    return buf[1];
#endif
}
