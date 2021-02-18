#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "util.h"

char **config_read_cmd_whitelist(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(g_errfile, "%s: failed to open command whitelist file\n", path);
		return NULL;
	}

	size_t cmds_alloc = 32;
	size_t cmds_count = 0;
	char **cmds = malloc(cmds_alloc * sizeof *cmds);

	while (1) {
		char *cmd = malloc(256); // commands won't reasonably be longer than this

		if (!fgets(cmd, 256, f)) {
			break;
		}

		if (strlen(cmd) == 255) {
			fprintf(g_errfile, "%s: whitelisted command %zu too long\n", path, cmds_count + 1);
			free(cmd);
			for (size_t i = 0; i < cmds_count; ++i) {
				free(cmds[i]);
			}
			free(cmds);
			return NULL;
		}

		util_strip_whitespace(cmd);

		// ignore blank lines
		if (strlen(cmd) == 0) {
			free(cmd);
			continue;
		}

		cmds[cmds_count++] = cmd;

		if (cmds_count == cmds_alloc) {
			cmds_alloc *= 2;
			cmds = realloc(cmds, cmds_alloc * sizeof *cmds);
		}
	}

	cmds[cmds_count] = NULL;

	fclose(f);

	return cmds;
}

bool config_check_cmd_whitelist(char **cmds, const char *cmd) {
	if (!cmds) return false;
	while (*cmds) {
		if (util_is_prefix(*cmds, cmd)) return true;
		++cmds;
	}
	return false;
}

void config_free_cmd_whitelist(char **cmds) {
	char **ptr = cmds;
	while (*ptr) {
		free(ptr);
		++ptr;
	}
	free(cmds);
}
