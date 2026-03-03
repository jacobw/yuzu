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
		return 0;
	}

	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		LOG_ERR("Failed to configure LED GPIO (err %d)", err);
		return err;
	}

	while (true) {
		struct battery_measurement batt = {0};
		uint16_t temp = 0;
		uint16_t hum = 0;
		struct sensor_value temp_raw, hum_raw;

		int battery_err = battery_measure(&batt);
		if (battery_err != 0) {
			LOG_WRN("Failed to read battery: %d", battery_err);
			continue;
		}

		// Get tempertaure and humidity data
		if (sht) {
			err = sensor_sample_fetch(sht);
			if (err == 0) {
				err = sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &temp_raw);
			}
			if (err == 0) {
				err = sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &hum_raw);
			}
			if (err != 0) {
				LOG_WRN("SHT failed: %d", err);
				continue;
			}
			temp = roundf(sensor_value_to_float(&temp_raw) * 10) * 10;
			hum = roundf(sensor_value_to_float(&hum_raw) * 10) * 10;
		}

		LOG_INF("Batt %u.%02u%% %umV, Temp: %uC, Hum %u%%RH", 
				batt.level / 100, batt.level % 100, 
				batt.mv, temp, hum);
		bluetooth_update(batt.level, batt.mv, temp, hum);

		/* Flash LED after successful update */
		gpio_pin_set_dt(&led, 1);
		k_msleep(10);
		gpio_pin_set_dt(&led, 0);

		k_msleep(CONFIG_YUZU_SAMPLE_INTERVAL_MS);
	};

	return 0;
}
