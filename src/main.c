#include <stdio.h>

#include "common.h"
#include "config.h"
#include "demo.h"

FILE *g_errfile;
FILE *g_outfile;

static char **g_cmd_whitelist;

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

	// TODO: sar checksum
}

void run_demo(const char *path) {
	struct demo *demo = demo_parse(path);

	if (!demo) {
		fputs("failed to parse demo!\n", g_errfile);
		return;
	}

	fprintf(g_outfile, "demo: '%s'\n", path);
	fprintf(g_outfile, "\theader:\n");
	fprintf(g_outfile, "\t\tserver: %s\n", demo->hdr.server_name);
	fprintf(g_outfile, "\t\tclient: %s\n", demo->hdr.client_name);
	fprintf(g_outfile, "\t\tmap: %s\n", demo->hdr.map_name);
	fprintf(g_outfile, "\t\tgame dir: %s\n", demo->hdr.game_directory);
	fprintf(g_outfile, "\t\tplayback time: %f\n", demo->hdr.playback_time);
	fprintf(g_outfile, "\t\tplayback ticks: %u\n", demo->hdr.playback_ticks);
	fprintf(g_outfile, "\t\tplayback frames: %u\n", demo->hdr.playback_frames);
	fprintf(g_outfile, "\t\tsign on length: %u\n", demo->hdr.sign_on_length);
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
	g_errfile = stderr;
	g_outfile = stdout;

	g_cmd_whitelist = config_read_cmd_whitelist("whitelist.txt");

	run_demo("test.dem");

	return 0;
}
