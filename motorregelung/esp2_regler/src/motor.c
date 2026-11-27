/*
 * motor.c  -  siehe motor.h
 */
#include "motor.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(motor, LOG_LEVEL_INF);

/* Bei Ansteuerung 0: 1 = bremsen (Motor kurzschliessen), 0 = frei auslaufen.
 * Bremsen ist fuer die Regelung besser (der Motor stoppt zuverlaessig). */
#define NULL_BREMST 1

#define BENUTZER DT_PATH(zephyr_user)
static const struct pwm_dt_spec pwm = PWM_DT_SPEC_GET(BENUTZER);
static const struct gpio_dt_spec pin_ina = GPIO_DT_SPEC_GET(BENUTZER, motor_ina_gpios);
static const struct gpio_dt_spec pin_inb = GPIO_DT_SPEC_GET(BENUTZER, motor_inb_gpios);

/* Optionaler Enable-Pin (beim Monster Moto Shield: "EN/DIAG"). Nur wenn im
 * Overlay "motor-en-gpios" angegeben ist. */
#if DT_NODE_HAS_PROP(BENUTZER, motor_en_gpios)
#define HAT_ENABLE 1
static const struct gpio_dt_spec pin_en = GPIO_DT_SPEC_GET(BENUTZER, motor_en_gpios);
#endif

int motor_start(void)
{
    if (!pwm_is_ready_dt(&pwm) || !gpio_is_ready_dt(&pin_ina) || !gpio_is_ready_dt(&pin_inb)) {
        LOG_ERR("Motor-Pins/PWM nicht bereit - Overlay pruefen");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&pin_ina, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&pin_inb, GPIO_OUTPUT_INACTIVE);
#ifdef HAT_ENABLE
    gpio_pin_configure_dt(&pin_en, GPIO_OUTPUT_ACTIVE);   /* Bruecke einschalten */
#endif
    motor_setzen(0);
    return 0;
}

void motor_setzen(int promille)
{
    if (promille > 1000) {
        promille = 1000;
    }
    if (promille < -1000) {
        promille = -1000;
    }

    if (promille == 0) {
        /* INA = INB = 0: Bruecke schliesst den Motor kurz (Bremse) */
        gpio_pin_set_dt(&pin_ina, 0);
        gpio_pin_set_dt(&pin_inb, 0);
        pwm_set_pulse_dt(&pwm, NULL_BREMST ? pwm.period : 0);
        return;
    }

    /* Richtung ueber INA/INB, Geschwindigkeit ueber die Pulsbreite */
    gpio_pin_set_dt(&pin_ina, promille > 0);
    gpio_pin_set_dt(&pin_inb, promille < 0);

    uint32_t betrag = (uint32_t)(promille > 0 ? promille : -promille);
    uint32_t puls = (uint32_t)(((uint64_t)pwm.period * betrag) / 1000U);

    pwm_set_pulse_dt(&pwm, puls);
}
