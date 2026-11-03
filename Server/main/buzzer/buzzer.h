#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include "esp_err.h"

/* Initialize the active-low buzzer output and start with the buzzer off. */
esp_err_t buzzer_start(void);

/* Sound the buzzer while fire or flood is reported. */
void buzzer_set_alarm(bool alarm);

#endif
