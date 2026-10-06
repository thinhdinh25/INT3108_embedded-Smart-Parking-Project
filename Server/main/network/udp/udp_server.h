#ifndef UDP_SERVER_H
#define UDP_SERVER_H

#include "esp_err.h"

#define UDP_SERVER_PORT 3333

/** Start the UDP request listener task. */
esp_err_t udp_server_start(void);

#endif // UDP_SERVER_H
