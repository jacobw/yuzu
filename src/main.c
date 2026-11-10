#include <errno.h>
#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gpio.h>

#include "bluetooth.h"
#include "battery.h"

LOG_MODULE_REGISTER(yuzu, CONFIG_LOG_DEFAULT_LEVEL);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define SHT_NODE DEVICE_DT_GET_ONE(sensirion_sht4x);
const struct device *const sht = SHT_NODE;

int main(void)
{
	int err;

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED GPIO device not ready");
		return -ENODEV;
	}

	if (!device_is_ready(sht)) {
		LOG_ERR("SHT device %s is not ready", sht->name);
	} else {
		LOG_INF("SHT Device ready");
	}

	err = bluetooth_init();
	if (err != 0) {
		LOG_ERR("Failed to update advertising data (err %d)", err);
		return err;
	}

	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		LOG_ERR("Failed to configure LED GPIO (err %d)", err);
		return err;
	}

	while (true) {
		struct battery_measurement batt = {0};
		int16_t temp = 0;
		uint16_t hum = 0;
		struct sensor_value temp_raw, hum_raw;

		/* A failed read falls through to the sleep: retrying at once would
		 * spin and drain the cell. */
		int battery_err = battery_measure(&batt);

		int sensor_err = sensor_sample_fetch(sht);
		if (sensor_err == 0) {
			sensor_err = sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &temp_raw);
		}
		if (sensor_err == 0) {
			sensor_err = sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &hum_raw);
		}

		if (battery_err == 0 && sensor_err == 0) {
			/* Hundredths, rounded to 0.1. Signed: BTHome's temperature is,
			 * and a negative float converted to an unsigned type is undefined.
			 * The driver doesn't clamp humidity (-6 to 119 %). */
			temp = (int16_t)(lroundf(sensor_value_to_float(&temp_raw) * 10) * 10);
			hum = (uint16_t)(CLAMP(lroundf(sensor_value_to_float(&hum_raw) * 10), 0, 1000) * 10);

			LOG_INF("Batt %u.%02u%% %umV, Temp: %dC, Hum %u%%RH", 
					batt.level / 100, batt.level % 100, 
					batt.mv, temp, hum);
			bluetooth_update(batt.level, batt.mv, temp, hum);

			/* Flash LED after successful update */
			gpio_pin_set_dt(&led, 1);
			k_msleep(10);
			gpio_pin_set_dt(&led, 0);
		} else {
			LOG_WRN("Measurement failed - battery err:%d sensor err:%d", 
					battery_err, sensor_err);
		}

		k_msleep(CONFIG_YUZU_SAMPLE_INTERVAL_MS);
	}

	return 0;
}
