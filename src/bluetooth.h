#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

int bluetooth_init();
int bluetooth_update(int level, int mv, int temp, int hum);
