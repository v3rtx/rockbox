/* Battery management for the Colorfly C4 Pro.
 *
 * The C4 uses a single-cell Li-Po (SRP903852, ~3.0-4.2 V). This driver follows
 * the canonical rk27xx pattern (see powermgmt-rk27generic.c and the ihifi/hm60x
 * ports): the battery ADC channel reads half of the cell voltage through a
 * resistive divider, the ADC full-scale (2^10) is assumed to represent 3.00 V,
 * so the battery voltage in millivolts is (adc * BATTERY_SCALE_FACTOR) >> 10.
 *
 * NOTE: the earlier stub was copied from HM801, the lone rk27xx port that
 * returned the RAW ADC value with a wrong-chemistry 4.25-5.20 "volt" curve.
 * That has been replaced with the proper millivolt-scaled single-cell Li-Po
 * form. BATTERY_SCALE_FACTOR and the discharge/charge curves below are the
 * generic rk27xx baseline and still need on-device calibration: log the raw
 * ADC (Rockbox debug battery screen) against a metered cell voltage from full
 * to empty, then adjust BATTERY_SCALE_FACTOR so the reading matches and fit
 * percent_to_volt_discharge to the measured curve.
 */

#include "config.h"
#include "adc.h"
#include "adc-target.h"
#include "powermgmt.h"

unsigned short battery_level_disksafe = 3400; /* TODO: calibrate on device */
unsigned short battery_level_shutoff = 3300;  /* TODO: calibrate; protects the
                                               * Li-Po from over-discharge */

/* voltages (millivolt) of 0%, 10%, ... 100% when charging disabled */
unsigned short percent_to_volt_discharge[11] =
{
    /* generic single-cell Li-Po baseline, uncalibrated */
    3300, 3600, 3665, 3730, 3795, 3860, 3925, 3990, 4055, 4120, 4185
};

/* voltages (millivolt) of 0%, 10%, ... 100% when charging enabled */
unsigned short percent_to_volt_charge[11] =
{
    3300, 3600, 3665, 3730, 3795, 3860, 3925, 3990, 4055, 4120, 4185
};

/* full-scale ADC readout (2^10) in millivolt: 3.00 V reference, half divider */
#define BATTERY_SCALE_FACTOR 6000

/* Returns battery voltage from ADC [millivolts] */
int _battery_voltage(void)
{
    return (adc_read(ADC_BATTERY) * BATTERY_SCALE_FACTOR) >> 10;
}
