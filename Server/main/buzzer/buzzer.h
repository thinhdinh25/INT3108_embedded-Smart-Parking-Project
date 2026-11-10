#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include "esp_err.h"

/* Initialize the active-low buzzer output and start with the buzzer off. */
esp_err_t buzzer_start(void);

/* Sound the buzzer while fire or flood is reported. */
void buzzer_set_alarm(bool alarm);

/* A second reason to sound, set separately so neither one silences the other. */
void buzzer_set_warning(bool warning);

#endif
