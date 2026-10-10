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
and halts when idle. Press `T` on the dashboard to open the terminal. It
supports `help`, `mem`, `ver`, `clear`, and `home`; COM1 serial input remains
available. The dashboard and memory reporting require a bootloader that
implements the Limine protocol. This is not yet a complete desktop: it has no
pointer input, window manager, process management, networking stack, or audio
driver. The dashboard is static; there is no animation or frame timer.

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
terminal only.

The kernel reports its startup on COM1 (I/O port `0x3f8`). An emulator or machine
without a serial console may not show this output. `make test` checks framebuffer
drawing/clipping and keyboard scancode translation on the host.

## Planned milestones

1. Validate boot protocol and framebuffer behavior in an emulator; add a
   reproducible boot image.
2. Add physical memory allocation, virtual memory, and a basic scheduler.
3. Add pointer input, a window manager, responsive animations, and
   accessible desktop controls.
4. Add user-mode processes, a filesystem, and developer tools.
5. Add tested device drivers and a network stack; design audio support against
   specific hardware and emulator targets.

The 2 GB RAM figure is a project target, not a measured property of this
prototype. Driver support and desktop performance must be tested on specific
hardware before they can be advertised as supported.
