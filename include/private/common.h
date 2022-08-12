#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>

#include "uthash.h"

#define MAX_MSG 16384
#define MAX_OFFS 128

#define TIPV4 1
#define TIPV6 2
#define TINT 3
#define TU32 4
#define TMAC 5

#define RU32 0xFFFFFFFF
#define RU16 0xFFFF
#define RU8 0xFF

struct flow_key {
	uint32_t src_ip;
	uint32_t dst_ip;
	uint8_t src_mac[6];
	uint8_t dst_mac[6];
	uint16_t src_port;
	uint16_t dst_port;
};

struct flow {
	struct flow_key flow_id;
	uint32_t handle;
	UT_hash_handle hh;         /* makes this structure hashable */
};

struct m_pedit_key {
	__u32           mask;  /* AND */
	__u32           val;   /*XOR */
	__u32           off;  /*offset */
	__u32           at;
	__u32           offmask;
	__u32           shift;

	enum pedit_header_type htype;
	enum pedit_cmd cmd;
};

struct m_pedit_key_ex {
	enum pedit_header_type htype;
	enum pedit_cmd cmd;
};

struct m_pedit_sel {
	struct tc_pedit_sel sel;
	struct tc_pedit_key keys[MAX_OFFS];
	struct m_pedit_key_ex keys_ex[MAX_OFFS];
	bool extended;
};

#endif
