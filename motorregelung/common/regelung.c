/*
 * regelung.c  -  siehe regelung.h
 */
#include "regelung.h"

#include <math.h>

#include "motor_protocol.h"

static float begrenzen(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

void regelung_init(struct regelung *r)
{
    r->pid_drehzahl = (struct pid){ .kp = DREHZAHL_KP, .ki = DREHZAHL_KI, .kd = 0.0f,
                                    .aus_min = -1000.0f, .aus_max = 1000.0f,
                                    .i_max = DREHZAHL_I_MAX };
    r->pid_winkel   = (struct pid){ .kp = WINKEL_KP, .ki = WINKEL_KI, .kd = WINKEL_KD,
                                    .aus_min = -WINKEL_MAX_PROMILLE,
                                    .aus_max = WINKEL_MAX_PROMILLE,
                                    .i_max = WINKEL_I_MAX };
    pid_reset(&r->pid_drehzahl);
    pid_reset(&r->pid_winkel);
    r->betriebsart = MODE_AUS;
    r->soll_rampe_rpm = 0.0f;
}

float regelung_schritt(struct regelung *r, int betriebsart, float soll,
                       float ist_rpm, float ist_winkel, float dt_s)
{
    /* Bei Wechsel der Betriebsart: Regler "frisch" starten, damit keine
     * alten Werte einen Ruck verursachen. Die Rampe startet bei der
     * aktuellen Drehzahl (nicht bei 0). */
    if (betriebsart != r->betriebsart) {
        r->betriebsart = betriebsart;
        pid_reset(&r->pid_drehzahl);
        pid_reset(&r->pid_winkel);
        r->soll_rampe_rpm = ist_rpm;
    }

    if (betriebsart == MODE_DREHZAHL) {
        /* 1) Rampe: Sollwert nur in kleinen Schritten dem Ziel naehern */
        float max_schritt = RAMPE_RPM_PRO_S * dt_s;
        float diff = soll - r->soll_rampe_rpm;
        r->soll_rampe_rpm += begrenzen(diff, -max_schritt, max_schritt);

        /* 2) Vorsteuerung: so viel PWM braucht der Motor ungefaehr */
        float vorsteuerung = r->soll_rampe_rpm / MOTOR_MAX_RPM * 1000.0f;

        /* 3) PI-Regler korrigiert den Rest (Last, Spannung, Reibung) */
        float korrektur = pid_rechnen(&r->pid_drehzahl, r->soll_rampe_rpm, ist_rpm, dt_s);

        return begrenzen(vorsteuerung + korrektur, -1000.0f, 1000.0f);
    }

    if (betriebsart == MODE_WINKEL) {
        float fehler = soll - ist_winkel;
        float aus = pid_rechnen(&r->pid_winkel, soll, ist_winkel, dt_s);

        /* Ziel (fast) erreicht: Motor aus und I-Anteil leeren, sonst
         * "brummt" der Motor staendig um die Zielposition. */
        if (fabsf(fehler) < WINKEL_TOLERANZ_GRAD) {
            r->pid_winkel.integral = 0.0f;
            return 0.0f;
        }
        /* Sehr kleine Ansteuerung wuerde den Motor nicht bewegen
         * (Haftreibung) -> auf Mindestwert anheben, Richtung beibehalten. */
        if (fabsf(aus) < MIN_PROMILLE) {
            aus = (aus < 0.0f) ? -MIN_PROMILLE : MIN_PROMILLE;
        }
        return aus;
    }

    /* MODE_AUS (oder unbekannt): Motor aus */
    r->soll_rampe_rpm = ist_rpm;
    return 0.0f;
}

float regelung_aktiver_soll(const struct regelung *r, float soll)
{
    return (r->betriebsart == MODE_DREHZAHL) ? r->soll_rampe_rpm :
           (r->betriebsart == MODE_WINKEL)   ? soll : 0.0f;
}
