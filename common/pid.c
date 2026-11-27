/*
 * pid.c  -  siehe pid.h
 */
#include "pid.h"

static float begrenzen(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

void pid_reset(struct pid *p)
{
    p->integral = 0.0f;
    p->letzter_ist = 0.0f;
    p->hat_letzten_ist = false;
}

float pid_rechnen(struct pid *p, float soll, float ist, float dt_s)
{
    float fehler = soll - ist;

    /* P-Anteil */
    float p_anteil = p->kp * fehler;

    /* I-Anteil: aufsummieren, aber begrenzen ("Anti-Windup").
     * Ohne Begrenzung wuerde der Wert bei blockiertem Motor immer weiter
     * wachsen und nach dem Loslassen lange ueberschwingen. */
    p->integral += p->ki * fehler * dt_s;
    p->integral = begrenzen(p->integral, -p->i_max, p->i_max);

    /* D-Anteil auf den Ist-Wert (nicht auf den Fehler): vermeidet einen
     * grossen Ruck, wenn sich der Sollwert sprunghaft aendert. */
    float d_anteil = 0.0f;
    if (p->hat_letzten_ist && dt_s > 0.0f) {
        d_anteil = -p->kd * (ist - p->letzter_ist) / dt_s;
    }
    p->letzter_ist = ist;
    p->hat_letzten_ist = true;

    return begrenzen(p_anteil + p->integral + d_anteil, p->aus_min, p->aus_max);
}
