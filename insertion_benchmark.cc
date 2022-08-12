#include <iostream>
#include <random>
#include <chrono>
#include <cassert>

#include <arpa/inet.h>

#include "forward.h"

//#define MAX_FLOWS 262144

struct flows {
	uint32_t *src_ip;
	uint32_t *dst_ip;

	uint8_t *src_mac;
	uint8_t *dst_mac;

	uint16_t *sport;
	uint16_t *dport;

	uint32_t *new_src_ip;
	uint32_t *new_dst_ip;

	uint8_t *new_src_mac;
	uint8_t *new_dst_mac;

	uint16_t *new_sport;
	uint16_t *new_dport;

	bool *block;
};


int main(int argc, char *argv[])
{
	if (argc != 5) {
		fprintf(stderr, "Useage: %s [device name] [ingress qdisc] [egress qdisc] [no. of rules]\n", argv[0]);
		exit(1);
	}

	struct flows random_flows;
	unsigned long MAX_FLOWS = atol(argv[4]);

	std::random_device rd;
	std::mt19937 mt(rd());
	std::uniform_int_distribution<uint32_t> u32_rand(1, UINT32_MAX);
	std::uniform_int_distribution<uint16_t> u16_rand(1, UINT16_MAX);
	std::uniform_int_distribution<uint8_t> u8_rand(1, UINT8_MAX);

	std::uniform_int_distribution<int> bool_rand(0, 1);

	random_flows.src_ip = (uint32_t*)calloc(MAX_FLOWS, sizeof(uint32_t));
	random_flows.dst_ip = (uint32_t*)calloc(MAX_FLOWS, sizeof(uint32_t));
	random_flows.src_mac = (uint8_t*)calloc(MAX_FLOWS * 8, sizeof(uint8_t));
	random_flows.dst_mac = (uint8_t*)calloc(MAX_FLOWS * 8, sizeof(uint8_t));
	random_flows.sport = (uint16_t*)calloc(MAX_FLOWS, sizeof(uint16_t));
	random_flows.dport = (uint16_t*)calloc(MAX_FLOWS, sizeof(uint16_t));
	random_flows.block = (bool*)calloc(MAX_FLOWS, sizeof(bool));

	random_flows.new_src_ip = (uint32_t*)calloc(MAX_FLOWS, sizeof(uint32_t));
	random_flows.new_dst_ip = (uint32_t*)calloc(MAX_FLOWS, sizeof(uint32_t));
	random_flows.new_src_mac = (uint8_t*)calloc(MAX_FLOWS * (unsigned long)8, sizeof(uint8_t));
	random_flows.new_dst_mac = (uint8_t*)calloc(MAX_FLOWS * (unsigned long)8, sizeof(uint8_t));
	random_flows.new_sport = (uint16_t*)calloc(MAX_FLOWS, sizeof(uint16_t));
	random_flows.new_dport = (uint16_t*)calloc(MAX_FLOWS, sizeof(uint16_t));

	init_forward(argv[1], argv[2], argv[3]);
	std::cout << "Initializing " << MAX_FLOWS << " rules..." << std::endl;
	for (unsigned long i = 0; i < MAX_FLOWS; i++) {
		//inet_pton(AF_INET, "192.168.11.131", &random_flows.src_ip[i]);
		random_flows.src_ip[i] = u32_rand(mt);
		random_flows.dst_ip[i] = u32_rand(mt);

		random_flows.sport[i] = u16_rand(mt);
		random_flows.dport[i] = u16_rand(mt);

		random_flows.new_src_ip[i] = u32_rand(mt);
		random_flows.new_dst_ip[i] = u32_rand(mt);

		random_flows.new_sport[i] = u16_rand(mt);
		random_flows.new_dport[i] = u16_rand(mt);

		random_flows.block[i] = bool_rand(mt);
		//random_flows.block[i] = 1;

		struct in_addr addr;
		addr.s_addr = random_flows.src_ip[i];
		addr.s_addr = random_flows.dst_ip[i];

		addr.s_addr = random_flows.new_src_ip[i];
		addr.s_addr = random_flows.new_dst_ip[i];

		for (int j = 0; j < 8; j++) {
			random_flows.src_mac[i * 8 + j] = u8_rand(mt);
			random_flows.dst_mac[i * 8 + j] = u8_rand(mt);

			random_flows.new_src_mac[i * 8 + j] = u8_rand(mt);
			random_flows.new_dst_mac[i * 8 + j] = u8_rand(mt);
		}
	}

	std::cout << "Begin insertion..." << std::endl;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	for (unsigned long i = 0; i < MAX_FLOWS; i++) {
		assert (apply_redirection(random_flows.src_ip[i], &random_flows.src_mac[i * 8], random_flows.dst_ip[i], &random_flows.dst_mac[i * 8],
					  random_flows.sport[i], random_flows.dport[i],
					  random_flows.new_src_ip[i], &random_flows.new_src_mac[i * 8], random_flows.new_dst_ip[i], &random_flows.new_dst_mac[i * 8],
					  random_flows.new_sport[i], random_flows.new_dport[i], random_flows.block[i]) == 0);
	}
	std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
	auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds> (stop - start).count();
	std::cout << "Total insertion time: " << (double)duration_ns * (double)1e-9 << " s" << std::endl;
	std::cout << "Insertion rate      : " << (double)MAX_FLOWS / ((double)duration_ns * (double)1e-9) << " rules/s" << std::endl;

//	std::cout << "Begin removal..." << std::endl;
//	start = std::chrono::steady_clock::now();
//	for (unsigned long i = 0; i < MAX_FLOWS; i++) {
//		remove_redirection(random_flows.src_ip[i], &random_flows.src_mac[i * 8], random_flows.dst_ip[i], &random_flows.dst_mac[i * 8],
//				   random_flows.sport[i], random_flows.dport[i]);
//	}
//	stop = std::chrono::steady_clock::now();
//	duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds> (stop - start).count();
//	std::cout << "Total removal time: " << (double)duration_ns * (double)1e-9 << " s" << std::endl;
//	std::cout << "Removal rate      : " << (double)MAX_FLOWS / ((double)duration_ns * (double)1e-9) << " rules/s" << std::endl;

	free(random_flows.src_ip);
	free(random_flows.dst_ip);

	free(random_flows.sport);
	free(random_flows.dport);

	free(random_flows.new_src_ip);
	free(random_flows.new_dst_ip);

	free(random_flows.new_sport);
	free(random_flows.new_dport);

	free(random_flows.src_mac);
	free(random_flows.dst_mac);

	free(random_flows.new_src_mac);
	free(random_flows.new_dst_mac);

	free(random_flows.block);

	return 0;
}
