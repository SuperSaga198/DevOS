#include "net_packet.h"

#include <stdint.h>

#define ETHERNET_HEADER_SIZE 14U
#define ETHERNET_TYPE_IPV4 0x0800U
#define IPV4_MIN_HEADER_SIZE 20U
#define IPV4_VERSION 4U
#define IPV4_PROTOCOL_ICMP 1U
#define IPV4_FLAG_MORE_FRAGMENTS 0x2000U
#define IPV4_FRAGMENT_OFFSET 0x1fffU
#define IPV4_DEFAULT_TTL 64U
#define ICMP_HEADER_SIZE 8U
#define ICMP_ECHO_REPLY 0U
#define ICMP_ECHO_REQUEST 8U

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
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
