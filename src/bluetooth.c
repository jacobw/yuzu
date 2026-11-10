#include <errno.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/app_version.h>
#include "bluetooth.h"

LOG_MODULE_REGISTER(yuzu_bluetooth, CONFIG_LOG_DEFAULT_LEVEL);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define BTHOME_SERVICE_UUID 0xfcd2 // BTHome service UUID
#define IDX_BATT 4                 // Index of battery level in service data
#define IDX_TEMPL 6                // Index of lo byte of temp in service data
#define IDX_TEMPH 7                // Index of hi byte of temp in service data
#define IDX_HUMDL 9                // Index of lo byte of humidity in service data
#define IDX_HUMDH 10               // Index of hi byte of humidity in service data

// 8.02125 s, not 8 s: at 8 s each broadcast lands about 17 ms later in a 102.4 ms Wi-Fi
// beacon period, so scanners that share their radio with Wi-Fi miss runs of them.
#define BT_GAP_ADV_VERY_SLOW_INT_MIN 0x3222
#define BT_GAP_ADV_VERY_SLOW_INT_MAX 0x3840 // 9s

#define ADV_PARAM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY, \
                                  BT_GAP_ADV_VERY_SLOW_INT_MIN, \
                                  BT_GAP_ADV_VERY_SLOW_INT_MAX, NULL)

static uint8_t battery_level;
static uint16_t humidity;
static bool advertising;
static int16_t temperature;

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
    /* Firmware version, patch first. In place of the voltage object: the
     * advertising data is exactly 31 bytes with it, legacy advertising's limit. */
    0xF2,
    APP_PATCHLEVEL,
    APP_VERSION_MINOR,
    APP_VERSION_MAJOR,
};

// define buffer for device name that we can populate on init
static char name[10];

/* BLE Advertising data structure */
static const struct bt_data ad[] = {
    /* Flags: General Discoverable Mode, BR/EDR not supported */
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* BTHome service data with sensor readings */
    BT_DATA(BT_DATA_SVC_DATA16, service_data, ARRAY_SIZE(service_data)),
    /* Device name with MAC address suffix */
    BT_DATA(BT_DATA_NAME_COMPLETE, name, sizeof(name) - 1)
};

/* Each AD structure costs its data plus a length and a type byte. Legacy
 * advertising refuses more than 31 bytes, and the sensor would go silent. */
BUILD_ASSERT(3 + (2 + sizeof(service_data)) + (2 + sizeof(name) - 1) <= BT_GAP_ADV_MAX_ADV_DATA_LEN,
             "advertising data too long for legacy advertising");


/* Declarations */

static ssize_t battery_level_characteristic_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset);

BT_GATT_SERVICE_DEFINE(battery,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
                       BT_GATT_CHARACTERISTIC(
                           BT_UUID_BAS_BATTERY_LEVEL,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           battery_level_characteristic_cb, NULL, NULL), );

/* Callbacks */

static ssize_t battery_level_characteristic_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &battery_level, sizeof(battery_level));
}

/* BT_LE_ADV_OPT_CONN stops advertising when a connection is made, so it is
 * restarted here. Started from the recycled callback, once the connection
 * object is free again, and retried until it succeeds. */
static void adv_restart_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(ADV_PARAM, ad, ARRAY_SIZE(ad), NULL, 0);

    if (err == 0 || err == -EALREADY) {
        LOG_INF("Advertising restarted");
        return;
    }
    LOG_ERR("Failed to restart advertising (err %d), retrying", err);
    k_work_reschedule(k_work_delayable_from_work(work), K_SECONDS(1));
}

static K_WORK_DELAYABLE_DEFINE(adv_restart_work, adv_restart_work_handler);

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x)", reason);
}

static void recycled(void)
{
    k_work_reschedule(&adv_restart_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .disconnected = disconnected,
    .recycled = recycled,
};

/* Custom functions */

// Add last 6 chars of mac address to name so devices can be identified
// Use ATC formac so it gets stripped correctly by Home Assistant BTHome integration
// https://github.com/Bluetooth-Devices/bthome-ble/blob/main/src/bthome_ble/parser.py#L263
int name_add_mac(char *str)
{
	bt_addr_le_t addr = {0};
	size_t count = 1;

    bt_id_get(&addr, &count);
    return sprintf(str, "%.2s %02X%02X%02X", DEVICE_NAME, addr.a.val[2], addr.a.val[1], addr.a.val[0]);
}

int bluetooth_init(void)
{
    int err;

    LOG_INF("Initializing bluetooth");

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable returned %d", err);
        return err;
    }
    LOG_INF("Bluetooth initialised");

    // Add mac to name
    name_add_mac(name);
    LOG_INF("Name: %s", name);

    /* Advertising starts with the first reading (bluetooth_update), so the
     * placeholder values above are never broadcast. */
    return 0;
}

int bluetooth_update(uint16_t level, int16_t temp, uint16_t hum)
{
    battery_level = (level / 100);
    temperature = temp;
    humidity = hum;

    service_data[IDX_BATT] = battery_level;
    service_data[IDX_TEMPH] = (uint16_t)temperature >> 8;
    service_data[IDX_TEMPL] = (uint16_t)temperature & 0xff;
    service_data[IDX_HUMDH] = humidity >> 8;
    service_data[IDX_HUMDL] = humidity & 0xff;

    if (!advertising) {
        int err = bt_le_adv_start(ADV_PARAM, ad, ARRAY_SIZE(ad), NULL, 0);
        if (err) {
            LOG_ERR("Advertising failed to start (err %d)", err);
            return err;
        }
        advertising = true;
        LOG_INF("Advertising started");
        return 0;
    }

    int err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
    if (err == -EAGAIN) {
        /* A client is connected; advertising is paused. Data will be
         * applied when advertising resumes after disconnection. */
        LOG_DBG("Advertising paused (connected), data queued");
        return 0;
    } else if (err) {
        LOG_ERR("Failed to update advertising data (err %d)", err);
        return err;
    }
    LOG_DBG("Updated advertising data");
    return 0;
}