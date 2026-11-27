/*
 * netzwerk.c  -  siehe netzwerk.h
 */
#include "netzwerk.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "projekt_config.h"

LOG_MODULE_REGISTER(netzwerk, LOG_LEVEL_INF);

/* Wird vom WLAN-Ereignis "Verbindungsergebnis" freigegeben */
static K_SEM_DEFINE(wlan_ergebnis, 0, 1);
static int wlan_status;
static struct net_mgmt_event_callback wifi_callback;

/* Das WLAN-System meldet hier alles, was passiert. Wir schreiben es ins Log,
 * damit man bei Verbindungsproblemen sieht, WARUM es nicht klappt. */
static void wifi_ereignis(struct net_mgmt_event_callback *cb, uint64_t ereignis,
                          struct net_if *iface)
{
    if (ereignis == NET_EVENT_WIFI_SCAN_RESULT) {
        /* Ein gefundenes WLAN in der Umgebung */
        const struct wifi_scan_result *e = cb->info;

        LOG_INF("  gefunden: '%s'  Kanal %u  Signal %d dBm  Sicherheit %s", e->ssid, e->channel,
                e->rssi, wifi_security_txt(e->security));
    } else if (ereignis == NET_EVENT_WIFI_SCAN_DONE) {
        LOG_INF("  Suche beendet");
    } else if (ereignis == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status *status = cb->info;

        wlan_status = status->status;   /* 0 = verbunden */
        LOG_INF("Verbindungsergebnis: %d (0 = ok)", wlan_status);
        k_sem_give(&wlan_ergebnis);
    } else if (ereignis == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        const struct wifi_status *status = cb->info;

        LOG_WRN("Verbindung getrennt, Grund %d", status->disconn_reason);
    }
}

int netzwerk_start(const char *meine_ip)
{
    struct net_if *iface = net_if_get_default();

    net_mgmt_init_event_callback(&wifi_callback, wifi_ereignis,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |
                                 NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
    net_mgmt_add_event_callback(&wifi_callback);

    /* Diagnose: Welche WLANs sieht der ESP? Dort muss unser SSID auftauchen. */
    struct wifi_scan_params suche = {0};

    LOG_INF("Suche WLANs ...");
    if (net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &suche, sizeof(suche)) != 0) {
        LOG_WRN("WLAN-Suche konnte nicht gestartet werden");
    }
    k_sleep(K_SECONDS(5));

    /* Zugangsdaten aus projekt_config.h */
    struct wifi_connect_req_params wlan = {0};

    wlan.ssid = (const uint8_t *)WIFI_SSID;
    wlan.ssid_length = sizeof(WIFI_SSID) - 1;
    wlan.psk = (const uint8_t *)WIFI_PASSWORT;
    wlan.psk_length = sizeof(WIFI_PASSWORT) - 1;
    wlan.security = WIFI_SECURITY_TYPE_PSK;
    wlan.band = WIFI_FREQ_BAND_2_4_GHZ;
    wlan.channel = WIFI_CHANNEL_ANY;
    wlan.mfp = WIFI_MFP_OPTIONAL;

    /* Verbinden - bei Fehlschlag nach 3 s erneut versuchen */
    for (;;) {
        LOG_INF("Verbinde mit WLAN '%s' ...", WIFI_SSID);
        k_sem_reset(&wlan_ergebnis);
        int res = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wlan, sizeof(wlan));

        if (res == 0 && k_sem_take(&wlan_ergebnis, K_SECONDS(20)) == 0 && wlan_status == 0) {
            break;
        }
        LOG_WRN("WLAN nicht verbunden (Fehler %d / Status %d), neuer Versuch", res, wlan_status);
        k_sleep(K_SECONDS(3));
    }
    LOG_INF("WLAN verbunden");

     /* WLAN-Stromsparmodus ausschalten: Im Sparmodus schlaeft der Funk zwischen
     * den Beacons des Access Points, Pakete kommen dann verzoegert und in
     * Schueben an. Fuer eine Regelung brauchen wir gleichmaessige Pakete. */
    struct wifi_ps_params ps = {.enabled = WIFI_PS_DISABLED};

    if (net_mgmt(NET_REQUEST_WIFI_PS, iface, &ps, sizeof(ps)) != 0) {
        LOG_WRN("Stromsparmodus konnte nicht ausgeschaltet werden");
    } else {
        LOG_INF("WLAN-Stromsparmodus aus");
    }

    /* Feste IP-Adresse, Netzmaske und Gateway setzen */
    LOG_INF("Setze IP-Adresse %s ...", meine_ip);   /* Diagnose: hier sieht man, ob es hier haengt */
    struct in_addr adresse, maske, gateway;

    if (net_addr_pton(AF_INET, meine_ip, &adresse) || net_addr_pton(AF_INET, IP_NETMASK, &maske) ||
        net_addr_pton(AF_INET, IP_GATEWAY, &gateway)) {
        LOG_ERR("Ungueltige IP-Adresse in projekt_config.h");
        return -EINVAL;
    }
    if (net_if_ipv4_addr_add(iface, &adresse, NET_ADDR_MANUAL, 0) == NULL) {
        LOG_ERR("IP-Adresse konnte nicht gesetzt werden");
        return -EIO;
    }
    net_if_ipv4_set_netmask_by_addr(iface, &adresse, &maske);
    net_if_ipv4_set_gw(iface, &gateway);
    LOG_INF("IP-Adresse: %s", meine_ip);
    return 0;
}

int udp_empfang_oeffnen(uint16_t port)
{
    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (sock < 0) {
        return -errno;
    }

    struct sockaddr_in adresse = {0};

    adresse.sin_family = AF_INET;
    adresse.sin_port = htons(port);
    adresse.sin_addr.s_addr = htonl(INADDR_ANY);   /* auf allen Adressen hoeren */

    if (zsock_bind(sock, (struct sockaddr *)&adresse, sizeof(adresse)) < 0) {
        int fehler = -errno;

        zsock_close(sock);
        return fehler;
    }
    return sock;
}

int udp_sender_oeffnen(void)
{
    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    return sock < 0 ? -errno : sock;
}

int udp_ziel(struct sockaddr_in *ziel, const char *ip, uint16_t port)
{
    memset(ziel, 0, sizeof(*ziel));
    ziel->sin_family = AF_INET;
    ziel->sin_port = htons(port);
    return zsock_inet_pton(AF_INET, ip, &ziel->sin_addr) == 1 ? 0 : -EINVAL;
}

int udp_senden(int sock, const struct sockaddr_in *ziel, const uint8_t *daten, size_t laenge)
{
    /* ZSOCK_MSG_DONTWAIT = nie warten. Sind die Netz-Puffer gerade voll, geht nur
     * dieses eine Paket verloren (kommt 5-10 ms spaeter wieder), aber die
     * Regelschleife bleibt nicht haengen und blockiert nicht die Netzwerk-Threads. */
    return (int)zsock_sendto(sock, daten, laenge, ZSOCK_MSG_DONTWAIT, (const struct sockaddr *)ziel,
                             sizeof(*ziel));
}