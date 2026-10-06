#ifndef UDP_SERVER_H
#define UDP_SERVER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define UDP_SERVER_PORT 3333

/** Start the UDP request listener task. */
esp_err_t udp_server_start(void);

/** Copy the latest parking state. A slot value of 1 means occupied. */
bool udp_server_get_parking_state(int *slots, size_t capacity, int *free_count);

#endif // UDP_SERVER_H
