#include "net_packet.h"

#include <stdint.h>

#define ETHERNET_HEADER_SIZE 14U
#define ETHERNET_TYPE_IPV4 0x0800U
#define IPV4_MIN_HEADER_SIZE 20U
#define IPV4_VERSION 4U
#define IPV4_PROTOCOL_ICMP 1U
#define IPV4_PROTOCOL_UDP 17U
#define IPV4_PROTOCOL_TCP 6U
#define IPV4_FLAG_MORE_FRAGMENTS 0x2000U
#define IPV4_FRAGMENT_OFFSET 0x1fffU
#define IPV4_DEFAULT_TTL 64U
#define ICMP_HEADER_SIZE 8U
#define ICMP_ECHO_REPLY 0U
#define ICMP_ECHO_REQUEST 8U
#define UDP_HEADER_SIZE 8U
#define TCP_MIN_HEADER_SIZE 20U
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           bytes[3];
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t checksum_add(uint32_t sum, const uint8_t *bytes, size_t size)
{
    while (size >= 2) {
        sum += read_be16(bytes);
        bytes += 2;
        size -= 2;
    }
    if (size != 0) {
        sum += (uint16_t)((uint16_t)bytes[0] << 8);
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return sum;
}

static uint16_t checksum_finish(uint32_t sum)
{
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static int checksum_valid(const uint8_t *bytes, size_t size)
{
    return checksum_finish(checksum_add(0, bytes, size)) == 0;
}

static uint16_t checksum_value(const uint8_t *bytes, size_t size)
{
    return checksum_finish(checksum_add(0, bytes, size));
}

static uint32_t checksum_add_pseudo(uint32_t sum, const uint8_t *ip,
                                    const uint8_t *udp, size_t udp_size)
{
    sum = checksum_add(sum, ip + 12, 8);
    sum += IPV4_PROTOCOL_UDP;
    sum += (uint16_t)udp_size;
    sum = checksum_add(sum, udp, udp_size);
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return sum;
}

static uint32_t checksum_add_tcp_pseudo(uint32_t sum, const uint8_t *ip,
                                        const uint8_t *tcp, size_t tcp_size)
{
    sum = checksum_add(sum, ip + 12, 8);
    sum += IPV4_PROTOCOL_TCP;
    sum += (uint16_t)tcp_size;
    sum = checksum_add(sum, tcp, tcp_size);
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return sum;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t size)
{
    const uintptr_t destination_address = (uintptr_t)destination;
    const uintptr_t source_address = (uintptr_t)source;
    if (destination_address > source_address &&
        destination_address - source_address < size) {
        for (size_t index = size; index != 0; --index) {
            destination[index - 1] = source[index - 1];
        }
    } else {
        for (size_t index = 0; index < size; ++index) {
            destination[index] = source[index];
        }
    }
}

static void swap_bytes(uint8_t *left, uint8_t *right, size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        const uint8_t temporary = left[index];
        left[index] = right[index];
        right[index] = temporary;
    }
}

enum net_packet_result net_ipv4_icmp_echo_reply(
    const void *frame, size_t frame_size,
    void *reply, size_t reply_capacity, size_t *reply_size)
{
    if (frame == NULL || reply == NULL || reply_size == NULL) {
        return NET_PACKET_DROP;
    }

    if (frame_size < ETHERNET_HEADER_SIZE) {
        return NET_PACKET_DROP;
    }

    const uint8_t *input = frame;
    if (read_be16(input + 12) != ETHERNET_TYPE_IPV4) {
        return NET_PACKET_IGNORED;
    }
    if (frame_size - ETHERNET_HEADER_SIZE < IPV4_MIN_HEADER_SIZE) {
        return NET_PACKET_DROP;
    }

    const uint8_t *ip = input + ETHERNET_HEADER_SIZE;
    const uint8_t version = ip[0] >> 4;
    const size_t ip_header_size = (size_t)(ip[0] & 0x0fU) * 4U;
    const size_t ip_total_size = read_be16(ip + 2);
    if (version != IPV4_VERSION || ip_header_size < IPV4_MIN_HEADER_SIZE ||
        ip_header_size > frame_size - ETHERNET_HEADER_SIZE ||
        ip_total_size < ip_header_size ||
        ip_total_size > frame_size - ETHERNET_HEADER_SIZE ||
        !checksum_valid(ip, ip_header_size)) {
        return NET_PACKET_DROP;
    }
    if (ip[9] != IPV4_PROTOCOL_ICMP) {
        return NET_PACKET_IGNORED;
    }

    const uint16_t fragment = read_be16(ip + 6);
    if ((fragment & (IPV4_FLAG_MORE_FRAGMENTS | IPV4_FRAGMENT_OFFSET)) != 0) {
        return NET_PACKET_IGNORED;
    }
    if (ip_total_size - ip_header_size < ICMP_HEADER_SIZE) {
        return NET_PACKET_DROP;
    }

    const uint8_t *icmp = ip + ip_header_size;
    const size_t icmp_size = ip_total_size - ip_header_size;
    if (icmp[0] != ICMP_ECHO_REQUEST || icmp[1] != 0) {
        return NET_PACKET_IGNORED;
    }
    if (!checksum_valid(icmp, icmp_size)) {
        return NET_PACKET_DROP;
    }

    const size_t output_size = ETHERNET_HEADER_SIZE + ip_total_size;
    if (reply_capacity < output_size) {
        return NET_PACKET_DROP;
    }
    copy_bytes(reply, input, output_size);

    uint8_t *output = reply;
    uint8_t *output_ip = output + ETHERNET_HEADER_SIZE;
    uint8_t *output_icmp = output_ip + ip_header_size;
    swap_bytes(output, output + 6, 6);
    swap_bytes(output_ip + 12, output_ip + 16, 4);
    output_ip[8] = IPV4_DEFAULT_TTL;
    write_be16(output_ip + 10, 0);
    write_be16(output_ip + 10,
               checksum_value(output_ip, ip_header_size));
    output_icmp[0] = ICMP_ECHO_REPLY;
    write_be16(output_icmp + 2, 0);
    write_be16(output_icmp + 2, checksum_value(output_icmp, icmp_size));
    *reply_size = output_size;
    return NET_PACKET_REPLY;
}

enum net_packet_result net_ipv4_udp_echo_reply(
    const void *frame, size_t frame_size, uint32_t local_address,
    uint16_t local_port, void *reply, size_t reply_capacity,
    size_t *reply_size)
{
    if (frame == NULL || reply == NULL || reply_size == NULL ||
        frame_size < ETHERNET_HEADER_SIZE) {
        return NET_PACKET_DROP;
    }

    const uint8_t *input = frame;
    if (read_be16(input + 12) != ETHERNET_TYPE_IPV4) {
        return NET_PACKET_IGNORED;
    }
    if (frame_size - ETHERNET_HEADER_SIZE < IPV4_MIN_HEADER_SIZE) {
        return NET_PACKET_DROP;
    }
    const uint8_t *ip = input + ETHERNET_HEADER_SIZE;
    const size_t ip_header_size = (size_t)(ip[0] & 0x0fU) * 4U;
    const size_t ip_total_size = read_be16(ip + 2);
    if ((ip[0] >> 4) != IPV4_VERSION ||
        ip_header_size < IPV4_MIN_HEADER_SIZE ||
        ip_header_size > frame_size - ETHERNET_HEADER_SIZE ||
        ip_total_size < ip_header_size + UDP_HEADER_SIZE ||
        ip_total_size > frame_size - ETHERNET_HEADER_SIZE ||
        !checksum_valid(ip, ip_header_size)) {
        return NET_PACKET_DROP;
    }
    if (ip[9] != IPV4_PROTOCOL_UDP) {
        return NET_PACKET_IGNORED;
    }
    const uint16_t fragment = read_be16(ip + 6);
    if ((fragment & (IPV4_FLAG_MORE_FRAGMENTS | IPV4_FRAGMENT_OFFSET)) != 0) {
        return NET_PACKET_IGNORED;
    }
    if (read_be32(ip + 16) != local_address) {
        return NET_PACKET_IGNORED;
    }

    const uint8_t *udp = ip + ip_header_size;
    const size_t udp_size = read_be16(udp + 4);
    if (udp_size < UDP_HEADER_SIZE ||
        udp_size != ip_total_size - ip_header_size) {
        return NET_PACKET_DROP;
    }
    if (read_be16(udp + 2) != local_port) {
        return NET_PACKET_IGNORED;
    }
    const uint16_t incoming_checksum = read_be16(udp + 6);
    if (incoming_checksum != 0 &&
        checksum_finish(checksum_add_pseudo(0, ip, udp, udp_size)) != 0) {
        return NET_PACKET_DROP;
    }

    const size_t output_size = ETHERNET_HEADER_SIZE + ip_total_size;
    if (reply_capacity < output_size) {
        return NET_PACKET_DROP;
    }
    copy_bytes(reply, input, output_size);
    uint8_t *output = reply;
    uint8_t *output_ip = output + ETHERNET_HEADER_SIZE;
    uint8_t *output_udp = output_ip + ip_header_size;
    swap_bytes(output, output + 6, 6);
    swap_bytes(output_ip + 12, output_ip + 16, 4);
    output_ip[8] = IPV4_DEFAULT_TTL;
    write_be16(output_ip + 10, 0);
    write_be16(output_ip + 10,
               checksum_value(output_ip, ip_header_size));
    swap_bytes(output_udp, output_udp + 2, 2);
    write_be16(output_udp + 6, 0);
    uint16_t response_checksum = checksum_finish(
        checksum_add_pseudo(0, output_ip, output_udp, udp_size));
    if (response_checksum == 0) {
        response_checksum = 0xffffU;
    }
    write_be16(output_udp + 6, response_checksum);
    *reply_size = output_size;
    return NET_PACKET_REPLY;
}

enum net_packet_result net_ipv4_tcp_echo_step(
    const void *frame, size_t frame_size, uint32_t local_address,
    uint16_t local_port, struct net_tcp_echo_state *state,
    void *reply, size_t reply_capacity, size_t *reply_size)
{
    if (frame == NULL || state == NULL || reply == NULL ||
        reply_size == NULL || frame_size < ETHERNET_HEADER_SIZE) {
        return NET_PACKET_DROP;
    }
    const struct net_tcp_echo_state previous_state = *state;
    const uint8_t *input = frame;
    if (read_be16(input + 12) != ETHERNET_TYPE_IPV4) {
        return NET_PACKET_IGNORED;
    }
    if (frame_size - ETHERNET_HEADER_SIZE < IPV4_MIN_HEADER_SIZE) {
        return NET_PACKET_DROP;
    }
    const uint8_t *ip = input + ETHERNET_HEADER_SIZE;
    const size_t ip_header_size = (size_t)(ip[0] & 0x0fU) * 4U;
    const size_t ip_total_size = read_be16(ip + 2);
    if ((ip[0] >> 4) != IPV4_VERSION ||
        ip_header_size < IPV4_MIN_HEADER_SIZE ||
        ip_header_size > frame_size - ETHERNET_HEADER_SIZE ||
        ip_total_size < ip_header_size + TCP_MIN_HEADER_SIZE ||
        ip_total_size > frame_size - ETHERNET_HEADER_SIZE ||
        !checksum_valid(ip, ip_header_size)) {
        return NET_PACKET_DROP;
    }
    if (ip[9] != IPV4_PROTOCOL_TCP) {
        return NET_PACKET_IGNORED;
    }
    if ((read_be16(ip + 6) &
         (IPV4_FLAG_MORE_FRAGMENTS | IPV4_FRAGMENT_OFFSET)) != 0) {
        return NET_PACKET_IGNORED;
    }
    if (read_be32(ip + 16) != local_address) {
        return NET_PACKET_IGNORED;
    }

    const uint8_t *tcp = ip + ip_header_size;
    const size_t tcp_size = ip_total_size - ip_header_size;
    const size_t tcp_header_size = (size_t)(tcp[12] >> 4) * 4U;
    if (tcp_header_size < TCP_MIN_HEADER_SIZE || tcp_header_size > tcp_size ||
        checksum_finish(checksum_add_tcp_pseudo(0, ip, tcp, tcp_size)) != 0) {
        return NET_PACKET_DROP;
    }

    const uint16_t source_port = read_be16(tcp);
    const uint16_t destination_port = read_be16(tcp + 2);
    const uint32_t source_address = read_be32(ip + 12);
    const uint32_t sequence = read_be32(tcp + 4);
    const uint32_t acknowledgement = read_be32(tcp + 8);
    const uint8_t flags = tcp[13];
    const size_t payload_size = tcp_size - tcp_header_size;
    int syn_retransmission = 0;
    if (destination_port != local_port) {
        return NET_PACKET_IGNORED;
    }

    if ((flags & TCP_FLAG_RST) != 0) {
        if (state->phase != 0 && state->remote_address == source_address &&
            state->remote_port == source_port) {
            state->phase = 0;
        }
        return NET_PACKET_IGNORED;
    }

    if (state->phase == 0) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_FIN)) !=
            TCP_FLAG_SYN || payload_size != 0) {
            return NET_PACKET_IGNORED;
        }
        state->phase = 1;
        state->remote_address = source_address;
        state->remote_port = source_port;
        state->remote_next_sequence = sequence;
        state->local_next_sequence = 0x4445564fU;
    } else if (state->remote_address != source_address ||
               state->remote_port != source_port) {
        return NET_PACKET_IGNORED;
    } else if (state->phase == 1) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
                (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            sequence + 1 == state->remote_next_sequence) {
            state->local_next_sequence = 0x4445564fU;
            syn_retransmission = 1;
        } else if ((flags & TCP_FLAG_ACK) != 0 &&
                   acknowledgement == state->local_next_sequence &&
                   sequence == state->remote_next_sequence) {
            state->phase = 2;
            return NET_PACKET_IGNORED;
        } else {
            return NET_PACKET_IGNORED;
        }
    } else if (state->phase == 3) {
        if ((flags & TCP_FLAG_ACK) != 0 &&
            acknowledgement == state->local_next_sequence) {
            state->phase = 0;
        }
        return NET_PACKET_IGNORED;
    } else if (state->phase != 2 || sequence != state->remote_next_sequence ||
               (flags & TCP_FLAG_ACK) == 0 ||
               acknowledgement != state->local_next_sequence) {
        return NET_PACKET_IGNORED;
    }

    const int syn_ack = state->phase == 1;
    const int closing = !syn_ack && (flags & TCP_FLAG_FIN) != 0;
    if (syn_ack && (flags & TCP_FLAG_SYN) == 0) {
        return NET_PACKET_IGNORED;
    }
    if (syn_ack) {
        ++state->local_next_sequence;
    }
    const uint32_t input_advance = (uint32_t)payload_size +
        (((flags & TCP_FLAG_SYN) != 0 && !syn_retransmission) ? 1u : 0u) +
        ((flags & TCP_FLAG_FIN) != 0 ? 1u : 0u);
    state->remote_next_sequence += input_advance;

    const size_t response_payload_size = syn_ack ? 0 : payload_size;
    const size_t response_tcp_size = TCP_MIN_HEADER_SIZE +
                                     response_payload_size;
    const size_t response_ip_size = IPV4_MIN_HEADER_SIZE +
                                    response_tcp_size;
    const size_t response_frame_size = ETHERNET_HEADER_SIZE +
                                       response_ip_size;
    if (response_frame_size > reply_capacity ||
        response_frame_size > 1500) {
        *state = previous_state;
        return NET_PACKET_DROP;
    }
    for (size_t index = 0; index < ETHERNET_HEADER_SIZE; ++index) {
        ((uint8_t *)reply)[index] = input[index];
    }
    uint8_t *output = reply;
    uint8_t *output_ip = output + ETHERNET_HEADER_SIZE;
    uint8_t *output_tcp = output_ip + IPV4_MIN_HEADER_SIZE;
    swap_bytes(output, output + 6, 6);
    output_ip[0] = 0x45;
    output_ip[1] = 0;
    write_be16(output_ip + 2, (uint16_t)response_ip_size);
    write_be16(output_ip + 4, 0);
    write_be16(output_ip + 6, 0x4000);
    output_ip[8] = IPV4_DEFAULT_TTL;
    output_ip[9] = IPV4_PROTOCOL_TCP;
    write_be32(output_ip + 12, local_address);
    write_be32(output_ip + 16, source_address);
    write_be16(output_ip + 10, 0);
    write_be16(output_ip + 10,
               checksum_value(output_ip, IPV4_MIN_HEADER_SIZE));

    write_be16(output_tcp, local_port);
    write_be16(output_tcp + 2, source_port);
    const uint32_t response_sequence =
        syn_ack ? state->local_next_sequence - 1 :
                  state->local_next_sequence;
    write_be32(output_tcp + 4, response_sequence);
    write_be32(output_tcp + 8, state->remote_next_sequence);
    output_tcp[12] = 0x50;
    output_tcp[13] = syn_ack ? TCP_FLAG_SYN | TCP_FLAG_ACK :
        closing ? TCP_FLAG_FIN | TCP_FLAG_ACK :
        response_payload_size != 0 ? TCP_FLAG_PSH | TCP_FLAG_ACK :
                                     TCP_FLAG_ACK;
    write_be16(output_tcp + 14, 4096);
    write_be16(output_tcp + 16, 0);
    write_be16(output_tcp + 18, 0);
    if (response_payload_size != 0) {
        copy_bytes(output_tcp + TCP_MIN_HEADER_SIZE,
                   tcp + tcp_header_size, response_payload_size);
    }
    write_be16(output_tcp + 16, checksum_finish(
        checksum_add_tcp_pseudo(0, output_ip, output_tcp,
                                response_tcp_size)));

    if (response_payload_size != 0) {
        state->local_next_sequence += (uint32_t)response_payload_size;
    }
    if (closing) {
        ++state->local_next_sequence;
        state->phase = 3;
    }
    *reply_size = response_frame_size;
    return NET_PACKET_REPLY;
}
