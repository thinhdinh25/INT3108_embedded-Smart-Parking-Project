#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include "esp_err.h"

/* Initialize the active-low buzzer output and start with the buzzer off. */
esp_err_t buzzer_start(void);

/* Set the buzzer state from the latest fire sensor reading. */
void buzzer_set_fire(bool fire_detected);

#endif
