#ifndef __FORWARD_H__
#define __FORWARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

int init_forward(const char *interface_name, const char *ingress_qdisc_parent, const char *egress_qdisc_parent);

int remove_redirection_str(const char *src_ip_str, const char *src_mac_str,
			   const char *dst_ip_str, const char *dst_mac_str,
			   const uint16_t sport, const uint16_t dport);
int remove_redirection(uint32_t src_ip,  uint8_t *src_mac, uint32_t dst_ip, uint8_t *dst_mac, uint16_t sport, uint16_t dport);

int apply_redirection_str(const char *src_ip_str, const char *src_mac_str, const char *dst_ip_str, const char *dst_mac_str,
			  const uint16_t sport_str, const uint16_t dport_str,
			  const char *new_src_ip_str, const char *new_src_mac_str, const char *new_dst_ip_str, const char *new_dst_mac_str,
			  const uint16_t new_sport_str, const uint16_t new_dport_str,
			  bool block);
int apply_redirection(uint32_t src_ip,  uint8_t *src_mac, uint32_t dst_ip, uint8_t *dst_mac,
		      uint16_t sport, uint16_t dport,
		      uint32_t new_src_ip, uint8_t *new_src_mac, uint32_t new_dst_ip, uint8_t *new_dst_mac,
		      uint16_t new_sport, uint16_t new_dport,
		      bool block);


#ifdef __cplusplus
}
#endif

#endif
