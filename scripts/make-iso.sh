#!/usr/bin/env bash
set -euo pipefail

root_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="$root_dir/build"
limine_version="v12.9.3"
archive="$build_dir/limine-binary.tar.gz"
archive_tmp="$build_dir/limine-binary.tar.gz.tmp.$$"
limine_dir="$build_dir/limine-binary"
iso_root="$build_dir/iso-root"
iso_path="$build_dir/devos.iso"

trap 'rm -f "$archive_tmp"' EXIT

for tool in curl tar make xorriso; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'error: required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

mkdir -p "$build_dir"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 3 \
        "https://github.com/Limine-Bootloader/Limine/releases/download/${limine_version}/limine-binary.tar.gz" \
        --output "$archive_tmp"
    mv "$archive_tmp" "$archive"
fi

if [[ ! -x "$limine_dir/limine" ]]; then
    mkdir -p "$limine_dir"
    tar -xzf "$archive" --strip-components=1 -C "$limine_dir"
    make -C "$limine_dir" CC="${HOST_CC:-gcc}"
fi

rm -rf "$iso_root"
mkdir -p "$iso_root/boot" "$iso_root/EFI/BOOT"
cp "$build_dir/devos-kernel" "$iso_root/boot/devos-kernel"
cp "$root_dir/boot/limine.conf" "$iso_root/limine.conf"
cp "$limine_dir/limine-bios.sys" "$iso_root/boot/limine-bios.sys"
cp "$limine_dir/limine-bios-cd.bin" "$iso_root/boot/limine-bios-cd.bin"
cp "$limine_dir/limine-uefi-cd.bin" "$iso_root/boot/limine-uefi-cd.bin"
cp "$limine_dir/BOOTX64.EFI" "$iso_root/EFI/BOOT/BOOTX64.EFI"

xorriso -as mkisofs \
    -R -r -J \
    -b boot/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
    -apm-block-size 2048 \
    --efi-boot boot/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$iso_root" -o "$iso_path"
"$limine_dir/limine" bios-install "$iso_path"

printf 'Created bootable ISO: %s\n' "$iso_path"
