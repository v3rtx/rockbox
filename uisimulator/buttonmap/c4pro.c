/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 by Groove
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/

#include <SDL.h>
#include "button.h"
#include "buttonmap.h"

int key_to_button(int keyboard_button)
{
    switch (keyboard_button)
    {
        case SDLK_ESCAPE:
            return BUTTON_POWER;
        case SDLK_UP:
            return BUTTON_UP;
        case SDLK_DOWN:
            return BUTTON_DOWN;
        case SDLK_LEFT:
            return BUTTON_LEFT;
        case SDLK_RIGHT:
            return BUTTON_RIGHT;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return BUTTON_SELECT;
        case SDLK_SPACE:
            return BUTTON_PLAY;
        case SDLK_PAGEUP:
            return BUTTON_PREV;
        case SDLK_PAGEDOWN:
            return BUTTON_NEXT;
        default:
            return BUTTON_NONE;
    }
}

struct button_map bm[] = {
    { SDLK_UP,        160,  35, 24, "Up" },
    { SDLK_DOWN,      160, 205, 24, "Down" },
    { SDLK_LEFT,       75, 120, 24, "Left" },
    { SDLK_RIGHT,     245, 120, 24, "Right" },
    { SDLK_RETURN,    160, 120, 28, "Select" },
    { SDLK_SPACE,     285, 205, 24, "Play" },
    { SDLK_PAGEUP,     35, 205, 24, "Prev" },
    { SDLK_PAGEDOWN,   95, 205, 24, "Next" },
    { SDLK_ESCAPE,    285,  35, 24, "Power" },
    { 0, 0, 0, 0, "None" }
};
