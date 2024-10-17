#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "crc_table.h"
#include "demo.h"
#include "util.h"
#include "ed25519/ed25519.h"

#define HDR_SIZE 1072

static unsigned char _g_demo_sign_pubkey[32] = {
	0xF5, 0x84, 0x77, 0x86, 0x98, 0x45, 0x91, 0xA8,
	0x4E, 0x6E, 0x51, 0x1F, 0x34, 0xDF, 0x59, 0x81,
	0x76, 0x81, 0xF7, 0x0E, 0x95, 0x7B, 0x31, 0xD9,
	0xD8, 0x0E, 0x79, 0xD0, 0x5F, 0xDB, 0x9B, 0x19,
};

// Freeing {{{

static inline void _sar_data_free(struct sar_data data) {
	switch (data.type) {
	case SAR_DATA_INITIAL_CVAR:
		free(data.initial_cvar.cvar);
		free(data.initial_cvar.val);
		break;

	case SAR_DATA_ENTITY_INPUT_SLOT:
	case SAR_DATA_ENTITY_INPUT:
		free(data.entity_input.targetname);
		free(data.entity_input.classname);
		free(data.entity_input.inputname);
		free(data.entity_input.parameter);
		break;

	case SAR_DATA_WAIT_RUN:
		free(data.wait_run.cmd);
		break;

	case SAR_DATA_HWAIT_RUN:
		free(data.hwait_run.cmd);
		break;

	case SAR_DATA_SPEEDRUN_TIME:
		for (size_t i = 0; i < data.speedrun_time.nsplits; ++i) {
			for (size_t j = 0; j < data.speedrun_time.splits[i].nsegs; ++j) {
				free(data.speedrun_time.splits[i].segs[j].name);
			}
			free(data.speedrun_time.splits[i].name);
			free(data.speedrun_time.splits[i].segs);
		}
		free(data.speedrun_time.splits);
		break;

	case SAR_DATA_FILE_CHECKSUM:
		free(data.file_checksum.path);
		break;

	default:
		break;
	}
}

static inline void _msg_free(struct demo_msg *msg) {
	switch (msg->type) {
	case DEMO_MSG_CONSOLE_CMD:
		free(msg->con_cmd);
		break;

	case DEMO_MSG_SAR_DATA:
		_sar_data_free(msg->sar_data);
		break;

	default:
		break;
	}

	free(msg);
}

void demo_free(struct demo *demo) {
	if (!demo) return;
	free(demo->hdr.server_name);
	free(demo->hdr.client_name);
	free(demo->hdr.map_name);
	free(demo->hdr.game_directory);
	for (size_t i = 0; i < demo->nmsgs; ++i) {
		_msg_free(demo->msgs[i]);
	}
	free(demo->msgs);
	free(demo);
}

// }}}

// Utilities {{{

static inline uint32_t _read_u32(const uint8_t *buf) {
	return
		(buf[0] << 0) |
		(buf[1] << 8) |
		(buf[2] << 16) |
		(buf[3] << 24);
}

static inline float _read_f32(const uint8_t *buf) {
	union { uint32_t i; float f; } u;
	u.i = _read_u32(buf);
	return u.f;
}

// }}}

// _parse_sar_data {{{

static int _parse_sar_data(struct sar_data *out, FILE *f, size_t len) {
	if (len == 0) {
		fprintf(g_errfile, "[SAR] Empty message\n");
		out->type = SAR_DATA_INVALID;
		return 0;
	}

	uint8_t type;
	if (fread(&type, 1, 1, f) != 1) {
		return 1;
	}

	// workaround for bug in initial 1.12 release
	if (type == SAR_DATA_CHECKSUM && len == 5) {
		len = 9;
	}

	uint8_t *data = malloc(len - 1);
	if (fread(data, 1, len - 1, f) != len - 1) {
		return 1;
	}

	uint8_t *data_orig = data; // for freeing later, in case data is mutated

	out->type = type;

	switch (out->type) {
	case SAR_DATA_TIMESCALE_CHEAT:
		if (len != 5) {
			fprintf(g_errfile, "[SAR] Invalid timescale cheat message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->timescale = _read_f32(data);
		break;

	case SAR_DATA_INITIAL_CVAR:
		out->initial_cvar.cvar = strdup((char *)data);
		out->initial_cvar.val = strdup((char *)data + strlen((const char *)data) + 1);
		break;

	case SAR_DATA_ENTITY_INPUT_SLOT:
		out->slot = data[0];
		++data;
	case SAR_DATA_ENTITY_INPUT:
		out->entity_input.targetname = strdup((char *)data);
		size_t targetname_len = strlen(out->entity_input.targetname);
		out->entity_input.classname = strdup((char *)data + targetname_len + 1);
		size_t classname_len = strlen(out->entity_input.classname);
		out->entity_input.inputname = strdup((char *)data + targetname_len + classname_len + 2);
		size_t inputname_len = strlen(out->entity_input.inputname);
		out->entity_input.parameter = strdup((char *)data + targetname_len + classname_len + inputname_len + 3);
		break;

	case SAR_DATA_CHECKSUM:
		if (len != 9) {
			fprintf(g_errfile, "[SAR] Invalid checksum message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->checksum.demo_sum = _read_u32(data);
		out->checksum.sar_sum = _read_u32(data + 4);

		break;

	case SAR_DATA_CHECKSUM_V2:
		if (len != 69) {
			fprintf(g_errfile, "[SAR] Invalid checksum v2 message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->checksum_v2.sar_sum = _read_u32(data);
		memcpy(out->checksum_v2.signature, data + 4, 64);

		break;

	case SAR_DATA_PORTAL_PLACEMENT:
		if (len != 15) {
			fprintf(g_errfile, "[SAR] Invalid portal placement message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->slot = data[0];
		out->portal_placement.orange = data[1];
		out->portal_placement.x = _read_f32(data + 2);
		out->portal_placement.y = _read_f32(data + 6);
		out->portal_placement.z = _read_f32(data + 10);

		break;

	case SAR_DATA_CHALLENGE_FLAGS:
	case SAR_DATA_CROUCH_FLY:
		if (len != 2) {
			fprintf(g_errfile, "[SAR] Invalid challenge flags message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->slot = data[0];
		break;

	case SAR_DATA_PAUSE:
		if (len < 5 || len > 6) {
			fprintf(g_errfile, "[SAR] Invalid pause message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->pause_time.ticks = _read_u32(data);
		out->pause_time.timed = len == 6 ? data[4] : -1;
		break;

	case SAR_DATA_WAIT_RUN:
		if (len < 6) {
			fprintf(g_errfile, "[SAR] Invalid wait run message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->wait_run.tick = _read_u32(data);
		out->wait_run.cmd = strdup((char *)data + 4);

		break;

	case SAR_DATA_HWAIT_RUN:
		if (len < 6) {
			fprintf(g_errfile, "[SAR] Invalid hwait run message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->hwait_run.ticks = _read_u32(data);
		out->hwait_run.cmd = strdup((char *)data + 4);

		break;

	case SAR_DATA_ENTITY_SERIAL:
		if (len != 9) {
			fprintf(g_errfile, "[SAR] Invalid entity serial message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->entity_serial.slot = _read_f32(data);
		out->entity_serial.serial = _read_f32(data);

		break;

	case SAR_DATA_FRAMETIME:
		if (len != 5) {
			fprintf(g_errfile, "[SAR] Invalid frametime message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->frametime = _read_f32(data);

		break;

	case SAR_DATA_SPEEDRUN_TIME:
		if (len < 5) {
			fprintf(g_errfile, "[SAR] Invalid speedrun time message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->speedrun_time.nsplits = _read_u32(data);
		data += 4;

		out->speedrun_time.splits = malloc(out->speedrun_time.nsplits * sizeof out->speedrun_time.splits[0]);

		for (size_t i = 0; i < out->speedrun_time.nsplits; ++i) {
			out->speedrun_time.splits[i].name = strdup((char *)data);
			data += strlen(out->speedrun_time.splits[i].name) + 1;

			out->speedrun_time.splits[i].nsegs = _read_u32(data);
			data += 4;

			out->speedrun_time.splits[i].segs = malloc(out->speedrun_time.splits[i].nsegs * sizeof out->speedrun_time.splits[i].segs[0]);

			for (size_t j = 0; j < out->speedrun_time.splits[i].nsegs; ++j) {
				out->speedrun_time.splits[i].segs[j].name = strdup((char *)data);
				data += strlen(out->speedrun_time.splits[i].segs[j].name) + 1;

				out->speedrun_time.splits[i].segs[j].ticks = _read_u32(data);
				data += 4;
			}
		}

		if (data != data_orig + len - 1) {
			fprintf(g_errfile, "[SAR] Speedrun time data length mismatch %zu %zu\n", data - data_orig, len - 1);
			out->type = SAR_DATA_INVALID;
		}

		break;

	case SAR_DATA_TIMESTAMP:
		if (len != 8) {
			fprintf(g_errfile, "[SAR] Invalid timestamp message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->timestamp.year = data[0] | (data[1] << 8);
		out->timestamp.mon = data[2] + 1; // month off-by-one lol
		out->timestamp.day = data[3];
		out->timestamp.hour = data[4];
		out->timestamp.min = data[5];
		out->timestamp.sec = data[6];

		break;

	case SAR_DATA_FILE_CHECKSUM:
		if (len < 6) {
			fprintf(g_errfile, "[SAR] Invalid file checksum message length %zu\n", len);
			out->type = SAR_DATA_INVALID;
			break;
		}

		out->file_checksum.sum = _read_u32(data);
		out->file_checksum.path = strdup((char *)(data + 4));

		break;

	case SAR_DATA_QUEUEDCMD:
		out->queuedcmd = strdup((char *)data);
		
		break;

	default:
		fprintf(g_errfile, "[SAR] Unhandled message type %02X\n", out->type);
		out->type = SAR_DATA_INVALID;
		break;
	}

	free(data_orig);
	return 0;
}

// }}}

// _parse_msg {{{

static struct demo_msg *_parse_msg(FILE *f) {
	uint8_t msg_hdr_buf[6];
	if (fread(msg_hdr_buf, 1, sizeof msg_hdr_buf, f) != sizeof msg_hdr_buf) {
		return NULL;
	}

#define READ_U32(x) \
	uint32_t x; \
	do { \
		uint8_t _u32_buf[4]; \
		if (fread(_u32_buf, 1, sizeof _u32_buf, f) != sizeof _u32_buf) { \
			free(msg); \
			return NULL; \
		} \
		x = _read_u32(_u32_buf); \
	} while (0)

#define SKIP_BYTES(n) \
	do { \
		if (fseek(f, (n), SEEK_CUR) == -1) { \
			free(msg); \
			return NULL; \
		} \
	} while (0)

	struct demo_msg *msg = malloc(sizeof *msg);
	msg->type = msg_hdr_buf[0];
	msg->tick = _read_u32(msg_hdr_buf + 1);
	msg->slot = msg_hdr_buf[5];

	switch (msg->type) {
	case DEMO_MSG_SIGN_ON:
	case DEMO_MSG_PACKET: {
		// skip PacketInfo, InSequence, OutSequence
		SKIP_BYTES(76*2 + 4 + 4);

		// read size
		READ_U32(size);

		// skip data
		SKIP_BYTES(size);

		return msg;
	}

	case DEMO_MSG_SYNC_TICK:
		// no data
		return msg;

	case DEMO_MSG_CONSOLE_CMD: {
		// read size
		READ_U32(size);

		// read string
		msg->con_cmd = malloc(size + 1);
		if (!fgets(msg->con_cmd, size + 1, f) || feof(f)) {
			free(msg->con_cmd);
			free(msg);
			return NULL;
		}

		util_strip_whitespace(msg->con_cmd);

		return msg;
	}

	case DEMO_MSG_USER_CMD: {
		// skip command
		SKIP_BYTES(4);

		// read size
		READ_U32(size);

		// skip data
		SKIP_BYTES(size);

		return msg;
	}

	case DEMO_MSG_DATA_TABLES: {
		// read size
		READ_U32(size);

		// skip data
		SKIP_BYTES(size);

		return msg;
	}

	case DEMO_MSG_STOP:
		// no data
		return msg;

	case DEMO_MSG_CUSTOM_DATA: {
		// read type and size
		READ_U32(type);
		READ_U32(size);

		if (type != 0 || size == 8) {
			// not SAR data
			SKIP_BYTES(size);
			return msg;
		}

		// SAR data!
		msg->type = DEMO_MSG_SAR_DATA;

		// the first 8 bytes we ignore
		SKIP_BYTES(8);

		// now, parse SAR data!
		if (_parse_sar_data(&msg->sar_data, f, size - 8)) {
			free(msg);
			return NULL;
		}

		return msg;
	}

	case DEMO_MSG_STRING_TABLES: {
		// read size
		READ_U32(size);

		// skip data
		SKIP_BYTES(size);

		return msg;
	}

	default:
		free(msg);
		return NULL;
	}

#undef READ_U32
#undef SKIP_BYTES
}

// }}}

// _demo_checksum {{{

static uint32_t _demo_checksum(FILE *f) {
	if (fseek(f, -31, SEEK_END)) return 0; // ignore checksum message

	size_t size = ftell(f);
	if (size == -1) return 0;

	if (fseek(f, 0, SEEK_SET)) return 0;

	uint32_t crc = 0xFFFFFFFF;

	for (size_t i = 0; i < size; ++i) {
		uint8_t byte = fgetc(f);

		uint8_t lookup_idx = (crc ^ byte) & 0xFF;
		crc = (crc >> 8) ^ g_crc_table[lookup_idx];
	}

	return ~crc;
}

// }}}

// _demo_verify_sig {{{

static bool _demo_verify_sig(FILE *f, uint32_t sar_sum, const unsigned char *signature) {
	if (fseek(f, -91, SEEK_END)) return false; // ignore checksum message

	size_t size = ftell(f);
	if (size == -1) return false;

	if (fseek(f, 0, SEEK_SET)) return false;

	char *buf = malloc(size + 4); // extra space for sar checksum
	fread(buf, 1, size, f);
	if (ferror(f)) {
		free(buf);
		return false;
	}

	// write sar checksum to end
	*(uint32_t *)(buf + size) = sar_sum;

	bool res = ed25519_verify(signature, (unsigned char *)buf, size + 4, _g_demo_sign_pubkey);

	free(buf);

	return res;
}

// }}}

// demo_parse {{{

struct demo *demo_parse(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(g_errfile, "%s: failed to open file\n", path);
		return NULL;
	}

	// Header {{{

	uint8_t *hdr_buf = malloc(HDR_SIZE);

	if (fread(hdr_buf, 1, HDR_SIZE, f) != HDR_SIZE) {
		fprintf(g_errfile, "%s: incomplete header\n", path);
		free(hdr_buf);
		fclose(f);
		return NULL;
	}

	// check DemoFileStamp
	if (strncmp((char *)hdr_buf, "HL2DEMO\0", 8)) {
		fprintf(g_errfile, "%s: invalid header\n", path);
		free(hdr_buf);
		fclose(f);
		return NULL;
	}

	// check DemoProtocol
	if (_read_u32(hdr_buf + 8) != 4) {
		fprintf(g_errfile, "%s: unsupported protocol version\n", path);
		free(hdr_buf);
		fclose(f);
		return NULL;
	}

	struct demo_hdr hdr = {
		.server_name = strdup((char *)(hdr_buf + 16)),
		.client_name = strdup((char *)(hdr_buf + 276)),
		.map_name = strdup((char *)(hdr_buf + 536)),
		.game_directory = strdup((char *)(hdr_buf + 796)),
		.playback_time = fabsf(_read_f32(hdr_buf + 1056)),
		.playback_ticks = abs((int32_t)_read_u32(hdr_buf + 1060)),
		.playback_frames = _read_u32(hdr_buf + 1064),
		.sign_on_length = _read_u32(hdr_buf + 1068),
	};

	free(hdr_buf);

	// }}}

	// Messages {{{

	size_t msg_alloc = 512;
	size_t msg_count = 0;
	struct demo_msg **msgs = malloc(msg_alloc * sizeof msgs[0]);

	while (1) {
		int c = fgetc(f);
		if (c == EOF) break;
		ungetc(c, f);

		long p = ftell(f);

		struct demo_msg *msg = _parse_msg(f);
		if (!msg) {
			fprintf(g_errfile, "%s: malformed demo message at offset %ld %ld\n", path, ftell(f), p);
			fputs("THE FOLLOWING DEMO IS CORRUPTED. PARSING AS MUCH AS POSSIBLE\n", g_outfile);
			/*
			for (size_t i = 0; i < msg_count; ++i) {
				_msg_free(msgs[i]);
			}
			free(msgs);
			fclose(f);
			return NULL;*/
			break;
		}

		if (msg_count == msg_alloc) {
			msg_alloc *= 2;
			msgs = realloc(msgs, msg_alloc * sizeof msgs[0]);
		}

		msgs[msg_count++] = msg;
	}

	// }}}

	// Checksum {{{

	uint32_t checksum = 0;
	bool v2sum_present = false;
	bool v2sum_valid = false;

	if (msg_count > 0) {
		struct demo_msg *last = msgs[msg_count - 1];
		if (last->type == DEMO_MSG_SAR_DATA && last->sar_data.type == SAR_DATA_CHECKSUM) {
			// There's a SAR checksum message - calculate the demo checksum
			checksum = _demo_checksum(f);
		} else if (last->type == DEMO_MSG_SAR_DATA && last->sar_data.type == SAR_DATA_CHECKSUM_V2) {
			// v2 checksum - extract SAR checksum and verify signature
			v2sum_present = true;
			v2sum_valid = _demo_verify_sig(f, last->sar_data.checksum_v2.sar_sum, last->sar_data.checksum_v2.signature);
		}
	}

	// }}}

	fclose(f);

	struct demo *demo = malloc(sizeof *demo);
	demo->hdr = hdr;
	demo->nmsgs = msg_count;
	demo->msgs = msgs;
	demo->checksum = checksum;
	demo->v2sum_state = v2sum_present ? (v2sum_valid ? V2SUM_VALID : V2SUM_INVALID) : V2SUM_NONE;
	demo->tickrate = (float)hdr.playback_ticks / hdr.playback_time;

	return demo;
}

// }}}
