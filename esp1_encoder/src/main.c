/*
 * ESP1  -  Encoder-Knoten
 * =======================
 * Aufgabe: Den Hall-Encoder des Motors lesen und Drehzahl + Winkel melden.
 *
 *   Hall-Encoder (A/B) --> [ESP1] --1722/WLAN--> ESP2 (jede Messung, 100 Hz)
 *                                 --1722/WLAN--> Pi   (jede 5. Messung, 20 Hz)
 *                                 --CAN-Bus----> Bus  (jede Messung, 100 Hz)
 *
 * So funktioniert es:
 *   1) Jede Flanke von A oder B loest einen Interrupt aus. Aus dem Zustand
 *      vor und nach der Flanke erkennt die Funktion enc_isr(), ob sich der
 *      Motor um einen Schritt vorwaerts oder rueckwaerts gedreht hat, und
 *      zaehlt einen Zaehler hoch bzw. runter ("Quadratur-Decodierung").
 *   2) Alle 10 ms liest die Hauptschleife den Zaehler und rechnet
 *        Winkel  = Zaehler * 360 / Schritte_pro_Umdrehung
 *        Drehzahl = Zaehleraenderung im letzten Zeitfenster -> U/min
 *   3) Das Ergebnis wird als MESSWERT-Nachricht verschickt (Format:
 *      common/motor_protocol.h).
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <stdlib.h>

#include "avtp_link.h"
#include "can_bus.h"
#include "motor_protocol.h"
#include "netzwerk.h"
#include "projekt_config.h"

LOG_MODULE_REGISTER(esp1, LOG_LEVEL_INF);

/* Wie viele Messungen die Drehzahl-Berechnung zurueckschaut (5 x 10 ms = 50 ms).
 * Groesser = ruhigerer Wert aber traeger; kleiner = schneller aber "zittriger". */
#define RPM_FENSTER 5

/* ------------------------------------------------------------------------
 * Encoder-Pins: stehen im Overlay (boards/*.overlay) im Knoten "zephyr,user"
 * ------------------------------------------------------------------------ */
#define BENUTZER DT_PATH(zephyr_user)
static const struct gpio_dt_spec enc_a = GPIO_DT_SPEC_GET(BENUTZER, encoder_a_gpios);
static const struct gpio_dt_spec enc_b = GPIO_DT_SPEC_GET(BENUTZER, encoder_b_gpios);

static struct gpio_callback enc_callback;
static atomic_t enc_zaehler;    /* aktueller Zaehlerstand (Schritte)        */
static uint8_t  enc_zustand;    /* letzter Zustand der Pins: Bit1=A, Bit0=B */

/*
 * Tabelle der Quadratur-Schritte. Index = (alter_Zustand << 2) | neuer_Zustand,
 * Zustand = (A << 1) | B.
 * Vorwaerts laeuft die Folge 00 -> 01 -> 11 -> 10 -> 00 (jeweils +1),
 * rueckwaerts in umgekehrter Reihenfolge (jeweils -1).
 * Eintraege mit 0: keine Aenderung oder ungueltiger Sprung (z.B. Stoerimpuls).
 */
static const int8_t SCHRITT[16] = {
    /* alt 00 */  0, +1, -1,  0,
    /* alt 01 */ -1,  0,  0, +1,
    /* alt 10 */ +1,  0,  0, -1,
    /* alt 11 */  0, -1, +1,  0,
};

/* Interrupt: wird bei JEDER Flanke von A oder B aufgerufen. Muss kurz sein! */
static void enc_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    uint8_t neu = (uint8_t)((gpio_pin_get_dt(&enc_a) ? 2 : 0) | (gpio_pin_get_dt(&enc_b) ? 1 : 0));
    int8_t schritt = SCHRITT[(enc_zustand << 2) | neu];

    enc_zustand = neu;
    if (schritt != 0) {
        atomic_add(&enc_zaehler, ENCODER_RICHTUNG * schritt);
    }
}

static int encoder_start(void)
{
    if (!gpio_is_ready_dt(&enc_a) || !gpio_is_ready_dt(&enc_b)) {
        return -ENODEV;
    }
    /* Beide Pins als Eingang (Pull-Up kommt aus dem Overlay) */
    gpio_pin_configure_dt(&enc_a, GPIO_INPUT);
    gpio_pin_configure_dt(&enc_b, GPIO_INPUT);

    /* Startzustand merken, dann Interrupts bei beiden Flanken einschalten */
    enc_zustand = (uint8_t)((gpio_pin_get_dt(&enc_a) ? 2 : 0) | (gpio_pin_get_dt(&enc_b) ? 1 : 0));
    gpio_init_callback(&enc_callback, enc_isr, BIT(enc_a.pin) | BIT(enc_b.pin));
    gpio_add_callback(enc_a.port, &enc_callback);   /* A und B liegen am selben Port */
    gpio_pin_interrupt_configure_dt(&enc_a, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure_dt(&enc_b, GPIO_INT_EDGE_BOTH);
    return 0;
}

/* Float -> int32 mit Runden (ohne Mathe-Bibliothek) */
static int32_t runden(float x)
{
    return (int32_t)(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

int main(void)
{
    LOG_INF("ESP1 Encoder-Knoten startet");

    if (encoder_start() != 0) {
        LOG_ERR("Encoder-Pins nicht bereit - Overlay pruefen");
        return -1;
    }
    can_bus_start();                 /* CAN laeuft auch, falls es spaeter ausfaellt nur Log */
    netzwerk_start(IP_ESP1);         /* wartet, bis WLAN verbunden ist */

    /* Zieladressen und Sender fuer die beiden WLAN-Empfaenger.
     * Jeder Empfaenger bekommt seinen eigenen Paketzaehler, damit er Verluste
     * richtig erkennt (der Pi bekommt ja nur jedes 5. Paket). */
    int sock = udp_sender_oeffnen();
    struct sockaddr_in ziel_esp2, ziel_pi;

    udp_ziel(&ziel_esp2, IP_ESP2, PORT_MESSWERT);
    udp_ziel(&ziel_pi, IP_PI, PORT_MESSWERT);
    avtp_sender_t tx_esp2, tx_pi;

    avtp_sender_init(&tx_esp2, STREAM_ID_MESSWERT);
    avtp_sender_init(&tx_pi, STREAM_ID_MESSWERT);

    /* Verlauf der letzten Zaehlerstaende fuer die Drehzahl-Berechnung */
    int32_t hist_zaehler[RPM_FENSTER];
    int64_t hist_zeit_ms[RPM_FENSTER];
    int32_t start = (int32_t)atomic_get(&enc_zaehler);
    int64_t jetzt = k_uptime_get();

    for (int i = 0; i < RPM_FENSTER; i++) {
        hist_zaehler[i] = start;
        hist_zeit_ms[i] = jetzt;
    }

    int64_t naechster_takt = jetzt;
    int teiler = 0;          /* zaehlt bis PI_TEILER */
    int ausgabe = 0;         /* fuer die Log-Ausgabe 1x pro Sekunde */
    int slot = 0;            /* aktuelle Position im Verlaufsspeicher */

    for (;;) {
        /* Auf den naechsten 10-ms-Takt warten (feste Zeitraster, kein Driften) */
        naechster_takt += MESS_PERIODE_MS;
        k_sleep(K_TIMEOUT_ABS_MS(naechster_takt));

        /* 1) Zaehlerstand lesen */
        int32_t zaehler = (int32_t)atomic_get(&enc_zaehler);
        int64_t zeit_ms = k_uptime_get();

        /* 2) Winkel in Grad: Zaehler * 360 / Schritte pro Umdrehung */
        float winkel = (float)zaehler * 360.0f / (float)ENCODER_COUNTS_PER_REV;

        /* 3) Drehzahl in U/min aus der Aenderung gegenueber dem aeltesten
         *    Eintrag im Verlauf (= RPM_FENSTER Messungen zurueck) */
        int32_t d_zaehler = zaehler - hist_zaehler[slot];
        int64_t d_zeit_ms = zeit_ms - hist_zeit_ms[slot];
        float drehzahl = 0.0f;

        if (d_zeit_ms > 0) {
            /* Umdrehungen = d_zaehler / Schritte_pro_U;  U/min = Umdrehungen / Zeit_in_min */
            drehzahl = (float)d_zaehler * 60000.0f /
                       ((float)ENCODER_COUNTS_PER_REV * (float)d_zeit_ms);
        }
        hist_zaehler[slot] = zaehler;       /* neuen Stand an die Stelle des aeltesten */
        hist_zeit_ms[slot] = zeit_ms;
        slot = (slot + 1) % RPM_FENSTER;

            /* 3b) Auf die richtigen Wellen umrechnen: 'drehzahl'/'winkel' gelten fuer
         *    die Welle mit dem Encoder, die FAKTOR_-Werte rechnen sie um. */
        float drehzahl_motor = drehzahl * FAKTOR_MOTOR;       /* vor dem Getriebe */
        float drehzahl_ausgang = drehzahl * FAKTOR_AUSGANG;   /* nach dem Getriebe */
        float winkel_ausgang = winkel * FAKTOR_AUSGANG;

        /* 4) MESSWERT: Drehzahl und Winkel der AUSGANGSWELLE (darauf regelt ESP2) */
        uint8_t daten[8];
        int32_t drehzahl_centi = runden(drehzahl_ausgang * 100.0f);
        int32_t winkel_centi = runden(winkel_ausgang * 100.0f);

        put_i32(&daten[0], drehzahl_centi);
        put_i32(&daten[4], winkel_centi);

        /* 4b) MOTORDREHZAHL (nur Anzeige): Motorwelle + Uebersetzung x1000 */
        uint8_t daten_motor[8];
        put_i32(&daten_motor[0], runden(drehzahl_motor * 100.0f));
        put_i32(&daten_motor[4], runden(UEBERSETZUNG * 1000.0f));

        /* 5) Senden: CAN-Bus, ESP2 (immer), Pi (jedes PI_TEILER-te Mal) */
        can_bus_senden(MSG_ID_MESSWERT, daten);
        can_bus_senden(MSG_ID_MOTORDREHZAHL, daten_motor);

        uint8_t paket[AVTP_PAKET_LAENGE];
        size_t laenge = avtp_paket_bauen(&tx_esp2, MSG_ID_MESSWERT, daten, paket);

        udp_senden(sock, &ziel_esp2, paket, laenge);

        if (++teiler >= PI_TEILER) {
            teiler = 0;
            laenge = avtp_paket_bauen(&tx_pi, MSG_ID_MESSWERT, daten, paket);
            udp_senden(sock, &ziel_pi, paket, laenge);
            /* direkt danach die MOTORDREHZAHL (gleicher Strom, gleicher Zaehler) */
            laenge = avtp_paket_bauen(&tx_pi, MSG_ID_MOTORDREHZAHL, daten_motor, paket);
            udp_senden(sock, &ziel_pi, paket, laenge);
        }

        /* 6) Einmal pro Sekunde eine Zeile ins Log */
        if (++ausgabe >= 1000 / MESS_PERIODE_MS) {
            ausgabe = 0;
            LOG_INF("Ausgang: %d.%02d U/min, Winkel %d.%02d Grad  |  Motor: %d U/min",
                    (int)(drehzahl_centi / 100), (int)(abs(drehzahl_centi) % 100),
                    (int)(winkel_centi / 100), (int)(abs(winkel_centi) % 100),
                    (int)runden(drehzahl_motor));
        }
    }
    return 0;
}
