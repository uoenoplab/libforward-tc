#include <iostream>
#include <random>
#include <chrono>
#include <cassert>
#include <cstring>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <linux/if.h>

#include <arpa/inet.h>

#include "forward.h"

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
	if (argc != 7) {
		fprintf(stderr, "Useage: %s [device name] [ingress qdisc] [egress qdisc] [pedit/block/mix] [in/out] [no. of rules]\n", argv[0]);
		exit(1);
	}

	struct flows random_flows;
	unsigned long MAX_FLOWS = atol(argv[6]);

	int action;
	if (strcmp(argv[4], "pedit") == 0)
		action = 0;
	else if (strcmp(argv[4], "block") == 0)
		action = 1;
	else
		action = 2;

	int direction;
	if (strcmp(argv[5], "in") == 0)
		direction = 1;
	else
		direction = 0;

	struct ifreq interface_request;
	memcpy(interface_request.ifr_name, argv[1], strlen(argv[1])+1);
	interface_request.ifr_name[strlen(argv[1])] = 0;
	struct sockaddr_in my_ip;
	int fd;

	if ((fd = socket(AF_INET,SOCK_DGRAM, 0)) == -1) {
		fprintf(stderr, "fail to open simple socket: %s\n", strerror(errno));
		return -1;
	}


	/* resolve interface IP address */
	if (ioctl(fd, SIOCGIFADDR, &interface_request) == -1) {
		fprintf(stderr, "fail to request interface: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	memcpy(&my_ip, &interface_request.ifr_addr, sizeof(struct sockaddr_in));
        close(fd);

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
		if (direction == 1)
			random_flows.src_ip[i] = u32_rand(mt);
		else
			random_flows.src_ip[i] = my_ip.sin_addr.s_addr;

		random_flows.dst_ip[i] = u32_rand(mt);

		random_flows.sport[i] = u16_rand(mt);
		random_flows.dport[i] = u16_rand(mt);

		random_flows.new_src_ip[i] = u32_rand(mt);
		random_flows.new_dst_ip[i] = u32_rand(mt);

		random_flows.new_sport[i] = u16_rand(mt);
		random_flows.new_dport[i] = u16_rand(mt);

		if (action == 2)
			random_flows.block[i] = bool_rand(mt);
		else
			random_flows.block[i] = action;

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

	std::cout << "Begin removal..." << std::endl;
	start = std::chrono::steady_clock::now();
	for (unsigned long i = 0; i < MAX_FLOWS; i++) {
		remove_redirection(random_flows.src_ip[i], &random_flows.src_mac[i * 8], random_flows.dst_ip[i], &random_flows.dst_mac[i * 8],
				   random_flows.sport[i], random_flows.dport[i]);
	}
	stop = std::chrono::steady_clock::now();
	duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds> (stop - start).count();
	std::cout << "Total removal time: " << (double)duration_ns * (double)1e-9 << " s" << std::endl;
	std::cout << "Removal rate      : " << (double)MAX_FLOWS / ((double)duration_ns * (double)1e-9) << " rules/s" << std::endl;

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
