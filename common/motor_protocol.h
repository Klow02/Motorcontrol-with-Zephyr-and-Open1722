/*
 * motor_protocol.h  -  Aufbau der drei Nachrichten
 * =================================================
 * Alle Nachrichten sind "CAN-Frames": eine ID und 8 Datenbytes.
 *
 * Der Trick: Open1722 kann CAN-Frames direkt in IEEE-1722-Pakete verpacken
 * (ACF-CAN). Deshalb benutzen wir fuer WLAN (1722) und fuer den echten
 * CAN-Bus DASSELBE Format - ESP1 und ESP2 muessen eine Nachricht nur
 * einmal zusammenbauen und koennen sie auf beiden Wegen senden.
 *
 * Zahlen: mehrbyte-Werte werden "little endian" abgelegt (niedrigstes
 * Byte zuerst). "centi" bedeutet: Wert x 100, also 1234 = 12,34.
 *   - Drehzahl in 1/100 U/min   (z.B. 150000 = 1500,00 U/min)
 *   - Winkel   in 1/100 Grad    (z.B. 36000  = 360,00 Grad = 1 Umdrehung)
 * Der Winkel zaehlt ueber mehrere Umdrehungen hinweg weiter (36000, 72000..)
 * und ist 0 beim Einschalten von ESP1.
 */
#ifndef MOTOR_PROTOCOL_H
#define MOTOR_PROTOCOL_H

#include <stdint.h>

/* ---- Nachrichten-IDs ------------------------------------------------ */
#define MSG_ID_SOLLWERT  0x100  /* Pi   -> ESP2  (nur ueber WLAN/1722)      */
#define MSG_ID_MESSWERT  0x101  /* ESP1 -> ESP2, Pi (WLAN) und CAN-Bus      */
#define MSG_ID_REGLER    0x102  /* ESP2 -> Pi (WLAN) und CAN-Bus            */
#define MSG_ID_MOTORDREHZAHL 0x103 /* ESP1 -> Pi (WLAN) und CAN-Bus: Motordrehzahl + Uebersetzung */

/* ---- Betriebsarten --------------------------------------------------- */
#define MODE_AUS        0       /* Motor stromlos / Bremse                  */
#define MODE_DREHZAHL   1       /* Sollwert ist eine Drehzahl (U/min)       */
#define MODE_WINKEL     2       /* Sollwert ist ein Winkel (Grad)           */

/*
 * SOLLWERT (0x100), Pi -> ESP2
 *   Byte 0     : Betriebsart (MODE_...)
 *   Byte 1..4  : Sollwert  int32 centi  (U/min*100 oder Grad*100)
 *   Byte 5..7  : unbenutzt (0)
 *
 * MESSWERT (0x101), ESP1 -> ESP2 / Pi / CAN
 *   Byte 0..3  : Ist-Drehzahl int32 centi (U/min*100, negativ = rueckwaerts)
 *   Byte 4..7  : Ist-Winkel   int32 centi (Grad*100)
 *
 * REGLER (0x102), ESP2 -> Pi / CAN
 *   Byte 0     : aktive Betriebsart
 *   Byte 1..4  : "geregelter Sollwert" int32 centi: der Sollwert, dem der
 *                Regler gerade wirklich folgt (bei Drehzahl: nach der
 *                Rampe, siehe esp2 main.c)
 *   Byte 5..6  : Motor-Ansteuerung int16 in Promille (-1000..+1000 = +-100 %)
 *   Byte 7     : Status-Bits: Bit0 = 1 -> Sicherheitsabschaltung aktiv
 *                (Messwert oder Sollwert fehlt)
 */
#define STATUS_SICHERHEIT_AKTIV  0x01

/* ---- Hilfsfunktionen zum Schreiben/Lesen der Bytes -------------------- */
static inline void put_i32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u);
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}

static inline int32_t get_i32(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (int32_t)u;
}

static inline void put_i16(uint8_t *p, int16_t v)
{
    uint16_t u = (uint16_t)v;
    p[0] = (uint8_t)(u);
    p[1] = (uint8_t)(u >> 8);
}

static inline int16_t get_i16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

#endif /* MOTOR_PROTOCOL_H */
