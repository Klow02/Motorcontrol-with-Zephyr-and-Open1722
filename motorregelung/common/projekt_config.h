/*
 * projekt_config.h  -  ALLE Einstellungen des Projekts an EINER Stelle
 * ====================================================================
 * Diese Datei wird von ESP1 (Encoder) und ESP2 (Regler) eingebunden.
 * Der Pi (dashboard.py) hat oben im Skript dieselben Zahlen - wenn du hier
 * etwas aenderst (IP, Port, Stream-ID), aendere es dort bitte auch.
 */
#ifndef PROJEKT_CONFIG_H
#define PROJEKT_CONFIG_H

/* ---------------------------------------------------------------------
 * 1) WLAN und IP-Adressen   <-- HIER DEINE WERTE EINTRAGEN
 * ---------------------------------------------------------------------
 * Der Raspberry Pi spielt selbst den WLAN-Access-Point (Router). Die ESPs
 * verbinden sich mit DIESEM Netz. SSID und Passwort muessen exakt zu dem
 * passen, was pi_dashboard/setup_access_point.sh auf dem Pi einrichtet
 * (Passwort: mindestens 8 Zeichen, sonst lehnt der Pi es ab).
 * Alle Teilnehmer haben feste IP-Adressen, damit sie sich ohne Suche finden.
 * Der ESP32-C6 kann nur 2,4 GHz WLAN.                                    */
#define WIFI_SSID       "MotorRegelung"
#define WIFI_PASSWORT   "motor12345"

#define IP_ESP1         "192.168.4.51"   /* Encoder-Knoten               */
#define IP_ESP2         "192.168.4.52"   /* Regler-Knoten                */
#define IP_PI           "192.168.4.1"    /* Raspberry Pi (= Access Point) */
#define IP_NETMASK      "255.255.255.0"
#define IP_GATEWAY      "192.168.4.1"    /* der Pi                       */

/* ---------------------------------------------------------------------
 * 2) IEEE 1722 (Open1722): Streams und UDP-Ports
 * ---------------------------------------------------------------------
 * In IEEE 1722 sendet jeder "Talker" einen eigenen Datenstrom mit einer
 * 64-Bit Stream-ID. Wir haben drei Strome, jeder bekommt einen UDP-Port:
 *
 *   Strom        Sender -> Empfaenger        Port
 *   SOLLWERT     Pi     -> ESP2              17221
 *   MESSWERT     ESP1   -> ESP2 und Pi       17222
 *   REGLER       ESP2   -> Pi                17223
 */
#define PORT_SOLLWERT   17221
#define PORT_MESSWERT   17222
#define PORT_REGLER     17223

#define STREAM_ID_SOLLWERT  0xAABBCCDD01000001ULL
#define STREAM_ID_MESSWERT  0xAABBCCDD02000001ULL
#define STREAM_ID_REGLER    0xAABBCCDD03000001ULL

/* ---------------------------------------------------------------------
 * 3) CAN-Bus (ESP1 und ESP2 senden je eine Nachricht auf den Bus)
 * --------------------------------------------------------------------- */
#define CAN_BITRATE     500000          /* 500 kbit/s                     */

/* ---------------------------------------------------------------------
 * 4) Encoder (nur ESP1)
 * ---------------------------------------------------------------------
 * ENCODER_PULSES_PER_REV: Impulse pro Umdrehung an Encoderwelle
 *   und pro Kanal. Steht im Datenblatt deines Motors, z.B.
 *   "11 PPR" am Motor x 30:1 Getriebe = 330 PPR.
 * Wir werten beide Flanken beider Kanaele aus ("4-fach"), dadurch ist
 * die Aufloesung  ENCODER_COUNTS_PER_REV = 4 x PULSES_PER_REV.
 * ENCODER_RICHTUNG: auf -1 setzen, wenn "vorwaerts" negativ zaehlt.       */
#define ENCODER_PULSES_PER_REV  147
#define ENCODER_COUNTS_PER_REV  (4 * ENCODER_PULSES_PER_REV)
#define ENCODER_RICHTUNG        1

/* 4b) Getriebe (nur ESP1 rechnet damit)
 * GETRIEBE_VORHANDEN    0 = kein Getriebe (Motorwelle = Ausgang), 1 = Getriebe
 * GETRIEBE_UEBERSETZUNG Motorumdrehungen pro Umdrehung des Ausgangs (30.0 = "30:1")
 * ENCODER_AM_MOTOR      1 = Encoder auf der Motorwelle (VOR dem Getriebe)
 *                       0 = Encoder am Getriebeausgang (NACH dem Getriebe)
 * ENCODER_PULSES_PER_REV (oben) gilt fuer die Welle, an der der Encoder sitzt. */
#define GETRIEBE_VORHANDEN     1        /* <-- anpassen */
#define GETRIEBE_UEBERSETZUNG  48.0f    /* <-- anpassen */
#define ENCODER_AM_MOTOR       1        /* <-- anpassen */

/* Ab hier nichts aendern: Umrechnungsfaktoren */
#if GETRIEBE_VORHANDEN
#define UEBERSETZUNG    GETRIEBE_UEBERSETZUNG
#else
#define UEBERSETZUNG    1.0f
#endif
#if ENCODER_AM_MOTOR
#define FAKTOR_MOTOR    1.0f                    /* Encoder-Welle -> Motorwelle    */
#define FAKTOR_AUSGANG  (1.0f / UEBERSETZUNG)   /* Encoder-Welle -> Ausgangswelle */
#else
#define FAKTOR_MOTOR    UEBERSETZUNG
#define FAKTOR_AUSGANG  1.0f
#endif

/* ---------------------------------------------------------------------
 * 5) Zeitverhalten
 * --------------------------------------------------------------------- */
#define MESS_PERIODE_MS     5   /* ESP1 misst + sendet alle 10 ms (100 Hz) */
#define REGEL_PERIODE_MS    5   /* ESP2 regelt alle 10 ms (100 Hz)         */
#define PI_TEILER           5    /* ESP1 schickt nur jede 5. Messung an den
                                    Pi (= 20 Hz): reicht fuer die Anzeige   */
#define TIMEOUT_MESSWERT_MS 200  /* ESP2: keine Messung -> Motor aus        */
#define TIMEOUT_SOLLWERT_MS 1000 /* ESP2: kein Sollwert vom Pi -> Motor aus */

#endif /* PROJEKT_CONFIG_H */
