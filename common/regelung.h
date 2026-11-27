/*
 * regelung.h / regelung.c  -  Die eigentliche Motorregelung (ESP2)
 * =================================================================
 * Hier steckt die ganze Logik "Soll + Ist -> Motor-Ansteuerung":
 *   - Betriebsart DREHZAHL: Rampe + Vorsteuerung + PI-Regler
 *   - Betriebsart WINKEL  : PID-Regler mit Toleranzfenster und
 *                           Mindest-Ansteuerung gegen Haftreibung
 * Reines C ohne Zephyr -> auf dem PC testbar (tests/test_pid.c).
 *
 * Einheiten in dieser Datei: Drehzahl in U/min, Winkel in Grad,
 * Ausgang in Promille (-1000 .. +1000 = -100 % .. +100 % PWM).
 */
#ifndef REGELUNG_H
#define REGELUNG_H

#include "pid.h"

/* ---- Einstellwerte (an deinen Motor anpassen!) ------------------------- */

/* Drehzahl des Motors bei 100 % PWM ohne Last (Datenblatt, am Abtrieb).
 * Damit rechnet die Vorsteuerung eine Start-Ansteuerung aus; der Regler
 * muss dann nur noch den Rest korrigieren.                                 */
#define MOTOR_MAX_RPM            100.0f

/* Wie schnell sich der Drehzahl-Sollwert aendern darf (Rampe in U/min je
 * Sekunde). Verhindert Stromspitzen und Ruckeln bei Sollwert-Spruengen.    */
#define RAMPE_RPM_PRO_S          42.0f

/* PI-Regler Drehzahl (Ausgang in Promille). Erst kp erhoehen, dann ki.     */
#define DREHZAHL_KP              12.0f
#define DREHZAHL_KI              57.6f
#define DREHZAHL_I_MAX           400.0f

/* PID-Regler Winkel (Fehler in Grad -> Ausgang in Promille).              */
#define WINKEL_KP                288.0f
#define WINKEL_KI                96.0f
#define WINKEL_KD                19.2f
#define WINKEL_I_MAX             150.0f
#define WINKEL_MAX_PROMILLE      800.0f  /* im Winkelmodus nie schneller    */
#define WINKEL_TOLERANZ_GRAD     0.15f    /* innerhalb: Ziel erreicht, Motor aus */
#define MIN_PROMILLE             60.0f   /* darunter bewegt sich der Motor
                                            wegen Haftreibung nicht -> bei
                                            Bedarf mindestens so viel geben  */

struct regelung {
    struct pid pid_drehzahl;
    struct pid pid_winkel;
    int   betriebsart;        /* MODE_AUS / MODE_DREHZAHL / MODE_WINKEL     */
    float soll_rampe_rpm;     /* Drehzahl-Sollwert nach der Rampe           */
};

void regelung_init(struct regelung *r);

/*
 * Ein Regelschritt. Rueckgabe: Motor-Ansteuerung in Promille.
 *   betriebsart : MODE_AUS / MODE_DREHZAHL / MODE_WINKEL
 *   soll        : U/min (Drehzahlmodus) oder Grad (Winkelmodus)
 *   ist_rpm, ist_winkel : Messwerte von ESP1
 *   dt_s        : Zeit seit dem letzten Aufruf in Sekunden
 */
float regelung_schritt(struct regelung *r, int betriebsart, float soll,
                       float ist_rpm, float ist_winkel, float dt_s);

/* Der Sollwert, dem der Regler gerade wirklich folgt (bei Drehzahl: nach
 * der Rampe, bei Winkel: der eingegebene Winkel). Wird auf CAN ausgegeben. */
float regelung_aktiver_soll(const struct regelung *r, float soll);

#endif /* REGELUNG_H */
