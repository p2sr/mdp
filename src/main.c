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
#define VPK_DIRECTORIES_WHITELIST_FILE "vpk_directories_whitelist.txt"
#define GENERAL_CONF_FILE "config.txt"

FILE *g_errfile;
FILE *g_outfile;

static char **g_cmd_whitelist;
static char **g_sar_sum_whitelist;
static struct var_whitelist *g_filesum_whitelist;
static struct var_whitelist *g_vpk_directories_whitelist;
static struct var_whitelist *g_cvar_whitelist;

// general config options
struct {
	int file_sum_mode; // 0 = don't show, 1 = show not matching, 2 (default) = show not matching or not present
	int initial_cvar_mode; // 0 = don't show, 1 = show not matching, 2 (default) = show not matching or not present
	bool show_passing_checksums; // should we output successful checksums?
	bool show_speedrun_identifier; // should we show speedrun identifier data?
	bool show_incomplete_speedrun_summaries; // should we show incomplete speedrun summaries?
	bool show_wait; // should we show when 'wait' was run?
	bool show_splits; // should we show split times?
	int show_netmessages; // 0 = don't show, 1 = show all except srtimer, 2 = show all
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

static int _check_vpk_directory_whitelist(const char *path, const char *sum) {
	if (!g_vpk_directories_whitelist) return 0;

	bool found = false;
	for (struct var_whitelist *entry = g_vpk_directories_whitelist; entry->var_name; ++entry) {
		size_t path_len = strlen(path);
		size_t entry_len = strlen(entry->var_name);
		if (path_len < entry_len) continue;
		if (strcmp(entry->var_name, "*") && strcmp(path + path_len - entry_len, entry->var_name)) continue;

		found = true;
		if (!entry->val || !strcmp(entry->val, "*") || !strcmp(entry->val, sum)) return 2;
	}

	return found ? 1 : 0;
}

// janky hack lol
static const char *const _g_map_found = "_MAP_FOUND";
static const char **_g_expected_maps;

static bool _g_detected_timescale;
static int _g_num_timescale;

static void _output_speedrun_summary(const struct demo *demo, uint32_t tick, const char *label, struct sar_speedrun_summary summary) {
	if (!g_config.show_splits) return;

	fprintf(g_outfile, "\t\t[%5u] [SAR] %s with %zu splits!\n", tick, label, summary.nsplits);

	size_t ticks = 0;
	for (size_t i = 0; i < summary.nsplits; ++i) {
		fprintf(g_outfile, "\t\t\t%s (%zu segments):\n", summary.splits[i].name, summary.splits[i].nsegs);
		for (size_t j = 0; j < summary.splits[i].nsegs; ++j) {
			fprintf(g_outfile, "\t\t\t\t%s (%d ticks)\n", summary.splits[i].segs[j].name, summary.splits[i].segs[j].ticks);
			ticks += summary.splits[i].segs[j].ticks;
		}
	}

	if (summary.nrules > 0) {
		fprintf(g_outfile, "\t\t\tRules:\n");
		for (size_t i = 0; i < summary.nrules; ++i) {
			fprintf(g_outfile, "\t\t\t\t%s = %s\n", summary.rules[i].name, summary.rules[i].data);
		}
	}

	size_t total = roundf((float)(ticks * 1000) / demo->tickrate);
	int ms = total % 1000;
	total /= 1000;
	int secs = total % 60;
	total /= 60;
	int mins = total % 60;
	total /= 60;
	int hrs = total;

	fprintf(g_outfile, "\t\t\tTotal: %zu ticks = %d:%02d:%02d.%03d\n", ticks, hrs, mins, secs, ms);
}

static void _output_sar_data(struct demo *demo, uint32_t tick, struct sar_data data) {
	switch (data.type) {
	case SAR_DATA_TIMESCALE_CHEAT:
		if (!_g_detected_timescale) {
			++_g_num_timescale;
			_g_detected_timescale = true;
		}
		fprintf(g_outfile, "\t\t[%5u] [SAR] timescale %.2f\n", tick, data.timescale);
		break;
	case SAR_DATA_INITIAL_CVAR:
		if (g_config.initial_cvar_mode != 0) {
			int whitelist_status = config_check_var_whitelist(g_cvar_whitelist, data.initial_cvar.cvar, data.initial_cvar.val);
			if (!_allow_initial_cvar(data.initial_cvar.cvar, data.initial_cvar.val) && (whitelist_status == 1 || (whitelist_status == 0 && g_config.initial_cvar_mode == 2))) {
				fprintf(g_outfile, "\t\t[%5u] [SAR] cvar '%s' = '%s'\n", tick, data.initial_cvar.cvar, data.initial_cvar.val);
			}
		}
		break;
	case SAR_DATA_PAUSE:
		fprintf(g_outfile, "\t\t[%5u] [SAR] paused for %d ticks (%.2fs)", tick, data.pause_time.ticks, (float)data.pause_time.ticks / demo->tickrate);
		if (data.pause_time.timed != -1) fprintf(g_outfile, " (%s)", data.pause_time.timed ? "timed" : "untimed");
		fprintf(g_outfile, "\n");
		break;
	case SAR_DATA_INVALID:
		fprintf(g_outfile, "\t\t[%5u] [SAR] corrupt data!\n", tick);
		break;
	case SAR_DATA_WAIT_RUN:
		if (g_config.show_wait) fprintf(g_outfile, "\t\t[%5u] [SAR] wait to %d for '%s'\n", tick, data.wait_run.tick, data.wait_run.cmd);
		break;
	case SAR_DATA_HWAIT_RUN:
		if (g_config.show_wait) fprintf(g_outfile, "\t\t[%5u] [SAR] hwait %d ticks for '%s'\n", tick, data.hwait_run.ticks, data.hwait_run.cmd);
		break;
	case SAR_DATA_ENTITY_SERIAL:
		fprintf(g_outfile, "\t\t[%5u] [SAR] Entity slot %d serial changed to %d\n", tick, data.entity_serial.slot, data.entity_serial.serial);
		break;
	case SAR_DATA_FRAMETIME:
		fprintf(g_outfile, "\t\t[%5u] [SAR] Frame took %fms\n", tick, data.frametime * 1000.0f);
		break;
	case SAR_DATA_SPEEDRUN_TIME:
		_output_speedrun_summary(demo, tick, "Speedrun finished", data.speedrun_time);
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
		if (g_config.file_sum_mode != 0) {
			char strbuf[9];
			snprintf(strbuf, sizeof strbuf, "%08X", data.file_checksum.sum);
			int whitelist_status = config_check_var_whitelist(g_filesum_whitelist, data.file_checksum.path, strbuf);
			if (!_ignore_filesum(data.file_checksum.path) && (whitelist_status == 1 || (whitelist_status == 0 && g_config.file_sum_mode == 2))) {
				fprintf(g_outfile, "\t\t[%5u] [SAR] file \"%s\" has checksum %08X\n", tick, data.file_checksum.path, data.file_checksum.sum);
			}
		}
		break;
	case SAR_DATA_QUEUEDCMD:
		if (!config_check_cmd_whitelist(g_cmd_whitelist, data.queuedcmd)) {
			fprintf(g_outfile, "\t\t[%5u] [SAR] queued command: %s\n", tick, data.queuedcmd);
		}
		break;
	case SAR_DATA_VPK_CHECKSUM:
		if (g_config.file_sum_mode != 0) {
			char strbuf[9];
			snprintf(strbuf, sizeof strbuf, "%08X", data.vpk_checksum.sum);
			if (config_check_var_whitelist(g_filesum_whitelist, data.vpk_checksum.path, strbuf) == 2) {
				break;
			}

			bool printed = false;
			for (size_t i = 0; i < data.vpk_checksum.nentries; ++i) {
				snprintf(strbuf, sizeof strbuf, "%08X", data.vpk_checksum.entries[i].sum);
				int whitelist_status = _check_vpk_directory_whitelist(data.vpk_checksum.entries[i].path, strbuf);
				if (whitelist_status == 1 || (whitelist_status == 0 && g_config.file_sum_mode == 2)) {
					if (!printed) {
						fprintf(g_outfile, "\t\t[%5u] [SAR] VPK \"%s\" has checksum %08X\n", tick, data.vpk_checksum.path, data.vpk_checksum.sum);
						printed = true;
					}
					fprintf(g_outfile, "\t\t\t\"%s\" has checksum %08X\n", data.vpk_checksum.entries[i].path, data.vpk_checksum.entries[i].sum);
				}
			}
		}
		break;
	case SAR_DATA_SPEEDRUN_TIME_INCOMPLETE:
		if (g_config.show_incomplete_speedrun_summaries) {
			_output_speedrun_summary(demo, tick, "Incomplete speedrun summary", data.speedrun_time_incomplete);
		}
		break;
	case SAR_DATA_SPEEDRUN_ID:
		if (g_config.show_speedrun_identifier) {
			fprintf(
				g_outfile,
				"\t\t[%5u] [SAR] speedrun identifier %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
				tick,
				data.speedrun_id[0], data.speedrun_id[1], data.speedrun_id[2], data.speedrun_id[3],
				data.speedrun_id[4], data.speedrun_id[5], data.speedrun_id[6], data.speedrun_id[7],
				data.speedrun_id[8], data.speedrun_id[9], data.speedrun_id[10], data.speedrun_id[11],
				data.speedrun_id[12], data.speedrun_id[13], data.speedrun_id[14], data.speedrun_id[15]
			);
		}
		break;
	default:
		// don't care
		break;
	}
}

#define SAR_MSG_INIT_B "&^!$"
#define SAR_MSG_INIT_O "&^!%"
#define SAR_MSG_CONT_B "&^?$"
#define SAR_MSG_CONT_O "&^?%"

static char g_partial[8192];
static int g_expected_len = 0;

///// START BASE92 /////

// This isn't really base92. Instead, we encode 4-byte input chunks into 5 base92 characters. If the
// final chunk is not 4 bytes, each byte of it is sent as 2 base92 characters; the receiver infers
// this from the buffer length and decodes accordingly. This system is almost as space-efficient as
// is possible.

static char base92_chars[93] = // 93 because null terminator
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"0123456789"
	"!$%^&*-_=+()[]{}<>'@#~;:/?,.|\\";

// doing this at runtime is a little silly but shh
static const char *base92_reverse() {
	static char map[256];
	static int initd = 0;
	if (initd == 0) {
		initd = 1;
		for (int i = 0; i < 92; ++i) {
			char c = base92_chars[i];
			map[(int)c] = i;
		}
	}
	return map;
}

static char *base92_decode(const char *encoded, int len) {
	const char *base92_rev = base92_reverse();

	static char out[512]; // leave some overhead lol
	static int outLen;
	memset(out, 0, sizeof(out));
	outLen = 0;
	#define push(val) \
		out[outLen++] = val;
	while (len > 6 || len == 5) {
		unsigned val = base92_rev[(unsigned)encoded[4]];
		val = (val * 92) + base92_rev[(unsigned)encoded[3]];
		val = (val * 92) + base92_rev[(unsigned)encoded[2]];
		val = (val * 92) + base92_rev[(unsigned)encoded[1]];
		val = (val * 92) + base92_rev[(unsigned)encoded[0]];

		char *raw = (char *)&val;
		push(raw[0]);
		push(raw[1]);
		push(raw[2]);
		push(raw[3]);

		encoded += 5;
		len -= 5;
	}
	while (len > 0) {
		unsigned val = base92_rev[(unsigned)encoded[1]];
		val = (val * 92) + base92_rev[(unsigned)encoded[0]];

		char *raw = (char *)&val;
		push(raw[0]);
		encoded += 2;
		len -= 2;
	}
	return out;
}

///// END BASE92 /////

static bool handleMessage(struct demo_msg *msg) {
	if (strncmp(msg->con_cmd, "say \"", 5)) return false;
	if (strlen(msg->con_cmd) < 10) return false;
	bool has_prefix = true;
	char *prefix = msg->con_cmd + 5;
	bool cont, orange;
	if (!strncmp(prefix, SAR_MSG_INIT_B, 4)) {
		cont = false;
		orange = false;
	} else if (!strncmp(prefix, SAR_MSG_INIT_O, 4)) {
		cont = false;
		orange = true;
	} else if (!strncmp(prefix, SAR_MSG_CONT_B, 4)) {
		cont = true;
		orange = false;
	} else if (!strncmp(prefix, SAR_MSG_CONT_O, 4)) {
		cont = true;
		orange = true;
	} else {
		has_prefix = false;
	}

	if (!has_prefix) return false;

	if (cont) {
		if (!g_expected_len) {
			fprintf(g_errfile, "\t\t[%5u] Unmatched NetMessage continuation %s\n", msg->tick, msg->con_cmd);
			return false;
		}
		strcat(g_partial, msg->con_cmd + 9);
	} else {
		char *raw = base92_decode(msg->con_cmd + 9, 5);
		g_expected_len = (int)*raw;
		strcat(g_partial, msg->con_cmd + 14);
	}

	if (strlen(g_partial) < g_expected_len) {
		fprintf(g_outfile, "\t\t[%5u] NetMessage continuation %d != %d\n", msg->tick, g_expected_len, (int)strlen(g_partial));
		return true;
	} else if (strlen(g_partial) > g_expected_len) {
		fprintf(g_errfile, "\t\t[%5u] NetMessage length mismatch %d != %d\n", msg->tick, g_expected_len, (int)strlen(g_partial));
		return false;
	} else {
		char *decoded = base92_decode(g_partial, g_expected_len);
		char *type = decoded;
		char *data = decoded + strlen(type) + 1;

		if (g_config.show_netmessages == 2 || (g_config.show_netmessages == 1 && strcmp(type, "srtimer"))) {
			fprintf(g_outfile, "\t\t[%5u] NetMessage (%s): %s", msg->tick, orange ? "o" : "b", type);

			// print data
			int datalen = strlen(data);
			bool printdata = true;
			if (!strcmp(type, "srtimer")) {
				datalen = 4;
				printdata = false;
			}
			if (!strcmp(type, "cmboard")) {
				datalen = 0;
			}

			if (datalen > 0) {
				if (printdata) fprintf(g_outfile, " = %s (", data);
				else fprintf(g_outfile, " (");

				for (int i = 0; i < datalen; ++i) {
					fprintf(g_outfile, "%02X", (unsigned char)data[i]);
					if (i < datalen - 1) fprintf(g_outfile, " ");
				}
				if (datalen > 0) fprintf(g_outfile, ")");
			}

			fprintf(g_outfile, "\n");
		}
	}
	g_expected_len = 0;
	g_partial[0] = 0;
	return true;
}

static void _output_msg(struct demo *demo, struct demo_msg *msg) {
	switch (msg->type) {
	case DEMO_MSG_CONSOLE_CMD:
		if (!config_check_cmd_whitelist(g_cmd_whitelist, msg->con_cmd)) {
			if (!handleMessage(msg)) {
				fprintf(g_outfile, "\t\t[%5u] %s\n", msg->tick, msg->con_cmd);
			}
		}
		break;
	case DEMO_MSG_SAR_DATA:
		_output_sar_data(demo, msg->tick, msg->sar_data);
		break;
	default:
		break;
	}
}

static void _validate_checksum(uint32_t demo_given, uint32_t sar_given, uint32_t demo_real) {
	bool demo_matches = demo_given == demo_real;
	if (demo_matches) {
		if (g_config.show_passing_checksums) fprintf(g_outfile, "\tdemo checksum PASS (%X)\n", demo_real);
	} else {
		fprintf(g_outfile, "\tdemo checksum FAIL (%X; should be %X)\n", demo_given, demo_real);
	}

	if (config_check_sum_whitelist(g_sar_sum_whitelist, sar_given)) {
		if (g_config.show_passing_checksums) fprintf(g_outfile, "\tSAR checksum PASS (%X)\n", sar_given);
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

	bool has_csum = false;

	fprintf(g_outfile, "demo: '%s'\n", path);
	fprintf(g_outfile, "\t'%s' on %s - %.2f TPS - %d ticks\n", demo->hdr.client_name, demo->hdr.map_name, demo->tickrate, demo->hdr.playback_ticks);
	fprintf(g_outfile, "\tevents:\n");
	for (size_t i = 0; i < demo->nmsgs; ++i) {
		struct demo_msg *msg = demo->msgs[i];
		if (i == demo->nmsgs - 1 && msg->type == DEMO_MSG_SAR_DATA && msg->sar_data.type == SAR_DATA_CHECKSUM) {
			// ending checksum data - validate it
			_validate_checksum(msg->sar_data.checksum.demo_sum, msg->sar_data.checksum.sar_sum, demo->checksum);
			has_csum = true;
		} else {
			// normal message
			_output_msg(demo, msg);
		}
	}

	if (demo->v2sum_state == V2SUM_INVALID) {
		fprintf(g_outfile, "\tdemo v2 checksum FAIL\n");
	} else if (demo->v2sum_state == V2SUM_VALID) {
		if (g_config.show_passing_checksums) fprintf(g_outfile, "\tdemo v2 checksum PASS\n");
		struct demo_msg *msg = demo->msgs[demo->nmsgs - 1];
		uint32_t sar_sum = msg->sar_data.checksum_v2.sar_sum;
		if (config_check_sum_whitelist(g_sar_sum_whitelist, sar_sum)) {
			if (g_config.show_passing_checksums) fprintf(g_outfile, "\tSAR checksum PASS (%X)\n", sar_sum);
		} else {
			fprintf(g_outfile, "\tSAR checksum FAIL (%X)\n", sar_sum);
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

	if (!has_csum && demo->v2sum_state == V2SUM_NONE) {
		fputs("\tno checksums found; vanilla demo?\n", g_outfile);
	}

	demo_free(demo);
}

int main(int argc, char **argv) {
	const char *dem_name = NULL;
	if (argc == 1) {
		g_errfile = fopen(ERR_FILE, "w");
		g_outfile = fopen(OUT_FILE, "w");
	} else if (argc == 2) {
		g_errfile = stderr;
		g_outfile = stdout;
		dem_name = argv[1];
	} else {
		const char *name = argv[0] ? argv[0] : "mdp";
		fprintf(stderr, "Usage:\n");
		fprintf(stderr, " %s          Traverses every demo in ./demos; outputs to " OUT_FILE " and " ERR_FILE "\n", name);
		fprintf(stderr, " %s [in.dem] Runs on a specific demo; outputs to stdio\n", name);
		return 1;
	}

	_g_expected_maps = (const char **)config_read_newline_sep(EXPECTED_MAPS_FILE);
	g_cmd_whitelist = config_read_newline_sep(CMD_WHITELIST_FILE);
	g_sar_sum_whitelist = config_read_newline_sep(SAR_WHITELIST_FILE);
	g_filesum_whitelist = config_read_var_whitelist(FILESUM_WHITELIST_FILE);
	g_vpk_directories_whitelist = config_read_var_whitelist(VPK_DIRECTORIES_WHITELIST_FILE);
	g_cvar_whitelist = config_read_var_whitelist(CVAR_WHITELIST_FILE);

	g_config.file_sum_mode = 2;
	g_config.initial_cvar_mode = 2;
	g_config.show_passing_checksums = false;
	g_config.show_speedrun_identifier = true;
	g_config.show_incomplete_speedrun_summaries = true;
	g_config.show_wait = true;
	g_config.show_splits = true;
	g_config.show_netmessages = 2;
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

			if (!strcmp(ptr->var_name, "show_speedrun_identifier")) {
				int val = atoi(ptr->val);
				g_config.show_speedrun_identifier = val != 0;
				continue;
			}

			if (!strcmp(ptr->var_name, "show_incomplete_speedrun_summaries")) {
				int val = atoi(ptr->val);
				g_config.show_incomplete_speedrun_summaries = val != 0;
				continue;
			}

			if (!strcmp(ptr->var_name, "show_wait")) {
				int val = atoi(ptr->val);
				g_config.show_wait = val != 0;
				continue;
			}

			if (!strcmp(ptr->var_name, "show_splits")) {
				int val = atoi(ptr->val);
				g_config.show_splits = val != 0;
				continue;
			}

			if (!strcmp(ptr->var_name, "show_netmessages")) {
				int val = atoi(ptr->val);
				if (val < 0) val = 0;
				if (val > 2) val = 2;
				g_config.show_netmessages = val;
				continue;
			}

			fprintf(g_errfile, "bad config option '%s'\n", ptr->var_name);
		}
		config_free_var_whitelist(general_conf);
	}

	if (dem_name) {
		run_demo(dem_name);
		if (_g_detected_timescale) {
			fputs("\nTIMESCALE DETECTED\n", g_outfile);
		}
	} else {
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

			closedir(d);
		} else {
			fprintf(g_errfile, "failed to open demos folder '%s'\n", DEMO_DIR);
		}

		fprintf(g_outfile, "\ntimescale detected on %u demos\n", _g_num_timescale);
	}

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
	config_free_newline_sep(g_sar_sum_whitelist);
	config_free_var_whitelist(g_filesum_whitelist);
	config_free_var_whitelist(g_vpk_directories_whitelist);
	config_free_var_whitelist(g_cvar_whitelist);

	fclose(g_errfile);
	fclose(g_outfile);

	return 0;
}
