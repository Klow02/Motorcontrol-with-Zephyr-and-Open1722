/*
 * ESP2  -  Regler-Knoten
 * ======================
 * Aufgabe: Aus Sollwert (vom Pi) und Messwert (von ESP1) die Motor-
 * Ansteuerung berechnen und die H-Bruecke steuern.
 *
 *   Pi  --SOLLWERT (1722/WLAN)--> [ESP2] --PWM + Richtung--> H-Bruecke --> Motor
 *   ESP1 --MESSWERT (1722/WLAN)-> [ESP2] --REGLER (1722/WLAN)--> Pi
 *                                        --REGLER (CAN)--------> Bus
 *
 * Ablauf:
 *   - Ein eigener Thread (empfang_thread) wartet auf UDP-Pakete und
 *     merkt sich den neuesten SOLLWERT und MESSWERT samt Zeitstempel.
 *   - Die Hauptschleife rechnet alle 10 ms den Regler (common/regelung.c),
 *     setzt den Motor und sendet den REGLER-Status an Pi und CAN.
 *
 * Sicherheit: Fehlt der Messwert laenger als TIMEOUT_MESSWERT_MS oder der
 * Sollwert laenger als TIMEOUT_SOLLWERT_MS, wird der Motor gebremst.
 * (Ohne Messwert wuerde der Regler sonst "blind" Vollgas geben.)
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "avtp_link.h"
#include "can_bus.h"
#include "motor.h"
#include "motor_protocol.h"
#include "netzwerk.h"
#include "projekt_config.h"
#include "regelung.h"

LOG_MODULE_REGISTER(esp2, LOG_LEVEL_INF);

/* ------------------------------------------------------------------------
 * Gemeinsame Daten zwischen Empfangs-Thread und Hauptschleife.
 * Das Mutex sorgt dafuer, dass man nie halb geschriebene Werte liest.
 * ------------------------------------------------------------------------ */
static K_MUTEX_DEFINE(daten_mutex);
static int     soll_betriebsart = MODE_AUS;
static int32_t soll_centi;            /* Sollwert in U/min*100 oder Grad*100   */
static int64_t soll_zeit_ms;          /* wann kam der letzte Sollwert an       */
static int32_t ist_rpm_centi;         /* Ist-Drehzahl von ESP1                 */
static int32_t ist_winkel_centi;      /* Ist-Winkel von ESP1                   */
static int64_t ist_zeit_ms;           /* wann kam der letzte Messwert an       */

/* Empfangs-Thread: horcht auf SOLLWERT (Port 17221) und MESSWERT (Port 17222) */
#define EMPFANG_STACK 4096
K_THREAD_STACK_DEFINE(empfang_stack, EMPFANG_STACK);
static struct k_thread empfang_thread_daten;

static void empfang_thread(void *a, void *b, void *c)
{
    int sock_soll = udp_empfang_oeffnen(PORT_SOLLWERT);
    int sock_ist = udp_empfang_oeffnen(PORT_MESSWERT);

    if (sock_soll < 0 || sock_ist < 0) {
        LOG_ERR("UDP-Empfang konnte nicht geoeffnet werden (%d / %d)", sock_soll, sock_ist);
        return;
    }

    /* Pro Strom ein Empfaenger: er prueft die Stream-ID und zaehlt Luecken */
    avtp_empfaenger_t rx_soll, rx_ist;

    avtp_empfaenger_init(&rx_soll, STREAM_ID_SOLLWERT);
    avtp_empfaenger_init(&rx_ist, STREAM_ID_MESSWERT);
    LOG_INF("Empfang bereit auf Port %d (Sollwert) und %d (Messwert)", PORT_SOLLWERT, PORT_MESSWERT);

    struct zsock_pollfd fds[2] = {
        { .fd = sock_soll, .events = ZSOCK_POLLIN },
        { .fd = sock_ist,  .events = ZSOCK_POLLIN },
    };
    uint8_t puffer[128];

    for (;;) {
        /* Warten, bis auf einem der beiden Sockets etwas ankommt */
        if (zsock_poll(fds, 2, 1000) <= 0) {
            continue;
        }

        for (int i = 0; i < 2; i++) {
            if (!(fds[i].revents & ZSOCK_POLLIN)) {
                continue;
            }
            int n = (int)zsock_recv(fds[i].fd, puffer, sizeof(puffer), 0);
            uint32_t id;
            uint8_t daten[8];

            if (n <= 0) {
                continue;
            }

            k_mutex_lock(&daten_mutex, K_FOREVER);
            if (i == 0 && avtp_paket_lesen(&rx_soll, puffer, n, &id, daten) &&
                id == MSG_ID_SOLLWERT) {
                soll_betriebsart = daten[0];
                soll_centi = get_i32(&daten[1]);
                soll_zeit_ms = k_uptime_get();
            } else if (i == 1 && avtp_paket_lesen(&rx_ist, puffer, n, &id, daten) &&
                       id == MSG_ID_MESSWERT) {
                ist_rpm_centi = get_i32(&daten[0]);
                ist_winkel_centi = get_i32(&daten[4]);
                ist_zeit_ms = k_uptime_get();
            }
            k_mutex_unlock(&daten_mutex);
        }
    }
}

int main(void)
{
    LOG_INF("ESP2 Regler-Knoten startet");

    if (motor_start() != 0) {
        return -1;                   /* Motor-Pins nicht bereit -> nichts tun */
    }
    can_bus_start();
    netzwerk_start(IP_ESP2);         /* wartet, bis WLAN verbunden ist */

    /* Empfang in eigenem Thread starten */
    k_thread_create(&empfang_thread_daten, empfang_stack, EMPFANG_STACK, empfang_thread,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    /* Sender fuer den REGLER-Status an den Pi */
    int sock = udp_sender_oeffnen();
    struct sockaddr_in ziel_pi;
    avtp_sender_t tx;

    udp_ziel(&ziel_pi, IP_PI, PORT_REGLER);
    avtp_sender_init(&tx, STREAM_ID_REGLER);

    struct regelung regler;

    regelung_init(&regler);

    int64_t naechster_takt = k_uptime_get();
    int64_t letzte_zeit = naechster_takt;
    int teiler = 0;                  /* Status nur jede PI_TEILER-te Runde an den Pi */
    int ausgabe = 0;

    for (;;) {
        /* Festes 10-ms-Zeitraster */
        naechster_takt += REGEL_PERIODE_MS;
        k_sleep(K_TIMEOUT_ABS_MS(naechster_takt));

        int64_t jetzt = k_uptime_get();
        float dt_s = (float)(jetzt - letzte_zeit) / 1000.0f;

        letzte_zeit = jetzt;

        /* 1) Aktuelle Werte holen (Kopie unter Mutex) */
        k_mutex_lock(&daten_mutex, K_FOREVER);
        int     betriebsart = soll_betriebsart;
        int32_t soll = soll_centi;
        int32_t rpm = ist_rpm_centi;
        int32_t winkel = ist_winkel_centi;
        bool    soll_alt = (jetzt - soll_zeit_ms) > TIMEOUT_SOLLWERT_MS;
        bool    ist_alt = (jetzt - ist_zeit_ms) > TIMEOUT_MESSWERT_MS;

        k_mutex_unlock(&daten_mutex);

        /* 2) Sicherheit: fehlende Daten -> Motor aus */
        bool sicherheit = soll_alt || ist_alt;

        if (sicherheit) {
            betriebsart = MODE_AUS;
        }

        /* 3) Regler rechnen (intern in U/min und Grad, deshalb /100) */
        float promille = regelung_schritt(&regler, betriebsart, (float)soll / 100.0f,
                                          (float)rpm / 100.0f, (float)winkel / 100.0f, dt_s);
        int16_t ausgang = (int16_t)(promille >= 0 ? promille + 0.5f : promille - 0.5f);

        /* 4) Motor ansteuern */
        motor_setzen(ausgang);

        /* 5) REGLER-Status senden: CAN (jede Runde), Pi (jede PI_TEILER-te) */
        float aktiver_soll = regelung_aktiver_soll(&regler, (float)soll / 100.0f);
        int32_t aktiver_soll_centi = (int32_t)(aktiver_soll * 100.0f + (aktiver_soll >= 0 ? 0.5f : -0.5f));
        uint8_t daten[8] = {0};

        daten[0] = (uint8_t)betriebsart;
        put_i32(&daten[1], aktiver_soll_centi);
        put_i16(&daten[5], ausgang);
        daten[7] = sicherheit ? STATUS_SICHERHEIT_AKTIV : 0;

        can_bus_senden(MSG_ID_REGLER, daten);

        if (++teiler >= PI_TEILER) {
            teiler = 0;
            uint8_t paket[AVTP_PAKET_LAENGE];
            size_t laenge = avtp_paket_bauen(&tx, MSG_ID_REGLER, daten, paket);

            udp_senden(sock, &ziel_pi, paket, laenge);
        }

        if (++ausgabe >= 1000 / REGEL_PERIODE_MS) {
            ausgabe = 0;
            LOG_INF("Modus %d, Soll %d, Ist %d U/min, Ausgang %d Promille%s%s%s", betriebsart,
                (int)(aktiver_soll_centi / 100), (int)(rpm / 100), ausgang,
                sicherheit ? "  [SICHERHEIT]" : "",
                soll_alt ? "  (Sollwert vom Pi fehlt)" : "",     /* kommt nichts vom Pi an? */
                ist_alt ? "  (Messwert von ESP1 fehlt)" : "");   /* kommt nichts von ESP1 an? */
        }
    }
    return 0;
}
