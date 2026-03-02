#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(yuzu_battery, CONFIG_ADC_LOG_LEVEL);

static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static int16_t buf;
static struct adc_sequence sequence = {
	.buffer = &buf,
	.buffer_size = sizeof(buf),
};

static int battery_setup(void)
{
	int ret;

	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&adc_channel);
	if (ret < 0) {
		LOG_ERR("Could not setup channel #%d (%d)", 0, ret);
		return ret;
	}

	ret = adc_sequence_init_dt(&adc_channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Could not initialize sequence (%d)", ret);
		return ret;
	}

	LOG_INF("Battery setup complete");
	return 0;
}

SYS_INIT(battery_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int battery_sample(void)
{
	int ret;
	int32_t val_mv;

	ret = adc_read_dt(&adc_channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Could not read ADC (%d)", ret);
		return ret;
	}

	val_mv = (int)buf;
	ret = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
	if (ret < 0) {
		LOG_WRN("Value in mV not available (%d)", ret);
		return ret;
	}	

	return val_mv;
}

unsigned int battery_level_pptt(unsigned int batt_mV,
				const struct battery_level_point *curve)
{
	const struct battery_level_point *pb = curve;

	if (batt_mV >= pb->lvl_mV) {
		/* Measured voltage above highest point, cap at maximum. */
		return pb->lvl_pptt;
	}
	/* Go down to the last point at or below the measured voltage. */
	while ((pb->lvl_pptt > 0)
	       && (batt_mV < pb->lvl_mV)) {
		++pb;
	}
	if (batt_mV < pb->lvl_mV) {
		/* Below lowest point, cap at minimum */
		return pb->lvl_pptt;
	}

	/* Linear interpolation between below and above points. */
	const struct battery_level_point *pa = pb - 1;

	return pb->lvl_pptt
	       + ((pa->lvl_pptt - pb->lvl_pptt)
		  * (batt_mV - pb->lvl_mV)
		  / (pa->lvl_mV - pb->lvl_mV));
}
