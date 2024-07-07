#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include "bluetooth.h"

LOG_MODULE_REGISTER(yuzu_bluetooth, CONFIG_LOG_DEFAULT_LEVEL);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define BTHOME_SERVICE_UUID 0xfcd2 // BTHome service UUID
#define IDX_BATT 4                 // Index of battery level in service data
#define IDX_TEMPL 6                // Index of lo byte of temp in service data
#define IDX_TEMPH 7                // Index of hi byte of temp in service data
#define IDX_HUMDL 9                // Index of lo byte of temp in service data
#define IDX_HUMDH 10               // Index of hi byte of temp in service data
#define IDX_VOLTL 12               // Index of hi byte of temp in service data
#define IDX_VOLTH 13               // Index of hi byte of temp in service data

#define BT_GAP_ADV_VERY_SLOW_INT_MIN 0x3200 /* 8 s      */
#define BT_GAP_ADV_VERY_SLOW_INT_MAX 0x3840 /* 9 s      */


#define ADV_PARAM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD, \
                                  BT_GAP_ADV_VERY_SLOW_INT_MIN, \
                                  BT_GAP_ADV_VERY_SLOW_INT_MAX, NULL)


// #define ADV_PARAM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD, \
//                                   BT_GAP_ADV_VERY_SLOW_INT_MIN,                                                         \
//                                   BT_GAP_ADV_VERY_SLOW_INT_MAX, NULL)

static K_SEM_DEFINE(bt_init_ok, 1, 1);

uint8_t battery_level = 0;
uint16_t battery_voltage, temperature, humidity;

static uint8_t service_data[] = {
    BT_UUID_16_ENCODE(BTHOME_SERVICE_UUID),
    0x40,
    0x01, // Battery level
    0x32, // 50%
    0x02, // Temperature
    0x00, // Low byte
    0x00, // High byte
    0x03, // Humidity
    0xbf, // Low byte | 50.55%
    0x13, // High byte
    0x0C, // Voltage type
    0x00, // Low byte
    0x00, // High byte
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                  BT_UUID_16_ENCODE(BT_UUID_DIS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL))};

/* Declarations */

static ssize_t battery_level_charachteristic_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset);

BT_GATT_SERVICE_DEFINE(battery,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
                       BT_GATT_CHARACTERISTIC(
                           BT_UUID_BAS_BATTERY_LEVEL,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           battery_level_charachteristic_cb, NULL, NULL), );

/* Callbacks */

static ssize_t battery_level_charachteristic_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &battery_level, sizeof(battery_level));
}

void bt_ready(int err)
{
    if (err)
    {
        LOG_ERR("bt_enable returned %d", err);
    }
    k_sem_give(&bt_init_ok);
}

/* Custom functions */

int bluetooth_init()
{
    int err;

    LOG_INF("Initializing bluetooth");

    err = bt_enable(bt_ready);
    if (err)
    {
        LOG_ERR("bt_enable returned %d", err);
        return err;
    }
    k_sem_take(&bt_init_ok, K_FOREVER);
    LOG_INF("Bluetooth initialised");

    err = bt_le_adv_start(ADV_PARAM, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }
    LOG_INF("Advertising started");

    return err;
}

int bluetooth_update(int level, int mv, int temp, int hum)
{
    battery_level = (level / 100);
    battery_voltage = mv;
    temperature = temp;
    humidity = hum;

    service_data[IDX_BATT] = battery_level;
    service_data[IDX_TEMPH] = temperature >> 8;
    service_data[IDX_TEMPL] = temperature & 0xff;
    service_data[IDX_HUMDH] = humidity >> 8;
    service_data[IDX_HUMDL] = humidity & 0xff;
    service_data[IDX_VOLTH] = battery_voltage >> 8;
    service_data[IDX_VOLTL] = battery_voltage & 0xff;

    int err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Failed to update advertising data (err %d)", err);
        return err;
    }
    LOG_DBG("Updated advertising data");
    return 0;
}