#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "util.h"

char **config_read_newline_sep(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(g_errfile, "%s: failed to open file\n", path);
		return NULL;
	}

	size_t lines_alloc = 32;
	size_t lines_count = 0;
	char **lines = malloc(lines_alloc * sizeof *lines);

	while (1) {
		char *line = malloc(256); // lines won't reasonably be longer than this

		if (!fgets(line, 256, f)) {
			free(line);
			break;
		}

		if (strlen(line) == 255) {
			fprintf(g_errfile, "%s: line %zu too long\n", path, lines_count + 1);
			free(line);
			for (size_t i = 0; i < lines_count; ++i) {
				free(lines[i]);
			}
			free(lines);
			return NULL;
		}

		util_strip_whitespace(line);

		// ignore blank lines
		if (strlen(line) == 0) {
			free(line);
			continue;
		}

		lines[lines_count++] = line;

		if (lines_count == lines_alloc) {
			lines_alloc *= 2;
			lines = realloc(lines, lines_alloc * sizeof *lines);
		}
	}

	lines[lines_count] = NULL;

	fclose(f);

	return lines;
}

bool config_check_cmd_whitelist(char **cmds, const char *cmd) {
	if (!cmds) return false;
	while (*cmds) {
		if (util_is_prefix(*cmds, cmd)) return true;
		++cmds;
	}
	return false;
}

bool config_check_sum_whitelist(char **sums, uint32_t sum) {
	if (!sums) return false;
	while (*sums) {
		char *end;
		uint32_t valid = strtol(*sums, &end, 16);
		if (end == *sums || *end != 0) continue;
		if (valid == sum) return true;
	}
	return false;
}

void config_free_newline_sep(char **lines) {
	if (!lines) return;
	char **ptr = lines;
	while (*ptr) {
		free(*ptr);
		++ptr;
	}
	free(lines);
}
