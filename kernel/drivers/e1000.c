#include "e1000.h"

#include "memory.h"
#include "net_packet.h"
#include "timer.h"

#include <stddef.h>

#define E1000_VENDOR_ID 0x8086u
#define E1000_DEVICE_ID 0x100eu
#define E1000_RX_COUNT 8u
#define E1000_TX_COUNT 8u
#define E1000_BUFFER_SIZE 2048u
#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_EERD 0x0014u
#define E1000_REG_IMC 0x00d8u
#define E1000_REG_RCTL 0x0100u
#define E1000_REG_TCTL 0x0400u
#define E1000_REG_TIPG 0x0410u
#define E1000_REG_RDBAL 0x2800u
#define E1000_REG_RDBAH 0x2804u
#define E1000_REG_RDLEN 0x2808u
#define E1000_REG_RDH 0x2810u
#define E1000_REG_RDT 0x2818u
#define E1000_REG_TDBAL 0x3800u
#define E1000_REG_TDBAH 0x3804u
#define E1000_REG_TDLEN 0x3808u
#define E1000_REG_TDH 0x3810u
#define E1000_REG_TDT 0x3818u
#define E1000_REG_RAL 0x5400u
#define E1000_REG_RAH 0x5404u
#define E1000_REG_MTA 0x5200u
#define E1000_REG_MTA_COUNT 128u
#define E1000_RX_STATUS_DD 0x01u
#define E1000_TX_STATUS_DD 0x01u
#define E1000_TX_CMD_EOP 0x01u
#define E1000_TX_CMD_IFCS 0x02u
#define E1000_TX_CMD_RS 0x08u
#define E1000_CTRL_RST 0x04000000u
#define E1000_RCTL_EN 0x00000002u
#define E1000_RCTL_BAM 0x00000004u
#define E1000_RCTL_SECRC 0x04000000u
#define E1000_TCTL_EN 0x00000002u
#define E1000_TCTL_PSP 0x00000008u
#define CONFIG_ADDRESS 0x0cf8u
#define CONFIG_DATA 0x0cfcu
#define DEVOS_IPV4_ADDRESS 0x0a00020fu
#define GATEWAY_IPV4_ADDRESS 0x0a000202u
#define ARP_FRAME_SIZE 42u
#define ICMP_FRAME_SIZE 47u

struct e1000_rx_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

static volatile uint32_t *registers;
static struct e1000_rx_descriptor *rx_ring;
static struct e1000_tx_descriptor *tx_ring;
static uint64_t rx_ring_physical;
static uint64_t tx_ring_physical;
static uint64_t rx_buffer_physical[E1000_RX_COUNT];
static uint64_t tx_buffer_physical;
static uint8_t *rx_buffers[E1000_RX_COUNT];
static uint8_t *tx_buffer;
static uint8_t mac[6];
static uint8_t gateway_mac[6];
static volatile uint64_t received_frames;
static volatile uint64_t echo_replies;
static uint32_t rx_index;
static uint32_t tx_index;
static uint16_t icmp_sequence;
static int ready;

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t config_read(uint8_t slot, uint8_t offset)
{
    outl(CONFIG_ADDRESS, 0x80000000u | ((uint32_t)slot << 11) |
                             ((uint32_t)offset & 0xfcu));
    return inl(CONFIG_DATA);
}

static void config_write(uint8_t slot, uint8_t offset, uint32_t value)
{
    outl(CONFIG_ADDRESS, 0x80000000u | ((uint32_t)slot << 11) |
                             ((uint32_t)offset & 0xfcu));
    outl(CONFIG_DATA, value);
}

static uint32_t reg_read(uint32_t offset)
{
    return registers[offset / sizeof(uint32_t)];
}

static void reg_write(uint32_t offset, uint32_t value)
{
    registers[offset / sizeof(uint32_t)] = value;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t size)
{
    for (size_t index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

static void write_be16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static uint16_t read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
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
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void write_be32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           source[3];
}

static int allocate_page(uint64_t *physical, void **virtual_address)
{
    if (!page_alloc(physical)) {
        return 0;
    }
    *virtual_address = physical_to_virtual(*physical);
    if (*virtual_address == NULL) {
        (void)page_free(*physical);
        return 0;
    }
    uint8_t *bytes = *virtual_address;
    for (size_t index = 0; index < PAGE_SIZE; ++index) {
        bytes[index] = 0;
    }
    return 1;
}

static int send_frame(const void *frame, size_t size)
{
    if (!ready || frame == NULL || size == 0 || size > E1000_BUFFER_SIZE) {
        return 0;
    }

    uint64_t interrupt_flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(interrupt_flags) : :
                      "memory");
    struct e1000_tx_descriptor *descriptor = &tx_ring[tx_index];
    if ((descriptor->status & E1000_TX_STATUS_DD) == 0) {
        __asm__ volatile ("pushq %0; popfq" : : "r"(interrupt_flags) :
                          "memory", "cc");
        return 0;
    }
    copy_bytes(tx_buffer, frame, size);
    descriptor->address = tx_buffer_physical;
    descriptor->length = (uint16_t)size;
    descriptor->command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS |
                          E1000_TX_CMD_RS;
    descriptor->status = 0;
    const uint32_t submitted = (tx_index + 1) % E1000_TX_COUNT;
    reg_write(E1000_REG_TDT, submitted);

    uint32_t attempts = 1000000;
    while ((descriptor->status & E1000_TX_STATUS_DD) == 0 &&
           attempts != 0) {
        --attempts;
        __asm__ volatile ("pause");
    }
    tx_index = submitted;
    __asm__ volatile ("pushq %0; popfq" : : "r"(interrupt_flags) :
                      "memory", "cc");
    return attempts != 0;
}

static void send_arp_reply(const uint8_t *request)
{
    uint8_t reply[ARP_FRAME_SIZE] = { 0 };
    copy_bytes(reply, request + 6, 6);
    copy_bytes(reply + 6, mac, 6);
    write_be16(reply + 12, 0x0806);
    write_be16(reply + 14, 1);
    write_be16(reply + 16, 0x0800);
    reply[18] = 6;
    reply[19] = 4;
    write_be16(reply + 20, 2);
    copy_bytes(reply + 22, mac, 6);
    write_be32(reply + 28, DEVOS_IPV4_ADDRESS);
    copy_bytes(reply + 32, request + 22, 6);
    copy_bytes(reply + 38, request + 28, 4);
    (void)send_frame(reply, sizeof(reply));
}

static void handle_arp(const uint8_t *frame, size_t size)
{
    if (size < ARP_FRAME_SIZE || read_be16(frame + 14) != 1 ||
        read_be16(frame + 16) != 0x0800 ||
        frame[18] != 6 || frame[19] != 4) {
        return;
    }
    const uint16_t opcode = read_be16(frame + 20);
    const uint32_t sender_ip = read_be32(frame + 28);
    const uint32_t target_ip = read_be32(frame + 38);
    if (sender_ip == GATEWAY_IPV4_ADDRESS) {
        copy_bytes(gateway_mac, frame + 22, sizeof(gateway_mac));
    }
    if (opcode == 1 && target_ip == DEVOS_IPV4_ADDRESS) {
        send_arp_reply(frame);
    }
}

static void handle_ipv4(const uint8_t *frame, size_t size)
{
    if (size < 34 || (frame[12] != 0x08 || frame[13] != 0x00)) {
        return;
    }

    uint8_t reply[E1000_BUFFER_SIZE];
    size_t reply_size = 0;
    const enum net_packet_result result = net_ipv4_icmp_echo_reply(
        frame, size, reply, sizeof(reply), &reply_size);
    if (result == NET_PACKET_REPLY) {
        (void)send_frame(reply, reply_size);
        return;
    }

    const uint8_t *ip = frame + 14;
    const size_t ip_header_size = (size_t)(ip[0] & 0x0f) * 4;
    const size_t ip_total_size = read_be16(ip + 2);
    if ((ip[0] >> 4) == 4 && ip[9] == 1 &&
        ip_header_size >= 20 && ip_header_size <= size - 14 &&
        ip_total_size >= ip_header_size + 8 &&
        ip_total_size <= size - 14 &&
        ip[ip_header_size] == 0 &&
        ip[ip_header_size + 1] == 0) {
        ++echo_replies;
    }
}

static void handle_frame(const uint8_t *frame, size_t size)
{
    if (size < 14) {
        return;
    }
    ++received_frames;
    const uint16_t ether_type = read_be16(frame + 12);
    if (ether_type == 0x0806) {
        handle_arp(frame, size);
    } else if (ether_type == 0x0800) {
        handle_ipv4(frame, size);
    }
}

int e1000_init(void)
{
    ready = 0;
    registers = NULL;
    uint8_t slot = 0;
    for (; slot < 32; ++slot) {
        const uint32_t identity = config_read(slot, 0);
        if ((identity & 0xffffu) == E1000_VENDOR_ID &&
            (identity >> 16) == E1000_DEVICE_ID) {
            break;
        }
    }
    if (slot == 32) {
        return 0;
    }

    uint32_t command_status = config_read(slot, 4);
    command_status = (command_status & 0xffff0000u) |
                     ((command_status & 0xffffu) | 0x0006u);
    config_write(slot, 4, command_status);

    const uint32_t bar = config_read(slot, 0x10);
    if ((bar & 1u) != 0 || (bar & 0x6u) == 0x4u ||
        (bar & 0xfffffff0u) == 0) {
        return 0;
    }
    registers = memory_map_mmio(bar & 0xfffffff0u, 0x6000);
    if (registers == NULL) {
        return 0;
    }

    reg_write(E1000_REG_IMC, 0xffffffffu);
    reg_write(E1000_REG_CTRL, reg_read(E1000_REG_CTRL) | E1000_CTRL_RST);
    uint32_t reset_attempts = 1000000;
    while ((reg_read(E1000_REG_CTRL) & E1000_CTRL_RST) != 0 &&
           reset_attempts != 0) {
        --reset_attempts;
        __asm__ volatile ("pause");
    }
    if (reset_attempts == 0) {
        registers = NULL;
        return 0;
    }

    uint32_t ral = reg_read(E1000_REG_RAL);
    uint32_t rah = reg_read(E1000_REG_RAH);
    for (size_t index = 0; index < 4; ++index) {
        mac[index] = (uint8_t)(ral >> (index * 8));
    }
    mac[4] = (uint8_t)rah;
    mac[5] = (uint8_t)(rah >> 8);
    int valid_mac = 0;
    for (size_t index = 0; index < sizeof(mac); ++index) {
        valid_mac |= mac[index] != 0 && mac[index] != 0xff;
    }
    if (!valid_mac) {
        registers = NULL;
        return 0;
    }

    void *memory = NULL;
    if (!allocate_page(&rx_ring_physical, &memory)) {
        registers = NULL;
        return 0;
    }
    rx_ring = memory;
    if (!allocate_page(&tx_ring_physical, &memory)) {
        (void)page_free(rx_ring_physical);
        registers = NULL;
        return 0;
    }
    tx_ring = memory;
    if (!allocate_page(&tx_buffer_physical, (void **)&tx_buffer)) {
        (void)page_free(tx_ring_physical);
        (void)page_free(rx_ring_physical);
        registers = NULL;
        return 0;
    }
    size_t allocated_rx = 0;
    for (; allocated_rx < E1000_RX_COUNT; ++allocated_rx) {
        if (!allocate_page(&rx_buffer_physical[allocated_rx],
                           (void **)&rx_buffers[allocated_rx])) {
            break;
        }
        rx_ring[allocated_rx].address = rx_buffer_physical[allocated_rx];
    }
    if (allocated_rx != E1000_RX_COUNT) {
        for (size_t index = 0; index < allocated_rx; ++index) {
            (void)page_free(rx_buffer_physical[index]);
        }
        (void)page_free(tx_buffer_physical);
        (void)page_free(tx_ring_physical);
        (void)page_free(rx_ring_physical);
        registers = NULL;
        return 0;
    }

    reg_write(E1000_REG_RDBAL, (uint32_t)rx_ring_physical);
    reg_write(E1000_REG_RDBAH, (uint32_t)(rx_ring_physical >> 32));
    reg_write(E1000_REG_RDLEN,
              E1000_RX_COUNT * sizeof(struct e1000_rx_descriptor));
    reg_write(E1000_REG_RDH, 0);
    reg_write(E1000_REG_RDT, E1000_RX_COUNT - 1);
    reg_write(E1000_REG_TDBAL, (uint32_t)tx_ring_physical);
    reg_write(E1000_REG_TDBAH, (uint32_t)(tx_ring_physical >> 32));
    reg_write(E1000_REG_TDLEN,
              E1000_TX_COUNT * sizeof(struct e1000_tx_descriptor));
    reg_write(E1000_REG_TDH, 0);
    reg_write(E1000_REG_TDT, 0);
    for (size_t index = 0; index < E1000_TX_COUNT; ++index) {
        tx_ring[index].status = E1000_TX_STATUS_DD;
    }
    for (size_t index = 0; index < E1000_REG_MTA_COUNT; ++index) {
        reg_write(E1000_REG_MTA + (uint32_t)(index * 4), 0);
    }

    reg_write(E1000_REG_TIPG, 0x0060200au);
    reg_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                                  (0x10u << 4) | (0x40u << 12));
    reg_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM |
                                  E1000_RCTL_SECRC);
    rx_index = 0;
    tx_index = 0;
    received_frames = 0;
    echo_replies = 0;
    icmp_sequence = 0;
    ready = 1;
    (void)reg_read(E1000_REG_STATUS);
    return 1;
}

int e1000_is_ready(void)
{
    return ready;
}

const uint8_t *e1000_mac_address(void)
{
    return ready ? mac : NULL;
}

void e1000_poll(void)
{
    if (!ready) {
        return;
    }
    for (size_t handled = 0; handled < E1000_RX_COUNT; ++handled) {
        struct e1000_rx_descriptor *descriptor = &rx_ring[rx_index];
        if ((descriptor->status & E1000_RX_STATUS_DD) == 0) {
            break;
        }
        const uint32_t recycled_index = rx_index;
        const size_t size = descriptor->length;
        if (descriptor->errors == 0 && size <= E1000_BUFFER_SIZE) {
            handle_frame(rx_buffers[rx_index], size);
        }
        descriptor->status = 0;
        descriptor->errors = 0;
        rx_index = (rx_index + 1) % E1000_RX_COUNT;
        reg_write(E1000_REG_RDT, recycled_index);
    }
}

static int send_arp_request(uint32_t target_ip)
{
    uint8_t request[ARP_FRAME_SIZE] = { 0 };
    for (size_t index = 0; index < 6; ++index) {
        request[index] = 0xff;
    }
    copy_bytes(request + 6, mac, 6);
    write_be16(request + 12, 0x0806);
    write_be16(request + 14, 1);
    write_be16(request + 16, 0x0800);
    request[18] = 6;
    request[19] = 4;
    write_be16(request + 20, 1);
    copy_bytes(request + 22, mac, 6);
    write_be32(request + 28, DEVOS_IPV4_ADDRESS);
    write_be32(request + 38, target_ip);
    return send_frame(request, sizeof(request));
}

static int send_echo_request(void)
{
    uint8_t frame[ICMP_FRAME_SIZE] = { 0 };
    copy_bytes(frame, gateway_mac, 6);
    copy_bytes(frame + 6, mac, 6);
    write_be16(frame + 12, 0x0800);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    write_be16(ip + 2, ICMP_FRAME_SIZE - 14);
    write_be16(ip + 4, ++icmp_sequence);
    ip[8] = 64;
    ip[9] = 1;
    write_be32(ip + 12, DEVOS_IPV4_ADDRESS);
    write_be32(ip + 16, GATEWAY_IPV4_ADDRESS);
    write_be16(ip + 10, checksum(ip, 20));
    uint8_t *icmp = ip + 20;
    icmp[0] = 8;
    write_be16(icmp + 4, 0x4445);
    write_be16(icmp + 6, icmp_sequence);
    icmp[8] = 'D';
    icmp[9] = 'e';
    icmp[10] = 'v';
    icmp[11] = 'O';
    icmp[12] = 'S';
    write_be16(icmp + 2, checksum(icmp, 13));
    return send_frame(frame, sizeof(frame));
}

int e1000_ping_gateway(void)
{
    if (!ready) {
        return 0;
    }
    int gateway_known = 0;
    for (size_t index = 0; index < sizeof(gateway_mac); ++index) {
        gateway_known |= gateway_mac[index] != 0;
    }
    if (!gateway_known) {
        if (!send_arp_request(GATEWAY_IPV4_ADDRESS)) {
            return 0;
        }
        const uint64_t deadline = timer_ticks() + 300;
        while (timer_ticks() < deadline) {
            gateway_known = 0;
            for (size_t index = 0; index < sizeof(gateway_mac); ++index) {
                gateway_known |= gateway_mac[index] != 0;
            }
            if (gateway_known) {
                break;
            }
            __asm__ volatile ("sti; hlt");
        }
    }
    if (!gateway_known) {
        return 0;
    }

    const uint64_t before = echo_replies;
    if (!send_echo_request()) {
        return 0;
    }
    const uint64_t deadline = timer_ticks() + 150;
    while (timer_ticks() < deadline) {
        if (echo_replies != before) {
            return 1;
        }
        __asm__ volatile ("sti; hlt");
    }
    return 0;
}

uint64_t e1000_received_frames(void)
{
    return received_frames;
}

uint64_t e1000_echo_replies(void)
{
    return echo_replies;
}
