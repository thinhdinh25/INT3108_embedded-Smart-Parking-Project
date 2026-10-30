#ifndef JSON_SCAN_H
#define JSON_SCAN_H

#include <stdbool.h>
#include <stddef.h>

/* Minimal, allocation-free helpers for the fixed UDP message schemas.
 * Each function advances *p only past what it accepted. */
void json_skip_ws(const char **p);
bool json_consume_char(const char **p, char c);
/* Non-empty string without escapes or control characters; fails if it does not fit. */
bool json_parse_string(const char **p, char *out, size_t out_size);
/* Number starting with a digit or '-'. Range checks are left to the caller. */
bool json_parse_number(const char **p, float *value);
bool json_parse_bool(const char **p, bool *value);
/* Closing '}' followed only by whitespace. */
bool json_parse_object_end(const char **p);

#endif
