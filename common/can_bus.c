/*
 * can_bus.c  -  siehe can_bus.h
 */
#include "can_bus.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(can_bus, LOG_LEVEL_INF);

/* Der CAN-Controller, den das Board als "zephyr,canbus" ausgewaehlt hat
 * (beim ESP32-C6 ist das twai0). */
static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

int can_bus_start(void)
{
    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN-Controller nicht bereit");
        return -ENODEV;
    }
    int res = can_start(can_dev);

    if (res != 0 && res != -EALREADY) {
        LOG_ERR("CAN-Start fehlgeschlagen: %d", res);
        return res;
    }
    LOG_INF("CAN gestartet");
    return 0;
}

int can_bus_senden(uint32_t id, const uint8_t daten[8])
{
    struct can_frame frame = {0};

    frame.id = id;          /* 11-Bit Standard-ID */
    frame.dlc = 8;          /* 8 Datenbytes */
    memcpy(frame.data, daten, 8);
    return can_send(can_dev, &frame, K_NO_WAIT, NULL, NULL);
}
