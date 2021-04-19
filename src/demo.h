#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>

struct demo_hdr {
	char *server_name;
	char *client_name;
	char *map_name;
	char *game_directory;
	float playback_time;
	uint32_t playback_ticks;
	uint32_t playback_frames;
	uint32_t sign_on_length;
};

struct sar_data {
	enum {
		SAR_DATA_TIMESCALE_CHEAT = 0x01,
		SAR_DATA_INITIAL_CVAR = 0x02,
		SAR_DATA_ENTITY_INPUT = 0x03,
		SAR_DATA_ENTITY_INPUT_SLOT = 0x04,
		SAR_DATA_PORTAL_PLACEMENT = 0x05,
		SAR_DATA_CHALLENGE_FLAGS = 0x06,
		SAR_DATA_CROUCH_FLY = 0x07,
		SAR_DATA_CHECKSUM = 0xFF,

		SAR_DATA_INVALID,
	} type;

	int slot;

	union {
		float timescale;

		struct {
			char *cvar;
			char *val;
		} initial_cvar;

		struct {
			uint32_t demo_sum;
			uint32_t sar_sum;
		} checksum;

		struct {
			char *targetname;
			char *classname;
			char *inputname;
			char *parameter;
		} entity_input;

		struct {
			float x, y, z;
			bool orange;
		} portal_placement;
	};
};

struct demo_msg {
	enum {
		DEMO_MSG_SIGN_ON = 1,
		DEMO_MSG_PACKET = 2,
		DEMO_MSG_SYNC_TICK = 3,
		DEMO_MSG_CONSOLE_CMD = 4,
		DEMO_MSG_USER_CMD = 5,
		DEMO_MSG_DATA_TABLES = 6,
		DEMO_MSG_STOP = 7,
		DEMO_MSG_CUSTOM_DATA = 8,
		DEMO_MSG_STRING_TABLES = 9,

		DEMO_MSG_SAR_DATA,
	} type;

	uint32_t tick;
	uint8_t slot;

	union {
		// ConsoleCmd
		char *con_cmd;

		// SAR data
		struct sar_data sar_data;
	};
};

struct demo {
	struct demo_hdr hdr;
	size_t nmsgs;
	struct demo_msg **msgs;
	uint32_t checksum;
};

struct demo *demo_parse(const char *path);
void demo_free(struct demo *demo);

#endif
