#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "crc_table.h"
#include "demo.h"
#include "util.h"

#define HDR_SIZE 1072

// Freeing {{{

static inline void _sar_data_free(struct sar_data data) {
	switch (data.type) {
	case SAR_DATA_INITIAL_CVAR:
		free(data.initial_cvar.cvar);
		free(data.initial_cvar.val);
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

	out->type = type;

	switch (out->type) {
	case SAR_DATA_TIMESCALE_CHEAT:
		if (len != 5) {
			out->type = SAR_DATA_INVALID;
			return 0;
		}

		out->timescale = _read_f32(data);
		return 0;

	case SAR_DATA_INITIAL_CVAR:
		out->initial_cvar.cvar = strdup((const char *)data);
		out->initial_cvar.val = strdup((const char *)(data + strlen((const char *)data) + 1));
		return 0;

	case SAR_DATA_CHECKSUM:
		if (len != 9) {
			out->type = SAR_DATA_INVALID;
			return 0;
		}

		out->checksum.demo_sum = _read_u32(data);
		out->checksum.sar_sum = _read_u32(data + 4);

		return 0;

	default:
		out->type = SAR_DATA_INVALID;
		return 0;
	}
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
		.playback_time = _read_f32(hdr_buf + 1056),
		.playback_ticks = _read_u32(hdr_buf + 1060),
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
			for (size_t i = 0; i < msg_count; ++i) {
				_msg_free(msgs[i]);
			}
			free(msgs);
			fclose(f);
			return NULL;
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

	if (msg_count > 0) {
		struct demo_msg *last = msgs[msg_count - 1];
		if (last->type == DEMO_MSG_SAR_DATA && last->sar_data.type == SAR_DATA_CHECKSUM) {
			// There's a SAR checksum message - calculate the demo checksum
			checksum = _demo_checksum(f);
		}
	}

	// }}}

	fclose(f);

	struct demo *demo = malloc(sizeof *demo);
	demo->hdr = hdr;
	demo->nmsgs = msg_count;
	demo->msgs = msgs;
	demo->checksum = checksum;

	return demo;
}

// }}}
