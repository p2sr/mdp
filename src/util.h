#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>

void util_strip_whitespace(char *str);
bool util_is_prefix(const char *prefix, const char *str);

#endif
