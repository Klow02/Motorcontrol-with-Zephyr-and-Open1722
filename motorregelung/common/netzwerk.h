/*
 * netzwerk.h / netzwerk.c  -  WLAN verbinden und UDP-Sockets (nur Zephyr)
 * ========================================================================
 * Drei kleine Hilfen, die ESP1 und ESP2 gemeinsam nutzen:
 *   1) netzwerk_start()       WLAN verbinden und feste IP setzen
 *   2) udp_empfang_oeffnen()  Socket zum Empfangen auf einem Port
 *   3) udp_ziel() + udp_senden()  Pakete an eine IP/Port-Adresse schicken
 */
#ifndef NETZWERK_H
#define NETZWERK_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/net/socket.h>

/* Verbindet mit dem WLAN aus projekt_config.h und setzt die feste IP-Adresse
 * `meine_ip`. Blockiert, bis das WLAN verbunden ist (wiederholt es bei
 * Fehlern). Rueckgabe 0 = ok. */
int netzwerk_start(const char *meine_ip);

/* Oeffnet einen UDP-Socket, der auf `port` hoert. Rueckgabe: Socket (>= 0) oder < 0. */
int udp_empfang_oeffnen(uint16_t port);

/* Oeffnet einen UDP-Socket zum Senden. Rueckgabe: Socket (>= 0) oder < 0. */
int udp_sender_oeffnen(void);

/* Fuellt eine Zieladresse aus IP-Text und Port. Rueckgabe 0 = ok. */
int udp_ziel(struct sockaddr_in *ziel, const char *ip, uint16_t port);

/* Schickt ein Paket. Rueckgabe: gesendete Bytes oder < 0 bei Fehler. */
int udp_senden(int sock, const struct sockaddr_in *ziel, const uint8_t *daten, size_t laenge);

#endif /* NETZWERK_H */
