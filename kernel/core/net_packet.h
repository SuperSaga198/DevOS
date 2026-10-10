#ifndef DEVOS_NET_PACKET_H
#define DEVOS_NET_PACKET_H

#include <stddef.h>

enum net_packet_result {
    NET_PACKET_DROP = -1,
    NET_PACKET_IGNORED = 0,
    NET_PACKET_REPLY = 1,
};

enum net_packet_result net_ipv4_icmp_echo_reply(
    const void *frame, size_t frame_size,
    void *reply, size_t reply_capacity, size_t *reply_size);

#endif
