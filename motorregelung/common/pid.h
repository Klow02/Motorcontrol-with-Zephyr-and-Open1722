/*
 * pid.h / pid.c  -  Ein einfacher PID-Regler
 * ===========================================
 * Rechnet aus "Soll" und "Ist" eine Stellgroesse (hier: Motor-Ansteuerung).
 *
 *   Fehler   = Soll - Ist
 *   P-Anteil = kp * Fehler                  (je groesser der Fehler, desto mehr Gas)
 *   I-Anteil = ki * Summe(Fehler * dt)      (raeumt bleibende Abweichung weg)
 *   D-Anteil = -kd * d(Ist)/dt              (daempft, verhindert Ueberschwingen)
 *
 * Der Regler ist reines C ohne Zephyr-Funktionen und laeuft deshalb auch auf
 * dem PC (siehe tests/test_pid.c).
 */
#ifndef PID_H
#define PID_H

#include <stdbool.h>

struct pid {
    /* --- Einstellungen (vom Benutzer gesetzt) --- */
    float kp, ki, kd;
    float aus_min, aus_max;    /* Ausgang wird auf diesen Bereich begrenzt    */
    float i_max;               /* Begrenzung des I-Anteils (Anti-Windup)      */

    /* --- interner Zustand --- */
    float integral;            /* aufsummierter I-Anteil (schon mit ki mal)   */
    float letzter_ist;
    bool  hat_letzten_ist;
};

/* Setzt den inneren Zustand zurueck (z.B. beim Wechsel der Betriebsart). */
void pid_reset(struct pid *p);

/* Ein Rechenschritt. dt_s = Zeit seit dem letzten Aufruf in Sekunden. */
float pid_rechnen(struct pid *p, float soll, float ist, float dt_s);

#endif /* PID_H */
