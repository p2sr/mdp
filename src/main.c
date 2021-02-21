#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "demo.h"

#define DEMO_DIR "demos"
#define ERR_FILE "errors.txt"
#define OUT_FILE "output.txt"
#define CMD_WHITELIST_FILE "cmd_whitelist.txt"
#define SAR_WHITELIST_FILE "sar_whitelist.txt"

FILE *g_errfile;
FILE *g_outfile;

static char **g_cmd_whitelist;
static char **g_sar_sum_whitelist;

static void _output_sar_data(uint32_t tick, struct sar_data data) {
	switch (data.type) {
	case SAR_DATA_TIMESCALE_CHEAT:
		fprintf(g_outfile, "\t\t[%5u] [SAR] timescale %.2f\n", tick, data.timescale);
		break;
	case SAR_DATA_INITIAL_CVAR:
		fprintf(g_outfile, "\t\t[%5u] [SAR] cvar '%s' = '%s'\n", tick, data.initial_cvar.cvar, data.initial_cvar.val);
		break;
	default:
		fprintf(g_outfile, "\t\t[%5u] [SAR] corrupt data!\n", tick);
		break;
	}
}

static void _output_msg(struct demo_msg *msg) {
	switch (msg->type) {
	case DEMO_MSG_CONSOLE_CMD:
		if (!config_check_cmd_whitelist(g_cmd_whitelist, msg->con_cmd)) {
			fprintf(g_outfile, "\t\t[%5u] %s\n", msg->tick, msg->con_cmd);
		}
		break;
	case DEMO_MSG_SAR_DATA:
		_output_sar_data(msg->tick, msg->sar_data);
		break;
	default:
		break;
	}
}

static void _validate_checksum(uint32_t demo_given, uint32_t sar_given, uint32_t demo_real) {
	bool demo_matches = demo_given == demo_real;
	if (demo_matches) {
		fprintf(g_outfile, "\tdemo checksum PASS (%X)\n", demo_real);
	} else {
		fprintf(g_outfile, "\tdemo checksum FAIL (%X; should be %X)\n", demo_given, demo_real);
	}

	if (config_check_sum_whitelist(g_sar_sum_whitelist, sar_given)) {
		fprintf(g_outfile, "\tSAR checksum PASS (%X)\n", sar_given);
	} else {
		fprintf(g_outfile, "\tSAR checksum FAIL (%X)\n", sar_given);
	}
}

void run_demo(const char *path) {
	struct demo *demo = demo_parse(path);

	if (!demo) {
		fputs("failed to parse demo!\n", g_errfile);
		return;
	}

	fprintf(g_outfile, "demo: '%s'\n", path);
	fprintf(g_outfile, "\t'%s' on %s - %.2f TPS\n", demo->hdr.client_name, demo->hdr.map_name, (float)demo->hdr.playback_ticks / demo->hdr.playback_time);
	fprintf(g_outfile, "\tevents:\n");
	for (size_t i = 0; i < demo->nmsgs; ++i) {
		struct demo_msg *msg = demo->msgs[i];
		if (i == demo->nmsgs - 1 && msg->type == DEMO_MSG_SAR_DATA && msg->sar_data.type == SAR_DATA_CHECKSUM) {
			// ending checksum data - validate it
			_validate_checksum(msg->sar_data.checksum.demo_sum, msg->sar_data.checksum.sar_sum, demo->checksum);
		} else {
			// normal message
			_output_msg(msg);
		}
	}

	demo_free(demo);
}

int main(void) {
	g_errfile = fopen(ERR_FILE, "w");
	g_outfile = fopen(OUT_FILE, "w");

	g_cmd_whitelist = config_read_newline_sep(CMD_WHITELIST_FILE);
	g_sar_sum_whitelist = config_read_newline_sep(SAR_WHITELIST_FILE);

	DIR *d = opendir(DEMO_DIR);
	if (d) {
		size_t demo_dir_len = strlen(DEMO_DIR);
		struct dirent *ent;
		bool is_first = true;
		while ((ent = readdir(d))) {
			if (!strcmp(ent->d_name, ".")) continue;
			if (!strcmp(ent->d_name, "..")) continue;

			if (!is_first) {
				fputs("\n", g_outfile);
			}

			is_first = false;

			char *path = malloc(demo_dir_len + strlen(ent->d_name) + 2);
			strcpy(path, DEMO_DIR);
			strcat(path, "/");
			strcat(path, ent->d_name);

			run_demo(path);

			free(path);
		}
	} else {
		fprintf(g_errfile, "failed to open demos folder '%s'\n", DEMO_DIR);
	}

	config_free_newline_sep(g_cmd_whitelist);
	config_free_newline_sep(g_sar_sum_whitelist);

	fclose(g_errfile);
	fclose(g_outfile);

	return 0;
}
