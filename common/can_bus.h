/*
 * can_bus.h / can_bus.c  -  Echter CAN-Bus (nur Zephyr)
 * =====================================================
 * Startet den CAN-Controller (TWAI des ESP32-C6) und sendet Frames.
 * Pins und Bitrate stehen im Overlay der jeweiligen App (boards/*.overlay).
 * Am Bus haengt ein CAN-Transceiver (z.B. SN65HVD230 oder TJA1051).
 */
#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdint.h>

/* Startet den CAN-Controller. Rueckgabe 0 = ok. */
int can_bus_start(void);

/* Sendet einen Frame mit 8 Datenbytes. Wartet nicht (wenn der Sendepuffer
 * voll ist, geht dieser eine Frame verloren). Rueckgabe 0 = ok. */
int can_bus_senden(uint32_t id, const uint8_t daten[8]);

#endif /* CAN_BUS_H */
