#ifndef DEVOS_NET_PACKET_H
#define DEVOS_NET_PACKET_H

#include <stddef.h>
#include <stdint.h>

struct net_tcp_echo_state {
    uint32_t remote_address;
    uint32_t remote_next_sequence;
    uint32_t local_next_sequence;
    uint16_t remote_port;
    uint8_t phase;
};

enum net_packet_result {
    NET_PACKET_DROP = -1,
    NET_PACKET_IGNORED = 0,
    NET_PACKET_REPLY = 1,
};

enum net_packet_result net_ipv4_icmp_echo_reply(
    const void *frame, size_t frame_size,
    void *reply, size_t reply_capacity, size_t *reply_size);
enum net_packet_result net_ipv4_udp_echo_reply(
    const void *frame, size_t frame_size, uint32_t local_address,
    uint16_t local_port, void *reply, size_t reply_capacity,
    size_t *reply_size);
enum net_packet_result net_ipv4_tcp_echo_step(
    const void *frame, size_t frame_size, uint32_t local_address,
    uint16_t local_port, struct net_tcp_echo_state *state,
    void *reply, size_t reply_capacity, size_t *reply_size);

#endif
