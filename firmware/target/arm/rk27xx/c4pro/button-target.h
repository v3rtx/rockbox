#ifndef _BUTTON_TARGET_H_
#define _BUTTON_TARGET_H_

/* Colorfly C4 Pro front-panel buttons are read from ADC_BUTTONS (LSADC A1).
 * The side analog potentiometer is not a discrete hold switch, so Rockbox uses
 * software keylock rather than HAS_BUTTON_HOLD.
 */

#define BUTTON_UP          0x00000001
#define BUTTON_POWER       0x00000002
#define BUTTON_DOWN        0x00000004
#define BUTTON_LEFT        0x00000008
#define BUTTON_RIGHT       0x00000010
#define BUTTON_SELECT      0x00000020
#define BUTTON_NEXT        0x00000040
#define BUTTON_PREV        0x00000080
#define BUTTON_PLAY        BUTTON_SELECT
#define BUTTON_MENU        0x00000200
#define BUTTON_BACK        0x00000400
#define BUTTON_SRC         0x00000800
#define BUTTON_EQ          0x00001000

#define BUTTON_MAIN        (BUTTON_UP|BUTTON_POWER| \
                            BUTTON_DOWN|BUTTON_LEFT| \
                            BUTTON_RIGHT|BUTTON_SELECT| \
                            BUTTON_NEXT|BUTTON_PREV| \
                            BUTTON_PLAY|BUTTON_MENU| \
                            BUTTON_BACK|BUTTON_SRC| \
                            BUTTON_EQ)

/* Software poweroff on a long hold of the centre (SELECT) button. The ADC keypad
 * never produces BUTTON_POWER, so poweroff was previously unreachable. SELECT
 * also drives the UI context action on a short repeat (~0.3 s), but the poweroff
 * fires only after POWEROFF_COUNT repeats (~2.7 s hold), well past the context
 * press, so the two coexist: tap = select, brief hold = context, long hold = off.
 */
#define POWEROFF_BUTTON BUTTON_SELECT
#define POWEROFF_COUNT 30

#define DEBUG_CANCEL BUTTON_BACK

#endif /* _BUTTON_TARGET_H_ */
