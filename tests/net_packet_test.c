#include "../kernel/core/net_packet.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_FRAME_CAPACITY 128
#define ETHERNET_SIZE 14
#define IPV4_SIZE 24
#define ICMP_SIZE 16
#define IPV4_TOTAL_SIZE (IPV4_SIZE + ICMP_SIZE)
#define FRAME_SIZE (ETHERNET_SIZE + IPV4_TOTAL_SIZE)

static uint8_t input[TEST_FRAME_CAPACITY];
static uint8_t output[TEST_FRAME_CAPACITY];

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static uint16_t checksum(const uint8_t *bytes, size_t size)
{
    uint32_t sum = 0;
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
    return (uint16_t)~sum;
}

static int valid_checksum(const uint8_t *bytes, size_t size)
{
    return checksum(bytes, size) == 0;
}

static void set_checksum(uint8_t *bytes, size_t size, size_t checksum_offset)
{
    write_be16(bytes + checksum_offset, 0);
    write_be16(bytes + checksum_offset, checksum(bytes, size));
}

static void make_echo_request(void)
{
    for (size_t index = 0; index < sizeof(input); ++index) {
        input[index] = 0;
    }

    for (size_t index = 0; index < 6; ++index) {
        input[index] = (uint8_t)(0x10 + index);
        input[6 + index] = (uint8_t)(0x20 + index);
    }
    write_be16(input + 12, 0x0800);

    uint8_t *ip = input + ETHERNET_SIZE;
    ip[0] = 0x46;
    write_be16(ip + 2, IPV4_TOTAL_SIZE);
    write_be16(ip + 4, 0x1234);
    write_be16(ip + 6, 0x4000);
    ip[8] = 42;
    ip[9] = 1;
    ip[12] = 192;
    ip[13] = 0;
    ip[14] = 2;
    ip[15] = 10;
    ip[16] = 198;
    ip[17] = 51;
    ip[18] = 100;
    ip[19] = 7;
    ip[20] = 1;
    ip[21] = 1;
    ip[22] = 1;
    ip[23] = 1;
    set_checksum(ip, IPV4_SIZE, 10);

    uint8_t *icmp = ip + IPV4_SIZE;
    icmp[0] = 8;
    icmp[1] = 0;
    write_be16(icmp + 4, 0xabcd);
    write_be16(icmp + 6, 9);
    for (size_t index = 8; index < ICMP_SIZE; ++index) {
        icmp[index] = (uint8_t)(0xa0 + index);
    }
    set_checksum(icmp, ICMP_SIZE, 2);
}

static void test_echo_reply(void)
{
    make_echo_request();
    uint8_t original[FRAME_SIZE];
    for (size_t index = 0; index < sizeof(original); ++index) {
        original[index] = input[index];
    }

    size_t reply_size = 999;
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_REPLY);
    assert(reply_size == FRAME_SIZE);

    for (size_t index = 0; index < 6; ++index) {
        assert(output[index] == original[6 + index]);
        assert(output[6 + index] == original[index]);
    }
    assert(read_be16(output + 12) == 0x0800);

    const uint8_t *reply_ip = output + ETHERNET_SIZE;
    const uint8_t *request_ip = original + ETHERNET_SIZE;
    assert(reply_ip[0] == 0x46);
    assert(reply_ip[8] == 64);
    assert(reply_ip[9] == 1);
    assert(read_be16(reply_ip + 2) == IPV4_TOTAL_SIZE);
    assert(valid_checksum(reply_ip, IPV4_SIZE));
    for (size_t index = 0; index < 4; ++index) {
        assert(reply_ip[12 + index] == request_ip[16 + index]);
        assert(reply_ip[16 + index] == request_ip[12 + index]);
    }

    const uint8_t *reply_icmp = reply_ip + IPV4_SIZE;
    const uint8_t *request_icmp = request_ip + IPV4_SIZE;
    assert(reply_icmp[0] == 0);
    assert(reply_icmp[1] == 0);
    assert(reply_icmp[4] == request_icmp[4]);
    assert(reply_icmp[5] == request_icmp[5]);
    assert(reply_icmp[6] == request_icmp[6]);
    assert(reply_icmp[7] == request_icmp[7]);
    for (size_t index = 8; index < ICMP_SIZE; ++index) {
        assert(reply_icmp[index] == request_icmp[index]);
    }
    assert(valid_checksum(reply_icmp, ICMP_SIZE));
    for (size_t index = 0; index < sizeof(original); ++index) {
        assert(input[index] == original[index]);
    }
}

static void test_rejections(void)
{
    size_t reply_size = 77;
    assert(net_ipv4_icmp_echo_reply(NULL, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, NULL,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), NULL) ==
           NET_PACKET_DROP);
    assert(reply_size == 77);
    assert(net_ipv4_icmp_echo_reply(input, ETHERNET_SIZE - 1, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_REPLY);

    make_echo_request();
    write_be16(input + 12, 0x0806);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_IGNORED);

    make_echo_request();
    input[ETHERNET_SIZE] = 0x65;
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    input[ETHERNET_SIZE] = 0x44;
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    write_be16(input + ETHERNET_SIZE + 2, 19);
    set_checksum(input + ETHERNET_SIZE, IPV4_SIZE, 10);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    write_be16(input + ETHERNET_SIZE + 2, 0xffff);
    set_checksum(input + ETHERNET_SIZE, IPV4_SIZE, 10);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    input[ETHERNET_SIZE + 10] ^= 1;
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    input[ETHERNET_SIZE + 9] = 17;
    set_checksum(input + ETHERNET_SIZE, IPV4_SIZE, 10);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_IGNORED);

    make_echo_request();
    write_be16(input + ETHERNET_SIZE + 6, 0x2000);
    set_checksum(input + ETHERNET_SIZE, IPV4_SIZE, 10);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_IGNORED);

    make_echo_request();
    write_be16(input + ETHERNET_SIZE + 6, 1);
    set_checksum(input + ETHERNET_SIZE, IPV4_SIZE, 10);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_IGNORED);

    make_echo_request();
    write_be16(input + ETHERNET_SIZE + 2, IPV4_SIZE + 7);
    set_checksum(input + ETHERNET_SIZE, IPV4_SIZE, 10);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    input[ETHERNET_SIZE + IPV4_SIZE] = 3;
    set_checksum(input + ETHERNET_SIZE + IPV4_SIZE, ICMP_SIZE, 2);
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_IGNORED);

    make_echo_request();
    input[ETHERNET_SIZE + IPV4_SIZE + 2] ^= 1;
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    sizeof(output), &reply_size) ==
           NET_PACKET_DROP);

    make_echo_request();
    reply_size = 77;
    assert(net_ipv4_icmp_echo_reply(input, FRAME_SIZE, output,
                                    FRAME_SIZE - 1, &reply_size) ==
           NET_PACKET_DROP);
    assert(reply_size == 77);
}

static void test_overlap(void)
{
    uint8_t storage[TEST_FRAME_CAPACITY + 8];
    make_echo_request();
    for (size_t index = 0; index < sizeof(storage); ++index) {
        storage[index] = 0;
    }
    for (size_t index = 0; index < FRAME_SIZE; ++index) {
        storage[index] = input[index];
    }
    size_t reply_size = 0;
    assert(net_ipv4_icmp_echo_reply(storage, FRAME_SIZE, storage + 4,
                                    sizeof(storage) - 4, &reply_size) ==
           NET_PACKET_REPLY);
    assert(reply_size == FRAME_SIZE);
    assert(storage[4 + ETHERNET_SIZE + IPV4_SIZE] == 0);
    assert(valid_checksum(storage + 4 + ETHERNET_SIZE, IPV4_SIZE));
    assert(valid_checksum(storage + 4 + ETHERNET_SIZE + IPV4_SIZE,
                          ICMP_SIZE));

    make_echo_request();
    for (size_t index = 0; index < sizeof(storage); ++index) {
        storage[index] = 0;
    }
    for (size_t index = 0; index < FRAME_SIZE; ++index) {
        storage[index + 4] = input[index];
    }
    assert(net_ipv4_icmp_echo_reply(storage + 4, FRAME_SIZE, storage,
                                    sizeof(storage) - 4, &reply_size) ==
           NET_PACKET_REPLY);
    assert(reply_size == FRAME_SIZE);
    assert(storage[ETHERNET_SIZE + IPV4_SIZE] == 0);
    assert(valid_checksum(storage + ETHERNET_SIZE, IPV4_SIZE));
    assert(valid_checksum(storage + ETHERNET_SIZE + IPV4_SIZE, ICMP_SIZE));
}

int main(void)
{
    test_echo_reply();
    test_rejections();
    test_overlap();
    puts("network packet tests passed");
    return 0;
}
