/*
 * motor.h / motor.c  -  H-Bruecke ansteuern (ESP2)
 * =================================================
 * Funktioniert mit dem SparkFun Monster Moto Shield (VNH2SP30) UND mit
 * einer Standard-H-Bruecke mit "PWM + 2 Richtungspins" (z.B. L298N-Modul:
 * ENA = PWM, IN1 = INA, IN2 = INB). Beide werden gleich angesteuert:
 *
 *   INA INB   Wirkung
 *    1   0    Motor dreht vorwaerts, Geschwindigkeit = PWM
 *    0   1    Motor dreht rueckwaerts, Geschwindigkeit = PWM
 *    0   0    Motor wird kurzgeschlossen = BREMSE
 *
 * Die Pins stehen im Overlay (boards/*.overlay) im Knoten "zephyr,user".
 */
#ifndef MOTOR_H
#define MOTOR_H

/* Initialisiert PWM und Richtungspins (Motor startet gebremst). 0 = ok. */
int motor_start(void);

/* Setzt die Motor-Ansteuerung in Promille: -1000 (voll rueckwaerts) bis
 * +1000 (voll vorwaerts). 0 = bremsen. */
void motor_setzen(int promille);

#endif /* MOTOR_H */
