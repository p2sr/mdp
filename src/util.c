#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "util.h"

void util_strip_whitespace(char *str) {
	size_t i = 0;
	while (isspace(str[i])) ++i;

	size_t len = strlen(str + i);
	while (isspace(str[i + len - 1]) && len > 0) --len;

	memmove(str, str + i, len);
	str[len] = 0;
}

bool util_is_prefix(const char *prefix, const char *str) {
	while (*prefix) {
		if (*prefix != *str) return false;
		++prefix, ++str;
	}
	return true;
}
