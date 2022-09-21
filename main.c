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
//	apply_redirection_str("192.168.11.163", "3c:fd:fe:e5:ba:10", "192.168.11.131", "00:15:4d:13:70:b5",
//			8888, 8889,
//			"192.168.11.131", "00:15:4d:13:70:b5", "192.168.11.164", "3c:fd:fe:e5:a4:d0",
//			9000, 9001, false);
//	apply_redirection_str("192.168.11.164", "3c:fd:fe:e5:a4:d0", "192.168.11.131", "00:15:4d:13:70:b5",
//			9000, 9001,
//			"192.168.11.131", "00:15:4d:13:70:b5", "192.168.11.163", "3c:fd:fe:e5:ba:10",
//			8888, 8889, false);



//	apply_redirection_str("192.168.11.163", "3c:fd:fe:e5:ba:10", "192.168.11.131", "00:15:4d:13:70:b5",
//			8888, 8889,
//			"192.168.11.131", "00:15:4d:13:70:b5", "192.168.11.164", "3c:fd:fe:e5:a4:d0",
//			8888, 8889, false);

	apply_redirection_str("192.168.11.164", "192.168.11.131",
			8888, 8889,
			"192.168.11.131", "3c:fd:fe:e5:a4:d0", "192.168.11.163", "3c:fd:fe:e5:ba:10",
			8888, 8889, false);

	remove_redirection_str("192.168.11.164", "192.168.11.131",
			      8888, 8889);

//	remove_redirection_str("192.168.11.131", "00:15:4d:13:70:b5", "192.168.11.164", "3c:fd:fe:e5:a4:d0",
//			      (uint16_t)8889, (uint16_t)8888);
//
//	remove_redirection_str("192.168.11.131", "00:15:4d:13:70:b5", "192.168.11.164", "3c:fd:fe:e5:a4:d0",
//			      (uint16_t)8880, (uint16_t)8881);
	return 0;
}
