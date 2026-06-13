/* Power driver placeholder for Colorfly C4 Pro.
 * GPIO routing for the power latch is currently unknown; mirroring the HM801
 * path (GPIO_PCDR/PCCON bit 0) is a reasonable first guess — the rk27xx SoC
 * reference design uses the same line on multiple HiFiMAN/IHIFI boards.
 */

#include <stdbool.h>
#include "config.h"
#include "inttypes.h"
#include "power.h"
#include "panic.h"
#include "system.h"
#include "usb_core.h"

void power_off(void)
{
    GPIO_PCDR &= ~(1<<0);
    while(1);
}

void power_init(void)
{
    GPIO_PCDR |= (1<<0);
    GPIO_PCCON |= (1<<0);
}

unsigned int power_input_status(void)
{
    return (usb_detect() == USB_INSERTED) ? POWER_INPUT_MAIN_CHARGER : POWER_INPUT_NONE;
}

bool charging_state(void)
{
    return true;
}
