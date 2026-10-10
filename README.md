# DevOS

DevOS is a new, x86-64 operating-system project with a Windows-inspired desktop
experience and developer-focused tools. It is not based on, or compatible with,
the Microsoft Windows kernel.

## Project goals

- Keep the idle system usable on machines with 2 GB of RAM. This is a design
  target, not a benchmark or a claim about the current prototype.
- Make the desktop responsive, with restrained, interruptible animations.
- Grow from a small kernel into isolated user processes, a window manager, and
  developer tools.
- Add hardware support incrementally, including storage, networking, and audio.

## Current status

The prototype contains a freestanding x86-64 kernel entry point, Limine
framebuffer and memory-map requests, a framebuffer dashboard, a PS/2 set-1
keyboard driver with IRQ1 input, a COM1 receive driver with IRQ4 input, and a
keyboard-driven terminal. The kernel installs an IDT, remaps the legacy PIC,
installs exception diagnostics and a 100 Hz timer, and halts between interrupts.
Press `T` on the dashboard to open the terminal. It supports `help`, `mem`,
`ver`, `uptime`, `disk`, `pointer`, `elfcheck`, `pagealloc`, `pagefree`, `fault`,
`faultpf`, `clear`, `home`, `ls`, `mkdir`, `touch`, `write`, `cat`, and `rm`;
COM1 serial input remains available. The `fault` command deliberately halts the kernel to
test invalid-opcode reporting; `faultpf` deliberately reads unmapped memory
to test page-fault reporting, including the fault address. Files live in a small
fixed-capacity filesystem: 63 entries, at most 31 characters per name, and 2
KiB per file. With a supported primary-master ATA disk, changes are saved as
checksummed snapshots in alternating disk slots, retaining an older valid
snapshot if an update is interrupted. Without a supported disk, files remain
in memory and are lost at reboot. The kernel enumerates PCI devices and
reads/writes sectors using primary-master ATA PIO. The persistent filesystem
occupies raw disk sectors 1-524 (there is no partition table yet) and uses a
DevOS-specific format, not FAT or NTFS. The dashboard and memory
reporting require a bootloader that implements the Limine protocol. This is not
yet a complete desktop: Stage 5 now includes a PS/2 mouse driver and framebuffer
pointer, but there is no window manager or pointer-driven application behavior.
Process management, a network interface/protocol stack, and an audio driver are
also absent. An offline Ethernet/IPv4/ICMP packet-processing core is present
for Stage 4, but it is not connected to a network device. The dashboard is
static; there is no animation or frame timer.

Example terminal session:

```text
mkdir /home
touch /home/notes
write /home/notes Build something great
cat /home/notes
ls /home
```

## Build

On an x86-64 Linux host with GCC, GNU binutils, and Make installed:

```sh
make
make test
```

This produces `build/devos-kernel`, a freestanding ELF kernel image. To create a
bootable BIOS/UEFI hybrid ISO, install `curl`, `tar`, `make`, and `xorriso`, then
run:

```sh
make iso
```

The script downloads the pinned Limine v12.9.3 binary release into `build/`,
packages the kernel as `build/devos.iso`, and installs the Limine BIOS boot
stage. The ISO includes an x86-64 UEFI loader. A native MSYS2 MinGW GCC targets
Windows PE binaries, not the ELF format this kernel needs. For Windows, build
with an x86-64 ELF cross-toolchain (or build in WSL/Linux) and keep a native GCC
available for Limine's host utility. The kernel still requires a
Limine-compatible bootloader and a 32-bit RGB framebuffer mode.

For example, in the MSYS2 shell:

```sh
make clean
make CC=x86_64-elf-gcc LD=x86_64-elf-ld run
```

If `x86_64-elf-gcc`/`x86_64-elf-ld` are not installed in MSYS2, build the ISO
in WSL/Linux instead, then point MSYS2 QEMU at the resulting ISO file.
`make iso` also requires `curl`, `tar`, `make`, and `xorriso`.
If your QEMU executable has a different name, override `QEMU`, for example
`make QEMU=qemu-system-x86_64.exe run`.

Press `T` in the QEMU window to open the terminal. Type `help` to see commands.
If QEMU cannot open its default display, add `-display none` to use the serial
terminal only. `make run` attaches a 16 MiB IDE disk at
`build/devos-disk.img`; that image is a disposable development disk.
`make test-storage` creates a temporary disk and verifies PCI discovery,
ATA IDENTIFY, and filesystem persistence across a QEMU reboot.
`make test-pointer` uses QEMU's QMP interface to verify guest mouse motion and
button events.

The kernel reports its startup on COM1 (I/O port `0x3f8`). An emulator or machine
without a serial console may not show this output. `make test` runs host tests
for framebuffer drawing, keyboard input, the in-memory filesystem, memory
allocation, persistent-filesystem snapshot recovery, ELF64 validation, and
Ethernet/IPv4/ICMP packet processing.

## Five-stage OS plan

The target is a small, usable, Windows-inspired developer OS, not a Windows
kernel replacement. Each stage should finish with a bootable image, automated
tests for its new components, and a QEMU smoke test before work moves to the
next stage.

### Stage 1 — Stable kernel and memory foundations (complete)

Already present: x86-64 bootable kernel, Limine boot protocol, framebuffer
dashboard, serial console, keyboard interrupts, idle halt, and a tested
in-memory filesystem and shell.

Already added: CPU exception reporting, a physical page allocator, a page-backed
kernel heap, a 100 Hz PIT timer, and uptime reporting. The physical allocator
currently manages usable RAM below 4 GiB and reserves the first MiB; the heap
allocates contiguous page runs. Virtual memory management, allocator locking
for multiple CPUs, and more complete boot failure diagnostics remain future
hardening work.
Completion means the kernel can boot in QEMU, report faults rather than silently
hang, and allocate and release memory without exceeding the configured memory
budget.

### Stage 2 — Persistent storage and hardware drivers (in progress)

Added: PCI configuration-space enumeration and an ATA PIO block driver for the
primary-master LBA28 disk. The driver performs bounded polling, identifies disk
capacity, and reads/writes individual 512-byte sectors. The filesystem can
load and save a checksummed snapshot, using alternating slots to recover from
an interrupted update; successful shell mutations are saved automatically.
`make test` covers snapshot validation and recovery, and `make test-storage`
verifies file persistence across a QEMU reboot. Still to do: partition support,
a scalable on-disk filesystem, a virtual-filesystem API, and additional device
drivers such as pointer and audio. Completion also requires files to survive
reboot in the supported QEMU disk image and each supported device to have
driver tests or a reproducible QEMU test.

### Stage 3 — Processes and developer environment

Started: the kernel has a bounded ELF64 x86-64 executable inspector, available
as `elfcheck PATH`. It validates executable headers, loadable segment ranges,
alignment, user address bounds, and that the entry point belongs to an
executable segment. It does not yet load or execute a program. Next are virtual
address spaces, ring-3 entry and return, a syscall ABI, process lifecycle and
scheduling, and permissions; user programs must not be run until those
isolation boundaries are implemented. Build developer utilities such as an
editor, task/process inspection, and system diagnostics on those interfaces
rather than as kernel-only commands. Completion means independent user
programs can run, use files and console I/O, and fail without crashing the
kernel.

### Stage 4 — Networking and system services

Started: a bounded Ethernet II/IPv4 ICMP packet core validates IP and ICMP
checksums and can construct echo replies, including IPv4 header options. It
drops malformed or fragmented ICMP packets and ignores unsupported EtherTypes,
protocols, and ICMP message types. Host tests cover reply construction,
malformed frames, capacity handling, and overlapping buffers. This is not live
networking: no network card driver, interface/address configuration, ARP, UDP,
TCP, DNS, or socket API is implemented yet. Next add a supported QEMU NIC and
connect packet I/O with explicit bounds and ownership; then layer address
configuration and protocols on that tested interface. Define supported
protocols and security boundaries before exposing network services. Completion
means the QEMU guest can configure a network interface and pass local
connectivity tests through QEMU's user-mode network.

### Stage 5 — Desktop, applications, and release hardening (in progress)

Started: PS/2 mouse input uses IRQ12 and a bounded event queue, and the
framebuffer draws a clipped pointer that restores the pixels behind it.
`pointer` reports packet, position, and button state. Host tests cover packet
sync, signed movement, overflow, button state, queue capacity, and cursor
restoration/clipping. `make test-pointer` injects QMP motion and button events
and checks that the guest reports the updated pointer state. Pointer clicks do
not yet activate desktop controls.
Remaining work includes window management, a composited desktop, responsive
and reduced-motion-aware animations, settings, accessibility, and useful
developer-facing applications. Add installation/recovery documentation,
reproducible releases, security and regression tests, and measure idle and
working-set memory on the 2 GB QEMU target. Completion means the desktop boots
and remains usable in the target VM while supported apps, storage, input, and
networking pass end-to-end tests.

Features such as broad real-PC hardware compatibility, a large commercial
application catalog, and full Windows binary compatibility are not implied by
this scope. They require separate designs, substantial additional work, and
their own compatibility tests.

The 2 GB RAM figure is a project target, not a measured property of this
prototype. Driver support and desktop performance must be tested on specific
hardware before they can be advertised as supported.
