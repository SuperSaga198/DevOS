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

.PHONY: all clean test test-storage test-pointer test-oobe test-scheduler \
		test-audio test-network test-user-process test-editor measure-memory \
		test-vm iso run

all: $(KERNEL)

test: $(BUILD_DIR)/framebuffer-test $(BUILD_DIR)/keyboard-test \
		$(BUILD_DIR)/fs-test $(BUILD_DIR)/memory-test \
		$(BUILD_DIR)/fs-disk-test $(BUILD_DIR)/elf64-test \
		$(BUILD_DIR)/net-packet-test $(BUILD_DIR)/mouse-test \
		$(BUILD_DIR)/desktop-window-test $(BUILD_DIR)/file-manager-test \
		$(BUILD_DIR)/text-editor-test \
		$(BUILD_DIR)/system-test $(BUILD_DIR)/setup-wizard-test \
		$(BUILD_DIR)/scheduler-test $(BUILD_DIR)/pc-speaker-test
	./$(BUILD_DIR)/framebuffer-test
	./$(BUILD_DIR)/keyboard-test
	./$(BUILD_DIR)/fs-test
	./$(BUILD_DIR)/memory-test
	./$(BUILD_DIR)/fs-disk-test
	./$(BUILD_DIR)/elf64-test
	./$(BUILD_DIR)/net-packet-test
	./$(BUILD_DIR)/mouse-test
	./$(BUILD_DIR)/desktop-window-test
	./$(BUILD_DIR)/file-manager-test
	./$(BUILD_DIR)/text-editor-test
	./$(BUILD_DIR)/system-test
	./$(BUILD_DIR)/setup-wizard-test
	./$(BUILD_DIR)/scheduler-test
	./$(BUILD_DIR)/pc-speaker-test

test-storage: iso
	bash scripts/test-storage.sh $(QEMU) $(BUILD_DIR)/devos.iso

test-pointer: iso
	python3 scripts/test-pointer.py $(QEMU) $(BUILD_DIR)/devos.iso

test-oobe: iso
	bash scripts/test-oobe.sh $(QEMU) $(BUILD_DIR)/devos.iso

test-scheduler: iso
	bash scripts/test-scheduler.sh $(QEMU) $(BUILD_DIR)/devos.iso

test-audio: iso
	bash scripts/test-audio.sh $(QEMU) $(BUILD_DIR)/devos.iso

test-network: iso
	bash scripts/test-network.sh $(QEMU) $(BUILD_DIR)/devos.iso
	python3 scripts/test-udp.py $(QEMU) $(BUILD_DIR)/devos.iso
	python3 scripts/test-tcp.py $(QEMU) $(BUILD_DIR)/devos.iso

test-user-process: iso
	bash scripts/test-user-process.sh $(QEMU) $(BUILD_DIR)/devos.iso

test-editor: iso
	bash scripts/test-editor.sh $(QEMU) $(BUILD_DIR)/devos.iso

measure-memory: iso
	python3 scripts/measure-memory.py $(QEMU) $(BUILD_DIR)/devos.iso

test-vm: iso test
	bash scripts/test-vm.sh $(QEMU) $(BUILD_DIR)/devos.iso

iso: $(KERNEL)
	bash scripts/make-iso.sh

run: iso $(BUILD_DIR)/devos-disk.img
	$(QEMU) -m 2G -cdrom $(BUILD_DIR)/devos.iso \
		-drive file=$(BUILD_DIR)/devos-disk.img,format=raw,if=ide,index=0,media=disk \
		-netdev user,id=devos-net -device e1000,netdev=devos-net \
		-serial stdio -monitor none -no-reboot

$(BUILD_DIR)/devos-disk.img: | $(BUILD_DIR)
	truncate -s 16M $@

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/boot.o: kernel/arch/x86_64/boot.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: kernel/core/main.c kernel/core/framebuffer.h kernel/core/fs.h \
		kernel/core/fs_disk.h \
		kernel/core/elf64.h \
		kernel/core/memory.h \
		kernel/core/file_manager.h \
		kernel/core/setup_wizard.h \
		kernel/core/scheduler.h \
		kernel/core/process.h \
		kernel/core/text_editor.h \
		kernel/drivers/pc_speaker.h \
		kernel/drivers/e1000.h \
		kernel/drivers/ata_pio.h \
		kernel/arch/x86_64/limine.h kernel/arch/x86_64/keyboard.h \
		kernel/arch/x86_64/mouse.h \
		kernel/arch/x86_64/interrupts.h kernel/arch/x86_64/pci.h \
		kernel/arch/x86_64/serial.h \
		kernel/arch/x86_64/timer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/arch/x86_64 \
		-Ikernel/drivers -c $< -o $@

$(BUILD_DIR)/pci.o: kernel/arch/x86_64/pci.c \
		kernel/arch/x86_64/pci.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/ata_pio.o: kernel/drivers/ata_pio.c \
		kernel/drivers/ata_pio.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/drivers -c $< -o $@

$(BUILD_DIR)/pc_speaker.o: kernel/drivers/pc_speaker.c \
		kernel/drivers/pc_speaker.h kernel/arch/x86_64/timer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/drivers -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/e1000.o: kernel/drivers/e1000.c \
		kernel/drivers/e1000.h kernel/core/memory.h \
		kernel/core/net_packet.h kernel/arch/x86_64/timer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/drivers -Ikernel/core \
		-Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/arch/x86_64/interrupts.c \
		kernel/arch/x86_64/interrupts.h kernel/arch/x86_64/keyboard.h \
		kernel/arch/x86_64/mouse.h \
		kernel/arch/x86_64/serial.h kernel/arch/x86_64/timer.h \
		kernel/arch/x86_64/tss.h kernel/core/memory.h \
		kernel/core/scheduler.h kernel/core/syscall.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -Ikernel/core -c $< -o $@

$(BUILD_DIR)/tss.o: kernel/arch/x86_64/tss.c \
		kernel/arch/x86_64/tss.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/arch/x86_64/timer.c \
		kernel/arch/x86_64/timer.h kernel/core/scheduler.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -Ikernel/core -c $< -o $@

$(BUILD_DIR)/keyboard.o: kernel/arch/x86_64/keyboard.c \
		kernel/arch/x86_64/keyboard.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/mouse.o: kernel/arch/x86_64/mouse.c \
		kernel/arch/x86_64/mouse.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/serial.o: kernel/arch/x86_64/serial.c \
		kernel/arch/x86_64/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/interrupt_stubs.o: kernel/arch/x86_64/interrupt_stubs.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/demo-app.o: kernel/apps/demo.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/demo-app.elf: $(BUILD_DIR)/demo-app.o kernel/apps/demo.ld
	$(LD) -nostdlib -z max-page-size=0x1000 \
		-T kernel/apps/demo.ld -o $@ $<

$(BUILD_DIR)/user-app-image.o: kernel/core/user_app_image.S \
		$(BUILD_DIR)/demo-app.elf
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/framebuffer.o: kernel/core/framebuffer.c kernel/core/framebuffer.h \
		kernel/arch/x86_64/limine.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/fs.o: kernel/core/fs.c kernel/core/fs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -c $< -o $@

$(BUILD_DIR)/fs_disk.o: kernel/core/fs_disk.c kernel/core/fs_disk.h \
		kernel/core/fs.h kernel/drivers/ata_pio.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/drivers -c $< -o $@

$(BUILD_DIR)/elf64.o: kernel/core/elf64.c kernel/core/elf64.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -c $< -o $@

$(BUILD_DIR)/net_packet.o: kernel/core/net_packet.c \
		kernel/core/net_packet.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -c $< -o $@

$(BUILD_DIR)/memory.o: kernel/core/memory.c kernel/core/memory.h \
		kernel/arch/x86_64/limine.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/system.o: kernel/core/system.c kernel/core/system.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/core/scheduler.c \
		kernel/core/scheduler.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/process.o: kernel/core/process.c kernel/core/process.h \
		kernel/core/elf64.h kernel/core/memory.h \
		kernel/core/scheduler.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/file_manager.o: kernel/core/file_manager.c \
		kernel/core/file_manager.h kernel/core/fs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -c $< -o $@

$(BUILD_DIR)/text_editor.o: kernel/core/text_editor.c \
		kernel/core/text_editor.h kernel/core/fs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -c $< -o $@

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

$(BUILD_DIR)/mouse-test: tests/mouse_test.c \
		kernel/arch/x86_64/mouse.c kernel/arch/x86_64/mouse.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/arch/x86_64 \
		tests/mouse_test.c kernel/arch/x86_64/mouse.c -o $@

$(BUILD_DIR)/fs-test: tests/fs_test.c kernel/core/fs.c \
		kernel/core/fs.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/fs_test.c kernel/core/fs.c -o $@

$(BUILD_DIR)/fs-disk-test: tests/fs_disk_test.c kernel/core/fs.c \
		kernel/core/fs_disk.c kernel/core/fs.h kernel/core/fs_disk.h \
		kernel/drivers/ata_pio.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core -Ikernel/drivers \
		tests/fs_disk_test.c kernel/core/fs.c kernel/core/fs_disk.c -o $@

$(BUILD_DIR)/elf64-test: tests/elf64_test.c kernel/core/elf64.c \
		kernel/core/elf64.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/elf64_test.c kernel/core/elf64.c -o $@

$(BUILD_DIR)/net-packet-test: tests/net_packet_test.c \
		kernel/core/net_packet.c kernel/core/net_packet.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/net_packet_test.c kernel/core/net_packet.c -o $@

$(BUILD_DIR)/memory-test: tests/memory_test.c kernel/core/memory.c \
		kernel/core/memory.h kernel/arch/x86_64/limine.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core -Ikernel/arch/x86_64 \
		tests/memory_test.c kernel/core/memory.c -o $@

$(BUILD_DIR)/desktop-window-test: tests/desktop_window_test.c \
		kernel/core/framebuffer.c kernel/core/framebuffer.h \
		kernel/arch/x86_64/limine.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core -Ikernel/arch/x86_64 \
		tests/desktop_window_test.c kernel/core/framebuffer.c -o $@

$(BUILD_DIR)/system-test: tests/system_state_test.c kernel/core/system.c \
		kernel/core/system.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/system_state_test.c kernel/core/system.c -o $@

$(BUILD_DIR)/file-manager-test: tests/file_manager_test.c \
		kernel/core/file_manager.c kernel/core/file_manager.h \
		kernel/core/fs.c kernel/core/fs.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/file_manager_test.c kernel/core/file_manager.c \
		kernel/core/fs.c -o $@

$(BUILD_DIR)/text-editor-test: tests/text_editor_test.c \
		kernel/core/text_editor.c kernel/core/text_editor.h \
		kernel/core/fs.c kernel/core/fs.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/text_editor_test.c \
		kernel/core/text_editor.c kernel/core/fs.c -o $@

$(BUILD_DIR)/setup-wizard.o: kernel/core/setup_wizard.c \
		kernel/core/setup_wizard.h kernel/core/fs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/core -c $< -o $@

$(BUILD_DIR)/setup-wizard-test: tests/setup_wizard_test.c \
		kernel/core/setup_wizard.c kernel/core/fs.c \
		kernel/core/setup_wizard.h kernel/core/fs.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/setup_wizard_test.c \
		kernel/core/setup_wizard.c kernel/core/fs.c -o $@

$(BUILD_DIR)/scheduler-test: tests/scheduler_test.c \
		kernel/core/scheduler.c kernel/core/scheduler.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/core tests/scheduler_test.c \
		kernel/core/scheduler.c -o $@

$(BUILD_DIR)/pc-speaker-test: tests/pc_speaker_test.c \
		kernel/drivers/pc_speaker.c kernel/drivers/pc_speaker.h \
		kernel/arch/x86_64/timer.h | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror \
		-Ikernel/drivers -Ikernel/arch/x86_64 \
		tests/pc_speaker_test.c kernel/drivers/pc_speaker.c -o $@

$(KERNEL): $(BUILD_DIR)/boot.o $(BUILD_DIR)/main.o \
		$(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/interrupts.o \
		$(BUILD_DIR)/tss.o $(BUILD_DIR)/user-app-image.o \
		$(BUILD_DIR)/pci.o $(BUILD_DIR)/ata_pio.o \
		$(BUILD_DIR)/pc_speaker.o $(BUILD_DIR)/e1000.o \
		$(BUILD_DIR)/fs.o $(BUILD_DIR)/fs_disk.o \
		$(BUILD_DIR)/elf64.o \
		$(BUILD_DIR)/net_packet.o \
		$(BUILD_DIR)/memory.o $(BUILD_DIR)/timer.o \
		$(BUILD_DIR)/keyboard.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/serial.o \
		$(BUILD_DIR)/file_manager.o \
		$(BUILD_DIR)/text_editor.o \
		$(BUILD_DIR)/system.o $(BUILD_DIR)/setup-wizard.o \
		$(BUILD_DIR)/scheduler.o $(BUILD_DIR)/process.o \
		$(BUILD_DIR)/interrupt_stubs.o \
		kernel/arch/x86_64/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/boot.o $(BUILD_DIR)/main.o \
		$(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/interrupts.o \
		$(BUILD_DIR)/tss.o $(BUILD_DIR)/user-app-image.o \
		$(BUILD_DIR)/pci.o $(BUILD_DIR)/ata_pio.o \
		$(BUILD_DIR)/pc_speaker.o $(BUILD_DIR)/e1000.o \
		$(BUILD_DIR)/fs.o $(BUILD_DIR)/fs_disk.o \
		$(BUILD_DIR)/elf64.o \
		$(BUILD_DIR)/net_packet.o \
		$(BUILD_DIR)/memory.o $(BUILD_DIR)/timer.o \
		$(BUILD_DIR)/keyboard.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/serial.o \
		$(BUILD_DIR)/file_manager.o \
		$(BUILD_DIR)/text_editor.o \
		$(BUILD_DIR)/system.o $(BUILD_DIR)/setup-wizard.o \
		$(BUILD_DIR)/scheduler.o $(BUILD_DIR)/process.o \
		$(BUILD_DIR)/interrupt_stubs.o

clean:
	rm -rf $(BUILD_DIR)
