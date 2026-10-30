#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "esp_err.h"

esp_err_t lcd_display_start(void);
/* Redraw now; call after the parking state or RFID receipt changes. */
void lcd_display_notify(void);

#endif
