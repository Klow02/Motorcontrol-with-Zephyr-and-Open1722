/*
 * avtp_link.h / avtp_link.c  -  Dünne Schicht um Open1722
 * ========================================================
 * Diese Dateien verpacken EINEN CAN-Frame (ID + 8 Bytes) in ein
 * IEEE-1722-Paket und packen ihn auf der Empfangsseite wieder aus.
 * Sie enthalten KEINE Netzwerk-Funktionen (Sockets) - nur das Paketformat.
 * Dadurch lassen sie sich auch auf dem PC testen (siehe tests/).
 *
 * Aufbau eines Pakets (alles "big endian", 32 Byte, wird als UDP gesendet):
 *
 *   Byte  0.. 3  UDP-Kapselung   : 32-Bit Paketzaehler (IEEE 1722 UDP-Encap.)
 *   Byte  4..15  NTSCF-Kopf      : Typ 0x82, Laenge, 8-Bit Zaehler, Stream-ID
 *                                  (NTSCF = "Non-Time-Synchronous Control Format")
 *   Byte 16..23  ACF-CAN-Brief   : Nachrichtentyp, Laenge, CAN-ID (29 Bit)
 *   Byte 24..31  Nutzdaten       : die 8 CAN-Datenbytes
 *
 * Alle Funktionen des Kopfes (Avtp_Ntscf_..., Avtp_CanBrief_...) kommen
 * aus der Open1722-Bibliothek; wir rufen sie nur in der richtigen
 * Reihenfolge auf.
 */
#ifndef AVTP_LINK_H
#define AVTP_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AVTP_PAKET_LAENGE 32   /* 4 (UDP-Encap.) + 12 (NTSCF) + 8 + 8 (ACF) */

/* Zustand eines Senders: wer bin ich (Stream-ID) und welcher Zaehler ist dran */
typedef struct {
    uint64_t stream_id;
    uint32_t zaehler;
} avtp_sender_t;

/* Zustand eines Empfaengers: von welchem Strom hoere ich, was erwarte ich */
typedef struct {
    uint64_t stream_id;       /* nur Pakete dieses Stroms werden angenommen */
    uint32_t erwarteter_zaehler;
    uint32_t luecken;         /* wie oft fehlte ein Paket (Diagnose)        */
    bool     erstes_paket;
} avtp_empfaenger_t;

void avtp_sender_init(avtp_sender_t *s, uint64_t stream_id);
void avtp_empfaenger_init(avtp_empfaenger_t *e, uint64_t stream_id);

/*
 * Baut ein Paket in `puffer` (mindestens AVTP_PAKET_LAENGE Byte gross).
 * Rueckgabe: Laenge des Pakets in Byte (= AVTP_PAKET_LAENGE).
 */
size_t avtp_paket_bauen(avtp_sender_t *s, uint32_t can_id,
                        const uint8_t daten[8], uint8_t *puffer);

/*
 * Liest ein empfangenes Paket. Rueckgabe true, wenn das Paket gueltig ist
 * UND zum erwarteten Strom gehoert; dann stehen CAN-ID und Daten bereit.
 * Alles andere (kaputt, falscher Strom, falscher Typ) wird verworfen (false).
 */
bool avtp_paket_lesen(avtp_empfaenger_t *e, const uint8_t *puffer, size_t laenge,
                      uint32_t *can_id, uint8_t daten[8]);

#endif /* AVTP_LINK_H */
