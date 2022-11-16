#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "forward.h"

int main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "Usage: %s [device] [ingress class parent] [egress class parent]\n", argv[0]);
		exit(1);
	}

	assert(init_forward(argv[1], argv[2], argv[3]) >= 0);

	apply_redirection_str("192.168.11.131", "192.168.11.143",
			8888, 8889,
			"192.168.11.131", "b8:ce:f6:b8:9e:06", "192.168.11.143", "3c:ec:ef:64:a9:0e",
			8888, 8889, true, true);
	apply_redirection_str("192.168.11.131", "192.168.11.143",
			8888, 8889,
			"192.168.11.131", "b8:ce:f6:b8:9e:06", "192.168.11.143", "3c:ec:ef:64:a9:0e",
			8888, 8889, false, false);

	remove_redirection_str("192.168.11.131", "192.168.11.143", 8888, 8889, true);
	remove_redirection_str("192.168.11.131", "192.168.11.143", 8888, 8889, false);

//	remove_redirection_str("192.168.11.131", "00:15:4d:13:70:b5", "192.168.11.164", "3c:fd:fe:e5:a4:d0",
//			      (uint16_t)8889, (uint16_t)8888);
//
//	remove_redirection_str("192.168.11.131", "00:15:4d:13:70:b5", "192.168.11.164", "3c:fd:fe:e5:a4:d0",
//			      (uint16_t)8880, (uint16_t)8881);
	return 0;
}
