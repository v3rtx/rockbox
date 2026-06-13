/* Battery management placeholder for Colorfly C4 Pro.
 * Curve is uncalibrated — copied from HM801 as a working stub. The C4 uses
 * a single-cell Li-Po (SRP903852); ADC channel and voltage scaling will need
 * empirical calibration once button-c4pro.c is functional.
 */

#include "config.h"
#include "adc.h"
#include "adc-target.h"
#include "powermgmt.h"

unsigned short battery_level_disksafe = 430;
unsigned short battery_level_shutoff = 425;

unsigned short percent_to_volt_discharge[11] =
{
    425, 430, 440, 450, 460, 470, 480, 490, 500, 510, 520
};

unsigned short percent_to_volt_charge[11] =
{
    425, 430, 440, 450, 460, 470, 480, 490, 500, 510, 520
};

int _battery_voltage(void)
{
    return adc_read(ADC_BATTERY);
}
