#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "config.h"
#include "demo.h"

#define EXPECTED_MAPS_FILE "expected_maps.txt"
#define CMD_WHITELIST_FILE "cmd_whitelist.txt"
#define SAR_WHITELIST_FILE "sar_whitelist.txt"
#define CVAR_WHITELIST_FILE "cvar_whitelist.txt"
#define FILESUM_WHITELIST_FILE "filesum_whitelist.txt"
#define GENERAL_CONF_FILE "config.txt"

static char **g_cmd_whitelist;
static char **g_sar_sum_whitelist;
static struct var_whitelist *g_filesum_whitelist;
static struct var_whitelist *g_cvar_whitelist;

// general config options
struct {
	int file_sum_mode; // 0 = don't show, 1 = show not matching, 2 (default) = show not matching or not present
	int initial_cvar_mode; // 0 = don't show, 1 = show not matching, 2 (default) = show not matching or not present
	bool show_passing_checksums; // should we output successful checksums?
	bool show_wait; // should we show when 'wait' was run?
} g_config;

static bool _allow_initial_cvar(const char *var, const char *val) {
#define ALLOW(x, y) if (!strcmp(var, #x) && !strcmp(val, y)) return true
#define ALLOWINT(x, y) if (!strcmp(var, #x) && atoi(val) == y) return true
#define ALLOWRANGE(x, y, z) if (!strcmp(var, #x) && atoi(val) >= y && atoi(val) <= z) return true

	ALLOWINT(host_timescale, 1);
	ALLOWINT(sv_alternateticks, 1);
	ALLOWINT(sv_allow_mobile_portals, 0);
	ALLOWINT(sv_portal_placement_debug, 0);
	ALLOWINT(cl_cmdrate, 30);
	ALLOWINT(cl_updaterate, 20);
	ALLOWRANGE(cl_fov, 45, 140);
	ALLOWRANGE(fps_max, 30, 999);
	ALLOW(sv_use_trace_duration, "0.5");
	ALLOW(m_yaw, "0.022");

	return false;

#undef ALLOW
#undef ALLOWINT
#undef ALLOWRANGE
}

static bool _ignore_filesum(const char *path) {
	// hack to deal with the fact that older sar versions included way
	// more filesums than now
	size_t len = strlen(path);
	const char *end = path + len;
	if (len > 3 && !strcasecmp(end - 3, ".so")) return true;
	if (len > 4 && !strcasecmp(end - 4, ".dll")) return true;
	if (len > 4 && !strcasecmp(end - 4, ".bsp")) return true;
	if (len > 4 && !strcasecmp(end - 4, ".vpk")) {
		if (!strncmp(path, "./portal2_dlc1/", 15)) return true;
		if (!strncmp(path, "./portal2_dlc2/", 15)) return true;
		if (!strncmp(path, "portal2_dlc1/", 13)) return true;
		if (!strncmp(path, "portal2_dlc2/", 13)) return true;
	}
	return false;
}

// janky hack lol
static const char *const _g_map_found = "_MAP_FOUND";
static const char **_g_expected_maps;

static void _output_sar_data(uint32_t tick, struct sar_data data) {
	switch (data.type) {
	case SAR_DATA_TIMESCALE_CHEAT:
		printf("\t\t\t\t{ \"tick\": %d, \"type\": \"timescale\", \"value\": \"%.2f\" },\n", tick, data.timescale);
		break;
	case SAR_DATA_INITIAL_CVAR:
		if (g_config.initial_cvar_mode != 0) {
			int whitelist_status = config_check_var_whitelist(g_cvar_whitelist, data.initial_cvar.cvar, data.initial_cvar.val);
			if (!_allow_initial_cvar(data.initial_cvar.cvar, data.initial_cvar.val) && (whitelist_status == 1 || (whitelist_status == 0 && g_config.initial_cvar_mode == 2))) {
				for (size_t i = 0; i < strlen(data.initial_cvar.val); ++i) {
					if (data.initial_cvar.val[i] == '"') data.initial_cvar.val[i] = '\'';
				}
				printf("\t\t\t\t{ \"tick\": %d, \"type\": \"cvar\", \"val\": { \"cvar\": \"%s\", \"val\": \"%s\" } },\n", tick, data.initial_cvar.cvar, data.initial_cvar.val);
			}
		}
		break;
	case SAR_DATA_PAUSE:
		printf("\t\t\t\t{ \"tick\": %d, \"type\": \"pause\", \"value\": %d },\n", tick, data.pause_ticks);
		break;
	case SAR_DATA_INVALID:
		printf("\t\t\t\t{ \"tick\": %d, \"type\": \"invalid\" },\n", tick);
		break;
	case SAR_DATA_WAIT_RUN:
		if (g_config.show_wait) printf("\t\t\t\t{ \"tick\": %d, \"type\": \"wait\", \"value\": %d },\n", tick, data.wait_run.tick);
		break;
	case SAR_DATA_HWAIT_RUN:
		if (g_config.show_wait) printf("\t\t\t\t{ \"tick\": %d, \"type\": \"hwait\", \"value\": %d },\n", tick, data.hwait_run.ticks);
		break;
	case SAR_DATA_SPEEDRUN_TIME:
		printf("\t\t\t\t{ \"tick\": %d, \"type\": \"speedrun\", \"value\":\n\t\t\t\t\t{\n\t\t\t\t\t\t\"splits\": [\n", tick);
		{
			size_t ticks = 0;
			for (size_t i = 0; i < data.speedrun_time.nsplits; ++i) {
				printf("\t\t\t\t\t\t\t{ \"name\": \"%s\", \"segments\":\n\t\t\t\t\t\t\t\t[\n", data.speedrun_time.splits[i].name);
				for (size_t j = 0; j < data.speedrun_time.splits[i].nsegs; ++j) {
					printf("\t\t\t\t\t\t\t\t\t{ \"name\": \"%s\", \"ticks\": %d }", data.speedrun_time.splits[i].segs[j].name, data.speedrun_time.splits[i].segs[j].ticks);
					if (j != data.speedrun_time.splits[i].nsegs - 1) printf(",");
					printf("\n");
					ticks += data.speedrun_time.splits[i].segs[j].ticks;
				}
				printf("\t\t\t\t\t\t\t\t]\n\t\t\t\t\t\t\t}");
				if (i != data.speedrun_time.nsplits - 1) printf(",");
				printf("\n");
			}
			printf("\t\t\t\t\t\t],\n");

			size_t total = roundf((float)(ticks * 1000) / 60.0f);

			int ms = total % 1000;
			total /= 1000;
			int secs = total % 60;
			total /= 60;
			int mins = total % 60;
			total /= 60;
			int hrs = total;

			printf("\t\t\t\t\t\t\"total\": { \"ticks\": %zu, \"time\": \"%d:%02d:%02d.%03d\" }\n\t\t\t\t\t}\n\t\t\t\t},\n", ticks, hrs, mins, secs, ms);
		}
		break;
	case SAR_DATA_TIMESTAMP:
		printf(
			"\t\t\t\t{ \"tick\": %d, \"type\": \"timestamp\", \"value\": \"%04d/%02d/%02d %02d:%02d:%02d UTC\" },\n",
			tick,
			(int)data.timestamp.year,
			(int)data.timestamp.mon,
			(int)data.timestamp.day,
			(int)data.timestamp.hour,
			(int)data.timestamp.min,
			(int)data.timestamp.sec
		);
		break;
	case SAR_DATA_FILE_CHECKSUM:
		if (g_config.file_sum_mode != 0) {
			char strbuf[9];
			snprintf(strbuf, sizeof strbuf, "%08X", data.file_checksum.sum);
			int whitelist_status = config_check_var_whitelist(g_filesum_whitelist, data.file_checksum.path, strbuf);
			if (!_ignore_filesum(data.file_checksum.path) && (whitelist_status == 1 || (whitelist_status == 0 && g_config.file_sum_mode == 2))) {
				printf("\t\t\t\t{ \"tick\": %d, \"type\": \"file\", \"value\": { \"path\": \"%s\", \"sum\": \"%08X\" } },\n", tick, data.file_checksum.path, data.file_checksum.sum);
			}
		}
		break;
	default:
		// don't care
		break;
	}
}

static void _output_msg(struct demo_msg *msg) {
	switch (msg->type) {
	case DEMO_MSG_CONSOLE_CMD:
		if (!config_check_cmd_whitelist(g_cmd_whitelist, msg->con_cmd)) {
			for (size_t i = 0; i < strlen(msg->con_cmd); ++i) {
				if (msg->con_cmd[i] == '"') msg->con_cmd[i] = '\'';
			}
			printf("\t\t\t\t{ \"tick\": %d, \"type\": \"cmd\", \"value\": \"%s\" },\n", msg->tick, msg->con_cmd);
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
	if (!demo_matches) {
		printf("\t\t\t\t{ \"tick\": 0, \"type\": \"demosum\", \"value\": { \"given\": \"%X\", \"real\": \"%X\" } },\n", demo_given, demo_real);
	}

	if (!config_check_sum_whitelist(g_sar_sum_whitelist, sar_given)) {
		printf("\t\t\t\t{ \"tick\": 0, \"type\": \"sarsum\", \"value\": \"%X\" },\n", sar_given);
	}
}

void run_demo(const char *path) {
	struct demo *demo = demo_parse(path);

	if (!demo) {
		printf("\t\tnull");
		return;
	}

	bool has_csum = false;

	printf("\t\t{\n");
	printf("\t\t\t\"file\": \"%s\",\n", path);
	printf("\t\t\t\"user\": \"%s\",\n", demo->hdr.client_name);
	printf("\t\t\t\"map\": \"%s\",\n", demo->hdr.map_name);
	printf("\t\t\t\"tps\": %.2f,\n", (float)demo->hdr.playback_ticks / demo->hdr.playback_time);
	printf("\t\t\t\"ticks\": %d,\n", demo->hdr.playback_ticks);
	printf("\t\t\t\"events\": [\n");
	for (size_t i = 0; i < demo->nmsgs; ++i) {
		struct demo_msg *msg = demo->msgs[i];
		if (i == demo->nmsgs - 1 && msg->type == DEMO_MSG_SAR_DATA && msg->sar_data.type == SAR_DATA_CHECKSUM) {
			// ending checksum data - validate it
			_validate_checksum(msg->sar_data.checksum.demo_sum, msg->sar_data.checksum.sar_sum, demo->checksum);
			has_csum = true;
		} else {
			// normal message
			_output_msg(msg);
		}
	}

	if (demo->v2sum_state == V2SUM_INVALID) {

		printf("\t\t\t\t{ \"tick\": 0, \"type\": \"demosum\", \"value\": null },\n");

	} else if (demo->v2sum_state == V2SUM_VALID) {
		
		struct demo_msg *msg = demo->msgs[demo->nmsgs - 1];
		uint32_t sar_sum = msg->sar_data.checksum_v2.sar_sum;

		if (!config_check_sum_whitelist(g_sar_sum_whitelist, sar_sum)) {
			printf("\t\t\t\t{ \"tick\": 0, \"type\": \"sarsum\", \"value\": \"%X\" },\n", sar_sum);
		}

	}
	// this really just compensates for the extra comma in the last event
	printf("\t\t\t\t{ \"tick\": 0, \"type\": \"end\", \"value\": null }\n\t\t\t]\n\t\t}\n");

	if (_g_expected_maps) {
		for (size_t i = 0; _g_expected_maps[i]; ++i) {
			if (_g_expected_maps[i] == _g_map_found) continue; // This pointer equality check is intentional
			if (!strcmp(_g_expected_maps[i], demo->hdr.map_name)) {
				_g_expected_maps[i] = _g_map_found;
			}
		}
	}

	if (!has_csum && demo->v2sum_state == V2SUM_NONE) {
		printf("\tno checksums found; vanilla demo?\n");
	}

	demo_free(demo);
}

int main(int argc, char **argv) {
	_g_expected_maps = (const char **)config_read_newline_sep(EXPECTED_MAPS_FILE);
	g_cmd_whitelist = config_read_newline_sep(CMD_WHITELIST_FILE);
	g_sar_sum_whitelist = config_read_newline_sep(SAR_WHITELIST_FILE);
	g_filesum_whitelist = config_read_var_whitelist(FILESUM_WHITELIST_FILE);
	g_cvar_whitelist = config_read_var_whitelist(CVAR_WHITELIST_FILE);

	g_config.file_sum_mode = 2;
	g_config.initial_cvar_mode = 2;
	g_config.show_passing_checksums = false;
	g_config.show_wait = true;
	struct var_whitelist *general_conf = config_read_var_whitelist(GENERAL_CONF_FILE);
	if (general_conf) {
		for (struct var_whitelist *ptr = general_conf; ptr->var_name; ++ptr) {
			if (!strcmp(ptr->var_name, "file_sum_mode")) {
				int val = atoi(ptr->val);
				if (val < 0) val = 0;
				if (val > 2) val = 2;
				g_config.file_sum_mode = val;
				continue;
			}

			if (!strcmp(ptr->var_name, "initial_cvar_mode")) {
				int val = atoi(ptr->val);
				if (val < 0) val = 0;
				if (val > 2) val = 2;
				g_config.initial_cvar_mode = val;
				continue;
			}

			if (!strcmp(ptr->var_name, "show_passing_checksums")) {
				int val = atoi(ptr->val);
				g_config.show_passing_checksums = val != 0;
				continue;
			}

			if (!strcmp(ptr->var_name, "show_wait")) {
				int val = atoi(ptr->val);
				g_config.show_wait = val != 0;
				continue;
			}

			printf("bad config option '%s'\n", ptr->var_name);
		}
		config_free_var_whitelist(general_conf);
	}

	if (argc >= 2) {
		printf("\n{\n\t\"demos\": [\n");
		for (int i = 1; i < argc; i ++) {
			char *path = argv[i];
			if (i > 1) printf(",\n");
			run_demo(path);
		}
		printf("\t]\n}\n");
	} else {
		printf("no demo file provided on command line\n");
	}

	if (_g_expected_maps) {
		bool did_hdr = false;
		for (size_t i = 0; _g_expected_maps[i]; ++i) {
			if (_g_expected_maps[i] == _g_map_found) continue; // This pointer equality check is intentional
			if (!did_hdr) {
				did_hdr = true;
				printf("missing maps:\n");
			}
			printf("\t%s\n", _g_expected_maps[i]);
		}
	}

	config_free_newline_sep(g_cmd_whitelist);
	config_free_newline_sep(g_sar_sum_whitelist);
	config_free_var_whitelist(g_filesum_whitelist);
	config_free_var_whitelist(g_cvar_whitelist);

	return 0;
}
