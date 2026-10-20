#ifndef PARKING_UPDATE_H
#define PARKING_UPDATE_H

#include <stdbool.h>

#include "parking_state.h"

bool parking_update_parse_json(const char *payload, parking_state_t *state);

#endif
