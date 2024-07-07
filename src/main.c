
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/gpio.h>

#include "bluetooth.h"
#include "battery.h"

LOG_MODULE_REGISTER(yuzu, CONFIG_LOG_DEFAULT_LEVEL);

// 60 seconds for production
#define SLEEP_TIME_MS 60000
// 10 secs for dev
// #define SLEEP_TIME_MS 10000

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define SHT_NODE DEVICE_DT_GET_ONE(sensirion_sht4x);
const struct device *const sht = SHT_NODE;

int main(void)
{
    int err;
    unsigned int batt_pct = 0;
    unsigned int batt_mv = 0;
    unsigned int temp = 0;
    unsigned int hum = 0;
    struct sensor_value temp_raw, hum_raw;

    if (!gpio_is_ready_dt(&led))
    {
        return 0;
    }

    if (!device_is_ready(sht))
    {
        LOG_ERR("SHT device %s is not ready", sht->name);
    }
    else
    {
        LOG_INF("SHT Device ready");
    }

    err = battery_measure_enable(true);
    if (err != 0)
    {
        LOG_ERR("Failed initialize battery measurement: %d", err);
        return 0;
    }

    err = bluetooth_init();
    if (err != 0)
    {
        LOG_ERR("Failed to update advertising data (err %d)", err);
        return 0;
    }

    while (true)
    {
        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (err < 0)
        {
            return 0;
        }
        // Get battery data
        batt_mv = battery_sample();
        if (batt_mv < 0)
        {
            LOG_WRN("Failed to read battery voltage: %d", batt_mv);
            continue;
        }
        batt_pct = battery_level_pptt(batt_mv, discharge_curve_cr2032);

        // Get tempertaure and humidity data
        if (sht)
        {
            err = sensor_sample_fetch(sht);
            if (err == 0)
            {
                err = sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &temp_raw);
            }
            if (err == 0)
            {
                err = sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &hum_raw);
            }
            if (err != 0)
            {
                LOG_WRN("SHT failed: %d", err);
                continue;
            }
            temp = roundf(sensor_value_to_float(&temp_raw) * 100);
            hum = roundf(sensor_value_to_float(&hum_raw) * 100);
        }

        LOG_INF("Batt %d%% %dmV, Temp: %dC, Hum %d%%RH", batt_pct / 100, batt_mv, temp, hum);
        bluetooth_update(batt_pct, batt_mv, temp, hum);

        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
        if (err < 0)
        {
            return 0;
        }
        k_msleep(SLEEP_TIME_MS);
    };
    LOG_INF("Disable: %d", battery_measure_enable(false));
    return 0;
}
