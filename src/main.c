#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "config.h"
#include "demo.h"

#define DEMO_DIR "demos"
#define ERR_FILE "errors.txt"
#define OUT_FILE "output.txt"
#define EXPECTED_MAPS_FILE "expected_maps.txt"
#define CMD_WHITELIST_FILE "cmd_whitelist.txt"
#define SAR_WHITELIST_FILE "sar_whitelist.txt"
#define CVAR_WHITELIST_FILE "cvar_whitelist.txt"
#define FILESUM_WHITELIST_FILE "filesum_whitelist.txt"

FILE *g_errfile;
FILE *g_outfile;

static char **g_cmd_whitelist;
#ifdef SHOW_ANTICHEAT
static char **g_sar_sum_whitelist;
static struct var_whitelist *g_filesum_whitelist;
static struct var_whitelist *g_cvar_whitelist;
#endif

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
		if (!strncmp(path, "./portal2_dlc3/", 15)) return false;
		if (!strncmp(path, "portal2_dlc3/", 13)) return false;
		return true;
	}
	return false;
}

// janky hack lol
static const char *const _g_map_found = "_MAP_FOUND";
static const char **_g_expected_maps;

static bool _g_detected_timescale;
static int _g_num_timescale;

static void _output_sar_data(uint32_t tick, struct sar_data data) {
	switch (data.type) {
	case SAR_DATA_TIMESCALE_CHEAT:
		if (!_g_detected_timescale) {
			++_g_num_timescale;
			_g_detected_timescale = true;
		}
#ifdef SHOW_ANTICHEAT
		fprintf(g_outfile, "\t\t[%5u] [SAR] timescale %.2f\n", tick, data.timescale);
#endif
		break;
	case SAR_DATA_INITIAL_CVAR:
#ifdef SHOW_ANTICHEAT
		if (!_allow_initial_cvar(data.initial_cvar.cvar, data.initial_cvar.val) && !config_check_var_whitelist(g_cvar_whitelist, data.initial_cvar.cvar, data.initial_cvar.val)) {
			fprintf(g_outfile, "\t\t[%5u] [SAR] cvar '%s' = '%s'\n", tick, data.initial_cvar.cvar, data.initial_cvar.val);
		}
#else
		(void)_allow_initial_cvar; // suppress warnings
#endif
		break;
	case SAR_DATA_PAUSE:
		fprintf(g_outfile, "\t\t[%5u] [SAR] paused for %d ticks (%.2fs)\n", tick, data.pause_ticks, (float)data.pause_ticks / 60.0f);
		break;
	case SAR_DATA_INVALID:
		fprintf(g_outfile, "\t\t[%5u] [SAR] corrupt data!\n", tick);
		break;
	case SAR_DATA_WAIT_RUN:
		fprintf(g_outfile, "\t\t[%5u] [SAR] wait to %d for '%s'\n", tick, data.wait_run.tick, data.wait_run.cmd);
		break;
	case SAR_DATA_HWAIT_RUN:
		fprintf(g_outfile, "\t\t[%5u] [SAR] hwait %d ticks for '%s'\n", tick, data.hwait_run.ticks, data.hwait_run.cmd);
		break;
	case SAR_DATA_SPEEDRUN_TIME:
		fprintf(g_outfile, "\t\t[%5u] [SAR] Speedrun finished with %zu splits!\n", tick, data.speedrun_time.nsplits);
		{
			size_t ticks = 0;
			for (size_t i = 0; i < data.speedrun_time.nsplits; ++i) {
				fprintf(g_outfile, "\t\t\t%s (%zu segments):\n", data.speedrun_time.splits[i].name, data.speedrun_time.splits[i].nsegs);
				for (size_t j = 0; j < data.speedrun_time.splits[i].nsegs; ++j) {
					fprintf(g_outfile, "\t\t\t\t%s (%d ticks)\n", data.speedrun_time.splits[i].segs[j].name, data.speedrun_time.splits[i].segs[j].ticks);
					ticks += data.speedrun_time.splits[i].segs[j].ticks;
				}
			}

			size_t total = roundf((float)(ticks * 1000) / 60.0f);

			int ms = total % 1000;
			total /= 1000;
			int secs = total % 60;
			total /= 60;
			int mins = total % 60;
			total /= 60;
			int hrs = total;

			fprintf(g_outfile, "\t\t\tTotal: %zu ticks = %d:%02d:%02d.%03d\n", ticks, hrs, mins, secs, ms);
		}
		break;
	case SAR_DATA_TIMESTAMP:
		fprintf(
			g_outfile,
			"\t\t[%5u] [SAR] recorded at %04d/%02d/%02d %02d:%02d:%02d UTC\n",
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
#ifdef SHOW_ANTICHEAT
		{
			char strbuf[9];
			snprintf(strbuf, sizeof strbuf, "%08X", data.file_checksum.sum);
			if (!_ignore_filesum(data.file_checksum.path) && !config_check_var_whitelist(g_filesum_whitelist, data.file_checksum.path, strbuf)) {
				fprintf(g_outfile, "\t\t[%5u] [SAR] file \"%s\" has checksum %08X\n", tick, data.file_checksum.path, data.file_checksum.sum);
			}
		}
#else
		(void)_ignore_filesum; // suppress warnings
#endif
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
#ifdef SHOW_ANTICHEAT
	bool demo_matches = demo_given == demo_real;
	if (demo_matches) {
		//fprintf(g_outfile, "\tdemo checksum PASS (%X)\n", demo_real);
	} else {
		fprintf(g_outfile, "\tdemo checksum FAIL (%X; should be %X)\n", demo_given, demo_real);
	}

	if (config_check_sum_whitelist(g_sar_sum_whitelist, sar_given)) {
		//fprintf(g_outfile, "\tSAR checksum PASS (%X)\n", sar_given);
	} else {
		fprintf(g_outfile, "\tSAR checksum FAIL (%X)\n", sar_given);
	}
#endif
}

void run_demo(const char *path) {
	struct demo *demo = demo_parse(path);

	if (!demo) {
		fputs("failed to parse demo!\n", g_errfile);
		return;
	}

	bool has_csum = false;

	fprintf(g_outfile, "demo: '%s'\n", path);
	fprintf(g_outfile, "\t'%s' on %s - %.2f TPS - %d ticks\n", demo->hdr.client_name, demo->hdr.map_name, (float)demo->hdr.playback_ticks / demo->hdr.playback_time, demo->hdr.playback_ticks);
	fprintf(g_outfile, "\tevents:\n");
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

	if (_g_expected_maps) {
		for (size_t i = 0; _g_expected_maps[i]; ++i) {
			if (_g_expected_maps[i] == _g_map_found) continue; // This pointer equality check is intentional
			if (!strcmp(_g_expected_maps[i], demo->hdr.map_name)) {
				_g_expected_maps[i] = _g_map_found;
			}
		}
	}

	if (!has_csum) {
#ifdef SHOW_ANTICHEAT
		fputs("\tno checksums found; vanilla demo?\n", g_outfile);
#endif
	}

	demo_free(demo);
}

int main(void) {
	g_errfile = fopen(ERR_FILE, "w");
	g_outfile = fopen(OUT_FILE, "w");

	_g_expected_maps = (const char **)config_read_newline_sep(EXPECTED_MAPS_FILE);
	g_cmd_whitelist = config_read_newline_sep(CMD_WHITELIST_FILE);
#ifdef SHOW_ANTICHEAT
	g_sar_sum_whitelist = config_read_newline_sep(SAR_WHITELIST_FILE);
	g_filesum_whitelist = config_read_var_whitelist(FILESUM_WHITELIST_FILE);
	g_cvar_whitelist = config_read_var_whitelist(CVAR_WHITELIST_FILE);
#endif

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

			_g_detected_timescale = false;
			run_demo(path);

			free(path);
		}
	} else {
		fprintf(g_errfile, "failed to open demos folder '%s'\n", DEMO_DIR);
	}

#ifdef SHOW_ANTICHEAT
	fprintf(g_outfile, "\ntimescale detected on %u demos\n", _g_num_timescale);
#endif

	if (_g_expected_maps) {
		bool did_hdr = false;
		for (size_t i = 0; _g_expected_maps[i]; ++i) {
			if (_g_expected_maps[i] == _g_map_found) continue; // This pointer equality check is intentional
			if (!did_hdr) {
				did_hdr = true;
				fputs("missing maps:\n", g_outfile);
			}
			fprintf(g_outfile, "\t%s\n", _g_expected_maps[i]);
		}
	}

	config_free_newline_sep(g_cmd_whitelist);
#ifdef SHOW_ANTICHEAT
	config_free_newline_sep(g_sar_sum_whitelist);
	config_free_var_whitelist(g_filesum_whitelist);
	config_free_var_whitelist(g_cvar_whitelist);
#endif

	fclose(g_errfile);
	fclose(g_outfile);

	return 0;
}
