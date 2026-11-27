/* Simulation: einfacher DC-Motor mit Totzone und Traegheit, geregelt mit common/regelung.c.
 * Motor: Drehzahl folgt der Spannung mit 0.15 s Zeitkonstante, 1000 Promille = 3000 U/min,
 * unter 50 Promille bewegt er sich nicht (Haftreibung), bei Ausgang 0 wird gebremst. */
#include <stdio.h>
#include <math.h>
#include "regelung.h"
#include "motor_protocol.h"

static float rpm, winkel;
static void motor_schritt(float promille, float dt)
{
    float ziel = promille / 1000.0f * 3000.0f;
    if (fabsf(promille) < 50.0f) ziel = 0.0f;
    /* Ausgang exakt 0 = H-Bruecke bremst (beide Eingaenge Low) -> Motor stoppt schnell */
    float tau = (promille == 0.0f) ? 0.03f : 0.15f;
    rpm += (ziel - rpm) * dt / tau;
    winkel += rpm / 60.0f * 360.0f * dt;
}

static int pruefe(const char *name, float ist, float soll, float tol)
{
    int ok = fabsf(ist - soll) <= tol;
    printf("%-34s ist %9.2f  soll %9.2f  -> %s\n", name, ist, soll, ok ? "OK" : "FEHLER");
    return ok ? 0 : 1;
}

int main(void)
{
    const float dt = 0.01f;
    struct regelung r; int fehler = 0;
    regelung_init(&r);

    /* 1) Drehzahl 1500 U/min anfahren */
    for (int i = 0; i < 300; i++) { motor_schritt(regelung_schritt(&r, MODE_DREHZAHL, 1500, rpm, winkel, dt), dt); }
    fehler += pruefe("Drehzahl 1500 nach 3 s", rpm, 1500, 15);
    printf("   Rampe: aktiver Soll = %.1f\n", regelung_aktiver_soll(&r, 1500));

    /* 2) Rampe: nach 0.1 s darf der Soll nur ~200 U/min weit sein */
    regelung_init(&r); rpm = 0;
    for (int i = 0; i < 10; i++) motor_schritt(regelung_schritt(&r, MODE_DREHZAHL, 3000, rpm, winkel, dt), dt);
    fehler += pruefe("Rampe nach 0.1 s (2000 U/min/s)", regelung_aktiver_soll(&r, 3000), 200, 1);

    /* 3) Umkehr auf -1000 U/min */
    for (int i = 0; i < 400; i++) motor_schritt(regelung_schritt(&r, MODE_DREHZAHL, -1000, rpm, winkel, dt), dt);
    fehler += pruefe("Drehzahl -1000 (rueckwaerts)", rpm, -1000, 15);

    /* 4) Winkel 720 Grad anfahren */
    regelung_init(&r); rpm = 0; winkel = 0;
    for (int i = 0; i < 800; i++) motor_schritt(regelung_schritt(&r, MODE_WINKEL, 720, rpm, winkel, dt), dt);
    fehler += pruefe("Winkel 720 nach 8 s", winkel, 720, 1.5f);
    fehler += pruefe("  Rest-Drehzahl", rpm, 0, 0.5f);

    /* 5) Winkel zurueck auf -90 Grad */
    for (int i = 0; i < 1000; i++) motor_schritt(regelung_schritt(&r, MODE_WINKEL, -90, rpm, winkel, dt), dt);
    fehler += pruefe("Winkel -90 nach 10 s", winkel, -90, 1.5f);

    /* 6) Aus */
    float aus = regelung_schritt(&r, MODE_AUS, 0, rpm, winkel, dt);
    fehler += pruefe("MODE_AUS -> Ausgang", aus, 0, 0.001f);

    printf(fehler ? "\nTEST FEHLGESCHLAGEN\n" : "\nRegelungs-Test OK\n");
    return fehler;
}
