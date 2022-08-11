#include <linux/if_ether.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_arp.h>

#include <libnetlink.h>
#include <linux/tc_act/tc_csum.h>
#include <linux/tc_act/tc_mirred.h>
#include <linux/tc_act/tc_pedit.h>
#include <linux/tc_act/tc_gact.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>

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

int initialized = -1;
static char device_name[256];
static char ingress_qdisc_parent[256];
static char egress_qdisc_parent[256];
static struct sockaddr_in my_ip;
static uint8_t my_mac[6];

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


int get_tc_classid(__u32 *h, const char *str)
{
	__u32 maj, min;
	char *p;

	maj = TC_H_ROOT;
	if (strcmp(str, "root") == 0)
		goto ok;
	maj = TC_H_UNSPEC;
	if (strcmp(str, "none") == 0)
		goto ok;
	maj = strtoul(str, &p, 16);
	if (p == str) {
		maj = 0;
		if (*p != ':')
			return -1;
	}
	if (*p == ':') {
		if (maj >= (1<<16))
			return -1;
		maj <<= 16;
		str = p+1;
		min = strtoul(str, &p, 16);
		if (*p != 0)
			return -1;
		if (min >= (1<<16))
			return -1;
		maj |= min;
	} else if (*p != 0)
		return -1;

ok:
	*h = maj;
	return 0;
}

static int pack_key(struct m_pedit_sel *_sel, struct m_pedit_key *tkey)
{
	struct tc_pedit_sel *sel = &_sel->sel;
	struct m_pedit_key_ex *keys_ex = _sel->keys_ex;
	int hwm = sel->nkeys;

	if (hwm >= MAX_OFFS)
		return -1;

	if (tkey->off % 4) {
		fprintf(stderr, "offsets MUST be in 32 bit boundaries\n");
		return -1;
	}

	sel->keys[hwm].val = tkey->val;
	sel->keys[hwm].mask = tkey->mask;
	sel->keys[hwm].off = tkey->off;
	sel->keys[hwm].at = tkey->at;
	sel->keys[hwm].offmask = tkey->offmask;
	sel->keys[hwm].shift = tkey->shift;

	if (_sel->extended) {
		keys_ex[hwm].htype = tkey->htype;
		keys_ex[hwm].cmd = tkey->cmd;
	} else {
		if (tkey->htype != TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK ||
		    tkey->cmd != TCA_PEDIT_KEY_EX_CMD_SET) {
			fprintf(stderr,
				"Munge parameters not supported. Use 'pedit ex munge ...'.\n");
			return -1;
		}
	}

	sel->nkeys++;
	return 0;
}

static int pack_key16(__u32 retain, struct m_pedit_sel *sel,
		      struct m_pedit_key *tkey)
{
	int ind, stride;
	__u32 m[4] = { 0x0000FFFF, 0xFF0000FF, 0xFFFF0000 };

	if (tkey->val > 0xFFFF || tkey->mask > 0xFFFF) {
		fprintf(stderr, "pack_key16 bad value\n");
		return -1;
	}

	ind = tkey->off & 3;

	if (ind == 3) {
		fprintf(stderr, "pack_key16 bad index value %d\n", ind);
		return -1;
	}

	stride = 8 * (2 - ind);
	tkey->val = htonl((tkey->val & retain) << stride);
	tkey->mask = htonl(((tkey->mask | ~retain) << stride) | m[ind]);

	tkey->off &= ~3;

	return pack_key(sel, tkey);
}


static int pack_key32(__u32 retain, struct m_pedit_sel *sel,
		      struct m_pedit_key *tkey)
{
	if (tkey->off > (tkey->off & ~3)) {
		fprintf(stderr,
			"pack_key32: 32 bit offsets must begin in 32bit boundaries\n");
		return -1;
	}

	tkey->val = htonl(tkey->val & retain);
	tkey->mask = htonl(tkey->mask | ~retain);
	return pack_key(sel, tkey);
}


static int pack_mac(struct m_pedit_sel *sel, struct m_pedit_key *tkey,
		    __u8 *mac)
{
	int ret = 0;

	if (!(tkey->off & 0x3)) {
		tkey->mask = 0;
		tkey->val = ntohl(*((__u32 *)mac));
		ret |= pack_key32(~0, sel, tkey);

		tkey->off += 4;
		tkey->mask = 0;
		tkey->val = ntohs(*((__u16 *)&mac[4]));
		ret |= pack_key16(~0, sel, tkey);
	} else if (!(tkey->off & 0x1)) {
		tkey->mask = 0;
		tkey->val = ntohs(*((__u16 *)mac));
		ret |= pack_key16(~0, sel, tkey);

		tkey->off += 4;
		tkey->mask = 0;
		tkey->val = ntohl(*((__u32 *)(mac + 2)));
		ret |= pack_key32(~0, sel, tkey);
	} else {
		fprintf(stderr,
			"pack_mac: mac offsets must begin in 32bit or 16bit boundaries\n");
		return -1;
	}

	return ret;
}

static int pedit_keys_ex_addattr(struct m_pedit_sel *sel, struct nlmsghdr *n)
{
	struct m_pedit_key_ex *k = sel->keys_ex;
	struct rtattr *keys_start;
	int i;

	if (!sel->extended)
		return 0;

	keys_start = addattr_nest(n, MAX_MSG, TCA_PEDIT_KEYS_EX | NLA_F_NESTED);

	for (i = 0; i < sel->sel.nkeys; i++) {
		struct rtattr *key_start;

		key_start = addattr_nest(n, MAX_MSG,
					 TCA_PEDIT_KEY_EX | NLA_F_NESTED);

	fprintf(stderr, "pedit_keys_ex_addrattr: sel->sel.nkeys=%d k->htype=%d k->cmd=%d\n", sel->sel.nkeys, k->htype, k->cmd);
		if (addattr16(n, MAX_MSG, TCA_PEDIT_KEY_EX_HTYPE, k->htype) ||
		    addattr16(n, MAX_MSG, TCA_PEDIT_KEY_EX_CMD, k->cmd)) {
			return -1;
		}

		addattr_nest_end(n, key_start);

		k++;
	}

	addattr_nest_end(n, keys_start);

	return 0;
}

/* http://docs.ros.org/en/diamondback/api/wpa_supplicant/html/common_8c_source.html */
static int hex2num(char c)
{
        if (c >= '0' && c <= '9')
                return c - '0';
        if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
        return -1;
}

static int hwaddr_aton(const char *txt, __u8 *addr)
{
        int i;

        for (i = 0; i < 6; i++) {
        int a, b;

        a = hex2num(*txt++);
        if (a < 0)
                return -1;
        b = hex2num(*txt++);
        if (b < 0)
                return -1;
        *addr++ = (a << 4) | b;
        if (i < 5 && *txt++ != ':')
                return -1;
        }

        return 0;
}

int add_filter(const uint32_t src_ip, const uint8_t *src_mac, const uint32_t dst_ip, const uint8_t *dst_mac, const uint16_t sport, const uint16_t dport, struct nlmsghdr *n)
{
        __u32 prio = 0;
        int ret;
        struct rtattr *tail;

	 __u8 ip_proto = IPPROTO_TCP;
	struct tcmsg *t = NLMSG_DATA(n);
	__be16 tc_proto = TC_H_MIN(t->tcm_info);
	fprintf(stderr, "TC_H_MIN(t->tcm_info): %u\n", TC_H_MIN(t->tcm_info));

	addattr_l(n, MAX_MSG, TCA_OPTIONS, NULL, 0);
        __u32 flags = 0;
	/* if traffic is outbound */
	if (src_ip == my_ip.sin_addr.s_addr) {
                flags |= TCA_CLS_FLAGS_SKIP_HW;
	}
        else {
                flags |= TCA_CLS_FLAGS_SKIP_SW;
        }

	ret = addattr32(n, MAX_MSG, TCA_FLOWER_FLAGS, flags);
	ret = addattr16(n, MAX_MSG, TCA_FLOWER_KEY_ETH_TYPE, tc_proto);
	ret = addattr8(n, MAX_MSG, TCA_FLOWER_KEY_IP_PROTO, ip_proto);

	uint8_t mask[6];
	memset(mask, 0xff, ETH_ALEN);

	addattr_l(n, MAX_MSG, TCA_FLOWER_KEY_ETH_DST, dst_mac, sizeof(uint8_t) * 6);
	addattr_l(n, MAX_MSG, TCA_FLOWER_KEY_ETH_DST_MASK, mask, sizeof(mask));

	addattr_l(n, MAX_MSG, TCA_FLOWER_KEY_ETH_SRC, src_mac, sizeof(uint8_t) * 6);
	addattr_l(n, MAX_MSG, TCA_FLOWER_KEY_ETH_SRC_MASK, mask, sizeof(mask));

	addattr_l(n, MAX_MSG, TCA_FLOWER_KEY_IPV4_DST, &dst_ip, sizeof(uint32_t));
	addattr_l(n, MAX_MSG, TCA_FLOWER_KEY_IPV4_SRC, &src_ip, sizeof(uint32_t));

	addattr16(n, MAX_MSG, TCA_FLOWER_KEY_TCP_SRC, sport);
	addattr16(n, MAX_MSG, TCA_FLOWER_KEY_TCP_DST, dport);

	return 0;
}

int add_pedit(const uint32_t new_src_ip, const uint8_t *new_src_mac, const uint32_t new_dst_ip, const uint8_t *new_dst_mac,
		const uint16_t new_sport, const uint16_t new_dport, const bool block, struct nlmsghdr *n)
{
	/* add action */
	struct rtattr *tail4;
	struct rtattr *tail3;
	struct rtattr *tail2 = addattr_nest(n, MAX_MSG, TCA_FLOWER_ACT);
	int prio = 0;

	if (!block) {
		tail3 = addattr_nest(n, MAX_MSG, ++prio);
		addattr_l(n, MAX_MSG, TCA_ACT_KIND, "pedit", strlen("pedit") + 1);
	
		struct m_pedit_key tkey;
		struct m_pedit_sel sel;
		__u32 mask[4] = { 0 };
		__u32 val[4] = { 0 };
		__u32 *m = &mask[0];
		__u32 *v = &val[0];
		__u32 retain;
	
		sel.extended = true;
		tkey.htype = TCA_PEDIT_KEY_EX_HDR_TYPE_ETH;
		tkey.off = 6;
		tkey.cmd = TCA_PEDIT_KEY_EX_CMD_SET;
		int res = pack_mac(&sel, &tkey, new_src_mac);
	
		tkey.off = 0;
		tkey.cmd = TCA_PEDIT_KEY_EX_CMD_SET;
		res = pack_mac(&sel, &tkey, new_dst_mac);
	
		bzero(val, sizeof(val));
		bzero(&tkey, sizeof(tkey));
		retain = RU32;
		tkey.off = 12;
		tkey.htype = TCA_PEDIT_KEY_EX_HDR_TYPE_IP4;
	
		tkey.val = new_src_ip;
		tkey.mask = *m;
		tkey.val = ntohl(tkey.val);
		res = pack_key32(retain, &sel, &tkey);
	
		bzero(val, sizeof(val));
		bzero(&tkey, sizeof(tkey));
		retain = RU32;
		tkey.off = 16;
		tkey.htype = TCA_PEDIT_KEY_EX_HDR_TYPE_IP4;
	
		tkey.val = new_dst_ip;
		tkey.mask = *m;
		tkey.val = ntohl(tkey.val);
		res = pack_key32(retain, &sel, &tkey);
	
		bzero(val, sizeof(val));
		bzero(&tkey, sizeof(tkey));
		tkey.off = 0;
		tkey.htype = TCA_PEDIT_KEY_EX_HDR_TYPE_TCP;
		*val = ntohs(new_sport);
		retain = RU16;
		tkey.val = *v;
		tkey.mask = *m;
		res = pack_key16(retain, &sel, &tkey);
	
		bzero(val, sizeof(val));
		bzero(&tkey, sizeof(tkey));
		tkey.off = 2;
		tkey.htype = TCA_PEDIT_KEY_EX_HDR_TYPE_TCP;
		*val = ntohs(new_dport);
		retain = RU16;
		tkey.val = *v;
		tkey.mask = *m;
		res = pack_key16(retain, &sel, &tkey);
	
		tail4 = addattr_nest(n, MAX_MSG, TCA_ACT_OPTIONS | NLA_F_NESTED);
		addattr_l(n, MAX_MSG, TCA_PEDIT_PARMS_EX, &sel, sizeof(sel.sel) + sel.sel.nkeys * sizeof(struct tc_pedit_key));
		pedit_keys_ex_addattr(&sel, n);
		addattr_nest_end(n, tail4);
	
		addattr_nest_end(n, tail3);
	
		/* add csum edit */
		tail3 = addattr_nest(n, MAX_MSG, ++prio);
		addattr_l(n, MAX_MSG, TCA_ACT_KIND, "csum", strlen("csum") + 1);
	
		struct tc_csum csum_sel = {};
		csum_sel.update_flags |= TCA_CSUM_UPDATE_FLAG_IPV4HDR;
		csum_sel.update_flags |= TCA_CSUM_UPDATE_FLAG_TCP;
	
		tail4 = addattr_nest(n, MAX_MSG, TCA_ACT_OPTIONS | NLA_F_NESTED);
		addattr_l(n, MAX_MSG, TCA_CSUM_PARMS, &csum_sel, sizeof(csum_sel));
		addattr_nest_end(n, tail4);
	
		addattr_nest_end(n, tail3);
		/* end csum edit */
	
		/* add mirred edit */
		tail3 = addattr_nest(n, MAX_MSG, ++prio);
		addattr_l(n, MAX_MSG, TCA_ACT_KIND, "mirred", strlen("mirred") + 1);
	
		struct tc_mirred mirred_p = {};
		mirred_p.eaction = TCA_EGRESS_REDIR;
		mirred_p.action = TC_ACT_STOLEN;
		mirred_p.ifindex = if_nametoindex(device_name);
	
		tail4 = addattr_nest(n, MAX_MSG, TCA_ACT_OPTIONS | NLA_F_NESTED);
		addattr_l(n, MAX_MSG, TCA_MIRRED_PARMS, &mirred_p, sizeof(mirred_p));
		addattr_nest_end(n, tail4);
	
		addattr_nest_end(n, tail3);
		/* end mirred edit */
	}
	else {
		/* add drop */
		struct tc_gact gact_p = {0};
		gact_p.action = TC_ACT_SHOT;

		tail3 = addattr_nest(n, MAX_MSG, ++prio);
		addattr_l(n, MAX_MSG, TCA_ACT_KIND, "gact", strlen("gact") + 1);

		tail4 = addattr_nest(n, MAX_MSG, TCA_ACT_OPTIONS | NLA_F_NESTED);
		addattr_l(n, MAX_MSG, TCA_GACT_PARMS, &gact_p, sizeof(gact_p));
		addattr_nest_end(n, tail4);
 
		addattr_nest_end(n, tail3);
		/* end add drop */
	}

	addattr_nest_end(n, tail2);
	/* end action */

	return 0;
}

int remove_redirection(const uint32_t src_ip, const uint8_t *src_mac, const uint32_t dst_ip, const uint8_t *dst_mac, uint16_t sport, uint16_t dport)
{
	struct rtnl_handle rth;

	// RTM_NEWTFILTER, NLM_F_EXCL|NLM_F_CREATE
	struct {
                struct nlmsghdr n;
                struct tcmsg            t;
                char                    buf[MAX_MSG];
        } req = {
                .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg)),
                .n.nlmsg_flags = NLM_F_REQUEST | NLM_F_EXCL|NLM_F_CREATE,
                .n.nlmsg_type = RTM_DELTFILTER,
                .t.tcm_family = AF_UNSPEC,
        };

	int ret;
        struct rtattr *tail;
        __u32 prio = 0;
	char addr[256];

	ret = rtnl_open(&rth, 0);
	assert(ret == 0);

	/* check if flow is egress or ingress */
	if (src_ip == my_ip.sin_addr.s_addr) {
		get_tc_classid(&req.t.tcm_parent, egress_qdisc_parent);
	}
	else {
		get_tc_classid(&req.t.tcm_parent, ingress_qdisc_parent);
	}
	fprintf(stderr, "req.t.tcm_parent: %lu\n", (unsigned long)req.t.tcm_parent);

	// prior
	prio = 1;
	fprintf(stderr, "prio: %lu\n", (unsigned long)prio);

	// flower
	sprintf(addr, "%s", "flower");

	req.t.tcm_info = TC_H_MAKE(prio<<16, 8/*IPv4*/);
	fprintf(stderr, "req.t.tcm_info: %lu\n", (unsigned long)req.t.tcm_info);
	addattr_l(&req.n, sizeof(req), TCA_KIND, addr, strlen(addr)+1);
	req.t.tcm_ifindex = if_nametoindex(device_name);
	fprintf(stderr, "req.t.tcm_ifindex: %lu\n", (unsigned long)req.t.tcm_ifindex);

	tail = (struct rtattr *) (((void *)&req.n) + NLMSG_ALIGN((&req.n)->nlmsg_len));
	add_filter(src_ip, src_mac, dst_ip, dst_mac, sport, dport, &req.n);
	tail->rta_len = (((void *)&req.n)+(&req.n)->nlmsg_len) - (void *)tail;

	if (rtnl_talk(&rth, &req.n, NULL) < 0) {
		fprintf(stderr, "We have an error talking to the kernel\n");
		return 2;
	}

	rtnl_close(&rth);

	return 0;

}

int remove_redirection_str(const char *src_ip_str, const char *src_mac_str, const char *dst_ip_str, const char *dst_mac_str, uint16_t sport, uint16_t dport)
{
	uint32_t src_ip;
	uint32_t dst_ip;

	uint8_t src_mac[6];
	uint8_t dst_mac[6];

	hwaddr_aton(src_mac_str, src_mac);
	hwaddr_aton(dst_mac_str, dst_mac);

	inet_pton(AF_INET, src_ip_str, &src_ip);
	inet_pton(AF_INET, dst_ip_str, &dst_ip);

	return remove_redirection(src_ip, src_mac, dst_ip, dst_mac, htons(sport), htons(dport));
}

int apply_redirection(const uint32_t src_ip, const uint8_t *src_mac, const uint32_t dst_ip, const uint8_t *dst_mac, uint16_t sport, uint16_t dport,
			const uint32_t new_src_ip, const uint8_t *new_src_mac, const uint32_t new_dst_ip, const uint8_t *new_dst_mac, uint16_t new_sport, uint16_t new_dport, bool block)
{
	struct rtnl_handle rth;

	// RTM_NEWTFILTER, NLM_F_EXCL|NLM_F_CREATE
	struct {
                struct nlmsghdr n;
                struct tcmsg            t;
                char                    buf[MAX_MSG];
        } req = {
                .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg)),
                .n.nlmsg_flags = NLM_F_REQUEST | NLM_F_EXCL|NLM_F_CREATE,
                .n.nlmsg_type = RTM_NEWTFILTER,
                .t.tcm_family = AF_UNSPEC,
        };

	int ret;
        struct rtattr *tail;
        __u32 prio = 0;
	char addr[256];

	ret = rtnl_open(&rth, 0);
	assert(ret == 0);

	/* check if flow is egress or ingress */
	if (src_ip == my_ip.sin_addr.s_addr) {
		get_tc_classid(&req.t.tcm_parent, egress_qdisc_parent);
	}
	else {
		get_tc_classid(&req.t.tcm_parent, ingress_qdisc_parent);
	}
	fprintf(stderr, "req.t.tcm_parent: %lu\n", (unsigned long)req.t.tcm_parent);

	// prior
	prio = 1;
	fprintf(stderr, "prio: %lu\n", (unsigned long)prio);

	// flower
	sprintf(addr, "%s", "flower");

	req.t.tcm_info = TC_H_MAKE(prio<<16, 8/*IPv4*/);
	fprintf(stderr, "req.t.tcm_info: %lu\n", (unsigned long)req.t.tcm_info);
	addattr_l(&req.n, sizeof(req), TCA_KIND, addr, strlen(addr)+1);
	req.t.tcm_ifindex = if_nametoindex(device_name);
	fprintf(stderr, "req.t.tcm_ifindex: %lu\n", (unsigned long)req.t.tcm_ifindex);

	tail = (struct rtattr *) (((void *)&req.n) + NLMSG_ALIGN((&req.n)->nlmsg_len));

	add_filter(src_ip, src_mac, dst_ip, dst_mac, sport, dport, &req.n);
	prio = 0;
	add_pedit(new_src_ip, new_src_mac, new_dst_ip, new_dst_mac, new_sport, new_dport, block, &req.n);

	tail->rta_len = (((void *)&req.n)+(&req.n)->nlmsg_len) - (void *)tail;

	req.n.nlmsg_type = RTM_DELTFILTER;
	ret = rtnl_talk(&rth, &req.n, NULL);
	req.n.nlmsg_type = RTM_NEWTFILTER;
	if (rtnl_talk(&rth, &req.n, NULL) < 0) {
		fprintf(stderr, "We have an error talking to the kernel\n");
		return 2;
	}

	rtnl_close(&rth);

	return 0;
}

int apply_redirection_str(const char *src_ip_str, const char *src_mac_str, const char *dst_ip_str, const char *dst_mac_str, uint16_t sport, uint16_t dport,
			const char *new_src_ip_str, const char *new_src_mac_str, const char *new_dst_ip_str, const char *new_dst_mac_str, uint16_t new_sport, uint16_t new_dport, bool block)
{
	uint32_t src_ip;
	uint32_t dst_ip;

	uint8_t src_mac[6];
	uint8_t dst_mac[6];

	uint32_t new_src_ip;
	uint32_t new_dst_ip;

	uint8_t new_src_mac[6];
	uint8_t new_dst_mac[6];

	hwaddr_aton(src_mac_str, src_mac);
	hwaddr_aton(dst_mac_str, dst_mac);
	hwaddr_aton(new_src_mac_str, new_src_mac);
	hwaddr_aton(new_dst_mac_str, new_dst_mac);

	inet_pton(AF_INET, src_ip_str, &src_ip);
	inet_pton(AF_INET, dst_ip_str, &dst_ip);
	inet_pton(AF_INET, new_src_ip_str, &new_src_ip);
	inet_pton(AF_INET, new_dst_ip_str, &new_dst_ip);

	return apply_redirection(src_ip, src_mac, dst_ip, dst_mac, htons(sport), htons(dport), new_src_ip, new_src_mac, new_dst_ip, new_dst_mac, htons(new_sport), htons(new_dport), block);
}

int init_forward(const char *interface_name, const char *ingress_qdisc, const char *egress_qdisc)
{
	if (initialized >= 0) {
		fprintf(stderr, "Warning: libforward already initialized\n");
		return -1;
	}

	strcpy(device_name, interface_name);
	strcpy(ingress_qdisc_parent, ingress_qdisc);
	strcpy(egress_qdisc_parent, egress_qdisc);

	struct ifreq interface_request;
	memcpy(interface_request.ifr_name, interface_name, strlen(interface_name)+1);
	interface_request.ifr_name[strlen(interface_name)] = 0;

	int fd;

	if ((fd = socket(AF_INET,SOCK_DGRAM, 0)) == -1) {
		fprintf(stderr, "Error: libforward fail to open simple socket: %s\n", strerror(errno));
		initialized = -1;
		return -1;
	}

	if (ioctl(fd, SIOCGIFHWADDR, &interface_request) == -1) {
		fprintf(stderr, "Error: libforward fail to request interface: %s\n", strerror(errno));
		initialized = -1;
		close(fd);
		return -1;
	}
	memcpy(my_mac, interface_request.ifr_hwaddr.sa_data, IFHWADDRLEN);

	if (ioctl(fd, SIOCGIFADDR, &interface_request) == -1) {
		fprintf(stderr, "Error: libforward fail to request interface: %s\n", strerror(errno));
		initialized = -1;
		close(fd);
		return -1;
	}
	memcpy(&my_ip, &interface_request.ifr_addr, sizeof(struct sockaddr_in));

        close(fd);
#ifdef DEV
	fprintf(stderr, "INFO: libforward: device: %s; ingress qdisc ID: %s; egress qdisc ID: %s; MAC: %02x:%02x:%02x:%02x:%02x:%02x; IP: %s\n",
		device_name, ingress_qdisc_parent, egress_qdisc_parent, my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5], inet_ntoa(my_ip.sin_addr));
#endif
	initialized = 1;
	return 1;
}
