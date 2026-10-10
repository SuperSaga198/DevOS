CC := gcc
LD := ld
BUILD_DIR := build
KERNEL := $(BUILD_DIR)/devos-kernel
QEMU ?= qemu-system-x86_64

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-m64 -mno-red-zone -mgeneral-regs-only -mcmodel=kernel
ASFLAGS := -m64
LDFLAGS := -nostdlib -z max-page-size=0x1000 \
	-T kernel/arch/x86_64/linker.ld

.PHONY: all clean test iso run

all: $(KERNEL)

test: $(BUILD_DIR)/framebuffer-test $(BUILD_DIR)/keyboard-test
	./$(BUILD_DIR)/framebuffer-test
	./$(BUILD_DIR)/keyboard-test

iso: $(KERNEL)
	bash scripts/make-iso.sh

run: iso
	$(QEMU) -m 2G -cdrom $(BUILD_DIR)/devos.iso \
		-serial stdio -monitor none -no-reboot

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/boot.o: kernel/arch/x86_64/boot.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: kernel/core/main.c kernel/core/framebuffer.h \
		kernel/arch/x86_64/limine.h kernel/arch/x86_64/keyboard.h \
		kernel/arch/x86_64/interrupts.h kernel/arch/x86_64/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/arch/x86_64/interrupts.c \
		kernel/arch/x86_64/interrupts.h kernel/arch/x86_64/keyboard.h \
		kernel/arch/x86_64/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/keyboard.o: kernel/arch/x86_64/keyboard.c \
		kernel/arch/x86_64/keyboard.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/serial.o: kernel/arch/x86_64/serial.c \
		kernel/arch/x86_64/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/interrupt_stubs.o: kernel/arch/x86_64/interrupt_stubs.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/framebuffer.o: kernel/core/framebuffer.c kernel/core/framebuffer.h \
		kernel/arch/x86_64/limine.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/framebuffer-test: tests/framebuffer_test.c \
		kernel/core/framebuffer.c kernel/core/framebuffer.h \
		kernel/arch/x86_64/limine.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core -Ikernel/arch/x86_64 \
		tests/framebuffer_test.c kernel/core/framebuffer.c -o $@

$(BUILD_DIR)/keyboard-test: tests/keyboard_test.c \
		kernel/arch/x86_64/keyboard.c kernel/arch/x86_64/keyboard.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/arch/x86_64 \
		tests/keyboard_test.c kernel/arch/x86_64/keyboard.c -o $@

$(KERNEL): $(BUILD_DIR)/boot.o $(BUILD_DIR)/main.o \
		$(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/interrupts.o \
		$(BUILD_DIR)/keyboard.o $(BUILD_DIR)/serial.o \
		$(BUILD_DIR)/interrupt_stubs.o \
		kernel/arch/x86_64/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/boot.o $(BUILD_DIR)/main.o \
		$(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/interrupts.o \
		$(BUILD_DIR)/keyboard.o $(BUILD_DIR)/serial.o \
		$(BUILD_DIR)/interrupt_stubs.o

clean:
	rm -rf $(BUILD_DIR)
