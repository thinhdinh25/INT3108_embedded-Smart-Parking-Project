#include <stdlib.h>
#include <string.h>

#include "json_scan.h"

void json_skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
        ++*p;
}

bool json_consume_char(const char **p, char c)
{
    json_skip_ws(p);
    if (**p != c)
        return false;
    ++*p;
    return true;
}

bool json_parse_string(const char **p, char *out, size_t out_size)
{
    json_skip_ws(p);
    if (**p != '"')
        return false;
    const char *s = *p + 1;

    size_t n = 0;
    while (*s && *s != '"')
    {
        unsigned char c = (unsigned char)*s;
        if (c == '\\' || c < 0x20 || n + 1 >= out_size)
            return false;
        out[n++] = (char)c;
        ++s;
    }
    if (*s != '"' || n == 0)
        return false;
    out[n] = '\0';
    *p = s + 1;
    return true;
}

bool json_parse_number(const char **p, float *value)
{
    json_skip_ws(p);
    if ((**p < '0' || **p > '9') && **p != '-')
        return false;
    char *end = NULL;
    *value = strtof(*p, &end);
    if (end == *p)
        return false;
    *p = end;
    return true;
}

bool json_parse_bool(const char **p, bool *value)
{
    json_skip_ws(p);
    if (strncmp(*p, "true", 4) == 0)
    {
        *value = true;
        *p += 4;
        return true;
    }
    if (strncmp(*p, "false", 5) == 0)
    {
        *value = false;
        *p += 5;
        return true;
    }
    return false;
}

bool json_parse_uint32(const char **p, uint32_t *value)
{
    json_skip_ws(p);
    const char *s = *p;
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (uint64_t)(*s++ - '0');
        if (v > UINT32_MAX)
            return false;
    }
    if (s == *p || *s == '.' || *s == 'e' || *s == 'E')
        return false;
    *value = (uint32_t)v;
    *p = s;
    return true;
}

bool json_parse_object_end(const char **p)
{
    if (!json_consume_char(p, '}'))
        return false;
    json_skip_ws(p);
    return **p == '\0';
}
